/*
 * Emery watchface: runs the shared core in timer slices and draws the field
 * with a LECO or Bitham clock on top, whose digits the field avoids. The
 * scheduling model is described in docs/behavior.md:
 *   - startup advances STARTUP_STEPS steps in consecutive slices of at most
 *     STARTUP_SLICE_BUDGET_MS, rescheduled SCHEDULE_NOW_MS apart, with the
 *     screen redrawn at most every WORK_FRAME_INTERVAL_MS,
 *   - every minute rebuilds the digit mask and raises the pending steps to
 *     RD_MINUTE_STEPS (adds them without avoidance), run the same way,
 *   - a backlight-on event animates for at most BACKLIGHT_WINDOW_MS, at most
 *     STEPS_PER_SLICE steps within SLICE_BUDGET_MS per frame,
 *   - focus loss cancels timers and focus restore resumes pending work.
 * With RD_LOG, timing counters feed the startup summary log; RD_PROFILE
 * adds a clock calibration loop and a periodic profile log.
 */
#include "../../../core/clock_mask.h"
#include "../../../core/rd.h"
#include "config.h"
#include <pebble.h>

#define STARTUP_STEPS RD_STARTUP_STEPS
#define STEPS_PER_SLICE 8
#define SLICE_BUDGET_MS 8
#define STARTUP_SLICE_BUDGET_MS 20
#define FRAME_INTERVAL_MS 100
/* Redraw interval while startup or minute work is pending. */
#define WORK_FRAME_INTERVAL_MS 40
#define SCHEDULE_NOW_MS 1
#define BACKLIGHT_WINDOW_MS 5000
#define LOG_INTERVAL_STEPS 256
/* Reported instead of a measured duration when wall time is discontinuous. */
#define TIMING_SENTINEL_MS 1000
/* A longer gap between startup slices means the face was interrupted. */
#define STARTUP_GAP_LIMIT_MS 5000
/* Iterations of the RD_PROFILE calibration loop. */
#define CALIBRATION_ITERATIONS 1000000u

static Window *window;
static Layer *layer;
static AppTimer *timer, *light_timer;
static GFont font_clock, font_date;
/* The time shown and masked, from the last tick. */
static struct tm clock_time;
static void *allocation, *state;
/* Cell mask of the clock digits, rebuilt every minute. */
static uint8_t *clock_mask;
static bool focused = true, lit;
static bool timing_unreliable;
static int pending = STARTUP_STEPS;
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
static bool slice_seen, interrupted, startup_logged;
#if RD_LOG
static uint32_t next_log_step = LOG_INTERVAL_STEPS;
static uint32_t calibration_ms, mask_ms;
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
 * with RD_STARTUP_STEPS=0 so that no compute competes. */
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

/* Draw the clock text with the system fonts, when the framebuffer cannot be
 * captured. */
static void draw_system_text(GContext *ctx) {
  const CmLayout *layout = cm_layout(RD_FONT);
  char text[16];
  graphics_context_set_text_color(ctx, GColorWhite);
  strftime(text, sizeof(text), "%H:%M", &clock_time);
  graphics_draw_text(
      ctx, text, font_clock,
      GRect(0, layout->time_top, RD_DISPLAY_WIDTH, layout->time_box),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  strftime(text, sizeof(text), "%Y.%m.%d", &clock_time);
  graphics_draw_text(
      ctx, text, font_date,
      GRect(0, layout->date_top, RD_DISPLAY_WIDTH, layout->date_box),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

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
      rd_row_rgb2_into(state, y, RD_PALETTE, RD_RENDER_FLAGS, row.data, first,
                       last);
      (void)pixels;
#else
#if RD_MODE == 1 || RD_MODE == 2
      /* Display rows 2k and 2k + 1 show the same grid row. */
      if (!pixels || (y & 1) == 0) {
        pixels = rd_row_rgb2(state, y, RD_PALETTE, RD_RENDER_FLAGS);
      }
#else
      pixels = rd_row_rgb2(state, y, RD_PALETTE, RD_RENDER_FLAGS);
#endif
      if (pixels) {
        memcpy(row.data + first, pixels + first, (size_t)(last - first + 1));
      }
#endif
    }
  }
  uint32_t blit = elapsed_ms(start);
  if (frame_buffer) {
    if (RD_CLOCK) {
      ClockRows rows = {frame_buffer, bounds.origin.y < 0 ? 0 : bounds.origin.y,
                        y_end};
      cm_draw(clock_row, &rows, GColorWhiteARGB8, RD_FONT, clock_time.tm_hour,
              clock_time.tm_min, clock_time.tm_year + 1900,
              clock_time.tm_mon + 1, clock_time.tm_mday);
    }
    graphics_release_frame_buffer(ctx, frame_buffer);
  } else if (RD_CLOCK) {
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

/* One timer slice: run pending startup or minute steps within the startup
 * budget, or animation steps while the backlight is on within the frame
 * budget. Pending work is rescheduled immediately and the screen is
 * redrawn at most every FRAME_INTERVAL_MS. */
static void update(void *context) {
  (void)context;
  timer = NULL;
  if (!focused) {
    return;
  }
  uint32_t start = now_ms();
  bool working = pending > 0;
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
  uint32_t budget = working ? STARTUP_SLICE_BUDGET_MS : SLICE_BUDGET_MS;
  int target = working ? pending : animate ? STEPS_PER_SLICE : 0;
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
  if (working) {
    pending -= done;
  }
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_compute) {
    max_compute = elapsed;
  }
  if (elapsed < TIMING_SENTINEL_MS) {
    busy_ms += elapsed;
  }
  steps_total += (uint32_t)done;
  slices++;
  if (done) {
    int32_t since = since_ms(last_mark_ms);
    /* While work is pending, redraw once another slice would overshoot the
     * interval, so frames come about every WORK_FRAME_INTERVAL_MS instead
     * of every second slice. */
    int32_t interval = working
                           ? WORK_FRAME_INTERVAL_MS - STARTUP_SLICE_BUDGET_MS
                           : FRAME_INTERVAL_MS;
    if (!marked || pending == 0 || since < 0 || since >= interval) {
      layer_mark_dirty(layer);
      last_mark_ms = now_ms();
      marked = true;
    }
  }
  sample_heap();
  if (pending > 0) {
    last_slice_end_ms = now_ms();
    slice_seen = true;
    schedule(SCHEDULE_NOW_MS);
  } else if (animate) {
    schedule(FRAME_INTERVAL_MS);
  }
#if RD_LOG
  if (pending == 0 && !startup_logged) {
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
  lit = false;
  if (light_timer) {
    app_timer_cancel(light_timer);
    light_timer = NULL;
  }
  if (timer) {
    app_timer_cancel(timer);
    timer = NULL;
  }
}

static void light_expired(void *context) {
  (void)context;
  light_timer = NULL;
  lit = false;
}

/* Backlight on starts a bounded animation window. Backlight off ends it but
 * keeps pending startup or minute work eligible. */
static void backlight(bool on) {
  if (!on) {
    stop();
    if (pending) {
      schedule(SCHEDULE_NOW_MS);
    }
    return;
  }
  if (!lit && focused) {
    lit = true;
    light_timer = app_timer_register(BACKLIGHT_WINDOW_MS, light_expired, NULL);
    schedule(pending ? SCHEDULE_NOW_MS : FRAME_INTERVAL_MS);
  }
}

static void focus(bool on) {
  focused = on;
  if (!on) {
    stop();
    if (pending > 0 && !startup_logged) {
      interrupted = true;
    }
  } else {
    layer_mark_dirty(layer);
    if (pending) {
      schedule(SCHEDULE_NOW_MS);
    }
  }
}

/* Build the mask of the digits of clock_time and install it: B is held at
 * 0 under the digits at once, and the kill rate rises toward them. */
static void rebuild_mask(void) {
#if RD_CLOCK && RD_AVOID
#if RD_LOG
  uint32_t start = now_ms();
#endif
  if (!clock_mask) {
    clock_mask = malloc(cm_bytes(rd_width(state), rd_height(state)));
  }
  if (clock_mask &&
      cm_build(clock_mask, rd_width(state), rd_height(state), RD_FONT,
               clock_time.tm_hour, clock_time.tm_min, clock_time.tm_year + 1900,
               clock_time.tm_mon + 1, clock_time.tm_mday, RD_HALO) == 0) {
    rd_mask(state, clock_mask);
  }
#if RD_LOG
  mask_ms = elapsed_ms(start);
#endif
#endif
}

/* A new minute: mask the new digits, and give the pattern RD_MINUTE_STEPS
 * to refill the strokes of the old ones. The steps do not accumulate while
 * the face is unfocused. */
static void tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  clock_time = *tick_time;
  rebuild_mask();
#ifndef RD_FRAME_BENCH
  if (RD_AVOID) {
    if (pending < RD_MINUTE_STEPS) {
      pending = RD_MINUTE_STEPS;
    }
  } else {
    pending += RD_MINUTE_STEPS;
  }
#endif
  layer_mark_dirty(layer);
  schedule(SCHEDULE_NOW_MS);
}

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

static void init(void) {
  allocation = malloc(rd_bytes(RD_MODE));
  if (!allocation) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Core allocation failed");
    return;
  }
  state = rd_init(allocation, rd_bytes(RD_MODE), RD_MODE, RD_SEED);
  if (!state) {
    return;
  }
  rd_params(state, RD_FEED, RD_KILL, RD_DA, RD_DB, RD_DT);
  font_clock = fonts_get_system_font(cm_layout(RD_FONT)->time_key);
  font_date = fonts_get_system_font(cm_layout(RD_FONT)->date_key);
  time_t now = time(NULL);
  clock_time = *localtime(&now);
  rebuild_mask();
  window = window_create();
  layer = layer_create(GRect(0, 0, RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT));
  layer_set_update_proc(layer, draw);
  layer_add_child(window_get_root_layer(window), layer);
  window_stack_push(window, false);
  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  backlight_service_subscribe(backlight);
  app_focus_service_subscribe(focus);
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
  startup_start_ms = now_ms();
#if RD_LOG
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD init mode=%d font=%d core_bytes=%lu heap_min=%lu mask_ms=%lu",
          RD_MODE, RD_FONT, (unsigned long)rd_bytes(RD_MODE),
          (unsigned long)min_heap, (unsigned long)mask_ms);
#else
  (void)startup_start_ms;
#endif
  schedule(SCHEDULE_NOW_MS);
}

static void deinit(void) {
  stop();
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
