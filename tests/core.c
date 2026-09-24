#include "../core/rd.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
/* Full-screen oracle with independent neighbor indexing. */
static void reference(State *s) {
  int n = s->w * s->h, b = s->bits / 8;
  uint8_t *out = malloc(n * b * 2);
  assert(out);
  for (int y = 0; y < s->h; y++)
    for (int x = 0; x < s->w; x++) {
      int laps[2];
      for (int c = 0; c < 2; c++) {
        int sum = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int weight = dx == 0 && dy == 0 ? -20 : dx == 0 || dy == 0 ? 4 : 1;
            sum += weight *
                   rd_get(s, (x + dx + s->w) % s->w, (y + dy + s->h) % s->h, c);
          }
        laps[c] = sum < 0 ? -((-sum + 10) / 20) : (sum + 10) / 20;
      }
      int a, bb, i = y * s->w + x;
      cell(s, i, rd_get(s, x, y, 0), rd_get(s, x, y, 1), laps[0], laps[1], &a,
           &bb);
      put(s, out, i, a);
      put(s, out + n * b, i, bb);
    }
  memcpy(field(s, 0), out, n * b * 2);
  free(out);
  s->step++;
}
int main(int argc, char **argv) {
  if (argc > 1) {
    int mode = atoi(argv[1]), count = argc > 2 ? atoi(argv[2]) : 100;
    void *mem = malloc(rd_bytes(mode));
    void *s = rd_init(mem, rd_bytes(mode), mode, 42);
    rd_step(s, count);
    printf("%u\n", rd_hash(s));
    free(mem);
    return 0;
  }
  assert(rd_bytes(-1) == 0);
  assert(rd_init(NULL, 0, 0, 0) == NULL);
  assert(roundq(-16384) == -1 && roundq(16384) == 1);
  for (int mode = 0; mode < 3; mode++) {
    size_t size = rd_bytes(mode);
    uint8_t *mem = malloc(size + 16), *other = malloc(size),
            *snapshot = malloc(size);
    memset(mem, 0xa5, size + 16);
    assert(!rd_init(mem, size - 1, mode, 42));
    for (size_t i = 0; i < size + 16; i++)
      assert(mem[i] == 0xa5);
    State *s = rd_init(mem, size, mode, 42),
          *r = rd_init(other, size, mode, 42);
    for (int i = 0; i < 25; i++) {
      rd_step(s, 1);
      reference(r);
      assert(rd_hash(s) == rd_hash(r));
    }
    for (int endpoint = 0; endpoint < 2; endpoint++) {
      int q = endpoint ? Q : 0;
      assert(!rd_params(s, q, q, q, q, q));
      assert(!rd_params(r, q, q, q, q, q));
      rd_step(s, 3);
      for (int j = 0; j < 3; j++)
        reference(r);
      assert(rd_hash(s) == rd_hash(r));
    }
    memcpy(snapshot, mem, size);
    assert(rd_params(s, -1, 0, 0, 0, 0) == -1);
    assert(rd_seed(s, 200, 0, 4) == -1);
    assert(rd_step(s, -1) == -1);
    assert(!rd_row(s, 228, 0, 1));
    assert(memcmp(snapshot, mem, size) == 0);
    for (size_t i = size; i < size + 16; i++)
      assert(mem[i] == 0xa5);
    rd_seed(s, 0, 0, 8);
    assert(rd_get(s, s->w - 1, s->h - 1, 1) > 0);
    memset(field(s, 1), 0, s->w * s->h * (s->bits / 8));
    for (int i = 0; i < s->w * s->h; i++)
      put(s, field(s, 0), i, s->bits == 8 ? 255 : Q);
    rd_params(s, Q, Q, Q, Q, Q);
    rd_step(s, 4);
    for (int y = 0; y < s->h; y++)
      for (int x = 0; x < s->w; x++) {
        assert(rd_get(s, x, y, 0) == Q);
        assert(rd_get(s, x, y, 1) == 0);
      }
    if (s->bits == 8) {
      double sum = 0;
      for (int i = 0; i < 100000; i++) {
        s->seed = (uint32_t)i;
        sum += pack(s, 12345, 21, 0);
      }
      assert(sum / 100000 > 12345. * 255 / Q - .01 &&
             sum / 100000 < 12345. * 255 / Q + .01);
    }
    for (int alignment = 0; alignment < 4; alignment++) {
      uint8_t *unaligned = malloc(size + 8);
      memset(unaligned, 0xa5, size + 8);
      State *a = rd_init(unaligned + alignment, size, mode, 42);
      assert(a);
      rd_step(a, 1);
      assert(rd_row(a, 227, 2, 1));
      for (int j = 0; j < alignment; j++)
        assert(unaligned[j] == 0xa5);
      for (size_t j = size + alignment; j < size + 8; j++)
        assert(unaligned[j] == 0xa5);
      free(unaligned);
    }
    printf("mode %d: %zu bytes, "
           "oracle/boundary/equilibrium/validation/rounding OK\n",
           mode, size);
    free(mem);
    free(other);
    free(snapshot);
  }
  return 0;
}
