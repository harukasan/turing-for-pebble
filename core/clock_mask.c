/*
 * Cell mask of the clock digits, shared by the watch and the Web preview so
 * that both hold the same cells at the equilibrium. The glyphs come from
 * core/clock_glyphs.h, extracted from the PebbleOS system fonts that the
 * watch draws with, and are placed as graphics_draw_text places them: each
 * line centered on the display by the sum of the glyph advances. The
 * analog face adds hands rasterized from the sine table in
 * core/clock_trig.h. Integer arithmetic only, so every build gives the same
 * bits.
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
/* A watch build defines RD_FACE and compiles the analog face only when it
 * shows it. */
#if !defined(RD_FACE) || RD_FACE
#include "clock_trig.h"
#endif

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
  return font >= 0 && font < CM_FONT_COUNT && CM_FONTS[font][CM_DATE].glyphs;
}

static int cm_date_valid(int year, int month, int day) {
  return year >= 0 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 &&
         day <= 31;
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

/* Glyph indices of the date line YYYY.MM.DD. */
static void cm_date_text(uint8_t out[CM_DATE_LENGTH], int year, int month,
                         int day) {
  out[0] = (uint8_t)(year / 1000);
  out[1] = (uint8_t)(year / 100 % 10);
  out[2] = (uint8_t)(year / 10 % 10);
  out[3] = (uint8_t)(year % 10);
  out[4] = CM_SEPARATOR;
  out[5] = (uint8_t)(month / 10);
  out[6] = (uint8_t)(month % 10);
  out[7] = CM_SEPARATOR;
  out[8] = (uint8_t)(day / 10);
  out[9] = (uint8_t)(day % 10);
}

/* Visit the glyph pixels of the time and date lines (only check the
 * arguments when visit is NULL), or return -1 for an unavailable font or
 * time line (an analog watch build has none) or a date or time out of
 * range. */
#if defined(__GNUC__)
/* Kept out of line: the mask builder and the text drawer share one copy. */
__attribute__((noinline))
#endif
static int cm_visit(int font, int hour, int minute, int year, int month,
                    int day, CmVisit visit, void *context) {
  if (!cm_font_available(font) || !CM_FONTS[font][CM_TIME].glyphs || hour < 0 ||
      hour > 23 || minute < 0 || minute > 59 ||
      !cm_date_valid(year, month, day)) {
    return -1;
  }
  if (!visit) {
    return 0;
  }
  const uint8_t time[CM_TIME_LENGTH] = {
      (uint8_t)(hour / 10), (uint8_t)(hour % 10), CM_SEPARATOR,
      (uint8_t)(minute / 10), (uint8_t)(minute % 10)};
  uint8_t date[CM_DATE_LENGTH];
  cm_date_text(date, year, month, day);
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

#if !defined(RD_FACE) || RD_FACE
/* Top of the date text box of the analog face for each font set, below the
 * reach of the hands (display row 186): LECO digits then occupy rows 210 to
 * 223 and Bitham digits rows 201 to 221. */
static const uint8_t CM_ANALOG_DATE_TOP[CM_FONT_COUNT] = {204 /* LECO */,
                                                          192 /* Bitham */};

#define CM_QUADRANT (CM_TURN / 4)

/* Sine of angle a in [0, CM_TURN) in Q14, folded onto the first quadrant so
 * that sin(CM_TURN - a) = -sin(a) and sin(CM_TURN / 2 - a) = sin(a)
 * exactly. */
static int cm_sin(int a) {
  int r = a % CM_QUADRANT;
  switch (a / CM_QUADRANT) {
  case 0:
    return CM_SIN_Q14[r];
  case 1:
    return CM_SIN_Q14[CM_QUADRANT - r];
  case 2:
    return -CM_SIN_Q14[r];
  default:
    return -CM_SIN_Q14[CM_QUADRANT - r];
  }
}

static int cm_cos(int a) { return cm_sin((a + CM_QUADRANT) % CM_TURN); }

/* length times a Q14 sine, rounded half away from zero so that opposite
 * sines give opposite offsets. */
static int cm_scale(int length, int s) {
  int magnitude = (length * (s < 0 ? -s : s) + (1 << 13)) >> 14;
  return s < 0 ? -magnitude : magnitude;
}

/* Whether pixel (px, py) lies within distance sqrt(r2) of the segment from
 * (x0, y0) to (x0 + dx, y0 + dy) of squared length l2, in exact integer
 * arithmetic. With t the projection of the pixel on the segment scaled by
 * l2, the squared distance to the segment times l2 is |P - C0|^2 l2 - t^2
 * between the ends. */
static int cm_inside(int px, int py, int x0, int y0, int dx, int dy, int l2,
                     int r2) {
  int ux = px - x0, uy = py - y0;
  int t = ux * dx + uy * dy;
  if (t <= 0) {
    return ux * ux + uy * uy <= r2;
  }
  if (t >= l2) {
    int vx = ux - dx, vy = uy - dy;
    return vx * vx + vy * vy <= r2;
  }
  return (ux * ux + uy * uy) * l2 - t * t <= r2 * l2;
}

/* n / d rounded half away from zero, for d > 0. */
static int cm_div_round(int n, int d) {
  return n < 0 ? -((-n + d / 2) / d) : (n + d / 2) / d;
}

/* Visit the pixels of the capsule of radius r around the segment from
 * (x0, y0) to (x1, y1), row by row from the top. The pixels of a row form
 * one run, since the capsule is convex, and the run holds the pixel nearest
 * to the segment: the segment's column at that row between the ends, else
 * the column of the nearer end. The scan goes left from there while inside,
 * then right. A zero-length segment gives a disk. */
static void cm_capsule(int x0, int y0, int x1, int y1, int r, CmVisit visit,
                       void *context) {
  if (y1 < y0) {
    int x = x0, y = y0;
    x0 = x1;
    y0 = y1;
    x1 = x;
    y1 = y;
  }
  int dx = x1 - x0, dy = y1 - y0;
  int l2 = dx * dx + dy * dy, r2 = r * r;
  int first = y0 - r < 0 ? 0 : y0 - r;
  int last = y1 + r >= RD_DISPLAY_HEIGHT ? RD_DISPLAY_HEIGHT - 1 : y1 + r;
  for (int y = first; y <= last; y++) {
    int seed = y <= y0   ? x0
               : y >= y1 ? x1
                         : x0 + cm_div_round(dx * (y - y0), dy);
    seed = seed < 0                   ? 0
           : seed >= RD_DISPLAY_WIDTH ? RD_DISPLAY_WIDTH - 1
                                      : seed;
    for (int step = -1; step <= 1; step += 2) {
      for (int x = step < 0 ? seed : seed + 1;
           x >= 0 && x < RD_DISPLAY_WIDTH &&
           cm_inside(x, y, x0, y0, dx, dy, l2, r2);
           x += step) {
        visit(context, x, y);
      }
    }
  }
}

/* Visit a hand of a length and radius at an angle. */
static void cm_hand(int angle, int length, int radius, CmVisit visit,
                    void *context) {
  cm_capsule(CM_DIAL_X, CM_DIAL_Y, CM_DIAL_X + cm_scale(length, cm_sin(angle)),
             CM_DIAL_Y - cm_scale(length, cm_cos(angle)), radius, visit,
             context);
}

/* Visit the pixels of the analog face: the hour hand, the minute hand, the
 * center disk, and the date line (only check the arguments when visit is
 * NULL), or return -1 for an unavailable font or an angle or date out of
 * range. */
#if defined(__GNUC__)
/* Kept out of line: the mask builder and the drawer share one copy. */
__attribute__((noinline))
#endif
static int cm_visit_analog(int font, int hour_angle, int minute_angle, int year,
                           int month, int day, CmVisit visit, void *context) {
  if (!cm_font_available(font) || hour_angle < 0 || hour_angle >= CM_TURN ||
      minute_angle < 0 || minute_angle >= CM_TURN ||
      !cm_date_valid(year, month, day)) {
    return -1;
  }
  if (!visit) {
    return 0;
  }
  cm_hand(hour_angle, CM_HOUR_LENGTH, CM_HOUR_RADIUS, visit, context);
  cm_hand(minute_angle, CM_MINUTE_LENGTH, CM_MINUTE_RADIUS, visit, context);
  cm_capsule(CM_DIAL_X, CM_DIAL_Y, CM_DIAL_X, CM_DIAL_Y, CM_CENTER_RADIUS,
             visit, context);
  uint8_t date[CM_DATE_LENGTH];
  cm_date_text(date, year, month, day);
  cm_line(&CM_FONTS[font][CM_DATE], date, CM_DATE_LENGTH,
          CM_ANALOG_DATE_TOP[font], visit, context);
  return 0;
}

int cm_build_analog(uint8_t *mask, int width, int height, int font,
                    int hour_angle, int minute_angle, int year, int month,
                    int day, int halo) {
  if (!mask || width < CM_MIN_WIDTH || width > RD_DISPLAY_WIDTH ||
      height != width * RD_DISPLAY_HEIGHT / RD_DISPLAY_WIDTH ||
      !cm_font_available(font) || halo < 0 || halo > CM_MAX_HALO ||
      cm_visit_analog(font, hour_angle, minute_angle, year, month, day, NULL,
                      NULL) != 0) {
    return -1;
  }
  CmMaskTarget target = {mask, width, height, halo};
  memset(mask, 0, cm_bytes(width, height));
  cm_visit_analog(font, hour_angle, minute_angle, year, month, day,
                  cm_mask_pixel, &target);
  return 0;
}

int cm_draw_analog(CmRow row, void *context, uint8_t color, int font,
                   int hour_angle, int minute_angle, int year, int month,
                   int day) {
  if (!row) {
    return -1;
  }
  CmDrawTarget target = {row, context, color, -1, NULL};
  return cm_visit_analog(font, hour_angle, minute_angle, year, month, day,
                         cm_draw_pixel, &target);
}

int cm_hour_angle(int hour, int minute) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return -1;
  }
  return hour % 12 * (CM_TURN / 12) + minute * (CM_TURN / 720);
}

int cm_minute_angle(int minute) {
  return minute < 0 || minute > 59 ? -1 : minute * (CM_TURN / 60);
}

/* Longest sweep, so that elapsed_ms * 1024 stays within 32 bits. */
#define CM_SWEEP_MAX_MS ((1 << 21) - 1)

int cm_sweep_angle(int from, int to, int elapsed_ms, int duration_ms) {
  if (from < 0 || from >= CM_TURN || to < 0 || to >= CM_TURN ||
      duration_ms <= 0 || duration_ms > CM_SWEEP_MAX_MS) {
    return -1;
  }
  if (elapsed_ms <= 0) {
    return from;
  }
  if (elapsed_ms >= duration_ms) {
    return to;
  }
  int delta = (to - from + CM_TURN) % CM_TURN;
  if (delta > CM_TURN / 2) {
    delta -= CM_TURN;
  }
  int linear = elapsed_ms * 1024 / duration_ms;
  int rest = 1024 - linear;
  int eased = 1024 - rest * rest / 1024;
  return ((from + (delta * eased + 512) / 1024) % CM_TURN + CM_TURN) % CM_TURN;
}

int cm_analog_date_top(int font) {
  return font >= 0 && font < CM_FONT_COUNT ? CM_ANALOG_DATE_TOP[font] : -1;
}
#endif
