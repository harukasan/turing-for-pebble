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
 * lines and the top and height of their text boxes, full display width,
 * and the time line top without the date (cm_time_top). */
typedef struct {
  const char *time_key, *date_key;
  uint8_t time_top, time_box, date_top, date_box, time_alone_top;
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

/* Top of the text box of the time line of a font set: the layout's with the
 * date, and without it the top that centers the ink of the time glyphs
 * (the digits and the separator) on the display. -1 for an unavailable
 * font. */
int cm_time_top(int font, int date);

/* Build the mask of the clock digits for a grid of width cells from
 * CM_MIN_WIDTH to 200 and height width * 228 / 200: the time HH:MM and,
 * when date is nonzero, the date YYYY.MM.DD, drawn in the font set as the
 * watch draws them with the time line at cm_time_top, each glyph
 * pixel widened by a square halo of `halo` display pixels and clipped to
 * the display, and every grid cell holding the center of such a pixel
 * marked. Returns 0, or -1 without touching the mask
 * when an argument is out of range. */
int cm_build(uint8_t *mask, int width, int height, int font, int hour,
             int minute, int year, int month, int day, int halo, int date);

/* A writable display row y of 200 pixels, or NULL to skip the row. */
typedef uint8_t *(*CmRow)(void *context, int y);

/* Draw the time and, when date is nonzero, the date of a font set as the
 * watch draws them, the time line at cm_time_top, setting
 * each glyph pixel of the display to color through row. The pixels are
 * those of graphics_draw_text with the system fonts, verified against the
 * emulator. Returns 0, or -1 for invalid arguments without drawing. */
int cm_draw(CmRow row, void *context, uint8_t color, int font, int hour,
            int minute, int year, int month, int day, int date);
#endif
