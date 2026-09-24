#include "../../../core/rd.h"
#include "config.h"
#include <pebble.h>
static Window *window;
static Layer *layer;
static AppTimer *timer, *light_timer;
static void *allocation, *state;
static bool focused = true, lit;
static bool timing_unreliable;
static int pending = 2000;
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
  if (delta < 0 || delta >= 1000) {
    timing_unreliable = true;
    return 1000;
  }
  return (uint32_t)delta;
}
static void sample_heap(void) {
  size_t free = heap_bytes_free();
  if (free < min_heap)
    min_heap = free;
}
static void draw(Layer *l, GContext *ctx) {
  (void)l;
  uint32_t start = now_ms();
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (fb) {
    for (int y = 0; y < 228; y++) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
      uint8_t *rgb = rd_row(state, y, RD_PALETTE, 1);
      for (int x = row.min_x; x <= row.max_x; x++)
        row.data[x] =
            GColorFromRGB(rgb[x * 4], rgb[x * 4 + 1], rgb[x * 4 + 2]).argb;
    }
    graphics_release_frame_buffer(ctx, fb);
  }
  if (RD_CLOCK) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char text[16];
    graphics_context_set_text_color(ctx, GColorWhite);
    strftime(text, sizeof(text), "%H:%M", tm);
    graphics_draw_text(ctx, text,
                       fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS),
                       GRect(0, 78, 200, 50), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    strftime(text, sizeof(text), "%Y.%m.%d", tm);
    graphics_draw_text(
        ctx, text, fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS),
        GRect(0, 128, 200, 30), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentCenter, NULL);
  }
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_draw)
    max_draw = elapsed;
  sample_heap();
}
static void schedule(uint32_t delay);
static void update(void *context) {
  (void)context;
  timer = NULL;
  if (!focused)
    return;
  uint32_t start = now_ms();
  bool animate = lit;
  int target = pending > 0 ? (pending < 8 ? pending : 8) : animate ? 8 : 0;
  int done = 0;
  while (done < target) {
    rd_step(state, 1);
    done++;
    if (elapsed_ms(start) >= 8)
      break;
  }
  if (pending > 0)
    pending -= done;
  uint32_t elapsed = elapsed_ms(start);
  if (elapsed > max_compute)
    max_compute = elapsed;
  if (done)
    layer_mark_dirty(layer);
  sample_heap();
  if (pending > 0 || animate)
    schedule(100);
  if (pending == 0 || rd_steps(state) % 16 == 0)
    APP_LOG(APP_LOG_LEVEL_INFO,
            "RD mode=%d step=%lu heap_min=%lu compute_max_ms=%lu "
            "draw_max_ms=%lu clock_invalid=%d",
            RD_MODE, (unsigned long)rd_steps(state), (unsigned long)min_heap,
            (unsigned long)max_compute, (unsigned long)max_draw,
            timing_unreliable);
}
static void schedule(uint32_t delay) {
  if (!timer && focused)
    timer = app_timer_register(delay, update, NULL);
}
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
static void backlight(bool on) {
  if (!on) {
    stop();
    if (pending)
      schedule(100);
    return;
  }
  if (!lit && focused) {
    lit = true;
    light_timer = app_timer_register(5000, light_expired, NULL);
    schedule(100);
  }
}
static void focus(bool on) {
  focused = on;
  if (!on)
    stop();
  else {
    layer_mark_dirty(layer);
    if (pending)
      schedule(100);
  }
}
static void tick(struct tm *tm, TimeUnits changed) {
  (void)tm;
  (void)changed;
  pending += 16;
  layer_mark_dirty(layer);
  schedule(1);
}
static void init(void) {
  allocation = malloc(rd_bytes(RD_MODE));
  if (!allocation) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Core allocation failed");
    return;
  }
  state = rd_init(allocation, rd_bytes(RD_MODE), RD_MODE, RD_SEED);
  if (!state)
    return;
  rd_params(state, RD_FEED, RD_KILL, RD_DA, RD_DB, RD_DT);
  window = window_create();
  layer = layer_create(GRect(0, 0, 200, 228));
  layer_set_update_proc(layer, draw);
  layer_add_child(window_get_root_layer(window), layer);
  window_stack_push(window, false);
  tick_timer_service_subscribe(MINUTE_UNIT, tick);
  backlight_service_subscribe(backlight);
  app_focus_service_subscribe(focus);
  sample_heap();
  APP_LOG(APP_LOG_LEVEL_INFO, "RD init mode=%d heap_min=%lu", RD_MODE,
          (unsigned long)min_heap);
  schedule(1);
}
static void deinit(void) {
  stop();
  tick_timer_service_unsubscribe();
  backlight_service_unsubscribe();
  app_focus_service_unsubscribe();
  if (layer)
    layer_destroy(layer);
  if (window)
    window_destroy(window);
  free(allocation);
}
int main(void) {
  init();
  if (state)
    app_event_loop();
  deinit();
}
