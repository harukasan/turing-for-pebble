# Shared C core

## Modes and memory contract

| Mode          | Grid      | Storage per cell                                     |   Fields | Row buffers |     Mask | Total caller allocation |
| ------------- | --------- | ---------------------------------------------------- | -------: | ----------: | -------: | ----------------------: |
| 0             | 200 × 228 | one 16-bit word: A 7-bit linear, B 9-bit square-root | 91,200 B |     8,000 B | 45,600 B |               146,175 B |
| 1             | 100 × 114 | one unsigned 16-bit Q15 word per species             | 45,600 B |     4,000 B | 11,400 B |                62,375 B |
| 2, diagnostic | 100 × 114 | one 16-bit word packed as in mode 0                  | 22,800 B |     4,000 B | 11,400 B |                39,575 B |

Totals include a 56-byte control structure, a 1,316-byte rendering area (an 800-byte RGBA output row and a 516-byte display lookup table), and up to 3 bytes of alignment padding. Row buffers hold four decoded Q24 rows per species (previous, current, next, and original first row, 32-bit each) and one 32-bit error diffusion residual row per species. The previous row is overwritten with the current row's Laplacian before it is decoded anew. Consumers must query `rd_bytes` and `rd_memory` instead of relying on these recorded values. The core performs no allocation. The caller owns a single contiguous block and frees the original pointer, not the potentially aligned state pointer returned by `rd_init`.

Rotating the previous, current, and next row pointers preserves the old field while writing in place. Each stored row is decoded once when it becomes the next row. The saved first row supplies the periodic boundary when the last row is updated.

## Numerical definition, version 3

The model is Gray–Scott with an explicit Euler step and periodic boundaries:

```text
A' = clamp(A + dt × (Da × lap(A) − A × B² + feed × (1 − A)))
B' = clamp(B + dt × (Db × lap(B) + A × B² − (feed + kill) × B))
```

Concentrations are calculated in Q24, where `RD_VALUE_ONE` = 16,777,216 means 1. Coefficients are Q15, where `RD_Q15_ONE` = 32,768 means 1. Arithmetic rounding is to nearest with halfway values away from zero. Storage rounding uses the dithered error diffusion described below.

The nine-point Laplacian (`laplacian` in `core/rd.c`) is evaluated on Q24 values as `(4 × axial_sum + diagonal_sum − 20 × center) / 20`, rounded. These exact rational weights preserve uniform fields exactly. The reaction is `A × B` rounded to Q24, then times `B` rounded to Q24. Each rate is accumulated as a Q39 sum of exact products of a Q15 coefficient and a Q24 concentration, with the reaction scaled by 2¹⁵. The rate times `dt` is a Q54 product rounded once to Q24 and added to the concentration. The result then passes through error diffusion, which clamps it to the codable range of its storage.

Version 1 rounded every product to Q15 and stored 8-bit codes with stochastic rounding. That definition is retained only as a baseline inside `tests/precision.c`. Version 2 introduced the codes, the error diffusion, and the Q24 arithmetic below with a dithered threshold in every mode. Version 3 differs from it only in mode 1, whose Q15 codes round to the nearest code without the dither. [Storage precision study](precision-optimization.md) records the comparison.

### Storage codes

Modes 0 and 2 store one 16-bit word per cell, `(codeA << 9) | codeB`. Mode 1 stores one Q15 word per species.

| Storage      | Decode to Q24                         | Floor code of a Q24 value v                                      | Largest value       |
| ------------ | ------------------------------------- | ---------------------------------------------------------------- | ------------------- |
| A, 7 bits    | `round(code × 2²⁴ / 127)`             | `v × 127 / 2²⁴` truncated, plus one if the next code decodes ≤ v | 1                   |
| B, 9 bits    | `code² × 64`, that is `(code / 512)²` | `isqrt(v / 64)`                                                  | 511² / 512² ≈ 0.996 |
| Q15, 16 bits | `code × 512`                          | `v / 512`                                                        | 1                   |

The floor code is the largest code that decodes to at most v. Error diffusion decides between it and the next code. The square-root companding of B spaces codes in proportion to √B: near B = 0.05 the step is 0.0009, near 0.25 it is 0.0020, and near 1 it is 0.0039, compared with 0.0020 everywhere for a 9-bit linear code. The 18-bit domain of `isqrt` covers every codable B value.

### Error diffusion

Every write uses Floyd–Steinberg error diffusion (`encode_cell` and `finish_row` in `core/rd.c`). The value to be written, plus the shares it received from already written neighbors, is clamped to the codable range. For the packed codes of modes 0 and 2, its fraction of the step from the floor code to the next code is compared with a dithered threshold `(2¹⁴ + random) / 2¹⁶`, where `random` is 15 bits of `mix32(cell_index ^ mix32(seed ^ mix32(step)))`, the low bits for A and the bits from 16 for B. The next code is taken when the fraction is at least the threshold, so the threshold is uniform over [1/4, 3/4) and exact codes never move. The Q15 codes of mode 1 take the next code when the fraction is at least 1/2, with no dither: at that resolution the study measured no front pinning and a smaller error without it. The residual, the clamped value minus the decoded code, is divided with C integer division: 7/16 to the right neighbor, 3/16 to the lower-left neighbor, 5/16 to the neighbor below, and the remainder to the lower-right neighbor. Cells are written row by row from the top and column by column from the left. The lower-left share of column 0 wraps to the last column and the lower-right share of the last column wraps to column 0 of the next row. The right share of the last column carries into column 0 of the next row. Shares held between cells are reset at the start of each step. The residual rows persist: shares given below the last row are applied to row 0 of the next step. A uniform field has zero residuals and remains exact.

### Masked cells

`rd_mask` takes a cell bitmap and derives a mask level for every cell: 0 in a masked cell, else the chessboard distance in cells to the nearest masked cell, capped at `RD_MASK_RAMP` (5). Distances do not wrap around the field edges. The levels take one byte per cell (the mask column above). A watch build that defines `RD_AVOID` as 0 has no mask area and `rd_mask` returns −1.

The kill rate of a cell rises linearly from the `rd_params` kill at level 5 to `RD_MASK_KILL` (2,458, 0.075) at level 0: `kill + (2458 − kill) × (5 − level) / 5` with C integer division. The reaction of every cell uses its own kill. A is computed and encoded as usual everywhere. B of a masked cell is held at code 0: `rd_mask` writes it at once, and every step writes 0 without encoding. The B shares it received from the row above and the right share of the cell to its left are dropped, its B residual entry is set to the lower-right share of the cell to its left, which passes through to the row below, and it gives no lower-left or wrapped share.

With A left free and the kill raised around the mask, B cannot grow near the digits and the pattern fades out a few cells before them. Holding A at 1 as well was tried first: the mask then supplies fresh A to its edge and a bright band forms along it. Only the rows that contain a level below 5 read the levels, so a step without a mask is unchanged. `rd_seed` skips masked cells. The levels are not part of `rd_hash`, but the golden hashes with a clock mask pin this rule.

Error diffusion makes the write deterministic and order dependent. The full-screen oracle in `tests/core.c` reproduces the same order. It conserves the encoded mass of a row exactly, which `tests/core.c` checks, and moves the quantization error to high spatial frequencies that diffusion removes within a few steps. The dither keeps slowly moving fronts from being pinned by the coarse packed codes without giving up that conservation. Both are measured in [Storage precision study](precision-optimization.md).

## Initialization

The initial equilibrium is A=1 and B=0. A 32-bit LCG places 24 disks in a common 200 × 228 display coordinate system. Disk radii range from 4 through 9 display pixels. Grid cells sample their corresponding display coordinates, with periodic distance used at edges. Seeded values are A=0.5 and B=0.25, written directly as codes: 64/127 ≈ 0.504 and exactly 0.25 in the packed modes, and exact Q15 values in mode 1. Grid resolution still changes the physical scale of emergent patterns.

## Public API

All functions are declared in `core/rd.h`.

| Function                                   | Contract                                                                               |
| ------------------------------------------ | -------------------------------------------------------------------------------------- |
| `rd_bytes(mode)`                           | Caller allocation size including alignment, zero for unsupported mode                  |
| `rd_memory(mode, component)`               | `RD_COMPONENT_*`: 0 fields, 1 row buffers, 2 control, 3 rendering, 4 alignment, 5 mask |
| `rd_row_rgb2(state, y, palette)`           | Shared 200-byte opaque ARGB8 row, the quantized `rd_row` colors at 2 bits per channel  |
| `rd_init(memory, bytes, mode, seed)`       | Initialize and seed, returning aligned state or null                                   |
| `rd_params(state, feed, kill, da, db, dt)` | Integer Q15 coefficients, each in [0,32768]                                            |
| `rd_seed(state, x, y, radius)`             | Display coordinates x∈[0,199], y∈[0,227], radius∈[1,100]                               |
| `rd_step(state, count)`                    | Advance count∈[0,1000000] synchronously                                                |
| `rd_get(state, x, y, species)`             | Grid coordinates, species 0=A or 1=B, return Q24 (`RD_VALUE_ONE` is 1) or −1           |
| `rd_steps(state)`                          | Unsigned step counter                                                                  |
| `rd_hash(state)`                           | FNV-1a over canonical little-endian stored 16-bit words, residual rows excluded        |
| `rd_row(state, y, palette, quantize)`      | Shared 800-byte RGBA output for display row y∈[0,227]                                  |
| `rd_width(state)`, `rd_height(state)`      | Grid size, or −1                                                                       |
| `rd_mask(state, mask)`                     | Derive the mask levels from a cell bitmap (NULL clears them) and set B to 0 under it   |
| `rd_mask_level(state, x, y)`               | Mask level 0 to 5 of grid cell (x, y), −1 for invalid coordinates                      |

A mask has one bit per grid cell in rows of `(width + 7) / 8` bytes, cell x in bit x % 8 of byte x / 8.

`core/clock_mask.c` builds the mask of the clock digits, shared by the watch and the Web. `cm_build(mask, width, height, font, hour, minute, year, month, day, halo)` draws HH:MM and YYYY.MM.DD with the glyphs of font set `font` (`CM_FONT_LECO` or `CM_FONT_BITHAM`) as the watch places them, each line centered by the sum of its advances. Each glyph pixel is widened by a square of `halo` display pixels (at most `CM_MAX_HALO`), clipped to the display, and every grid cell covering such a pixel is marked. It returns −1 without writing for arguments out of range. `cm_bytes`, `cm_layout` (font keys and text boxes), and `cm_font_available` complete it. The glyph tables in `core/clock_glyphs.h` are generated. A build that defines `RD_FONT` compiles only that set.

Palettes are 0 lime, 1 cyan, 2 monochrome (`RD_PALETTE_*`). Quantize is 0 or 1. `rd_row_rgb2` fills a lookup table from the B code (or, in mode 1, from 64-code buckets with a fallback for the few buckets that straddle a color step) the first time a palette is requested and reuses it afterwards; the watch copies its rows straight into the framebuffer. `core/rd.h` also defines `RD_VERSION`, `RD_Q15_ONE`, `RD_VALUE_BITS`, `RD_VALUE_ONE`, `RD_DISPLAY_WIDTH`, `RD_DISPLAY_HEIGHT`, `RD_ROW_BYTES`, and `RD_SPECIES_A` / `RD_SPECIES_B` for C callers. A returned rendering row is overwritten by the next row call. The Web adapter copies each row into a persistent ImageData. The watch adapter writes RGB2 values directly into the OS framebuffer and owns no full-screen image.

Validation occurs before mutations. Invalid coefficients, seed coordinates, step counts, output parameters, unsupported modes, and undersized initialization blocks leave state unchanged. Callers must supply live owned memory and a valid state pointer. Arbitrary dangling pointers are outside the C API contract. The API is not thread-safe for simultaneous use of one state.

## Implementation notes

These choices change no result. `tests/golden-hashes.txt` records the field hashes of the definition and `scripts/check-optimization.sh` checks every build against them.

- Each row's Laplacians are computed in one pass over the decoded rows, sharing the vertical sums `up[x] + down[x]` between neighboring columns and writing over the row above, which is decoded anew before it is read again. The wrapped first and last columns are handled outside the loop.
- The coefficients, the residual row pointers, and the diffusion shares live in a stack context during a step, so the stores into the planes cannot force the compiler to reload them.
- `decode_a` is a 128-entry table of the rounded division, and `floor_b` normalizes its argument with a leading-zero count, takes a lower bound from a 256-entry table, and increments at most twice.
- Products of the non-negative concentrations are rounded without a sign test, and the dithered comparison uses 32-bit products except for the packed A step, whose products need 64 bits.
- With `dt` = 1 the rate is rounded by 15 bits directly, which equals rounding the `dt` product by 30 bits.

The existing Float32 implementation remains in `lib/simulation.ts` as a reference with its original tests. The Web can explicitly select it through `lib/float-simulation.ts` for same-resolution visual comparisons. Pebble builds use only the C implementation.
