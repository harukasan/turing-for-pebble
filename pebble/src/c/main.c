/*
 * Emery watchface: runs the shared core in timer slices and draws the field
 * with a LECO or Bitham clock on top, digital or analog hands and the date,
 * which the field avoids. The scheduling model is described in
 * docs/behavior.md:
 *   - startup runs steps for STARTUP_MS after launch (at most
 *     STARTUP_STEPS_MAX of them) in consecutive slices of at most
 *     SLICE_BUDGET_MS, rescheduled SCHEDULE_NOW_MS apart, with the screen
 *     redrawn about every FRAME_INTERVAL_MS,
 *   - every minute rebuilds the digit mask and raises the pending steps to
 *     RD_MINUTE_STEPS_AVOID (adds RD_MINUTE_STEPS_PLAIN without avoidance),
 *     run the same way, and the analog hands sweep to the new minute over
 *     RD_SWEEP_MS in the same slices, the mask rebuilt whenever a hand angle
 *     changes,
 *   - a backlight-on event animates for at most BACKLIGHT_WINDOW_MS with the
 *     same slices and redraws,
 *   - focus loss cancels timers and snaps a sweep, and focus restore
 *     resumes pending work,
 *   - settings from the phone (settings.h) restart the field and its startup
 *     for new coefficients, rebuild the mask and refill for a new font or
 *     mask, and redraw for a new palette.
 * With RD_LOG, timing counters feed the startup summary log; RD_PROFILE
 * adds a clock calibration loop and a periodic profile log.
 */
#include "../../../core/clock_mask.h"
#include "../../../core/rd.h"
#include "config.h"
#include "settings.h"
#include <pebble.h>

#define STARTUP_MS RD_STARTUP_MS
#define STARTUP_STEPS_MAX RD_STARTUP_STEPS_MAX
/* Compute slice while startup, minute work, or the backlight animation is
 * active, and the redraw interval of those frames. */
#define SLICE_BUDGET_MS 30
#define FRAME_INTERVAL_MS 50
#define SCHEDULE_NOW_MS 1
#define BACKLIGHT_WINDOW_MS 5000
#define LOG_INTERVAL_STEPS 256
/* Reported instead of a measured duration when wall time is discontinuous. */
#define TIMING_SENTINEL_MS 1000
/* A longer gap between startup slices means the face was interrupted. */
#define STARTUP_GAP_LIMIT_MS 5000
/* Iterations of the RD_PROFILE calibration loop. */
#define CALIBRATION_ITERATIONS 1000000u
/* Whether the analog face is compiled in: always, unless a build that
 * defines RD_FACE fixes the digital face. */
#if !defined(RD_FACE) || RD_FACE
#define HAS_ANALOG 1
#else
#define HAS_ANALOG 0
#endif

static Window *window;
static Layer *layer;
static AppTimer *timer, *light_timer;
static GFont font_clock, font_date;
/* The time shown and masked, from the last tick. */
static struct tm clock_time;
static void *allocation, *state;
/* Cell mask of the clock digits, rebuilt every minute, and whether the core
 * holds it. */
static uint8_t *clock_mask;
static bool mask_installed;
static bool focused = true, lit;
static bool timing_unreliable;
/* Steps owed to minute changes and, while startup_active, the startup
 * itself, which ends at its deadline or step cap. A minute change during
 * the startup is covered by the startup steps, except for the minute steps
 * it is still owed at the deadline. */
static int pending;
static bool startup_active = true, tick_in_startup;
static uint32_t startup_steps, steps_since_tick;
static size_t min_heap = (size_t)-1;
static uint32_t max_compute, max_draw, max_step;
/* When the screen was last marked dirty, and whether it ever was. */
static uint32_t last_mark_ms;
static bool marked;

/* Startup accounting: validated compute and gap time, counts, and whether a
 * focus loss or long gap interrupted the startup. */
static uint32_t busy_ms, gap_ms, blit_ms_total, text_ms_total;
static uint32_t steps_total, slices, draws;
static uint32_t startup_start_ms, last_slice_end_ms;
static bool slice_seen, interrupted;
#if RD_LOG
static bool startup_logged;
static uint32_t next_log_step = LOG_INTERVAL_STEPS;
static uint32_t calibration_ms, mask_ms;
#endif
#if HAS_ANALOG
/* Angles of the hands shown and masked, always equal after set_hands, and
 * the sweep toward the angles of a new minute. */
static int hour_angle, minute_angle;
static int sweep_from_hour, sweep_from_minute, sweep_to_hour, sweep_to_minute;
static uint32_t sweep_start_ms;
static bool sweeping;
#if RD_LOG
/* Mask rebuilds, their longest time, and steps_total at the start of the
 * current sweep, for the RD sweep log. */
static uint32_t sweep_rebuilds, sweep_mask_max_ms, sweep_steps;
#endif
#endif

static uint32_t now_ms(void) {
  time_t seconds;
  uint16_t ms;
  time_ms(&seconds, &ms);
  return (uint32_t)seconds * 1000 + ms;
}

/* Clock adjustments and the emulator RTC can make wall time discontinuous.
 * Abort this slice and flag diagnostics instead of overflowing a duration. */
static uint32_t elapsed_ms(uint32_t start) {
  int32_t delta = (int32_t)(now_ms() - start);
  if (delta < 0 || delta >= TIMING_SENTINEL_MS) {
    timing_unreliable = true;
    return TIMING_SENTINEL_MS;
  }
  return (uint32_t)delta;
}

/* Milliseconds since a moment, without validation, for throttling. */
static int32_t since_ms(uint32_t then) { return (int32_t)(now_ms() - then); }

static void sample_heap(void) {
  size_t free_bytes = heap_bytes_free();
  if (free_bytes < min_heap) {
    min_heap = free_bytes;
  }
}

#ifdef RD_PROFILE
/* A fixed integer loop, timed, gives the CPU clock class and shows dynamic
 * frequency changes when repeated. */
static uint32_t calibrate(void) {
  uint32_t start = now_ms();
  volatile uint32_t sink = 0;
  for (uint32_t i = 0; i < CALIBRATION_ITERATIONS; i++) {
    sink += i;
  }
  return elapsed_ms(start);
}
#endif

#ifdef RD_FRAME_BENCH
/* Display limit study: mark the layer dirty as often as the OS allows for
 * FRAME_BENCH_MS with the normal draw (phase 1), then with an empty draw
 * (phase 2), and log the frames and the draw time of each phase. Built
 * with RD_STARTUP_MS=0 so that no compute competes. */
#define FRAME_BENCH_MS 5000
static int frame_phase;
static uint32_t frame_count, frame_phase_start, frame_phase_count,
    frame_phase_blit, frame_phase_text;

static void frame_bench(void *context) {
  (void)context;
  int32_t since = since_ms(frame_phase_start);
  if (since >= FRAME_BENCH_MS) {
    uint32_t n = frame_count - frame_phase_count;
    APP_LOG(
        APP_LOG_LEVEL_INFO,
        "RD frames phase=%d frames=%lu ms=%ld draw_us=%lu text_us=%lu",
        frame_phase, (unsigned long)n, (long)since,
        (unsigned long)(n ? (blit_ms_total - frame_phase_blit) * 1000 / n : 0),
        (unsigned long)(n ? (text_ms_total - frame_phase_text) * 1000 / n : 0));
    if (frame_phase == 2) {
      frame_phase = 0;
      return;
    }
    frame_phase = 2;
    frame_phase_start = now_ms();
    frame_phase_count = frame_count;
  }
  layer_mark_dirty(layer);
  app_timer_register(1, frame_bench, NULL);
}

static void frame_bench_start(void *context) {
  (void)context;
  frame_phase = 1;
  frame_phase_start = now_ms();
  frame_phase_count = frame_count;
  frame_phase_blit = blit_ms_total;
  frame_phase_text = text_ms_total;
  frame_bench(NULL);
}
#endif

/* The framebuffer rows the clock may draw into: the unobstructed ones. */
typedef struct {
  GBitmap *frame_buffer;
  int y_first, y_end;
} ClockRows;

static uint8_t *clock_row(void *context, int y) {
  ClockRows *rows = context;
  if (y < rows->y_first || y >= rows->y_end) {
    return NULL;
  }
  GBitmapDataRowInfo row = gbitmap_get_data_row_info(rows->frame_buffer, y);
  return row.min_x == 0 && row.max_x >= RD_DISPLAY_WIDTH - 1 ? row.data : NULL;
}

/* The system drawing runs only when the framebuffer cannot be captured, so
 * it is compiled for size. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

#if HAS_ANALOG
/* Tip of a hand of a length at an angle, for the system drawing. */
static GPoint hand_tip(int angle, int length) {
  int32_t trig = (int32_t)angle * TRIG_MAX_ANGLE / CM_TURN;
  return GPoint(CM_DIAL_X + sin_lookup(trig) * length / TRIG_MAX_RATIO,
                CM_DIAL_Y - cos_lookup(trig) * length / TRIG_MAX_RATIO);
}

/* The analog hands as system lines, close to but not the pixels of
 * cm_draw_analog, and the date below them. */
static void draw_system_hands(GContext *ctx, const CmLayout *layout,
                              bool date) {
  GPoint center = GPoint(CM_DIAL_X, CM_DIAL_Y);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2 * CM_HOUR_RADIUS + 1);
  graphics_draw_line(ctx, center, hand_tip(hour_angle, CM_HOUR_LENGTH));
  graphics_context_set_stroke_width(ctx, 2 * CM_MINUTE_RADIUS + 1);
  graphics_draw_line(ctx, center, hand_tip(minute_angle, CM_MINUTE_LENGTH));
  graphics_fill_circle(ctx, center, CM_CENTER_RADIUS);
  if (date) {
    char text[16];
    strftime(text, sizeof(text), "%Y.%m.%d", &clock_time);
    graphics_draw_text(ctx, text, font_date,
                       GRect(0, cm_analog_date_top(settings_font()),
                             RD_DISPLAY_WIDTH, layout->date_box),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
  }
}
#endif

/* Draw the clock text with the system fonts, when the framebuffer cannot be
 * captured. */
static void draw_system_text(GContext *ctx) {
  const CmLayout *layout = cm_layout(settings_font());
  bool date = settings_date();
  char text[16];
  graphics_context_set_text_color(ctx, GColorWhite);
#if HAS_ANALOG
  if (settings_analog()) {
    draw_system_hands(ctx, layout, date);
    return;
  }
#endif
  strftime(text, sizeof(text), "%H:%M", &clock_time);
  graphics_draw_text(ctx, text, font_clock,
                     GRect(0, cm_time_top(settings_font(), date),
                           RD_DISPLAY_WIDTH, layout->time_box),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
  if (!date) {
    return;
  }
  strftime(text, sizeof(text), "%Y.%m.%d", &clock_time);
  graphics_draw_text(
      ctx, text, font_date,
      GRect(0, layout->date_top, RD_DISPLAY_WIDTH, layout->date_box),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

/* Blit the field straight into the framebuffer as ARGB8 rows from the core,
 * then draw the clock over it from the core's copy of the system font
 * glyphs, the same pixels as graphics_draw_text and much faster. Only the
 * unobstructed rows are written, so a Timeline Peek keeps its area. */
static void draw(Layer *this_layer, GContext *ctx) {
#ifdef RD_FRAME_BENCH
  frame_count++;
  if (frame_phase == 2) {
    return;
  }
#endif
  uint32_t start = now_ms();
  GRect bounds = layer_get_unobstructed_bounds(this_layer);
  int y_end = bounds.origin.y + bounds.size.h;
  if (y_end > RD_DISPLAY_HEIGHT) {
    y_end = RD_DISPLAY_HEIGHT;
  }
  int palette = (int)setting(SETTING_PALETTE);
  bool clock = setting(SETTING_CLOCK);
  GBitmap *frame_buffer = graphics_capture_frame_buffer(ctx);
  if (frame_buffer) {
    const uint8_t *pixels = NULL;
    for (int y = bounds.origin.y < 0 ? 0 : bounds.origin.y; y < y_end; y++) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(frame_buffer, y);
      int first = row.min_x < 0 ? 0 : row.min_x;
      int last =
          row.max_x >= RD_DISPLAY_WIDTH ? RD_DISPLAY_WIDTH - 1 : row.max_x;
      if (last < first) {
        continue;
      }
#if RD_RENDER_FLAGS & RD_ROW_BILINEAR
      /* Interpolated rows are written straight into the framebuffer. */
      rd_row_rgb2_into(state, y, palette, RD_RENDER_FLAGS, row.data, first,
                       last);
      (void)pixels;
#else
#if RD_MODE == 1 || RD_MODE == 2
      /* Display rows 2k and 2k + 1 show the same grid row. */
      if (!pixels || (y & 1) == 0) {
        pixels = rd_row_rgb2(state, y, palette, RD_RENDER_FLAGS);
      }
#else
      pixels = rd_row_rgb2(state, y, palette, RD_RENDER_FLAGS);
#endif
      if (pixels) {
        memcpy(row.data + first, pixels + first, (size_t)(last - first + 1));
      }
#endif
    }
  }
  uint32_t blit = elapsed_ms(start);
  if (frame_buffer) {
    if (clock) {
      ClockRows rows = {frame_buffer, bounds.origin.y < 0 ? 0 : bounds.origin.y,
                        y_end};
#if HAS_ANALOG
      if (settings_analog()) {
        cm_draw_analog(clock_row, &rows, GColorWhiteARGB8, settings_font(),
                       hour_angle, minute_angle, clock_time.tm_year + 1900,
                       clock_time.tm_mon + 1, clock_time.tm_mday,
                       settings_date());
      } else
#endif
      {
        cm_draw(clock_row, &rows, GColorWhiteARGB8, settings_font(),
                clock_time.tm_hour, clock_time.tm_min,
                clock_time.tm_year + 1900, clock_time.tm_mon + 1,
                clock_time.tm_mday, settings_date());
      }
    }
    graphics_release_frame_buffer(ctx, frame_buffer);
  } else if (clock) {
    draw_system_text(ctx);
  }
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_draw) {
    max_draw = elapsed;
  }
  if (elapsed < TIMING_SENTINEL_MS) {
    blit_ms_total += blit;
    text_ms_total += elapsed - blit;
    draws++;
  }
  sample_heap();
}

static void schedule(uint32_t delay);
static void end_light(void);
#if HAS_ANALOG
static bool advance_sweep(void);
static void snap_sweep(void);
#endif

/* Steps a minute change owes: enough to refill the old digits' strokes with
 * avoidance, a few otherwise. */
static int minute_steps(void) {
  return settings_avoiding() ? RD_MINUTE_STEPS_AVOID : RD_MINUTE_STEPS_PLAIN;
}

#if RD_LOG
static void log_summary(void) {
  uint32_t steps = steps_total ? steps_total : 1;
  uint32_t frames = draws ? draws : 1;
  uint32_t wall = busy_ms + gap_ms;
  /* wall_ms sums validated slice and gap times; since_init_ms is the raw
   * clock difference as a cross-check. A log message is truncated beyond
   * about 90 characters, so the summary spans four short lines. */
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD startup mode=%d steps=%lu wall_ms=%lu since_init_ms=%ld", RD_MODE,
          (unsigned long)steps_total, (unsigned long)wall,
          (long)since_ms(startup_start_ms));
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD startup busy_ms=%lu rate_x10=%lu avg_step_us=%lu max_step_ms=%lu",
          (unsigned long)busy_ms,
          (unsigned long)(wall ? (uint64_t)steps_total * 10000 / wall : 0),
          (unsigned long)((uint64_t)busy_ms * 1000 / steps),
          (unsigned long)max_step);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD startup avg_draw_us=%lu avg_text_us=%lu slices=%lu draws=%lu",
          (unsigned long)((uint64_t)blit_ms_total * 1000 / frames),
          (unsigned long)((uint64_t)text_ms_total * 1000 / frames),
          (unsigned long)slices, (unsigned long)draws);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD startup heap_min=%lu loop_ms=%lu interrupted=%d clock_invalid=%d",
          (unsigned long)min_heap, (unsigned long)calibration_ms, interrupted,
          timing_unreliable);
}

static void log_progress(void) {
#ifdef RD_PROFILE
  if (!calibration_ms) {
    calibration_ms = calibrate();
  }
  uint32_t wall = busy_ms + gap_ms;
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD prof mode=%d step=%lu rate_x10=%lu busy_ms=%lu gap_ms=%lu",
          RD_MODE, (unsigned long)rd_steps(state),
          (unsigned long)(wall ? (uint64_t)steps_total * 10000 / wall : 0),
          (unsigned long)busy_ms, (unsigned long)gap_ms);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD prof max_step_ms=%lu draws=%lu blit_ms=%lu text_ms=%lu",
          (unsigned long)max_step, (unsigned long)draws,
          (unsigned long)blit_ms_total, (unsigned long)text_ms_total);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD prof heap_min=%lu loop_ms=%lu clock_invalid=%d",
          (unsigned long)min_heap, (unsigned long)calibration_ms,
          timing_unreliable);
#else
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD mode=%d step=%lu heap_min=%lu compute_max_ms=%lu "
          "draw_max_ms=%lu clock_invalid=%d",
          RD_MODE, (unsigned long)rd_steps(state), (unsigned long)min_heap,
          (unsigned long)max_compute, (unsigned long)max_draw,
          timing_unreliable);
#endif
}
#endif

/* One timer slice: run startup, minute, or backlight animation steps within
 * SLICE_BUDGET_MS, reschedule immediately while any of them continues, and
 * redraw about every FRAME_INTERVAL_MS. */
static void update(void *context) {
  (void)context;
  timer = NULL;
  if (!focused) {
    return;
  }
  uint32_t start = now_ms();
#if HAS_ANALOG
  /* The rebuild of a moving hand is charged to this slice's budget. */
  bool moved = sweeping && advance_sweep();
  bool sweep_left = sweeping;
#else
  bool moved = false, sweep_left = false;
#endif
  bool working = startup_active || pending > 0;
  if (working && slice_seen) {
    /* A backwards clock is a clock fault, a long gap an interruption. */
    int32_t gap = since_ms(last_slice_end_ms);
    if (gap < 0) {
      timing_unreliable = true;
    } else if (gap >= STARTUP_GAP_LIMIT_MS) {
      interrupted = true;
    } else {
      gap_ms += (uint32_t)gap;
    }
  }
  bool animate = lit;
  uint32_t budget = SLICE_BUDGET_MS;
  int target = startup_active ? (int)(STARTUP_STEPS_MAX - startup_steps)
               : working      ? pending
               : animate      ? (int)STARTUP_STEPS_MAX
                              : 0;
  int done = 0;
  while (done < target) {
    uint32_t step_start = now_ms();
    rd_step(state, 1);
    done++;
    uint32_t step = elapsed_ms(step_start);
    if (step > max_step) {
      max_step = step;
    }
    if (elapsed_ms(start) >= budget) {
      break;
    }
  }
  if (startup_active) {
    startup_steps += (uint32_t)done;
    steps_since_tick += (uint32_t)done;
    if (since_ms(startup_start_ms) >= (int32_t)STARTUP_MS ||
        startup_steps >= STARTUP_STEPS_MAX) {
      startup_active = false;
      pending = tick_in_startup && steps_since_tick < (uint32_t)minute_steps()
                    ? (int)((uint32_t)minute_steps() - steps_since_tick)
                    : 0;
    }
  } else if (working) {
    pending -= done;
  }
  bool remaining = startup_active || pending > 0 || sweep_left;
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_compute) {
    max_compute = elapsed;
  }
  if (elapsed < TIMING_SENTINEL_MS) {
    busy_ms += elapsed;
  }
  steps_total += (uint32_t)done;
  slices++;
  if (done || moved) {
    int32_t since = since_ms(last_mark_ms);
    /* While work or the animation is active, redraw once another slice would
     * overshoot the interval, so frames come about every FRAME_INTERVAL_MS
     * instead of every second slice. */
    int32_t interval = FRAME_INTERVAL_MS - SLICE_BUDGET_MS;
    if (!marked || !(remaining || animate) || since < 0 || since >= interval) {
      layer_mark_dirty(layer);
      last_mark_ms = now_ms();
      marked = true;
    }
  }
  sample_heap();
  if (remaining) {
    last_slice_end_ms = now_ms();
    slice_seen = true;
    schedule(SCHEDULE_NOW_MS);
  } else if (animate) {
    schedule(SCHEDULE_NOW_MS);
  }
#if RD_LOG
  if (!remaining && !startup_logged) {
    startup_logged = true;
    log_summary();
  } else if (rd_steps(state) >= next_log_step) {
    next_log_step += LOG_INTERVAL_STEPS;
    log_progress();
  }
#endif
}

static void schedule(uint32_t delay) {
  if (!timer && focused) {
    timer = app_timer_register(delay, update, NULL);
  }
}

/* Cancel animation and any scheduled slice. */
static void stop(void) {
  end_light();
  if (light_timer) {
    app_timer_cancel(light_timer);
    light_timer = NULL;
  }
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
}

#if RD_LOG
/* Steps and frames of the current backlight window, for the log. */
static uint32_t light_start_ms, light_steps, light_draws;
#endif

/* End of a backlight window: the last computed steps are drawn. */
static void end_light(void) {
  if (!lit) {
    return;
  }
  lit = false;
#if RD_LOG
  APP_LOG(APP_LOG_LEVEL_INFO, "RD light steps=%lu draws=%lu ms=%ld",
          (unsigned long)(steps_total - light_steps),
          (unsigned long)(draws - light_draws), (long)since_ms(light_start_ms));
#endif
  layer_mark_dirty(layer);
}

static void light_expired(void *context) {
  (void)context;
  light_timer = NULL;
  end_light();
}

/* Backlight on starts a bounded animation window. Backlight off ends it but
 * keeps pending startup or minute work eligible. */
static void backlight(bool on) {
  if (!on) {
    stop();
    if (pending || startup_active) {
      schedule(SCHEDULE_NOW_MS);
    }
    return;
  }
  if (!lit && focused) {
    lit = true;
#if RD_LOG
    light_start_ms = now_ms();
    light_steps = steps_total;
    light_draws = draws;
#endif
    light_timer = app_timer_register(BACKLIGHT_WINDOW_MS, light_expired, NULL);
    schedule(SCHEDULE_NOW_MS);
  }
}

static void focus(bool on) {
  focused = on;
  if (!on) {
    stop();
#if HAS_ANALOG
    snap_sweep();
#endif
    if (startup_active) {
      interrupted = true;
    }
  } else {
    layer_mark_dirty(layer);
    if (pending || startup_active) {
      schedule(SCHEDULE_NOW_MS);
    }
  }
}

#if RD_AVOID
/* Build the cell mask of the clock face of clock_time into clock_mask: the
 * digits, or the hands at the shown angles and the date. */
static int build_clock_mask(void) {
#if HAS_ANALOG
  if (settings_analog()) {
    return cm_build_analog(clock_mask, rd_width(state), rd_height(state),
                           settings_font(), hour_angle, minute_angle,
                           clock_time.tm_year + 1900, clock_time.tm_mon + 1,
                           clock_time.tm_mday, RD_HALO, settings_date());
  }
#endif
  return cm_build(clock_mask, rd_width(state), rd_height(state),
                  settings_font(), clock_time.tm_hour, clock_time.tm_min,
                  clock_time.tm_year + 1900, clock_time.tm_mon + 1,
                  clock_time.tm_mday, RD_HALO, settings_date());
}
#endif

/* Build the mask of the clock face of clock_time and install it: B is held
 * at 0 under the digits or hands at once, and the kill rate rises toward
 * them. Without avoidance the mask is removed. */
static void rebuild_mask(void) {
#if RD_AVOID
  if (!settings_avoiding()) {
    /* The bitmap stays allocated: the heap is measured with it, and
     * turning avoidance back on then cannot fail to allocate it. */
    rd_mask(state, NULL);
    mask_installed = false;
    return;
  }
#if RD_LOG
  uint32_t start = now_ms();
#endif
  if (!clock_mask) {
    clock_mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
  }
  if (clock_mask && build_clock_mask() == 0) {
    rd_mask(state, clock_mask);
    mask_installed = true;
  }
#if RD_LOG
  mask_ms = elapsed_ms(start);
#if HAS_ANALOG
  if (sweeping) {
    sweep_rebuilds++;
    if (mask_ms > sweep_mask_max_ms) {
      sweep_mask_max_ms = mask_ms;
    }
  }
#endif
#endif
#endif
}

/* Settings, startup, minute, and sweep code runs rarely, so it is compiled
 * for size instead of the -O3 of the step loop and the draw. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

#if HAS_ANALOG
/* Angles of the hands at a time. */
static void target_angles(const struct tm *time, int *hour, int *minute) {
  *hour = cm_hour_angle(time->tm_hour, time->tm_min);
  *minute = cm_minute_angle(time->tm_min);
}

/* Show the hands at these angles, rebuilding the mask only when either
 * changes. Returns whether they moved. */
static bool set_hands(int hour, int minute) {
  if (hour == hour_angle && minute == minute_angle) {
    return false;
  }
  hour_angle = hour;
  minute_angle = minute;
  rebuild_mask();
  return true;
}

/* End the sweep at its target angles. Returns whether the hands moved. */
static bool end_sweep(void) {
  bool moved = set_hands(sweep_to_hour, sweep_to_minute);
#if RD_LOG
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD sweep rebuilds=%lu mask_max_ms=%lu ms=%ld steps=%lu",
          (unsigned long)sweep_rebuilds, (unsigned long)sweep_mask_max_ms,
          (long)since_ms(sweep_start_ms),
          (unsigned long)(steps_total - sweep_steps));
#endif
  sweeping = false;
  return moved;
}

static void snap_sweep(void) {
  if (sweeping) {
    end_sweep();
  }
}

/* Move the hands to their eased angles RD_SWEEP_MS into the sweep, ending
 * it at its target once the time is up or the clock went backwards.
 * Returns whether the hands moved. */
static bool advance_sweep(void) {
  int32_t elapsed = since_ms(sweep_start_ms);
  if (elapsed < 0 || elapsed >= RD_SWEEP_MS) {
    return end_sweep();
  }
  return set_hands(
      cm_sweep_angle(sweep_from_hour, sweep_to_hour, (int)elapsed, RD_SWEEP_MS),
      cm_sweep_angle(sweep_from_minute, sweep_to_minute, (int)elapsed,
                     RD_SWEEP_MS));
}
#endif

/* Give the pattern steps to refill after the digits or the mask changed:
 * the pending steps rise to `steps`, or grow by it with `add`. A change
 * during the startup is covered by the startup steps, which owe the rest of
 * the minute steps at the deadline. */
static void refill(int steps, bool add) {
#ifndef RD_FRAME_BENCH
  if (startup_active) {
    /* The startup steps refill the digits, and the deadline settles the
     * rest. */
    tick_in_startup = true;
    steps_since_tick = 0;
  } else if (add) {
    pending += steps;
  } else if (pending < steps) {
    pending = steps;
  }
#endif
  layer_mark_dirty(layer);
  schedule(SCHEDULE_NOW_MS);
}

#if HAS_ANALOG
/* Sweep from the shown angles to those of clock_time, starting now. The
 * first slice moves the hands. */
static void start_sweep(void) {
  sweep_from_hour = hour_angle;
  sweep_from_minute = minute_angle;
  target_angles(&clock_time, &sweep_to_hour, &sweep_to_minute);
  sweep_start_ms = now_ms();
#if RD_LOG
  if (!sweeping) {
    sweep_rebuilds = 0;
    sweep_mask_max_ms = 0;
    sweep_steps = steps_total;
  }
#endif
  sweeping = true;
}

/* Show the hands at the angles of clock_time at once, ending any sweep.
 * The caller rebuilds the mask. */
static void reset_hands(void) {
  sweeping = false;
  target_angles(&clock_time, &hour_angle, &minute_angle);
}
#endif

/* A new minute: mask the new digits or sweep the hands, and give the
 * pattern the minute steps to refill the strokes of the old ones. With
 * avoidance the steps do not accumulate while the face is unfocused;
 * without it they add up. */
static void tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  clock_time = *tick_time;
#if HAS_ANALOG
  if (settings_analog()) {
    /* Focused with the clock shown, the hands sweep in the next slices.
     * Otherwise they jump. */
#ifndef RD_FRAME_BENCH
    if (focused && setting(SETTING_CLOCK)) {
      start_sweep();
    } else
#endif
    {
      int hour, minute;
      sweeping = false;
      target_angles(&clock_time, &hour, &minute);
      set_hands(hour, minute);
    }
  } else
#endif
  {
    rebuild_mask();
  }
  refill(minute_steps(), !settings_avoiding());
}

static void load_fonts(void) {
  const CmLayout *layout = cm_layout(settings_font());
  font_clock = fonts_get_system_font(layout->time_key);
  font_date = fonts_get_system_font(layout->date_key);
}

/* The custom palette's stops from the settings: the dark stop, the middle
 * stops in use, and the light stop last. */
static void apply_palette(void) {
  static const uint8_t ORDER[3] = {SETTING_LOW, SETTING_MID1, SETTING_MID2};
  int count = (int)setting(SETTING_STOPS);
  uint8_t stops[4 * 3];
  for (int i = 0; i < count; i++) {
    int32_t rgb = setting(i == count - 1 ? SETTING_HIGH : ORDER[i]);
    for (int c = 0; c < 3; c++) {
      stops[i * 3 + c] = (uint8_t)(rgb >> (16 - 8 * c));
    }
  }
  rd_palette(state, stops, count);
}

/* The pattern coefficients from the settings. */
static void apply_params(void) {
  rd_params(state, (int)setting(SETTING_FEED), (int)setting(SETTING_KILL),
            (int)setting(SETTING_DA), (int)setting(SETTING_DB),
            (int)setting(SETTING_DT));
}

/* Start the startup animation: its accounting starts over and the first
 * slice is scheduled. The minimum heap and the clock fault flag cover the
 * whole session. */
static void begin_startup(void) {
  startup_active = true;
  tick_in_startup = false;
  pending = 0;
  startup_steps = steps_since_tick = 0;
  busy_ms = gap_ms = blit_ms_total = text_ms_total = 0;
  steps_total = slices = draws = 0;
  max_compute = max_draw = max_step = 0;
  slice_seen = interrupted = marked = false;
#if RD_LOG
  startup_logged = false;
  next_log_step = LOG_INTERVAL_STEPS;
#endif
  startup_start_ms = now_ms();
  schedule(SCHEDULE_NOW_MS);
}

/* New coefficients: the field starts over from the seed and the startup
 * animation runs again. Settings arrive on the event loop, never inside a
 * slice. */
static void restart(void) {
  stop();
  state = rd_init(allocation, rd_bytes(RD_MODE), RD_MODE, RD_SEED);
  apply_params();
  apply_palette();
  rebuild_mask();
  layer_mark_dirty(layer);
  begin_startup();
}

/* Apply what a settings message changed. A removed mask leaves an empty
 * area where the digits were that minute steps would take hours to fill,
 * and showing or hiding the date moves the time and changes the mask as
 * much, so both restart the field like new coefficients do. */
static void apply_settings(unsigned changed) {
  if (changed & SETTINGS_CHANGED_FONT) {
    load_fonts();
  }
#if HAS_ANALOG
  if (changed & SETTINGS_CHANGED_LAYOUT) {
    /* A new face shows the hands at the current time. */
    reset_hands();
  }
#endif
  if ((changed & SETTINGS_CHANGED_PATTERN) ||
      (mask_installed && !settings_avoiding()) ||
      ((changed & SETTINGS_CHANGED_LAYOUT) && settings_avoiding())) {
    restart();
    return;
  }
  if (changed & SETTINGS_CHANGED_PALETTE) {
    apply_palette();
  }
  if (changed & (SETTINGS_CHANGED_FONT | SETTINGS_CHANGED_MASK)) {
    rebuild_mask();
    /* Only a new or moved mask leaves strokes to refill. Without avoidance
     * the field is untouched and only the clock is redrawn. */
    if (settings_avoiding()) {
      refill(RD_MINUTE_STEPS_AVOID, false);
    }
  }
  layer_mark_dirty(layer);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

#ifdef RD_BENCH
void rd_bench_log(void *state, int count, uint32_t (*now)(void),
                  uint32_t out[5]);

/* Phase timing of 20 steps, logged as microseconds per step, once the log
 * stream has had time to attach. */
static void bench(void *context) {
  (void)context;
  uint32_t t[5];
  rd_bench_log(state, 20, now_ms, t);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD bench decode=%lu lap=%lu react=%lu encode=%lu step=%lu",
          (unsigned long)(t[0] * 50), (unsigned long)((t[1] - t[0]) * 50),
          (unsigned long)((t[2] - t[1]) * 50),
          (unsigned long)((t[3] - t[2]) * 50), (unsigned long)(t[3] * 50));
  APP_LOG(APP_LOG_LEVEL_INFO, "RD bench render_us=%lu",
          (unsigned long)(t[4] * 50));
}
#endif

/* Launch and exit run once, so they are compiled for size. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

static void init(void) {
  settings_load();
  allocation = malloc(rd_bytes(RD_MODE));
  if (!allocation) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Core allocation failed");
    return;
  }
  state = rd_init(allocation, rd_bytes(RD_MODE), RD_MODE, RD_SEED);
  if (!state) {
    return;
  }
  apply_params();
  apply_palette();
  load_fonts();
  time_t now = time(NULL);
  clock_time = *localtime(&now);
#if HAS_ANALOG
  reset_hands();
#endif
  rebuild_mask();
  window = window_create();
  layer = layer_create(GRect(0, 0, RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT));
  layer_set_update_proc(layer, draw);
  layer_add_child(window_get_root_layer(window), layer);
  window_stack_push(window, false);
  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  backlight_service_subscribe(backlight);
  app_focus_service_subscribe(focus);
  settings_open(apply_settings);
  sample_heap();
#ifdef RD_PROFILE
  calibration_ms = calibrate();
#endif
#ifdef RD_BENCH
  app_timer_register(3000, bench, NULL);
#endif
#ifdef RD_FRAME_BENCH
  app_timer_register(3000, frame_bench_start, NULL);
#endif
#if RD_LOG
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD init mode=%d font=%d face=%d core_bytes=%lu heap_min=%lu "
          "mask_ms=%lu",
          RD_MODE, settings_font(), settings_analog(),
          (unsigned long)rd_bytes(RD_MODE), (unsigned long)min_heap,
          (unsigned long)mask_ms);
#endif
  begin_startup();
}

static void deinit(void) {
  stop();
  settings_close();
  tick_timer_service_unsubscribe();
  backlight_service_unsubscribe();
  app_focus_service_unsubscribe();
  if (layer) {
    layer_destroy(layer);
  }
  if (window) {
    window_destroy(window);
  }
  free(clock_mask);
  free(allocation);
}

int main(void) {
  init();
  if (state) {
    app_event_loop();
  }
  deinit();
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
