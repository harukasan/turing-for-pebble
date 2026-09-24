# Shared C core

## Modes and memory contract

| Mode          | Grid      | Storage per species |   Fields | Row buffers | Total caller allocation |
| ------------- | --------- | ------------------- | -------: | ----------: | ----------------------: |
| 0             | 200 × 228 | unsigned 8-bit      | 91,200 B |     1,600 B |                93,647 B |
| 1             | 100 × 114 | unsigned 16-bit Q15 | 45,600 B |     1,600 B |                48,047 B |
| 2, diagnostic | 100 × 114 | unsigned 8-bit      | 22,800 B |       800 B |                24,447 B |

Totals include a 44-byte control structure, an 800-byte RGBA output row, and up to 3 bytes of alignment padding. Consumers must query `rd_bytes` and `rd_memory` instead of relying on these recorded values. The core performs no allocation. The caller owns a single contiguous block and frees the original pointer, not the potentially aligned state pointer returned by `rd_init`.

Each species has one concentration plane. Four saved rows per species hold the previous, current, next, and original first row. Rotating the first three row pointers preserves the old field while writing in place. The saved first row supplies the periodic boundary when the last row is updated.

## Numerical definition, version 1

The model is Gray–Scott with an explicit Euler step and periodic boundaries:

```text
A' = clamp(A + dt × (Da × lap(A) − A × B² + feed × (1 − A)))
B' = clamp(B + dt × (Db × lap(B) + A × B² − (feed + kill) × B))
```

Concentrations and coefficients are calculated in Q15, where 32768 means 1. Products use signed 64-bit intermediates. Every Q15 multiplication is rounded to nearest, with halfway values rounded away from zero. The reaction is evaluated as `mul_q15(mul_q15(A, B), B)`.

The nine-point Laplacian (`laplacian` in `core/rd.c`) is evaluated as `(4 × axial_sum + diagonal_sum − 20 × center) / 20`, rounded to nearest with ties away from zero. These exact rational weights preserve uniform fields exactly, avoiding the equilibrium drift that independently rounded 0.2 and 0.05 Q15 coefficients would introduce.

16-bit storage preserves the Q15 value directly. For 8-bit storage, reading expands a byte to Q15 with `(value × 32768 + 127) / 255`. Writing clamps to [0,32768], scales by 255, and stochastically rounds the remaining fraction. A fixed unsigned 32-bit mixing function combines the seed, linear cell coordinate, step counter, and species. The lower 15 random bits are compared with the Q15 remainder. There is no per-cell random state or traversal-order dependency. Step counters wrap modulo 2³².

## Initialization

The initial equilibrium is A=1 and B=0. A 32-bit LCG places 24 disks in a common 200 × 228 display coordinate system. Disk radii range from 4 through 9 display pixels. Grid cells sample their corresponding display coordinates, with periodic distance used at edges. Seeded values are A=0.5 and B=0.25, stored as 128/255 and 64/255 in the 8-bit implementation. Grid resolution still changes the physical scale of emergent patterns.

## Public API

All functions are declared in `core/rd.h`.

| Function                                   | Contract                                                                       |
| ------------------------------------------ | ------------------------------------------------------------------------------ |
| `rd_bytes(mode)`                           | Caller allocation size including alignment, zero for unsupported mode          |
| `rd_memory(mode, component)`               | `RD_COMPONENT_*`: 0 fields, 1 row buffers, 2 control, 3 rendering, 4 alignment |
| `rd_init(memory, bytes, mode, seed)`       | Initialize and seed, returning aligned state or null                           |
| `rd_params(state, feed, kill, da, db, dt)` | Integer Q15 coefficients, each in [0,32768]                                    |
| `rd_seed(state, x, y, radius)`             | Display coordinates x∈[0,199], y∈[0,227], radius∈[1,100]                       |
| `rd_step(state, count)`                    | Advance count∈[0,1000000] synchronously                                        |
| `rd_get(state, x, y, species)`             | Grid coordinates, species 0=A or 1=B, return Q15 or −1                         |
| `rd_steps(state)`                          | Unsigned step counter                                                          |
| `rd_hash(state)`                           | FNV-1a over canonical little-endian two-byte stored values                     |
| `rd_row(state, y, palette, quantize)`      | Shared 800-byte RGBA output for display row y∈[0,227]                          |

Palettes are 0 lime, 1 cyan, 2 monochrome (`RD_PALETTE_*`). Quantize is 0 or 1. `core/rd.h` also defines `RD_Q15_ONE`, `RD_DISPLAY_WIDTH`, `RD_DISPLAY_HEIGHT`, `RD_ROW_BYTES`, and `RD_SPECIES_A` / `RD_SPECIES_B` for C callers. A returned rendering row is overwritten by the next row call. The Web adapter copies each row into a persistent ImageData. The watch adapter writes RGB2 values directly into the OS framebuffer and owns no full-screen image.

Validation occurs before mutations. Invalid coefficients, seed coordinates, step counts, output parameters, unsupported modes, and undersized initialization blocks leave state unchanged. Callers must supply live owned memory and a valid state pointer. Arbitrary dangling pointers are outside the C API contract. The API is not thread-safe for simultaneous use of one state.

The existing Float32 implementation remains in `lib/simulation.ts` as a reference with its original tests. The Web can explicitly select it through `lib/float-simulation.ts` for same-resolution visual comparisons. Pebble builds use only the C implementation.
