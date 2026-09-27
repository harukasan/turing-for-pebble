/*
 * Storage precision experiment: run one storage / rounding / arithmetic
 * candidate against the Float32 reference (float storage, double
 * intermediates, the operation order of lib/simulation.ts) from the same
 * initial field, and print cell-by-cell error metrics as JSON lines.
 *
 * The candidate is self-contained and does not use core/rd.c, so the
 * version 1 arithmetic remains reproducible as a baseline. Driven by
 * scripts/precision-sweep.sh, summarized by scripts/precision-aggregate.mjs.
 *
 * usage: precision <config> <preset 0-4> <seed> <checkpoint>...
 * config: comma-separated key=value pairs
 *   g=120           grid 120x136 (the default), seeded like rd_init
 *   a=CODEC b=CODEC storage of each species:
 *                   lin<bits>[@max]        linear codes over [0, max]
 *                   pow<bits>:<gamma>[@max] value = max * (s / N)^gamma
 *                   sqr<bits>              value = (s / 2^bits)^2, the core's
 *                                          closed form
 *                   q15                    Q15 word
 *   r=sto|near|ed1|fs  stochastic, nearest, 1-D error diffusion, or
 *                   Floyd-Steinberg rounding
 *   i=q15|q<P>|exact   version 1 Q15 arithmetic, version 2 integer
 *                   arithmetic with P fraction bits, or double
 *   i=q15x32c1|q15x32c2|q15x32c3
 *                   version 4 candidates, 32-bit arithmetic on Q15 codes
 *                   (a=q15,b=q15 only): c1 rounds the Laplacian and A B to
 *                   Q15, c2 folds the diffusion coefficients with 1/20 into
 *                   round(D / 20) and keeps the reaction exact, c3 rounds
 *                   the Laplacian like c1 with the exact reaction of c2
 *   d=<0..16>       threshold dither of near, ed1, and fs rounding in
 *                   sixteenths: 0 is nearest, 16 fully stochastic
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 228
#define INITIAL_DISKS 24
#define MIN_DISK_RADIUS 4
#define DISK_RADIUS_RANGE 6
#define Q15_ONE 32768
#define Q15_BITS 15
#define RATE_SHIFT 30
#define PRESET_COUNT 5

static const double PRESET_FEED[PRESET_COUNT] = {0.029, 0.0545, 0.0367, 0.035,
                                                 0.023};
static const double PRESET_KILL[PRESET_COUNT] = {0.057, 0.062, 0.0649, 0.065,
                                                 0.052};
static const uint32_t SPECIES_SALT[2] = {0x63d83595u, 0xa511e9b3u};

enum { R_STO, R_NEAR, R_ED1, R_FS };

enum { I_Q15, I_QP, I_EXACT, I_X32C1, I_X32C2, I_X32C3 };

/* Fraction bits of the integer arithmetic path. */
static int P = 24;
/* Threshold dither of error diffusion in sixteenths: 0 rounds to nearest,
 * 16 makes the rounding decision fully stochastic. */
static int DITHER = 0;

/* Rounding decision for a fraction f = num / den in [0, 1) with random r:
 * up when f >= 1/2 + DITHER/16 * (r / 32768 - 1/2). */
static int round_up(int64_t num, int64_t den, uint32_t r) {
  return num * (Q15_ONE * 16) >=
         den * ((Q15_ONE / 2) * 16 + DITHER * ((int64_t)r - Q15_ONE / 2));
}

typedef struct {
  int levels;    /* number of codes */
  double *dec;   /* decoded value in [0, 1] */
  int *dec15;    /* decoded value rounded to Q15 */
  int64_t *decp; /* decoded value rounded to Q_P */
  char name[32];
} Codec;

typedef struct {
  int grid, rounding, inter;
  Codec a, b;
} Config;

static int q15(double value) { return (int)floor(value * Q15_ONE + 0.5); }

static void fail(const char *message, const char *detail) {
  fprintf(stderr, "%s %s\n", message, detail);
  exit(2);
}

static void codec_parse(Codec *c, const char *spec) {
  int bits = 8;
  double gamma = 1, max = 1;
  snprintf(c->name, sizeof c->name, "%s", spec);
  if (!strcmp(spec, "q15")) {
    c->levels = Q15_ONE + 1;
  } else if (sscanf(spec, "sqr%d", &bits) == 1) {
    c->levels = 1 << bits;
    gamma = 2;
    max = -1; /* marker: (s / levels)^2 rather than (s / (levels - 1))^2 */
  } else if (sscanf(spec, "lin%d@%lf", &bits, &max) >= 1) {
    c->levels = 1 << bits;
  } else if (sscanf(spec, "pow%d:%lf@%lf", &bits, &gamma, &max) >= 2) {
    c->levels = 1 << bits;
  } else {
    fail("bad codec", spec);
  }
  c->dec = malloc(c->levels * sizeof(double));
  c->dec15 = malloc(c->levels * sizeof(int));
  c->decp = malloc(c->levels * sizeof(int64_t));
  for (int s = 0; s < c->levels; s++) {
    if (!strcmp(spec, "q15")) {
      c->dec[s] = s / (double)Q15_ONE;
    } else if (max < 0) {
      c->dec[s] = ((double)s / c->levels) * ((double)s / c->levels);
    } else {
      c->dec[s] = max * pow((double)s / (c->levels - 1), gamma);
    }
    c->dec15[s] = q15(c->dec[s]);
  }
}

static void codec_finish(Codec *c) {
  for (int s = 0; s < c->levels; s++) {
    c->decp[s] = (int64_t)floor(c->dec[s] * (double)((int64_t)1 << P) + 0.5);
  }
}

/* 32-bit integer mixing function (lowbias32), as in core version 1. */
static uint32_t mix32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

static uint32_t random15(uint32_t seed, int cell, uint32_t step, int species) {
  return mix32(seed ^ mix32((uint32_t)cell) ^ mix32(step) ^
               SPECIES_SALT[species]) &
         (Q15_ONE - 1);
}

/* Largest code whose decoded value (in the given table) is at most v. */
#define DEFINE_FLOOR_CODE(NAME, TYPE, TABLE)                                   \
  static int NAME(const Codec *c, TYPE v) {                                    \
    int lo = 0, hi = c->levels - 1;                                            \
    if (v >= c->TABLE[hi]) {                                                   \
      return hi;                                                               \
    }                                                                          \
    while (hi - lo > 1) {                                                      \
      int mid = (lo + hi) >> 1;                                                \
      if (c->TABLE[mid] <= v) {                                                \
        lo = mid;                                                              \
      } else {                                                                 \
        hi = mid;                                                              \
      }                                                                        \
    }                                                                          \
    return lo;                                                                 \
  }
DEFINE_FLOOR_CODE(floor_code, double, dec)
DEFINE_FLOOR_CODE(floor_code15, int, dec15)
DEFINE_FLOOR_CODE(floor_codep, int64_t, decp)

/* Encode a value; *residual carries the error diffusion input (added to v)
 * and receives the new error. r is the 15-bit random value for stochastic
 * rounding. */
static int encode(const Codec *c, double v, int rounding, uint32_t r,
                  double *residual) {
  if (rounding == R_ED1 || rounding == R_FS) {
    v += *residual;
  }
  if (v < 0) {
    v = 0;
  }
  if (v > c->dec[c->levels - 1]) {
    v = c->dec[c->levels - 1];
  }
  int s = floor_code(c, v);
  if (s < c->levels - 1) {
    double lo = c->dec[s], hi = c->dec[s + 1], f = (v - lo) / (hi - lo);
    s += rounding == R_STO ? (r / (double)Q15_ONE) < f
                           : round_up((int64_t)(f * Q15_ONE), Q15_ONE, r);
  }
  if (rounding == R_ED1 || rounding == R_FS) {
    *residual = v - c->dec[s];
  }
  return s;
}

static int encode15(const Codec *c, int v, int rounding, uint32_t r,
                    int64_t *residual) {
  if (rounding == R_ED1 || rounding == R_FS) {
    v += (int)*residual;
  }
  if (v < 0) {
    v = 0;
  }
  if (v > c->dec15[c->levels - 1]) {
    v = c->dec15[c->levels - 1];
  }
  int s = floor_code15(c, v);
  if (s < c->levels - 1) {
    int lo = c->dec15[s], hi = c->dec15[s + 1];
    s += rounding == R_STO
             ? (int64_t)r * (hi - lo) < (int64_t)(v - lo) * Q15_ONE
             : round_up(v - lo, hi - lo, r);
  }
  if (rounding == R_ED1 || rounding == R_FS) {
    *residual = v - c->dec15[s];
  }
  return s;
}

static int encodep(const Codec *c, int64_t v, int rounding, uint32_t r,
                   int64_t *residual) {
  if (rounding == R_ED1 || rounding == R_FS) {
    v += *residual;
  }
  if (v < 0) {
    v = 0;
  }
  if (v > c->decp[c->levels - 1]) {
    v = c->decp[c->levels - 1];
  }
  int s = floor_codep(c, v);
  if (s < c->levels - 1) {
    int64_t lo = c->decp[s], hi = c->decp[s + 1];
    s += rounding == R_STO ? (int64_t)r * (hi - lo) < (v - lo) * Q15_ONE
                           : round_up(v - lo, hi - lo, r);
  }
  if (rounding == R_ED1 || rounding == R_FS) {
    *residual = v - c->decp[s];
  }
  return s;
}

/* Version 1 and 2 rounding: nearest, halfway away from zero. */
static int64_t round_shift(int64_t x, int bits) {
  int64_t half = (int64_t)1 << (bits - 1);
  return x < 0 ? -((-x + half) >> bits) : (x + half) >> bits;
}

static int round_div20(int64_t x) {
  return (int)(x < 0 ? -((-x + 10) / 20) : (x + 10) / 20);
}

static int mul_q15(int a, int b) {
  return (int)round_shift((int64_t)a * b, Q15_BITS);
}

/* Float32 reference: float storage, double arithmetic, the operation order
 * of lib/simulation.ts. */
typedef struct {
  int w, h;
  float *a, *b, *na, *nb;
} Ref;

static void ref_step(Ref *f, double feed, double kill, double da, double db,
                     double dt) {
  int w = f->w, h = f->h;
  for (int y = 0; y < h; y++) {
    int row = y * w, up = ((y + h - 1) % h) * w, down = ((y + 1) % h) * w;
    for (int x = 0; x < w; x++) {
      int l = (x + w - 1) % w, r = (x + 1) % w, i = row + x;
      double a = f->a[i], b = f->b[i];
      double lap_a = -a +
                     0.2 * ((double)f->a[row + l] + f->a[row + r] +
                            f->a[up + x] + f->a[down + x]) +
                     0.05 * ((double)f->a[up + l] + f->a[up + r] +
                             f->a[down + l] + f->a[down + r]);
      double lap_b = -b +
                     0.2 * ((double)f->b[row + l] + f->b[row + r] +
                            f->b[up + x] + f->b[down + x]) +
                     0.05 * ((double)f->b[up + l] + f->b[up + r] +
                             f->b[down + l] + f->b[down + r]);
      double reaction = a * b * b;
      double va = a + (da * lap_a - reaction + feed * (1 - a)) * dt;
      double vb = b + (db * lap_b + reaction - (kill + feed) * b) * dt;
      f->na[i] = (float)(va < 0 ? 0 : va > 1 ? 1 : va);
      f->nb[i] = (float)(vb < 0 ? 0 : vb > 1 ? 1 : vb);
    }
  }
  float *t = f->a;
  f->a = f->na;
  f->na = t;
  t = f->b;
  f->b = f->nb;
  f->nb = t;
}

/* Candidate: codes per cell, double buffered, with error diffusion rows. */
typedef struct {
  int w, h;
  uint32_t seed, step;
  int *ca, *cb, *nca, *ncb;
  int64_t *res_a, *res_b;  /* shares for the next row, width + 2, integer */
  double *resd_a, *resd_b; /* same for the double path */
  const Config *cfg;
  int feed, kill, da, db, dt; /* Q15 */
  /* Version 4 c2: round(da / 20) and round(db / 20), so that
   * coefficient times the 20-fold Laplacian sum is the Q30 rate term. */
  int cda, cdb;
} Cand;

/* Version 4 rate: a 32-bit Q30 value; the exact sum saturates at the
 * int32 range, which the core reaches with one saturating final add. */
static int32_t saturate32(int64_t x) {
  return x > INT32_MAX ? INT32_MAX : x < INT32_MIN ? INT32_MIN : (int32_t)x;
}

/* Version 4 update of one species: the Q15 code plus the Q30 rate rounded
 * half up to Q24, as a Q24 value for the version 3 encoder. */
static int64_t update_x32(int code, int32_t rate) {
  return ((int64_t)code << 9) + ((rate >> 6) + ((rate >> 5) & 1));
}

/* Nine-point Laplacian sum (20 times the Laplacian) of decoded values. */
#define LAP_SUM(D, codes, x, y, w, h)                                          \
  (4 * (D[codes[(y) * (w) + ((x) + (w) - 1) % (w)]] +                          \
        D[codes[(y) * (w) + ((x) + 1) % (w)]] +                                \
        D[codes[(((y) + (h) - 1) % (h)) * (w) + (x)]] +                        \
        D[codes[(((y) + 1) % (h)) * (w) + (x)]]) +                             \
   (D[codes[(((y) + (h) - 1) % (h)) * (w) + ((x) + (w) - 1) % (w)]] +          \
    D[codes[(((y) + (h) - 1) % (h)) * (w) + ((x) + 1) % (w)]] +                \
    D[codes[(((y) + 1) % (h)) * (w) + ((x) + (w) - 1) % (w)]] +                \
    D[codes[(((y) + 1) % (h)) * (w) + ((x) + 1) % (w)]]) -                     \
   20 * D[codes[(y) * (w) + (x)]])

/* Distribute an integer error with Floyd-Steinberg weights or carry it. */
static void diffuse(int rounding, int64_t error, int64_t *carry, int64_t *next,
                    int x) {
  if (rounding == R_ED1) {
    *carry = error;
  } else if (rounding == R_FS) {
    int64_t right = error * 7 / 16, below_left = error * 3 / 16,
            below = error * 5 / 16;
    *carry = right;
    next[x] += below_left;
    next[x + 1] += below;
    next[x + 2] += error - right - below_left - below;
  }
}

static void diffuse_double(int rounding, double error, double *carry,
                           double *next, int x) {
  if (rounding == R_ED1) {
    *carry = error;
  } else if (rounding == R_FS) {
    *carry = error * 7 / 16;
    next[x] += error * 3 / 16;
    next[x + 1] += error * 5 / 16;
    next[x + 2] += error / 16;
  }
}

static void cand_step(Cand *s) {
  const Config *cfg = s->cfg;
  int w = s->w, h = s->h, rounding = cfg->rounding;
  int64_t carry_a = 0, carry_b = 0;
  double carryd_a = 0, carryd_b = 0;
  int64_t *next_a = calloc(w + 2, sizeof(int64_t)),
          *next_b = calloc(w + 2, sizeof(int64_t));
  double *nextd_a = calloc(w + 2, sizeof(double)),
         *nextd_b = calloc(w + 2, sizeof(double));
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int i = y * w + x;
      uint32_t ra = random15(s->seed, i, s->step, 0),
               rb = random15(s->seed, i, s->step, 1);
      if (cfg->inter == I_EXACT) {
        const double *da_ = cfg->a.dec, *db_ = cfg->b.dec;
        double a = da_[s->ca[i]], b = db_[s->cb[i]];
        double lap_a = LAP_SUM(da_, s->ca, x, y, w, h) / 20;
        double lap_b = LAP_SUM(db_, s->cb, x, y, w, h) / 20;
        double feed = s->feed / (double)Q15_ONE,
               kill = s->kill / (double)Q15_ONE, da = s->da / (double)Q15_ONE,
               db = s->db / (double)Q15_ONE, dt = s->dt / (double)Q15_ONE;
        double reaction = a * b * b;
        double na = a + (da * lap_a - reaction + feed * (1 - a)) * dt;
        double nb = b + (db * lap_b + reaction - (kill + feed) * b) * dt;
        double res_a =
                   rounding == R_FS ? carryd_a + s->resd_a[x + 1] : carryd_a,
               res_b =
                   rounding == R_FS ? carryd_b + s->resd_b[x + 1] : carryd_b;
        s->nca[i] = encode(&cfg->a, na, rounding, ra, &res_a);
        s->ncb[i] = encode(&cfg->b, nb, rounding, rb, &res_b);
        diffuse_double(rounding, res_a, &carryd_a, nextd_a, x);
        diffuse_double(rounding, res_b, &carryd_b, nextd_b, x);
        continue;
      }
      int64_t res_a = rounding == R_FS ? carry_a + s->res_a[x + 1] : carry_a,
              res_b = rounding == R_FS ? carry_b + s->res_b[x + 1] : carry_b;
      if (cfg->inter >= I_X32C1) {
        /* Version 4 candidates on Q15 codes with 32-bit products. */
        const int *da_ = cfg->a.dec15, *db_ = cfg->b.dec15;
        int a = da_[s->ca[i]], b = db_[s->cb[i]];
        int32_t sum_a = LAP_SUM(da_, s->ca, x, y, w, h);
        int32_t sum_b = LAP_SUM(db_, s->cb, x, y, w, h);
        int32_t lap_term_a, lap_term_b, reaction;
        if (cfg->inter == I_X32C2) {
          lap_term_a = s->cda * sum_a;
          lap_term_b = s->cdb * sum_b;
        } else {
          lap_term_a = s->da * round_div20(sum_a);
          lap_term_b = s->db * round_div20(sum_b);
        }
        if (cfg->inter == I_X32C1) {
          reaction = ((a * b + (1 << 14)) >> 15) * b;
        } else {
          uint32_t ab = (uint32_t)a * (uint32_t)b;
          reaction = (int32_t)((ab >> 15) * b + (((ab & 32767) * b) >> 15));
        }
        int32_t rate_a = saturate32((int64_t)lap_term_a - reaction +
                                    (int64_t)s->feed * (Q15_ONE - a));
        int32_t rate_b = saturate32((int64_t)lap_term_b + reaction -
                                    (int64_t)(s->kill + s->feed) * b);
        s->nca[i] =
            encodep(&cfg->a, update_x32(a, rate_a), rounding, ra, &res_a);
        s->ncb[i] =
            encodep(&cfg->b, update_x32(b, rate_b), rounding, rb, &res_b);
      } else if (cfg->inter == I_Q15) {
        /* Version 1: every product rounded to Q15. */
        const int *da_ = cfg->a.dec15, *db_ = cfg->b.dec15;
        int a = da_[s->ca[i]], b = db_[s->cb[i]];
        int lap_a = round_div20(LAP_SUM(da_, s->ca, x, y, w, h));
        int lap_b = round_div20(LAP_SUM(db_, s->cb, x, y, w, h));
        int reaction = mul_q15(mul_q15(a, b), b);
        int na = a + mul_q15(mul_q15(s->da, lap_a) - reaction +
                                 mul_q15(s->feed, Q15_ONE - a),
                             s->dt);
        int nb = b + mul_q15(mul_q15(s->db, lap_b) + reaction -
                                 mul_q15(s->kill + s->feed, b),
                             s->dt);
        s->nca[i] = encode15(&cfg->a, na, rounding, ra, &res_a);
        s->ncb[i] = encode15(&cfg->b, nb, rounding, rb, &res_b);
      } else {
        /* Version 2: Q_P concentrations, Q(P + 15) rates, one rounding. */
        const int64_t *da_ = cfg->a.decp, *db_ = cfg->b.decp;
        int64_t a = da_[s->ca[i]], b = db_[s->cb[i]];
        int64_t lap_a = round_div20(LAP_SUM(da_, s->ca, x, y, w, h));
        int64_t lap_b = round_div20(LAP_SUM(db_, s->cb, x, y, w, h));
        int64_t one = (int64_t)1 << P;
        int64_t ab = round_shift(a * b, P);
        int64_t reaction = round_shift(ab * b, P) << Q15_BITS;
        int64_t rate_a = s->da * lap_a - reaction + s->feed * (one - a);
        int64_t rate_b = s->db * lap_b + reaction - (s->kill + s->feed) * b;
        int64_t na = a + round_shift(rate_a * s->dt, RATE_SHIFT);
        int64_t nb = b + round_shift(rate_b * s->dt, RATE_SHIFT);
        s->nca[i] = encodep(&cfg->a, na, rounding, ra, &res_a);
        s->ncb[i] = encodep(&cfg->b, nb, rounding, rb, &res_b);
      }
      diffuse(rounding, res_a, &carry_a, next_a, x);
      diffuse(rounding, res_b, &carry_b, next_b, x);
    }
    /* Wrap the periodic row ends and hand the shares to the next row. */
    next_a[1] += next_a[w + 1];
    next_a[w] += next_a[0];
    next_b[1] += next_b[w + 1];
    next_b[w] += next_b[0];
    nextd_a[1] += nextd_a[w + 1];
    nextd_a[w] += nextd_a[0];
    nextd_b[1] += nextd_b[w + 1];
    nextd_b[w] += nextd_b[0];
    memcpy(s->res_a, next_a, (w + 2) * sizeof(int64_t));
    memcpy(s->res_b, next_b, (w + 2) * sizeof(int64_t));
    memcpy(s->resd_a, nextd_a, (w + 2) * sizeof(double));
    memcpy(s->resd_b, nextd_b, (w + 2) * sizeof(double));
    memset(next_a, 0, (w + 2) * sizeof(int64_t));
    memset(next_b, 0, (w + 2) * sizeof(int64_t));
    memset(nextd_a, 0, (w + 2) * sizeof(double));
    memset(nextd_b, 0, (w + 2) * sizeof(double));
  }
  free(next_a);
  free(next_b);
  free(nextd_a);
  free(nextd_b);
  int *t = s->ca;
  s->ca = s->nca;
  s->nca = t;
  t = s->cb;
  s->cb = s->ncb;
  s->ncb = t;
  s->step++;
}

static double cand_value(const Cand *s, int species, int i) {
  const Codec *c = species ? &s->cfg->b : &s->cfg->a;
  int code = species ? s->cb[i] : s->ca[i];
  switch (s->cfg->inter) {
  case I_Q15:
    return c->dec15[code] / (double)Q15_ONE;
  case I_QP:
    return c->decp[code] / (double)((int64_t)1 << P);
  default:
    return c->dec[code];
  }
}

/* MAE, RMSE, maximum, fraction above 0.01, correlation, means. */
static void metrics(const Ref *f, const Cand *s, int species, double *out) {
  int n = f->w * f->h;
  double abs_sum = 0, sq = 0, mx = 0, above = 0, sx = 0, sy = 0, sxx = 0,
         syy = 0, sxy = 0;
  for (int i = 0; i < n; i++) {
    double x = species ? f->b[i] : f->a[i], y = cand_value(s, species, i);
    double d = fabs(x - y);
    abs_sum += d;
    sq += d * d;
    if (d > mx) {
      mx = d;
    }
    above += d > 0.01;
    sx += x;
    sy += y;
    sxx += x * x;
    syy += y * y;
    sxy += x * y;
  }
  double cov = sxy - sx * sy / n,
         den = sqrt((sxx - sx * sx / n) * (syy - sy * sy / n));
  out[0] = abs_sum / n;
  out[1] = sqrt(sq / n);
  out[2] = mx;
  out[3] = above / n;
  out[4] = den > 0 ? cov / den : 0;
  out[5] = sx / n;
  out[6] = sy / n;
}

/* Initial field like rd_init: A = 1, B = 0, then INITIAL_DISKS disks with
 * A = 0.5 and B = 0.25 placed by the LCG in display coordinates. */
static void seed_field(double *a, double *b, int w, int h, uint32_t seed) {
  for (int i = 0; i < w * h; i++) {
    a[i] = 1;
    b[i] = 0;
  }
  uint32_t lcg = seed;
  for (int disk = 0; disk < INITIAL_DISKS; disk++) {
    int center[3];
    const int ranges[3] = {DISPLAY_WIDTH, DISPLAY_HEIGHT, DISK_RADIUS_RANGE};
    for (int k = 0; k < 3; k++) {
      lcg = lcg * 1664525u + 1013904223u;
      center[k] = (int)(((uint64_t)lcg * ranges[k]) >> 32);
    }
    int radius = MIN_DISK_RADIUS + center[2];
    for (int gy = 0; gy < h; gy++) {
      for (int gx = 0; gx < w; gx++) {
        int dx = abs(gx * DISPLAY_WIDTH / w - center[0]);
        int dy = abs(gy * DISPLAY_HEIGHT / h - center[1]);
        dx = dx < DISPLAY_WIDTH - dx ? dx : DISPLAY_WIDTH - dx;
        dy = dy < DISPLAY_HEIGHT - dy ? dy : DISPLAY_HEIGHT - dy;
        if (dx * dx + dy * dy <= radius * radius) {
          a[gy * w + gx] = 0.5;
          b[gy * w + gx] = 0.25;
        }
      }
    }
  }
}

static void parse_config(Config *cfg, const char *text) {
  char *spec = strdup(text);
  for (char *tok = strtok(spec, ","); tok; tok = strtok(NULL, ",")) {
    char *eq = strchr(tok, '=');
    if (!eq) {
      fail("bad token", tok);
    }
    *eq = 0;
    const char *val = eq + 1;
    if (!strcmp(tok, "g")) {
      cfg->grid = atoi(val);
    } else if (!strcmp(tok, "a")) {
      codec_parse(&cfg->a, val);
    } else if (!strcmp(tok, "b")) {
      codec_parse(&cfg->b, val);
    } else if (!strcmp(tok, "r")) {
      cfg->rounding = !strcmp(val, "sto")    ? R_STO
                      : !strcmp(val, "near") ? R_NEAR
                      : !strcmp(val, "ed1")  ? R_ED1
                      : !strcmp(val, "fs")   ? R_FS
                                             : -1;
    } else if (!strcmp(tok, "d")) {
      DITHER = atoi(val);
    } else if (!strcmp(tok, "i")) {
      if (!strcmp(val, "q15")) {
        cfg->inter = I_Q15;
      } else if (!strcmp(val, "exact")) {
        cfg->inter = I_EXACT;
      } else if (!strcmp(val, "q15x32c1")) {
        cfg->inter = I_X32C1;
        P = 24;
      } else if (!strcmp(val, "q15x32c2")) {
        cfg->inter = I_X32C2;
        P = 24;
      } else if (!strcmp(val, "q15x32c3")) {
        cfg->inter = I_X32C3;
        P = 24;
      } else if (sscanf(val, "q%d", &P) == 1 && P >= 16 && P <= 28) {
        cfg->inter = I_QP;
      } else {
        fail("bad arithmetic", val);
      }
    } else {
      fail("bad key", tok);
    }
    if (cfg->rounding < 0) {
      fail("bad rounding", val);
    }
  }
  free(spec);
  if (cfg->grid != 120 || !cfg->a.levels || !cfg->b.levels) {
    fail("incomplete config", text);
  }
  if (cfg->inter >= I_X32C1 &&
      (strcmp(cfg->a.name, "q15") || strcmp(cfg->b.name, "q15"))) {
    fail("version 4 candidates need a=q15,b=q15", text);
  }
  codec_finish(&cfg->a);
  codec_finish(&cfg->b);
}

int main(int argc, char **argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s config preset seed checkpoint...\n", argv[0]);
    return 2;
  }
  Config cfg = {.grid = 120, .rounding = R_STO, .inter = I_Q15};
  parse_config(&cfg, argv[1]);
  int preset = atoi(argv[2]);
  uint32_t seed = (uint32_t)strtoul(argv[3], NULL, 10);
  if (preset < 0 || preset >= PRESET_COUNT) {
    fail("bad preset", argv[2]);
  }
  int w = cfg.grid, h = w * DISPLAY_HEIGHT / DISPLAY_WIDTH, n = w * h;
  double *initial_a = malloc(n * sizeof(double)),
         *initial_b = malloc(n * sizeof(double));
  seed_field(initial_a, initial_b, w, h, seed);

  Cand s = {.w = w, .h = h, .seed = seed, .step = 0, .cfg = &cfg};
  s.feed = q15(PRESET_FEED[preset]);
  s.kill = q15(PRESET_KILL[preset]);
  s.da = Q15_ONE;
  s.db = Q15_ONE / 2;
  s.dt = Q15_ONE;
  s.cda = (s.da + 10) / 20;
  s.cdb = (s.db + 10) / 20;
  s.ca = malloc(n * sizeof(int));
  s.cb = malloc(n * sizeof(int));
  s.nca = malloc(n * sizeof(int));
  s.ncb = malloc(n * sizeof(int));
  s.res_a = calloc(w + 2, sizeof(int64_t));
  s.res_b = calloc(w + 2, sizeof(int64_t));
  s.resd_a = calloc(w + 2, sizeof(double));
  s.resd_b = calloc(w + 2, sizeof(double));
  Ref f = {.w = w, .h = h};
  f.a = malloc(n * sizeof(float));
  f.b = malloc(n * sizeof(float));
  f.na = malloc(n * sizeof(float));
  f.nb = malloc(n * sizeof(float));
  /* Each candidate starts from its own nearest codes; the reference starts
   * from those decoded values, so checkpoint 0 has no error. */
  for (int i = 0; i < n; i++) {
    int64_t unused = 0;
    double unused_d = 0;
    if (cfg.inter == I_Q15) {
      s.ca[i] = encode15(&cfg.a, q15(initial_a[i]), R_NEAR, 0, &unused);
      s.cb[i] = encode15(&cfg.b, q15(initial_b[i]), R_NEAR, 0, &unused);
    } else if (cfg.inter == I_QP || cfg.inter >= I_X32C1) {
      int64_t one = (int64_t)1 << P;
      s.ca[i] = encodep(&cfg.a, (int64_t)floor(initial_a[i] * one + 0.5),
                        R_NEAR, 0, &unused);
      s.cb[i] = encodep(&cfg.b, (int64_t)floor(initial_b[i] * one + 0.5),
                        R_NEAR, 0, &unused);
    } else {
      s.ca[i] = encode(&cfg.a, initial_a[i], R_NEAR, 0, &unused_d);
      s.cb[i] = encode(&cfg.b, initial_b[i], R_NEAR, 0, &unused_d);
    }
    f.a[i] = (float)cand_value(&s, 0, i);
    f.b[i] = (float)cand_value(&s, 1, i);
  }
  double feed = s.feed / (double)Q15_ONE, kill = s.kill / (double)Q15_ONE;
  /* The c2 candidate runs with the folded coefficients, so the reference
   * uses those effective values, as the Float32 comparison of the core
   * uses its Q15-effective parameters. */
  double ref_da = cfg.inter == I_X32C2 ? s.cda * 20 / (double)Q15_ONE : 1;
  double ref_db = cfg.inter == I_X32C2 ? s.cdb * 20 / (double)Q15_ONE : 0.5;
  int done = 0;
  for (int k = 4; k < argc; k++) {
    int target = atoi(argv[k]);
    for (; done < target; done++) {
      ref_step(&f, feed, kill, ref_da, ref_db, 1);
      cand_step(&s);
    }
    double ma[7], mb[7];
    metrics(&f, &s, 0, ma);
    metrics(&f, &s, 1, mb);
    printf("{\"config\":\"%s\",\"preset\":%d,\"seed\":%u,\"step\":%d,"
           "\"a\":{\"mae\":%.7g,\"rmse\":%.7g,\"max\":%.7g,\"above\":%.5g,"
           "\"corr\":%.6g,\"refMean\":%.7g,\"candMean\":%.7g},"
           "\"b\":{\"mae\":%.7g,\"rmse\":%.7g,\"max\":%.7g,\"above\":%.5g,"
           "\"corr\":%.6g,\"refMean\":%.7g,\"candMean\":%.7g}}\n",
           argv[1], preset, seed, target, ma[0], ma[1], ma[2], ma[3], ma[4],
           ma[5], ma[6], mb[0], mb[1], mb[2], mb[3], mb[4], mb[5], mb[6]);
    fflush(stdout);
  }
  return 0;
}
