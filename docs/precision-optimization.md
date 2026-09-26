# Storage precision study

Numerical definition version 1 stored the 200 × 228 field as two 8-bit planes with stochastic rounding and rounded every product to Q15. Its B field drifted from the Float32 reference by a mean absolute error of 0.0146 after 1,000 steps, with a worst-case spatial correlation of 0.76, so patterns visibly shifted. Two 16-bit planes would need 182,400 B, which does not fit next to the watch application in the 128 KiB app region. This study measured which techniques recover precision within 16 bits per cell. Version 2 of the core adopted the best combination, and versions 3 and 4 followed as recorded below; version 2 of the core adopts the best combination. [Shared C core](core.md) is the resulting contract and [Float32 precision comparison](float-precision.md) reports the production Wasm build.

## Method

`tests/precision.c` is a self-contained simulator with the version 1 arithmetic, the version 2 integer arithmetic, the version 4 32-bit candidates (`q15x32c1` to `q15x32c3`, below), and double arithmetic as selectable paths, plus general storage codes and rounding schemes. Each candidate starts from the same 24-disk initial field as `rd_init`, quantized to its own nearest codes. The Float32 reference (float storage, double intermediates, the operation order of `lib/simulation.ts`) starts from the candidate's decoded initial values, so checkpoint 0 has no error. Five presets including the thin-line case and seeds 42 and 1234 give ten cases per candidate. The reported numbers are the B mean absolute error over all cells at 100 and 1,000 steps, its worst case, the lowest B spatial correlation, and the A mean absolute error. Stochastic candidates use the version 1 hash, so a candidate's value at 1,000 steps varies by about ±15% between realizations. Comparisons rest on the ten-case mean.

```sh
sh scripts/precision-sweep.sh
```

Candidate names follow `tests/precision.c`: `lin<bits>` is a linear code over [0, 1], `pow<bits>:2` a code with value proportional to the square of the code, `sqr<bits>` the core's closed form `(code / 2^bits)²`, `q15` a Q15 word, `sto` stochastic rounding, `near` nearest rounding, `ed1` one-dimensional error diffusion along the row, `fs` Floyd–Steinberg error diffusion, `q15` version 1 arithmetic, `q<P>` version 2 arithmetic with P fraction bits, and `exact` double arithmetic. `d=<n>` dithers the rounding threshold of `near`, `ed1`, and `fs` by n sixteenths: 0 is the nearest code and 16 a fully random threshold.

## Techniques

**Square-root companded B codes.** The reaction `A × B²` and the decay `(feed + kill) × B` make the B update sensitive to small absolute B errors where B is between about 0.05 and 0.3, at pattern fronts. In the Float32 reference over 10,000 steps, B reaches 0.66 transiently in the coral preset and A drops to 0.08 in the thin-line preset, so codes must cover the full range. A code proportional to √B spaces steps in proportion to √B: half the linear step at B = 0.25 and a quarter at B = 0.06, at the cost of twice the linear step near B = 1, where the field never is. Codes limited to [0, 0.5] were rejected because they would clip.

**Bit reallocation.** Both species share 16 bits per cell. A ranges from about 0.1 to 1 and enters the reaction scaled by B², so it tolerates a coarser code. Seven bits for A and nine for B doubles the B resolution.

**Error diffusion.** Stochastic rounding is unbiased on average but injects independent noise of up to one code step into every cell at every step. Pattern position is a neutral direction of the dynamics, so this noise accumulates as a random walk of the pattern. Floyd–Steinberg error diffusion instead carries each cell's rounding error into the cells written next, which conserves the local average and moves the error to high spatial frequencies that diffusion removes within a few steps. Nearest rounding without diffusion creates a dead zone: rates below half a code step never move a cell.

**Higher-precision arithmetic.** Version 1 rounded the reaction and each rate term to Q15, an error of 1.5 × 10⁻⁵ per term per step that is systematic for slowly changing cells. Version 2 keeps concentrations in Q24, accumulates rates as exact Q39 products, and rounds once per update.

**Dithered rounding threshold.** Error diffusion with a fixed halfway threshold is deterministic, and with coarse codes it can pin a slowly moving front: the residual pattern around the front settles into a cycle that the front cannot leave. Spreading the threshold uniformly over [1/4, 3/4) of the code step with a per-cell hash breaks such cycles while the diffused residual still conserves the local average. A fully random threshold is ordinary stochastic rounding with error feedback.

## Results

Mean over 10 cases (5 presets × 2 seeds), 200 × 228 unless `g=100` or `g=120`:

| Candidate                             | Cases | B MAE @100 | Worst B MAE @100 | Min B corr @100 | A MAE @100 | B MAE @1000 | Worst B MAE @1000 | Min B corr @1000 | A MAE @1000 |
| ------------------------------------- | ----- | ---------- | ---------------- | --------------- | ---------- | ----------- | ----------------- | ---------------- | ----------- |
| `g=200,a=q15,b=q15,r=fs,i=q24`        | 10    | 0.00001    | 0.00001          | 1.000           | 0.0000     | 0.00002     | 0.00004           | 1.000            | 0.0000      |
| `g=100,a=q15,b=q15,r=fs,i=q24`        | 10    | 0.00001    | 0.00004          | 1.000           | 0.0000     | 0.00003     | 0.00010           | 1.000            | 0.0000      |
| `g=100,a=q15,b=q15,r=fs,i=q24,d=8`    | 10    | 0.00002    | 0.00014          | 1.000           | 0.0000     | 0.00006     | 0.00036           | 1.000            | 0.0001      |
| `g=100,a=q15,b=q15,r=sto,i=q24`       | 10    | 0.00003    | 0.00016          | 1.000           | 0.0000     | 0.00016     | 0.00039           | 1.000            | 0.0003      |
| `g=100,a=q15,b=q15,r=near,i=q24`      | 10    | 0.00007    | 0.00044          | 0.999           | 0.0001     | 0.00051     | 0.00162           | 0.999            | 0.0008      |
| `g=200,a=lin8,b=sqr10,r=fs,i=q24,d=8` | 10    | 0.00011    | 0.00018          | 1.000           | 0.0014     | 0.00101     | 0.00307           | 0.995            | 0.0025      |
| `g=200,a=lin8,b=sqr10,r=fs,i=q24`     | 10    | 0.00011    | 0.00018          | 1.000           | 0.0011     | 0.00135     | 0.00223           | 0.996            | 0.0030      |
| `g=200,a=lin7,b=sqr9,r=fs,i=q24,d=8`  | 10    | 0.00022    | 0.00039          | 1.000           | 0.0028     | 0.00215     | 0.00526           | 0.989            | 0.0051      |
| `g=200,a=lin7,b=sqr9,r=fs,i=q24,d=16` | 10    | 0.00031    | 0.00058          | 1.000           | 0.0034     | 0.00267     | 0.00771           | 0.966            | 0.0064      |
| `g=200,a=lin7,b=sqr9,r=fs,i=q24,d=4`  | 10    | 0.00023    | 0.00041          | 1.000           | 0.0022     | 0.00288     | 0.00540           | 0.963            | 0.0064      |
| `g=200,a=lin7,b=sqr9,r=fs,i=exact`    | 10    | 0.00024    | 0.00043          | 1.000           | 0.0021     | 0.00307     | 0.00474           | 0.967            | 0.0067      |
| `g=200,a=q15,b=q15,r=near,i=q15`      | 10    | 0.00011    | 0.00021          | 1.000           | 0.0002     | 0.00317     | 0.01495           | 0.944            | 0.0054      |
| `g=200,a=lin7,b=sqr9,r=fs,i=q24`      | 10    | 0.00024    | 0.00043          | 1.000           | 0.0021     | 0.00323     | 0.00563           | 0.969            | 0.0070      |
| `g=200,a=lin7,b=sqr9,r=fs,i=q20`      | 10    | 0.00024    | 0.00042          | 1.000           | 0.0021     | 0.00327     | 0.00541           | 0.963            | 0.0070      |
| `g=200,a=lin7,b=sqr9,r=ed1,i=q24`     | 10    | 0.00029    | 0.00054          | 1.000           | 0.0030     | 0.00358     | 0.01005           | 0.960            | 0.0077      |
| `g=200,a=lin7,b=lin9,r=fs,i=q24`      | 10    | 0.00033    | 0.00047          | 1.000           | 0.0021     | 0.00436     | 0.00971           | 0.960            | 0.0087      |
| `g=100,a=q15,b=q15,r=near,i=q15`      | 10    | 0.00019    | 0.00045          | 1.000           | 0.0003     | 0.00450     | 0.01280           | 0.953            | 0.0074      |
| `g=200,a=lin6,b=sqr10,r=fs,i=q24`     | 10    | 0.00039    | 0.00071          | 1.000           | 0.0042     | 0.00472     | 0.00970           | 0.954            | 0.0114      |
| `g=200,a=lin7,b=pow9:2,r=fs,i=q15`    | 10    | 0.00027    | 0.00049          | 1.000           | 0.0021     | 0.00482     | 0.01119           | 0.950            | 0.0097      |
| `g=200,a=lin7,b=lin9,r=fs,i=q15`      | 10    | 0.00035    | 0.00052          | 1.000           | 0.0021     | 0.00562     | 0.01241           | 0.960            | 0.0108      |
| `g=100,a=lin7,b=sqr9,r=fs,i=q24,d=8`  | 10    | 0.00058    | 0.00166          | 0.980           | 0.0031     | 0.00617     | 0.03096           | 0.834            | 0.0112      |
| `g=200,a=lin6,b=pow10:2,r=fs,i=q15`   | 10    | 0.00041    | 0.00074          | 1.000           | 0.0042     | 0.00630     | 0.01913           | 0.876            | 0.0144      |
| `g=200,a=lin8,b=lin8@0.5,r=sto,i=q15` | 10    | 0.00092    | 0.00169          | 0.999           | 0.0019     | 0.00923     | 0.02779           | 0.854            | 0.0160      |
| `g=100,a=lin7,b=sqr9,r=fs,i=q24`      | 10    | 0.00057    | 0.00184          | 0.973           | 0.0030     | 0.01025     | 0.03204           | 0.835            | 0.0181      |
| `g=200,a=lin8,b=sqr8,r=fs,i=q24`      | 10    | 0.00038    | 0.00060          | 1.000           | 0.0012     | 0.01155     | 0.02049           | 0.851            | 0.0202      |
| `g=200,a=lin8,b=pow8:2,r=fs,i=q15`    | 10    | 0.00041    | 0.00066          | 1.000           | 0.0012     | 0.01165     | 0.02135           | 0.782            | 0.0204      |
| `g=200,a=lin7,b=lin9,r=sto,i=q15`     | 10    | 0.00085    | 0.00125          | 0.999           | 0.0025     | 0.01205     | 0.03226           | 0.821            | 0.0212      |
| `g=200,a=lin8,b=pow8:1.5,r=sto,i=q15` | 10    | 0.00092    | 0.00117          | 0.997           | 0.0019     | 0.01259     | 0.03214           | 0.806            | 0.0220      |
| `g=200,a=lin7,b=pow9:2,r=sto,i=q15`   | 10    | 0.00070    | 0.00101          | 1.000           | 0.0024     | 0.01294     | 0.03673           | 0.765            | 0.0231      |
| `g=200,a=lin8,b=lin8,r=fs,i=q15`      | 10    | 0.00080    | 0.00111          | 0.999           | 0.0015     | 0.01329     | 0.01961           | 0.856            | 0.0225      |
| `g=200,a=lin7,b=sqr9,r=sto,i=q24`     | 10    | 0.00073    | 0.00100          | 0.999           | 0.0025     | 0.01342     | 0.03946           | 0.736            | 0.0239      |
| `g=200,a=lin8,b=pow8:2,r=sto,i=q15`   | 10    | 0.00090    | 0.00125          | 0.999           | 0.0018     | 0.01350     | 0.03353           | 0.809            | 0.0236      |
| `g=200,a=lin8,b=lin8,r=sto,i=exact`   | 10    | 0.00116    | 0.00158          | 0.999           | 0.0019     | 0.01431     | 0.03857           | 0.739            | 0.0246      |
| `g=200,a=lin8,b=lin8,r=ed1,i=q15`     | 10    | 0.00102    | 0.00136          | 1.000           | 0.0018     | 0.01441     | 0.02243           | 0.714            | 0.0255      |
| `g=200,a=lin8,b=lin8,r=sto,i=q15`     | 10    | 0.00117    | 0.00166          | 0.999           | 0.0019     | 0.01533     | 0.04045           | 0.686            | 0.0268      |
| `g=200,a=lin6,b=pow10:2,r=sto,i=q15`  | 10    | 0.00116    | 0.00164          | 0.999           | 0.0044     | 0.01793     | 0.04384           | 0.724            | 0.0322      |
| `g=100,a=lin8,b=lin8,r=sto,i=q15`     | 10    | 0.00206    | 0.00428          | 0.997           | 0.0031     | 0.01913     | 0.04527           | 0.716            | 0.0317      |
| `g=200,a=lin6,b=lin10,r=sto,i=q15`    | 10    | 0.00125    | 0.00172          | 0.999           | 0.0045     | 0.01953     | 0.05207           | 0.622            | 0.0352      |
| `g=200,a=lin8,b=lin8,r=near,i=q15`    | 10    | 0.00506    | 0.00735          | 0.941           | 0.0093     | 0.06453     | 0.13889           | 0.006            | 0.1226      |

Version 1 mode 0 is `g=200,a=lin8,b=lin8,r=sto,i=q15`, version 1 mode 1 is `g=100,a=q15,b=q15,r=near,i=q15`, and version 1 mode 2 is `g=100,a=lin8,b=lin8,r=sto,i=q15`. Version 3 mode 0 is `g=200,a=lin7,b=sqr9,r=fs,i=q24,d=8`, version 3 mode 1 is `g=100,a=q15,b=q15,r=fs,i=q24`, and version 3 mode 2 is `g=100,a=lin7,b=sqr9,r=fs,i=q24,d=8`. Version 2 used `d=8` in mode 1 as well.

## Findings

- **Each technique alone gains little on the 8-bit baseline.** Square-root B codes, Floyd–Steinberg diffusion, and double arithmetic reduce the 1,000-step error from 0.0153 only to 0.0135, 0.0133, and 0.0143. Nearest rounding is far worse at 0.0645 because of its dead zone.
- **Finer B codes are wasted under stochastic rounding.** Nine linear or companded B bits with seven A bits give 0.0121 and 0.0129, and six A bits with ten B bits is worse than the baseline, because the injected A noise then dominates.
- **Error diffusion turns finer codes into precision.** With Floyd–Steinberg diffusion, seven A bits and nine companded B bits reach 0.0048 with version 1 arithmetic and 0.0032 with version 2 arithmetic, 4.7 times better than the baseline with the same 91,200 B of field memory and slightly better than version 1 mode 1 at 100 × 114. Companding remains worth 25% over nine linear bits, one-dimensional diffusion loses 10% in the mean and doubles the worst case, and twenty fraction bits are within 2% of twenty-four.
- **Q15 arithmetic was the dominant error of the Q15 storage mode.** Version 2 arithmetic with error diffusion reduces the 100 × 114 Q15 error from 0.0045 to 0.00003, and nearest or stochastic rounding of the Q15 store would leave 0.0005 or 0.0002.
- **A half-strength threshold dither improves both accuracy and long-run statistics.** `d=8` lowers the version 2 mode 0 error from 0.0032 to 0.0022 and raises its lowest correlation from 0.97 to 0.99, while `d=4` and `d=16` are worse than `d=8`. It is adopted for the packed modes. The Q15 store of mode 1 rounds to the nearest code instead (`d=0`, 0.00003 against 0.00006 with the dither, and a spots mass ratio of 0.99 without it), which also removes the per-cell hash from that mode.
- **Eighteen bits per cell would halve the error again.** Eight A bits with ten companded B bits reach 0.0014 at 1,000 steps without dither and 0.0010 with it. This needs 11,400 B more than mode 0 and an extension plane for the two extra bits, and it is not implemented.

## Long runs

At 3,000 steps and beyond, every packed candidate diverges from the Float32 reference cell by cell, because the remaining differences move pattern fronts and reaction-diffusion patterns amplify any displacement. Whether the field still has the same statistics is a separate question, answered here with the mean B concentration at 10,000 steps, which counts how much pattern exists.

The spots preset near its survival boundary exposed a real bias of undithered error diffusion. Most disks die during the first 1,000 steps and the field then depends on whether a few survivors split. Over seeds 1 through 8, the reference B mean at 10,000 steps is 0.085. The undithered packed field (`d=0`) reaches 0.87 of it on average and 0.62 in the worst seed, so fewer spots multiply. The version 1 8-bit stochastic field reaches 0.99 and the Q15 field with version 2 arithmetic 0.99, so the loss belongs to the deterministic threshold on coarse codes, not to error diffusion itself. With the adopted `d=8` dither the packed field reaches 1.00 (0.97 to 1.02 by seed), and `d=16` 1.00 as well, while `d=4` still loses 6%. For the maze, cell-division, and thin-line presets over four seeds, the ratios are 1.00, 1.01, and 1.00 with the dither, and 0.99, 0.97, and 1.01 without it. The dither also raises the maze correlation at 10,000 steps from about 0.6 to about 0.9.

Even with the dither, the marginal spots case is reproduced statistically, not seed by seed: the Q15 field with version 2 arithmetic, whose error at 1,000 steps is 0.00003, ends seed 1234 at a correlation of 0.48.

## Version 4 arithmetic

The Q15 modes of version 3 computed every cell with 64-bit products, sums, and rounding, which the watch measured as the largest phase of a step. Three 32-bit candidates on the Q15 codes were added to the harness and swept over the five presets and seeds 42 and 1234 on the 100 × 114 and 120 × 136 grids, against version 3 (`i=q24`) and the version 1 arithmetic. C1 rounds the Laplacian and A × B to Q15 before the products. C2 keeps the exact 20-fold Laplacian sum, folds each diffusion coefficient with the 1/20 into `round(D / 20)`, and computes A × B² exactly to the Q30 unit with the product split at bit 15. C3 rounds the Laplacian like C1 and computes the reaction like C2. All three round the Q30 rate once to Q24 and encode as version 3 does. The C2 reference runs with the folded coefficients, as the Float32 comparison of the core does.

| Candidate                  | B MAE at 1,000 steps, 100 × 114 |   Worst | B MAE at 1,000 steps, 120 × 136 |   Worst |
| -------------------------- | ------------------------------: | ------: | ------------------------------: | ------: |
| version 1 (`r=near,i=q15`) |                         0.00450 | 0.01280 |                         0.00512 | 0.02943 |
| version 3 (`i=q24`)        |                         0.00003 | 0.00010 |                         0.00003 | 0.00005 |
| C1 (`i=q15x32c1`)          |                         0.00016 | 0.00090 |                         0.00009 | 0.00037 |
| C2 (`i=q15x32c2`)          |                         0.00004 | 0.00015 |                         0.00003 | 0.00006 |
| C3 (`i=q15x32c3`)          |                         0.00018 | 0.00098 |                         0.00008 | 0.00021 |

The per-step Q15 rounding of C1 and C3 costs three to six times the error of version 3, while C2 matches it. Over seeds 1 through 8 of the spots preset at 10,000 steps, every candidate reproduces the reference B mean within 0.1% with correlations above 0.9997, so none of them shifts the marginal case. C2 became numerical definition version 4. On the watch a mode 3 step fell from 18.0 ms to 14.25 ms with the row loop of that time, and to 13.1 ms with the later three-pass loop.

## Cost

On the host, the `tests/compare.c` binary advances mode 0 in 2.3 ms per step with version 2 and took 3.4 ms with version 1, because each stored row is decoded once into a 32-bit row and one hash per cell replaces the three per species of version 1. The B encode uses a 9-iteration integer square root. Mode 0 needs 100,075 B instead of 93,647 B and mode 1 50,475 B instead of 48,047 B for the decoded rows, the residual rows, and the dither salt (version 2 figures; the current allocations, with the mask levels and the interpolation tables, are 147,695 B and 63,179 B, see [Shared C core](core.md)). The Emery watchface builds for both modes with the SDK's `-Werror` in the same `.text` size class as before. On-watch timing and heap were measured later, as recorded in [Validation](validation.md).
