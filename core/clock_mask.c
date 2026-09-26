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

/* Grid cell holding the center of display pixel p on an axis of n cells
 * spanning size pixels. */
static int cm_cell(int p, int n, int size) {
  return (2 * p + 1) * n / (2 * size);
}

/* Mark the cells holding the centers of the halo square around display
 * pixel (x, y). A grid axis has at most as many cells as display pixels, so
 * consecutive pixel centers fall in the same or the next cell, and the
 * cells of a pixel range are exactly the range of the cells of its end
 * pixels. */
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
  int gx0 = cm_cell(x0, width, RD_DISPLAY_WIDTH);
  int gx1 = cm_cell(x1, width, RD_DISPLAY_WIDTH);
  int gy0 = cm_cell(y0, height, RD_DISPLAY_HEIGHT);
  int gy1 = cm_cell(y1, height, RD_DISPLAY_HEIGHT);
  for (int gy = gy0; gy <= gy1; gy++) {
    uint8_t *row = mask + (size_t)gy * stride;
    for (int gx = gx0; gx <= gx1; gx++) {
      row[gx >> 3] |= (uint8_t)(1u << (gx & 7));
    }
  }
}

/* Mark one line of glyph indices whose text box starts at display row
 * top. */
/* Visit every glyph pixel of one line of glyph indices whose text box
 * starts at display row top, centered by the sum of the advances. */
typedef void (*CmVisit)(void *context, int x, int y);

static void cm_line(const CmFont *font, const uint8_t *text, int length,
                    int top, CmVisit visit, void *context) {
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
          visit(context, pen + glyph->left + col, top + glyph->top + row);
        }
      }
    }
    pen += glyph->advance;
  }
}

/* Visit the glyph pixels of the time and date lines (only check the
 * arguments when visit is NULL), or return -1 for an unavailable font or a
 * date or time out of range. */
#if defined(__GNUC__)
/* Kept out of line: the mask builder and the text drawer share one copy. */
__attribute__((noinline))
#endif
static int cm_visit(int font, int hour, int minute, int year, int month,
                    int day, CmVisit visit, void *context) {
  if (!cm_font_available(font) || hour < 0 || hour > 23 || minute < 0 ||
      minute > 59 || year < 0 || year > 9999 || month < 1 || month > 12 ||
      day < 1 || day > 31) {
    return -1;
  }
  if (!visit) {
    return 0;
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
  cm_line(&CM_FONTS[font][CM_TIME], time, CM_TIME_LENGTH, layout->time_top,
          visit, context);
  cm_line(&CM_FONTS[font][CM_DATE], date, CM_DATE_LENGTH, layout->date_top,
          visit, context);
  return 0;
}

typedef struct {
  uint8_t *mask;
  int width, height, halo;
} CmMaskTarget;

static void cm_mask_pixel(void *context, int x, int y) {
  CmMaskTarget *target = context;
  cm_splat(target->mask, target->width, target->height, x, y, target->halo);
}

int cm_build(uint8_t *mask, int width, int height, int font, int hour,
             int minute, int year, int month, int day, int halo) {
  if (!mask || width < CM_MIN_WIDTH || width > RD_DISPLAY_WIDTH ||
      height != width * RD_DISPLAY_HEIGHT / RD_DISPLAY_WIDTH ||
      !cm_font_available(font) || halo < 0 || halo > CM_MAX_HALO ||
      cm_visit(font, hour, minute, year, month, day, NULL, NULL) != 0) {
    return -1;
  }
  CmMaskTarget target = {mask, width, height, halo};
  memset(mask, 0, cm_bytes(width, height));
  cm_visit(font, hour, minute, year, month, day, cm_mask_pixel, &target);
  return 0;
}

typedef struct {
  CmRow row;
  void *context;
  uint8_t color;
  int y;
  uint8_t *pixels;
} CmDrawTarget;

static void cm_draw_pixel(void *context, int x, int y) {
  CmDrawTarget *target = context;
  if (x < 0 || x >= RD_DISPLAY_WIDTH || y < 0 || y >= RD_DISPLAY_HEIGHT) {
    return;
  }
  if (y != target->y) {
    target->y = y;
    target->pixels = target->row(target->context, y);
  }
  if (target->pixels) {
    target->pixels[x] = target->color;
  }
}

int cm_draw(CmRow row, void *context, uint8_t color, int font, int hour,
            int minute, int year, int month, int day) {
  if (!row) {
    return -1;
  }
  CmDrawTarget target = {row, context, color, -1, NULL};
  return cm_visit(font, hour, minute, year, month, day, cm_draw_pixel, &target);
}
