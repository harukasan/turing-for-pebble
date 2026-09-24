/*
 * Emery watchface: runs the shared core in timer slices and draws the field
 * with a LECO clock on top. The scheduling model is described in
 * docs/behavior.md:
 *   - startup advances STARTUP_STEPS steps in slices,
 *   - every minute adds STEPS_PER_MINUTE pending steps,
 *   - a backlight-on event animates for at most BACKLIGHT_WINDOW_MS,
 *   - each slice runs at most STEPS_PER_SLICE steps within SLICE_BUDGET_MS,
 *   - focus loss cancels timers and focus restore resumes pending work.
 */
#include "../../../core/rd.h"
#include "config.h"
#include <pebble.h>

#define STARTUP_STEPS 2000
#define STEPS_PER_MINUTE 16
#define STEPS_PER_SLICE 8
#define SLICE_BUDGET_MS 8
#define FRAME_INTERVAL_MS 100
#define SCHEDULE_NOW_MS 1
#define BACKLIGHT_WINDOW_MS 5000
#define LOG_INTERVAL_STEPS 16
/* Reported instead of a measured duration when wall time is discontinuous. */
#define TIMING_SENTINEL_MS 1000

/* Clock layout, shared with the Web preview. */
#define CLOCK_Y 78
#define CLOCK_HEIGHT 50
#define DATE_Y 128
#define DATE_HEIGHT 30

static Window *window;
static Layer *layer;
static AppTimer *timer, *light_timer;
static void *allocation, *state;
static bool focused = true, lit;
static bool timing_unreliable;
static int pending = STARTUP_STEPS;
static size_t min_heap = (size_t)-1;
static uint32_t max_compute, max_draw;

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

static void sample_heap(void) {
  size_t free_bytes = heap_bytes_free();
  if (free_bytes < min_heap) {
    min_heap = free_bytes;
  }
}

/* Write the rendered field straight into the framebuffer as RGB2, then draw
 * the clock over it. */
static void draw(Layer *unused_layer, GContext *ctx) {
  (void)unused_layer;
  uint32_t start = now_ms();
  GBitmap *frame_buffer = graphics_capture_frame_buffer(ctx);
  if (frame_buffer) {
    for (int y = 0; y < RD_DISPLAY_HEIGHT; y++) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(frame_buffer, y);
      uint8_t *pixels = rd_row(state, y, RD_PALETTE, 1);
      for (int x = row.min_x; x <= row.max_x; x++) {
        row.data[x] =
            GColorFromRGB(pixels[x * 4], pixels[x * 4 + 1], pixels[x * 4 + 2])
                .argb;
      }
    }
    graphics_release_frame_buffer(ctx, frame_buffer);
  }
  if (RD_CLOCK) {
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char text[16];
    graphics_context_set_text_color(ctx, GColorWhite);
    strftime(text, sizeof(text), "%H:%M", local_time);
    graphics_draw_text(
        ctx, text, fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
        GRect(0, CLOCK_Y, RD_DISPLAY_WIDTH, CLOCK_HEIGHT),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    strftime(text, sizeof(text), "%Y.%m.%d", local_time);
    graphics_draw_text(
        ctx, text, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
        GRect(0, DATE_Y, RD_DISPLAY_WIDTH, DATE_HEIGHT),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_draw) {
    max_draw = elapsed;
  }
  sample_heap();
}

static void schedule(uint32_t delay);

/* One timer slice: run pending startup or minute steps, or animation steps
 * while the backlight is on, stopping at the time budget. */
static void update(void *context) {
  (void)context;
  timer = NULL;
  if (!focused) {
    return;
  }
  uint32_t start = now_ms();
  bool animate = lit;
  int target = pending > 0
                   ? (pending < STEPS_PER_SLICE ? pending : STEPS_PER_SLICE)
               : animate ? STEPS_PER_SLICE
                         : 0;
  int done = 0;
  while (done < target) {
    rd_step(state, 1);
    done++;
    if (elapsed_ms(start) >= SLICE_BUDGET_MS) {
      break;
    }
  }
  if (pending > 0) {
    pending -= done;
  }
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_compute) {
    max_compute = elapsed;
  }
  if (done) {
    layer_mark_dirty(layer);
  }
  sample_heap();
  if (pending > 0 || animate) {
    schedule(FRAME_INTERVAL_MS);
  }
  if (pending == 0 || rd_steps(state) % LOG_INTERVAL_STEPS == 0) {
    APP_LOG(APP_LOG_LEVEL_INFO,
            "RD mode=%d step=%lu heap_min=%lu compute_max_ms=%lu "
            "draw_max_ms=%lu clock_invalid=%d",
            RD_MODE, (unsigned long)rd_steps(state), (unsigned long)min_heap,
            (unsigned long)max_compute, (unsigned long)max_draw,
            timing_unreliable);
  }
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
      schedule(FRAME_INTERVAL_MS);
    }
    return;
  }
  if (!lit && focused) {
    lit = true;
    light_timer = app_timer_register(BACKLIGHT_WINDOW_MS, light_expired, NULL);
    schedule(FRAME_INTERVAL_MS);
  }
}

static void focus(bool on) {
  focused = on;
  if (!on) {
    stop();
  } else {
    layer_mark_dirty(layer);
    if (pending) {
      schedule(FRAME_INTERVAL_MS);
    }
  }
}

static void tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)tick_time;
  (void)units_changed;
  pending += STEPS_PER_MINUTE;
  layer_mark_dirty(layer);
  schedule(SCHEDULE_NOW_MS);
}

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
  window = window_create();
  layer = layer_create(GRect(0, 0, RD_DISPLAY_WIDTH, RD_DISPLAY_HEIGHT));
  layer_set_update_proc(layer, draw);
  layer_add_child(window_get_root_layer(window), layer);
  window_stack_push(window, false);
  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  backlight_service_subscribe(backlight);
  app_focus_service_subscribe(focus);
  sample_heap();
  APP_LOG(APP_LOG_LEVEL_INFO, "RD init mode=%d heap_min=%lu", RD_MODE,
          (unsigned long)min_heap);
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
  free(allocation);
}

int main(void) {
  init();
  if (state) {
    app_event_loop();
  }
  deinit();
}
