/*
 * Settings from the phone. The phone sends every setting as an integer
 * tuple under the message keys of pebble/package.json. A message is applied
 * only if every value it carries is in range, and the settings are stored as
 * one persist blob that is checked the same way when it is read back.
 */
#include "settings.h"
#include "../../../core/clock_mask.h"
#include "../../../core/rd.h"
#include "config.h"
#include <pebble.h>

/* Settings code runs at launch and when the phone sends settings, so it is
 * compiled for size instead of the -O3 of the step loop. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* Version 2 added the number of custom stops and the two middle stops,
 * version 3 the date line, version 4 the clock face, and version 5 replaced
 * the five Gray-Scott coefficients with the model and its parameter vector. */
#define SETTINGS_VERSION 5
#define SETTINGS_PERSIST_KEY 1
/* The phone sends twenty-two integers: dict_calc_buffer_size for twenty-two
 * 4-byte values is 1 + 22 * (7 + 4) = 243 bytes. Nothing is sent to the
 * phone. */
#define SETTINGS_INBOX_BYTES 248
#define SETTINGS_OUTBOX_BYTES 16
#define RGB_MAX 0xffffff

static uint32_t *const KEYS[SETTING_COUNT] = {
    &MESSAGE_KEY_MODEL, &MESSAGE_KEY_P0,    &MESSAGE_KEY_P1,
    &MESSAGE_KEY_P2,    &MESSAGE_KEY_P3,    &MESSAGE_KEY_P4,
    &MESSAGE_KEY_P5,    &MESSAGE_KEY_P6,    &MESSAGE_KEY_P7,
    &MESSAGE_KEY_P8,    &MESSAGE_KEY_P9,    &MESSAGE_KEY_PALETTE,
    &MESSAGE_KEY_LOW,   &MESSAGE_KEY_HIGH,  &MESSAGE_KEY_STOPS,
    &MESSAGE_KEY_MID1,  &MESSAGE_KEY_MID2,  &MESSAGE_KEY_FONT,
    &MESSAGE_KEY_AVOID, &MESSAGE_KEY_CLOCK, &MESSAGE_KEY_DATE,
    &MESSAGE_KEY_FACE};

/* Every setting from the palette on is an integer from 0, or 2 for the
 * number of stops, to its maximum. The model and its parameters are checked
 * by the core. */
static const int32_t MAXIMUM[SETTING_COUNT - SETTING_PALETTE] = {
    RD_PALETTE_COUNT - 1, RGB_MAX, RGB_MAX, 4, RGB_MAX, RGB_MAX,
    CM_FONT_COUNT - 1,    1,       1,       1, 1};

static const uint8_t CHANGE[SETTING_COUNT - SETTING_PALETTE] = {
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_FONT,    SETTINGS_CHANGED_MASK,
    SETTINGS_CHANGED_MASK,    SETTINGS_CHANGED_LAYOUT,
    SETTINGS_CHANGED_LAYOUT};

/* The persist blob. */
typedef struct {
  int32_t version;
  int32_t value[SETTING_COUNT];
} Stored;

static Stored current;
static SettingsChanged on_change;

static bool valid(const int32_t value[SETTING_COUNT]) {
  /* rd_check_params also rejects a model the build does not run, the other
   * model of a build folded to one. */
  int model = (int)value[SETTING_MODEL];
  int count = rd_param_count(model);
  int params[RD_PARAM_MAX];
  if (count < 0) {
    return false;
  }
  /* The vector's entries after the model's own are 0. */
  for (int i = 0; i < RD_PARAM_MAX; i++) {
    int32_t v = value[SETTING_P0 + i];
    if (v < -RD_Q15_ONE || v > RD_Q15_ONE || (i >= count && v != 0)) {
      return false;
    }
    params[i] = (int)v;
  }
  if (rd_check_params(model, params, count)) {
    return false;
  }
  for (int i = SETTING_PALETTE; i < SETTING_COUNT; i++) {
    int32_t low = i == SETTING_STOPS ? 2 : 0;
    if (value[i] < low || value[i] > MAXIMUM[i - SETTING_PALETTE]) {
      return false;
    }
  }
  return true;
}

/* An integer tuple of 1 to 4 bytes, signed or unsigned, whose value fits
 * int32. The value bytes are little endian. */
static bool tuple_int(const Tuple *t, int32_t *out) {
  if ((t->type != TUPLE_INT && t->type != TUPLE_UINT) || t->length < 1 ||
      t->length > 4) {
    return false;
  }
  /* The SDK declares the value bytes as a zero-length array, so read them
   * through a plain pointer, which GCC 14 does not bounds-check. */
  const uint8_t *bytes = (const uint8_t *)t->value;
  uint32_t v = 0;
  for (int i = t->length - 1; i >= 0; i--) {
    v = v << 8 | bytes[i];
  }
  if (t->type == TUPLE_INT) {
    /* Sign-extend from the tuple's width. */
    if (t->length < 4 && (v >> (8 * t->length - 1)) & 1) {
      v |= ~(uint32_t)0 << (8 * t->length);
    }
    *out = (int32_t)v;
    return true;
  }
  if (v > INT32_MAX) {
    return false;
  }
  *out = (int32_t)v;
  return true;
}

#if RD_LOG
/* Where the settings came from: d defaults, p persist, m message. */
static void log_settings(char source) {
  const int32_t *v = current.value;
  APP_LOG(
      APP_LOG_LEVEL_INFO, "RD settings model=%ld p0=%ld,%ld,%ld,%ld,%ld src=%c",
      (long)v[SETTING_MODEL], (long)v[SETTING_P0], (long)v[SETTING_P1],
      (long)v[SETTING_P2], (long)v[SETTING_P3], (long)v[SETTING_P4], source);
  APP_LOG(APP_LOG_LEVEL_INFO, "RD settings p5=%ld,%ld,%ld,%ld,%ld",
          (long)v[SETTING_P5], (long)v[SETTING_P6], (long)v[SETTING_P7],
          (long)v[SETTING_P8], (long)v[SETTING_P9]);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD settings pal=%ld low=%06lx high=%06lx font=%ld avoid=%ld "
          "clock=%ld",
          (long)v[SETTING_PALETTE], (unsigned long)v[SETTING_LOW],
          (unsigned long)v[SETTING_HIGH], (long)v[SETTING_FONT],
          (long)v[SETTING_AVOID], (long)v[SETTING_CLOCK]);
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD settings stops=%ld mid1=%06lx mid2=%06lx date=%ld face=%ld",
          (long)v[SETTING_STOPS], (unsigned long)v[SETTING_MID1],
          (unsigned long)v[SETTING_MID2], (long)v[SETTING_DATE],
          (long)v[SETTING_FACE]);
}

static void dropped(AppMessageResult reason, void *context) {
  (void)context;
  APP_LOG(APP_LOG_LEVEL_INFO, "RD settings dropped reason=%d", (int)reason);
}
#else
#define log_settings(source) ((void)(source))
#endif

static void inbox(DictionaryIterator *iterator, void *context) {
  (void)context;
  int32_t next[SETTING_COUNT];
  bool readable = true;
  memcpy(next, current.value, sizeof next);
  for (Tuple *t = dict_read_first(iterator); t; t = dict_read_next(iterator)) {
    for (int i = 0; i < SETTING_COUNT; i++) {
      if (t->key == *KEYS[i] && !tuple_int(t, &next[i])) {
        readable = false;
      }
    }
  }
  if (!readable || !valid(next)) {
#if RD_LOG
    APP_LOG(APP_LOG_LEVEL_INFO, "RD settings rejected");
#endif
    return;
  }
  unsigned changed = 0;
  for (int i = 0; i < SETTING_COUNT; i++) {
    if (next[i] != current.value[i]) {
      changed |= i < SETTING_PALETTE ? SETTINGS_CHANGED_PATTERN
                                     : CHANGE[i - SETTING_PALETTE];
    }
  }
  if (!changed) {
    return;
  }
  memcpy(current.value, next, sizeof next);
  persist_write_data(SETTINGS_PERSIST_KEY, &current, sizeof current);
  log_settings('m');
  if (on_change) {
    on_change(changed);
  }
}

int32_t setting(int field) { return current.value[field]; }

void settings_load(void) {
  if (persist_read_data(SETTINGS_PERSIST_KEY, &current, sizeof current) ==
          (int)sizeof current &&
      current.version == SETTINGS_VERSION && valid(current.value)) {
    log_settings('p');
    return;
  }
  static const Stored defaults = {SETTINGS_VERSION,
                                  {[SETTING_MODEL] = RD_DEFAULT_MODEL,
                                   [SETTING_P0] = RD_DEFAULT_PARAMS,
                                   [SETTING_PALETTE] = RD_DEFAULT_PALETTE,
                                   [SETTING_LOW] = RD_DEFAULT_LOW,
                                   [SETTING_HIGH] = RD_DEFAULT_HIGH,
                                   [SETTING_STOPS] = RD_DEFAULT_STOPS,
                                   [SETTING_MID1] = RD_DEFAULT_MID1,
                                   [SETTING_MID2] = RD_DEFAULT_MID2,
                                   [SETTING_FONT] = RD_DEFAULT_FONT,
                                   [SETTING_AVOID] = RD_DEFAULT_AVOID,
                                   [SETTING_CLOCK] = RD_DEFAULT_CLOCK,
                                   [SETTING_DATE] = RD_DEFAULT_DATE,
                                   [SETTING_FACE] = RD_DEFAULT_FACE}};
  current = defaults;
  log_settings('d');
}

void settings_open(SettingsChanged changed) {
  on_change = changed;
  app_message_register_inbox_received(inbox);
#if RD_LOG
  app_message_register_inbox_dropped(dropped);
#endif
  app_message_open(SETTINGS_INBOX_BYTES, SETTINGS_OUTBOX_BYTES);
}

void settings_close(void) { app_message_deregister_callbacks(); }

bool settings_date(void) { return current.value[SETTING_DATE] != 0; }

bool settings_avoiding(void) {
  return current.value[SETTING_AVOID] && current.value[SETTING_CLOCK];
}

int settings_font(void) {
#ifdef RD_FONT
  return RD_FONT;
#else
  int font = (int)current.value[SETTING_FONT];
  return cm_font_available(font) ? font : RD_DEFAULT_FONT;
#endif
}

bool settings_analog(void) {
#ifdef RD_FACE
  return RD_FACE;
#else
  return current.value[SETTING_FACE] != 0;
#endif
}
