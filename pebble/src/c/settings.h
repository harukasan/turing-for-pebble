#ifndef SETTINGS_H
#define SETTINGS_H
/*
 * Settings sent by the phone: the pattern coefficients, the palette and the
 * stops of the custom palette, the clock font, digit avoidance, the clock,
 * and the date line. They persist on the watch, config.h holds the defaults,
 * and docs/behavior.md describes how each change is applied.
 */
#include <stdbool.h>
#include <stdint.h>

/* The settings, in the order of the message keys of pebble/package.json:
 * the Q15 coefficients of rd_params, the palette (RD_PALETTE_*), the dark
 * and light stops of RD_PALETTE_CUSTOM as 0xRRGGBB, its number of stops (2
 * to 4) and the stops between them, the clock font set (CM_FONT_*), and
 * digit avoidance, the clock, and the date line as 0 or 1. */
enum {
  SETTING_FEED,
  SETTING_KILL,
  SETTING_DA,
  SETTING_DB,
  SETTING_DT,
  SETTING_PALETTE,
  SETTING_LOW,
  SETTING_HIGH,
  SETTING_STOPS,
  SETTING_MID1,
  SETTING_MID2,
  SETTING_FONT,
  SETTING_AVOID,
  SETTING_CLOCK,
  SETTING_DATE,
  SETTING_COUNT
};

/* What a received message changed, passed to the callback. */
enum {
  SETTINGS_CHANGED_PATTERN = 1,
  SETTINGS_CHANGED_PALETTE = 2,
  SETTINGS_CHANGED_FONT = 4,
  SETTINGS_CHANGED_MASK = 8,
  SETTINGS_CHANGED_LAYOUT = 16
};

typedef void (*SettingsChanged)(unsigned changed);

/* The current value of a setting. */
int32_t setting(int field);
/* Read the stored settings, or the defaults if none are stored or they are
 * invalid. */
void settings_load(void);
/* Receive settings from the phone. A valid message that changes anything is
 * stored and reported to the callback. */
void settings_open(SettingsChanged changed);
void settings_close(void);
/* Whether its digits are avoided, which needs the clock and a build with
 * the mask area. */
bool settings_avoiding(void);
/* Whether the clock shows the date line under the time, else the time
 * alone centered on the display. */
bool settings_date(void);
/* The clock font set to draw: the stored one when compiled in. */
int settings_font(void);
#endif
