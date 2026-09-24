#include "rd.h"
#include <string.h>
#define Q 32768
#define MAGIC 0x52443101u
#ifdef RD_MODE
#define BITS(s) (RD_MODE == 1 ? 16 : 8)
#else
#define BITS(s) ((s)->bits)
#endif
typedef struct {
  uint32_t magic, seed, step;
  int w, h, bits, f, k, da, db, dt;
} State;

static int mode_ok(int m) {
#ifdef RD_MODE
  return m == RD_MODE;
#else
  return m >= 0 && m <= 2;
#endif
}

static int width(int m) { return m == 0 ? 200 : 100; }

size_t rd_memory(int m, int c) {
  if (!mode_ok(m)) {
    return 0;
  }
  size_t w = width(m), b = m == 1 ? 2 : 1;
  switch (c) {
  case 0:
    return w * (w * 228 / 200) * 2 * b;
  case 1:
    return w * 8 * b;
  case 2:
    return sizeof(State);
  case 3:
    return 800;
  case 4:
    return 3;
  default:
    return 0;
  }
}

size_t rd_bytes(int m) {
  size_t n = 0;
  for (int i = 0; i < 5; i++) {
    n += rd_memory(m, i);
  }
  return n;
}

static uint8_t *field(State *s, int c) {
  return (uint8_t *)(s + 1) + (size_t)c * s->w * s->h * (BITS(s) / 8);
}

static int raw(State *s, const uint8_t *p, int i) {
  return BITS(s) == 8 ? p[i] : ((const uint16_t *)p)[i];
}

static void put(State *s, uint8_t *p, int i, int v) {
  if (BITS(s) == 8) {
    p[i] = (uint8_t)v;
  } else {
    ((uint16_t *)p)[i] = (uint16_t)v;
  }
}

static int expand(State *s, int v) {
  return BITS(s) == 8 ? (v * Q + 127) / 255 : v;
}

static int roundq(int64_t v) {
  return v < 0 ? -(int)((-v + Q / 2) / Q) : (int)((v + Q / 2) / Q);
}

static int mul(int a, int b) { return roundq((int64_t)a * b); }

static uint32_t mix(uint32_t v) {
  v ^= v >> 16;
  v *= 0x7feb352du;
  v ^= v >> 15;
  v *= 0x846ca68bu;
  return v ^ (v >> 16);
}

static int pack(State *s, int v, int i, int c) {
  if (v < 0) {
    v = 0;
  }
  if (v > Q) {
    v = Q;
  }
  if (BITS(s) == 16) {
    return v;
  }
  uint32_t n = (uint32_t)v * 255,
           r = mix(s->seed ^ mix((uint32_t)i) ^ mix(s->step) ^
                   (c ? 0xa511e9b3u : 0x63d83595u));
  return (int)(n / Q) + ((r & (Q - 1)) < n % Q);
}

static State *valid(void *p) {
  State *s = p;
  return s && s->magic == MAGIC ? s : NULL;
}

int rd_params(void *p, int f, int k, int da, int db, int dt) {
  State *s = valid(p);
  if (!s || f < 0 || f > Q || k < 0 || k > Q || da < 0 || da > Q || db < 0 ||
      db > Q || dt < 0 || dt > Q) {
    return -1;
  }
  s->f = f;
  s->k = k;
  s->da = da;
  s->db = db;
  s->dt = dt;
  return 0;
}

int rd_seed(void *p, int x, int y, int r) {
  State *s = valid(p);
  if (!s || x < 0 || x >= 200 || y < 0 || y >= 228 || r < 1 || r > 100) {
    return -1;
  }
  for (int yy = 0; yy < s->h; yy++) {
    for (int xx = 0; xx < s->w; xx++) {
      int dx = xx * 200 / s->w - x, dy = yy * 228 / s->h - y;
      if (dx > 100) {
        dx -= 200;
      }
      if (dx < -100) {
        dx += 200;
      }
      if (dy > 114) {
        dy -= 228;
      }
      if (dy < -114) {
        dy += 228;
      }
      if (dx * dx + dy * dy <= r * r) {
        int i = yy * s->w + xx;
        put(s, field(s, 0), i, BITS(s) == 8 ? 128 : Q / 2);
        put(s, field(s, 1), i, BITS(s) == 8 ? 64 : Q / 4);
      }
    }
  }
  return 0;
}

void *rd_init(void *p, size_t n, int m, uint32_t seed) {
  if (!p || !mode_ok(m) || n < rd_bytes(m)) {
    return NULL;
  }
  State *s = (State *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
  memset(s, 0, rd_bytes(m) - 3);
  s->magic = MAGIC;
  s->seed = seed;
  s->w = width(m);
  s->h = s->w * 228 / 200;
  s->bits = m == 1 ? 16 : 8;
  rd_params(s, 950, 1868, Q, Q / 2, Q);
  for (int i = 0; i < s->w * s->h; i++) {
    put(s, field(s, 0), i, BITS(s) == 8 ? 255 : Q);
  }
  uint32_t r = seed;
  for (int i = 0; i < 24; i++) {
    r = r * 1664525u + 1013904223u;
    int x = (int)(((uint64_t)r * 200) >> 32);
    r = r * 1664525u + 1013904223u;
    int y = (int)(((uint64_t)r * 228) >> 32);
    r = r * 1664525u + 1013904223u;
    rd_seed(s, x, y, 4 + (int)(((uint64_t)r * 6) >> 32));
  }
  return s;
}

static int lap(State *s, uint8_t *up, uint8_t *cur, uint8_t *down, int x) {
  int l = (x + s->w - 1) % s->w, r = (x + 1) % s->w;
  int axial = expand(s, raw(s, cur, l)) + expand(s, raw(s, cur, r)) +
              expand(s, raw(s, up, x)) + expand(s, raw(s, down, x));
  int diag = expand(s, raw(s, up, l)) + expand(s, raw(s, up, r)) +
             expand(s, raw(s, down, l)) + expand(s, raw(s, down, r));
  /* Exact rational weights preserve a uniform field, including A=1. */
  int v = 4 * axial + diag - 20 * expand(s, raw(s, cur, x));
  return v < 0 ? -((-v + 10) / 20) : (v + 10) / 20;
}

static void cell(State *s, int i, int a, int b, int la, int lb, int *oa,
                 int *ob) {
  int reaction = mul(mul(a, b), b);
  *oa = pack(s, a + mul(mul(s->da, la) - reaction + mul(s->f, Q - a), s->dt), i,
             0);
  *ob = pack(s, b + mul(mul(s->db, lb) + reaction - mul(s->k + s->f, b), s->dt),
             i, 1);
}

int rd_step(void *p, int count) {
  State *s = valid(p);
  if (!s || count < 0 || count > 1000000) {
    return -1;
  }
  int row = s->w * (BITS(s) / 8);
  uint8_t *buf = field(s, 2);
  for (int t = 0; t < count; t++) {
    uint8_t *prev[2], *cur[2], *next[2], *first[2];
    for (int c = 0; c < 2; c++) {
      prev[c] = buf + c * 4 * row;
      cur[c] = prev[c] + row;
      next[c] = cur[c] + row;
      first[c] = next[c] + row;
      memcpy(prev[c], field(s, c) + (s->h - 1) * row, row);
      memcpy(cur[c], field(s, c), row);
      memcpy(first[c], cur[c], row);
    }
    for (int y = 0; y < s->h; y++) {
      for (int c = 0; c < 2; c++) {
        memcpy(next[c], y == s->h - 1 ? first[c] : field(s, c) + (y + 1) * row,
               row);
      }
      for (int x = 0; x < s->w; x++) {
        int a, b, i = y * s->w + x;
        cell(s, i, expand(s, raw(s, cur[0], x)), expand(s, raw(s, cur[1], x)),
             lap(s, prev[0], cur[0], next[0], x),
             lap(s, prev[1], cur[1], next[1], x), &a, &b);
        put(s, field(s, 0), i, a);
        put(s, field(s, 1), i, b);
      }
      for (int c = 0; c < 2; c++) {
        uint8_t *tmp = prev[c];
        prev[c] = cur[c];
        cur[c] = next[c];
        next[c] = tmp;
      }
    }
    s->step++;
  }
  return 0;
}

int rd_get(void *p, int x, int y, int c) {
  State *s = valid(p);
  if (!s || x < 0 || x >= s->w || y < 0 || y >= s->h || c < 0 || c > 1) {
    return -1;
  }
  return expand(s, raw(s, field(s, c), y * s->w + x));
}

uint32_t rd_steps(void *p) {
  State *s = valid(p);
  return s ? s->step : 0;
}

uint32_t rd_hash(void *p) {
  State *s = valid(p);
  if (!s) {
    return 0;
  }
  uint32_t h = 2166136261u;
  for (int c = 0; c < 2; c++) {
    for (int i = 0; i < s->w * s->h; i++) {
      int v = raw(s, field(s, c), i);
      h = (h ^ (v & 255)) * 16777619u;
      h = (h ^ (v >> 8)) * 16777619u;
    }
  }
  return h;
}

uint8_t *rd_row(void *p, int y, int palette, int quantize) {
  State *s = valid(p);
  if (!s || y < 0 || y >= 228 || palette < 0 || palette > 2 ||
      (quantize != 0 && quantize != 1)) {
    return NULL;
  }
  uint8_t *out = field(s, 2) + 8 * s->w * (BITS(s) / 8);
  const int low[3][3] = {{0, 30, 18}, {0, 0, 45}, {0, 0, 0}},
            high[3][3] = {{210, 255, 85}, {85, 255, 255}, {255, 255, 255}};
  for (int x = 0; x < 200; x++) {
    int v = rd_get(s, x * s->w / 200, y * s->h / 228, 1) * 3;
    if (v > Q) {
      v = Q;
    }
    for (int c = 0; c < 3; c++) {
      int col =
          palette == 2
              ? (v * 100 >= 45 * Q ? 255 : 0)
              : low[palette][c] +
                    ((high[palette][c] - low[palette][c]) * v + Q / 2) / Q;
      out[x * 4 + c] = (uint8_t)(quantize ? ((col + 42) / 85) * 85 : col);
    }
    out[x * 4 + 3] = 255;
  }
  return out;
}
