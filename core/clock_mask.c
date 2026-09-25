/*
 * Cell mask of the clock digits, shared by the watch and the Web preview so
 * that both hold the same cells at the equilibrium. The glyphs come from
 * core/clock_glyphs.h, extracted from the PebbleOS system fonts that the
 * watch draws with, and are placed as graphics_draw_text places them: each
 * line centered on the display by the sum of the glyph advances. Integer
 * arithmetic only, so every build gives the same bits.
 */
#include "clock_mask.h"
#include "rd.h"
#include <string.h>

/* One glyph: its bitmap size, offsets from the pen position and the top of
 * the text box, advance, and first bit in the bit stream of its font. */
typedef struct {
  uint8_t width, height;
  int8_t left, top;
  uint8_t advance;
  uint16_t bit;
} CmGlyph;

/* The digits 0-9 and the separator of one line. */
#define CM_GLYPHS 11
#define CM_SEPARATOR 10

typedef struct {
  const CmGlyph *glyphs;
  const uint8_t *bits;
} CmFont;

#include "clock_glyphs.h"

enum { CM_TIME, CM_DATE };

#define CM_TIME_LENGTH 5
#define CM_DATE_LENGTH 10

size_t cm_bytes(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  return (size_t)((width + 7) / 8) * (size_t)height;
}

const CmLayout *cm_layout(int font) {
  return font >= 0 && font < CM_FONT_COUNT ? &CM_LAYOUTS[font] : NULL;
}

int cm_font_available(int font) {
  return font >= 0 && font < CM_FONT_COUNT && CM_FONTS[font][CM_TIME].glyphs;
}

/* Mark the cells covering the halo square around display pixel (x, y). A
 * grid axis is the display axis or half of it, so the cells covering a
 * pixel range are exactly the range of the cells of its end pixels. */
static void cm_splat(uint8_t *mask, int width, int height, int x, int y,
                     int halo) {
  int x0 = x - halo < 0 ? 0 : x - halo;
  int x1 = x + halo >= RD_DISPLAY_WIDTH ? RD_DISPLAY_WIDTH - 1 : x + halo;
  int y0 = y - halo < 0 ? 0 : y - halo;
  int y1 = y + halo >= RD_DISPLAY_HEIGHT ? RD_DISPLAY_HEIGHT - 1 : y + halo;
  if (x0 > x1 || y0 > y1) {
    return;
  }
  int stride = (width + 7) / 8;
  int gx0 = x0 * width / RD_DISPLAY_WIDTH, gx1 = x1 * width / RD_DISPLAY_WIDTH;
  int gy0 = y0 * height / RD_DISPLAY_HEIGHT;
  int gy1 = y1 * height / RD_DISPLAY_HEIGHT;
  for (int gy = gy0; gy <= gy1; gy++) {
    uint8_t *row = mask + (size_t)gy * stride;
    for (int gx = gx0; gx <= gx1; gx++) {
      row[gx >> 3] |= (uint8_t)(1u << (gx & 7));
    }
  }
}

/* Mark one line of glyph indices whose text box starts at display row
 * top. */
static void cm_line(uint8_t *mask, int width, int height, const CmFont *font,
                    const uint8_t *text, int length, int top, int halo) {
  int advance = 0;
  for (int i = 0; i < length; i++) {
    advance += font->glyphs[text[i]].advance;
  }
  int pen = (RD_DISPLAY_WIDTH - advance) / 2;
  for (int i = 0; i < length; i++) {
    const CmGlyph *glyph = &font->glyphs[text[i]];
    unsigned bit = glyph->bit;
    for (int row = 0; row < glyph->height; row++) {
      for (int col = 0; col < glyph->width; col++, bit++) {
        if (font->bits[bit >> 3] >> (bit & 7) & 1) {
          cm_splat(mask, width, height, pen + glyph->left + col,
                   top + glyph->top + row, halo);
        }
      }
    }
    pen += glyph->advance;
  }
}

int cm_build(uint8_t *mask, int width, int height, int font, int hour,
             int minute, int year, int month, int day, int halo) {
  if (!mask || (width != RD_DISPLAY_WIDTH && width != RD_DISPLAY_WIDTH / 2) ||
      height != width * RD_DISPLAY_HEIGHT / RD_DISPLAY_WIDTH ||
      !cm_font_available(font) || hour < 0 || hour > 23 || minute < 0 ||
      minute > 59 || year < 0 || year > 9999 || month < 1 || month > 12 ||
      day < 1 || day > 31 || halo < 0 || halo > CM_MAX_HALO) {
    return -1;
  }
  const uint8_t time[CM_TIME_LENGTH] = {
      (uint8_t)(hour / 10), (uint8_t)(hour % 10), CM_SEPARATOR,
      (uint8_t)(minute / 10), (uint8_t)(minute % 10)};
  const uint8_t date[CM_DATE_LENGTH] = {(uint8_t)(year / 1000),
                                        (uint8_t)(year / 100 % 10),
                                        (uint8_t)(year / 10 % 10),
                                        (uint8_t)(year % 10),
                                        CM_SEPARATOR,
                                        (uint8_t)(month / 10),
                                        (uint8_t)(month % 10),
                                        CM_SEPARATOR,
                                        (uint8_t)(day / 10),
                                        (uint8_t)(day % 10)};
  const CmLayout *layout = &CM_LAYOUTS[font];
  memset(mask, 0, cm_bytes(width, height));
  cm_line(mask, width, height, &CM_FONTS[font][CM_TIME], time, CM_TIME_LENGTH,
          layout->time_top, halo);
  cm_line(mask, width, height, &CM_FONTS[font][CM_DATE], date, CM_DATE_LENGTH,
          layout->date_top, halo);
  return 0;
}
