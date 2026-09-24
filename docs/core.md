# Shared C core

## Modes and memory contract

| Mode          | Grid      | Storage per cell                                     |   Fields | Row buffers | Total caller allocation |
| ------------- | --------- | ---------------------------------------------------- | -------: | ----------: | ----------------------: |
| 0             | 200 × 228 | one 16-bit word: A 7-bit linear, B 9-bit square-root | 91,200 B |     8,000 B |               100,075 B |
| 1             | 100 × 114 | one unsigned 16-bit Q15 word per species             | 45,600 B |     4,000 B |                50,475 B |
| 2, diagnostic | 100 × 114 | one 16-bit word packed as in mode 0                  | 22,800 B |     4,000 B |                27,675 B |

Totals include a 72-byte control structure, an 800-byte RGBA output row, and up to 3 bytes of alignment padding. Row buffers hold four decoded Q24 rows per species (previous, current, next, and original first row, 32-bit each) and one 32-bit error diffusion residual row per species. Consumers must query `rd_bytes` and `rd_memory` instead of relying on these recorded values. The core performs no allocation. The caller owns a single contiguous block and frees the original pointer, not the potentially aligned state pointer returned by `rd_init`.

Rotating the previous, current, and next row pointers preserves the old field while writing in place. Each stored row is decoded once when it becomes the next row. The saved first row supplies the periodic boundary when the last row is updated.

## Numerical definition, version 2

The model is Gray–Scott with an explicit Euler step and periodic boundaries:

```text
A' = clamp(A + dt × (Da × lap(A) − A × B² + feed × (1 − A)))
B' = clamp(B + dt × (Db × lap(B) + A × B² − (feed + kill) × B))
```

Concentrations are calculated in Q24, where `RD_VALUE_ONE` = 16,777,216 means 1. Coefficients are Q15, where `RD_Q15_ONE` = 32,768 means 1. Arithmetic rounding is to nearest with halfway values away from zero. Storage rounding uses the dithered error diffusion described below.

The nine-point Laplacian (`laplacian` in `core/rd.c`) is evaluated on Q24 values as `(4 × axial_sum + diagonal_sum − 20 × center) / 20`, rounded. These exact rational weights preserve uniform fields exactly. The reaction is `A × B` rounded to Q24, then times `B` rounded to Q24. Each rate is accumulated as a Q39 sum of exact products of a Q15 coefficient and a Q24 concentration, with the reaction scaled by 2¹⁵. The rate times `dt` is a Q54 product rounded once to Q24 and added to the concentration. The result then passes through error diffusion, which clamps it to the codable range of its storage.

Version 1 rounded every product to Q15 and stored 8-bit codes with stochastic rounding. That definition is retained only as a baseline inside `tests/precision.c`. [Storage precision study](precision-optimization.md) records the comparison.

### Storage codes

Modes 0 and 2 store one 16-bit word per cell, `(codeA << 9) | codeB`. Mode 1 stores one Q15 word per species.

| Storage      | Decode to Q24                         | Floor code of a Q24 value v                                      | Largest value       |
| ------------ | ------------------------------------- | ---------------------------------------------------------------- | ------------------- |
| A, 7 bits    | `round(code × 2²⁴ / 127)`             | `v × 127 / 2²⁴` truncated, plus one if the next code decodes ≤ v | 1                   |
| B, 9 bits    | `code² × 64`, that is `(code / 512)²` | `isqrt(v / 64)`                                                  | 511² / 512² ≈ 0.996 |
| Q15, 16 bits | `code × 512`                          | `v / 512`                                                        | 1                   |

The floor code is the largest code that decodes to at most v. Error diffusion decides between it and the next code. The square-root companding of B spaces codes in proportion to √B: near B = 0.05 the step is 0.0009, near 0.25 it is 0.0020, and near 1 it is 0.0039, compared with 0.0020 everywhere for a 9-bit linear code. The 18-bit domain of `isqrt` covers every codable B value.

### Error diffusion

Every write uses Floyd–Steinberg error diffusion with a dithered rounding threshold (`encode_cell` and `finish_row` in `core/rd.c`). The value to be written, plus the shares it received from already written neighbors, is clamped to the codable range. Its fraction of the step from the floor code to the next code is compared with a threshold `(2¹⁴ + random) / 2¹⁶`, where `random` is 15 bits of `mix32(cell_index ^ mix32(seed ^ mix32(step)))`, the low bits for A and the bits from 16 for B. The next code is taken when the fraction is at least the threshold, so the threshold is uniform over [1/4, 3/4) and exact codes never move. The residual, the clamped value minus the decoded code, is divided with C integer division: 7/16 to the right neighbor, 3/16 to the lower-left neighbor, 5/16 to the neighbor below, and the remainder to the lower-right neighbor. Cells are written row by row from the top and column by column from the left. The lower-left share of column 0 wraps to the last column and the lower-right share of the last column wraps to column 0 of the next row. The right share of the last column carries into column 0 of the next row. Shares held between cells are reset at the start of each step. The residual rows persist: shares given below the last row are applied to row 0 of the next step. A uniform field has zero residuals and remains exact.

Error diffusion makes the write deterministic and order dependent. The full-screen oracle in `tests/core.c` reproduces the same order. It conserves the encoded mass of a row exactly, which `tests/core.c` checks, and moves the quantization error to high spatial frequencies that diffusion removes within a few steps. The dither keeps slowly moving fronts from being pinned by the coarse packed codes without giving up that conservation. Both are measured in [Storage precision study](precision-optimization.md).

## Initialization

The initial equilibrium is A=1 and B=0. A 32-bit LCG places 24 disks in a common 200 × 228 display coordinate system. Disk radii range from 4 through 9 display pixels. Grid cells sample their corresponding display coordinates, with periodic distance used at edges. Seeded values are A=0.5 and B=0.25, written directly as codes: 64/127 ≈ 0.504 and exactly 0.25 in the packed modes, and exact Q15 values in mode 1. Grid resolution still changes the physical scale of emergent patterns.

## Public API

All functions are declared in `core/rd.h`.

| Function                                   | Contract                                                                        |
| ------------------------------------------ | ------------------------------------------------------------------------------- |
| `rd_bytes(mode)`                           | Caller allocation size including alignment, zero for unsupported mode           |
| `rd_memory(mode, component)`               | `RD_COMPONENT_*`: 0 fields, 1 row buffers, 2 control, 3 rendering, 4 alignment  |
| `rd_init(memory, bytes, mode, seed)`       | Initialize and seed, returning aligned state or null                            |
| `rd_params(state, feed, kill, da, db, dt)` | Integer Q15 coefficients, each in [0,32768]                                     |
| `rd_seed(state, x, y, radius)`             | Display coordinates x∈[0,199], y∈[0,227], radius∈[1,100]                        |
| `rd_step(state, count)`                    | Advance count∈[0,1000000] synchronously                                         |
| `rd_get(state, x, y, species)`             | Grid coordinates, species 0=A or 1=B, return Q24 (`RD_VALUE_ONE` is 1) or −1    |
| `rd_steps(state)`                          | Unsigned step counter                                                           |
| `rd_hash(state)`                           | FNV-1a over canonical little-endian stored 16-bit words, residual rows excluded |
| `rd_row(state, y, palette, quantize)`      | Shared 800-byte RGBA output for display row y∈[0,227]                           |

Palettes are 0 lime, 1 cyan, 2 monochrome (`RD_PALETTE_*`). Quantize is 0 or 1. `core/rd.h` also defines `RD_VERSION`, `RD_Q15_ONE`, `RD_VALUE_BITS`, `RD_VALUE_ONE`, `RD_DISPLAY_WIDTH`, `RD_DISPLAY_HEIGHT`, `RD_ROW_BYTES`, and `RD_SPECIES_A` / `RD_SPECIES_B` for C callers. A returned rendering row is overwritten by the next row call. The Web adapter copies each row into a persistent ImageData. The watch adapter writes RGB2 values directly into the OS framebuffer and owns no full-screen image.

Validation occurs before mutations. Invalid coefficients, seed coordinates, step counts, output parameters, unsupported modes, and undersized initialization blocks leave state unchanged. Callers must supply live owned memory and a valid state pointer. Arbitrary dangling pointers are outside the C API contract. The API is not thread-safe for simultaneous use of one state.

The existing Float32 implementation remains in `lib/simulation.ts` as a reference with its original tests. The Web can explicitly select it through `lib/float-simulation.ts` for same-resolution visual comparisons. Pebble builds use only the C implementation.
