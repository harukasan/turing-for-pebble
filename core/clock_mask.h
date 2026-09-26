#ifndef CLOCK_MASK_H
#define CLOCK_MASK_H
#include <stddef.h>
#include <stdint.h>

/* Clock font sets, in the order of the sets in public/fonts/clock-fonts.json
 * and of RD_FONT in the watch configuration. */
enum { CM_FONT_LECO, CM_FONT_BITHAM, CM_FONT_COUNT };

/* Grid widths accepted by cm_build, from this up to the display width. */
#define CM_MIN_WIDTH 50
/* Largest halo around the digits, in display pixels. */
#define CM_MAX_HALO 8

/* Watch layout of a font set: the system font keys of the time and date
 * lines and the top and height of their text boxes, full display width. */
typedef struct {
  const char *time_key, *date_key;
  uint8_t time_top, time_box, date_top, date_box;
} CmLayout;

/* Bytes of a cell mask for a grid: one bit per cell, rows of
 * (width + 7) / 8 bytes, cell x in bit x % 8 of byte x / 8. This is the
 * mask format of rd_mask. */
size_t cm_bytes(int width, int height);

/* Layout of a font set, or NULL for an unknown set. */
const CmLayout *cm_layout(int font);

/* Whether the glyphs of a font set are compiled in. A build that defines
 * RD_FONT has only that set. */
int cm_font_available(int font);

/* Build the mask of the clock digits for a grid of width cells from
 * CM_MIN_WIDTH to 200 and height width * 228 / 200: the time HH:MM and the
 * date YYYY.MM.DD drawn in the font set as the watch draws them, each glyph
 * pixel widened by a square halo of `halo` display pixels and clipped to
 * the display, and every grid cell holding the center of such a pixel
 * marked. Returns 0, or -1 without touching the mask
 * when an argument is out of range. */
int cm_build(uint8_t *mask, int width, int height, int font, int hour,
             int minute, int year, int month, int day, int halo);

/* A writable display row y of 200 pixels, or NULL to skip the row. */
typedef uint8_t *(*CmRow)(void *context, int y);

/* Draw the time and date of a font set as the watch draws them, setting
 * each glyph pixel of the display to color through row. The pixels are
 * those of graphics_draw_text with the system fonts, verified against the
 * emulator. Returns 0, or -1 for invalid arguments without drawing. */
int cm_draw(CmRow row, void *context, uint8_t color, int font, int hour,
            int minute, int year, int month, int day);

/* Analog face. Angles are in units of 1/CM_TURN of a turn, clockwise from
 * 12 o'clock. The hands are round-tipped capsules from the dial center, in
 * display pixels. */
#define CM_TURN 1440
#define CM_DIAL_X 100
#define CM_DIAL_Y 104
#define CM_MINUTE_LENGTH 80
#define CM_MINUTE_RADIUS 2
#define CM_HOUR_LENGTH 54
#define CM_HOUR_RADIUS 3
#define CM_CENTER_RADIUS 5

/* Angle of the hour hand at hour 0-23 and minute 0-59, or -1. */
int cm_hour_angle(int hour, int minute);

/* Angle of the minute hand at minute 0-59, or -1. */
int cm_minute_angle(int minute);

/* Angle of a hand sweeping from angle `from` to angle `to` the shorter way
 * round, elapsed_ms into a sweep of duration_ms (1 to 2,097,151), with a
 * quadratic ease-out: `from` until it starts, exactly `to` once it ends.
 * Returns -1 for an angle out of range or an invalid duration. */
int cm_sweep_angle(int from, int to, int elapsed_ms, int duration_ms);

/* Top of the date text box of the analog face for a font set, or -1. */
int cm_analog_date_top(int font);

/* Build the mask of the analog face, as cm_build does for the digits: the
 * hour and minute hands at their angles, the center disk, and the date
 * YYYY.MM.DD centered at the bottom. At width 200 and halo 0 the mask is
 * the pixel bitmap of the face. Returns 0, or -1 without touching the mask
 * when an argument is out of range. */
int cm_build_analog(uint8_t *mask, int width, int height, int font,
                    int hour_angle, int minute_angle, int year, int month,
                    int day, int halo);

/* Draw the analog face as cm_draw draws the digits: the pixels of the
 * 200-wide halo 0 mask of cm_build_analog. Returns 0, or -1 for invalid
 * arguments without drawing. */
int cm_draw_analog(CmRow row, void *context, uint8_t color, int font,
                   int hour_angle, int minute_angle, int year, int month,
                   int day);
#endif
