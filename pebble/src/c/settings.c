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
 * version 3 the date line, and version 4 the clock face. */
#define SETTINGS_VERSION 4
#define SETTINGS_PERSIST_KEY 1
/* The phone sends sixteen integers: dict_calc_buffer_size for sixteen
 * 4-byte values is 1 + 16 * (7 + 4) = 177 bytes. Nothing is sent to the
 * phone. */
#define SETTINGS_INBOX_BYTES 184
#define SETTINGS_OUTBOX_BYTES 16
#define RGB_MAX 0xffffff

static uint32_t *const KEYS[SETTING_COUNT] = {
    &MESSAGE_KEY_FEED,  &MESSAGE_KEY_KILL,  &MESSAGE_KEY_DA,
    &MESSAGE_KEY_DB,    &MESSAGE_KEY_DT,    &MESSAGE_KEY_PALETTE,
    &MESSAGE_KEY_LOW,   &MESSAGE_KEY_HIGH,  &MESSAGE_KEY_STOPS,
    &MESSAGE_KEY_MID1,  &MESSAGE_KEY_MID2,  &MESSAGE_KEY_FONT,
    &MESSAGE_KEY_AVOID, &MESSAGE_KEY_CLOCK, &MESSAGE_KEY_DATE,
    &MESSAGE_KEY_FACE};

/* Every setting is an integer from 0, or 2 for the number of stops, to its
 * maximum. */
static const int32_t MAXIMUM[SETTING_COUNT] = {
    RD_Q15_ONE, RD_Q15_ONE, RD_Q15_ONE,
    RD_Q15_ONE, RD_Q15_ONE, RD_PALETTE_COUNT - 1,
    RGB_MAX,    RGB_MAX,    4,
    RGB_MAX,    RGB_MAX,    CM_FONT_COUNT - 1,
    1,          1,          1,
    1};

static const uint8_t CHANGE[SETTING_COUNT] = {
    SETTINGS_CHANGED_PATTERN, SETTINGS_CHANGED_PATTERN,
    SETTINGS_CHANGED_PATTERN, SETTINGS_CHANGED_PATTERN,
    SETTINGS_CHANGED_PATTERN, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_PALETTE,
    SETTINGS_CHANGED_PALETTE, SETTINGS_CHANGED_FONT,
    SETTINGS_CHANGED_MASK,    SETTINGS_CHANGED_MASK,
    SETTINGS_CHANGED_LAYOUT,  SETTINGS_CHANGED_LAYOUT};

/* The persist blob. */
typedef struct {
  int32_t version;
  int32_t value[SETTING_COUNT];
} Stored;

static Stored current;
static SettingsChanged on_change;

static bool valid(const int32_t value[SETTING_COUNT]) {
  for (int i = 0; i < SETTING_COUNT; i++) {
    if (value[i] < (i == SETTING_STOPS ? 2 : 0) || value[i] > MAXIMUM[i]) {
      return false;
    }
  }
  return true;
}

/* A non-negative integer tuple of 1 to 4 bytes, signed or unsigned, or -1.
 * The value bytes are little endian. */
static int32_t tuple_int(const Tuple *t) {
  if ((t->type != TUPLE_INT && t->type != TUPLE_UINT) || t->length < 1 ||
      t->length > 4) {
    return -1;
  }
  uint32_t v = 0;
  for (int i = t->length - 1; i >= 0; i--) {
    v = v << 8 | t->value->data[i];
  }
  if ((t->type == TUPLE_INT && v >> (8 * t->length - 1)) || v > INT32_MAX) {
    return -1;
  }
  return (int32_t)v;
}

#if RD_LOG
/* Where the settings came from: d defaults, p persist, m message. */
static void log_settings(char source) {
  const int32_t *v = current.value;
  APP_LOG(APP_LOG_LEVEL_INFO,
          "RD settings feed=%ld kill=%ld da=%ld db=%ld dt=%ld src=%c",
          (long)v[SETTING_FEED], (long)v[SETTING_KILL], (long)v[SETTING_DA],
          (long)v[SETTING_DB], (long)v[SETTING_DT], source);
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
  memcpy(next, current.value, sizeof next);
  for (Tuple *t = dict_read_first(iterator); t; t = dict_read_next(iterator)) {
    for (int i = 0; i < SETTING_COUNT; i++) {
      if (t->key == *KEYS[i]) {
        next[i] = tuple_int(t);
      }
    }
  }
  if (!valid(next)) {
#if RD_LOG
    APP_LOG(APP_LOG_LEVEL_INFO, "RD settings rejected");
#endif
    return;
  }
  unsigned changed = 0;
  for (int i = 0; i < SETTING_COUNT; i++) {
    if (next[i] != current.value[i]) {
      changed |= CHANGE[i];
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
  static const Stored defaults = {
      SETTINGS_VERSION,
      {RD_DEFAULT_FEED, RD_DEFAULT_KILL, RD_DEFAULT_DA, RD_DEFAULT_DB,
       RD_DEFAULT_DT, RD_DEFAULT_PALETTE, RD_DEFAULT_LOW, RD_DEFAULT_HIGH,
       RD_DEFAULT_STOPS, RD_DEFAULT_MID1, RD_DEFAULT_MID2, RD_DEFAULT_FONT,
       RD_DEFAULT_AVOID, RD_DEFAULT_CLOCK, RD_DEFAULT_DATE, RD_DEFAULT_FACE}};
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
  return RD_AVOID && current.value[SETTING_AVOID] &&
         current.value[SETTING_CLOCK];
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
