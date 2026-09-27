# Float32 precision comparison

The fixed-point implementation does **not** retain the same cell-by-cell numerical precision as the original Float32 field implementation, but numerical definition version 4 comes very close to it on the 120 × 136 grid. Version 5 left every Gray–Scott result unchanged and added FitzHugh–Nagumo, measured in its own section below. Compiler optimization itself did not change the fixed-point result in the tested cases. The remaining differences come from the Q15 storage and the Q24 update of the 32-bit arithmetic.

## Reproducible method

```sh
mise run build-wasm
pnpm run test:optimization
pnpm run compare:float
```

`scripts/compare-float.mjs` loads the production Wasm compiled with `emcc -O3` and runs the existing `lib/simulation.ts` class. For the five Gray–Scott presets `maze`, `coral`, `mitosis`, `spots`, and `thin-line` of `lib/presets.ts` (fixed in the script, so presets added later do not change these averages), and seeds 42 and 1234, it starts Float32 fields from the exact Q24 concentrations returned by `rd_get`. It uses the same 120 × 136 grid and the same Q15-effective feed, kill, diffusion, and timestep parameters. The diffusion coefficients are the folded values the core computes with, `round(D × 32768 / 20) × 20 / 32768` (0.99976 of the request for Da = 1 and Db = 0.5). This removes seed placement, grid resolution, and parameter rounding as independent causes of the reported error. Both fields advance one step at a time through checkpoints 0, 1, 10, 100, and 1,000.

The reference stores its fields in `Float32Array` and uses JavaScript number arithmetic between writes, as in the original Web implementation. It is a comparison with that implementation, not with an independent all-operations-binary32 solver. Mean absolute error (MAE) is over every B cell on a concentration scale from 0 to 1. Spatial correlation compares the full B fields. All individual case results, including A errors, maxima, RMSE, fraction of cells differing by more than 0.01, effective parameters, and means are in [float-precision.json](float-precision.json).

## Version 4 and 5 Gray–Scott results

Version 5 reproduces these numbers exactly: the Gray–Scott rows of `float-precision.json` are unchanged apart from their `model` field, and each mean is still over the same 10 cases of five presets and two seeds.

| Grid and storage       | B MAE after 1 step, mean of 10 cases | B MAE after 100 steps, mean | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ---------------------- | -----------------------------------: | --------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 120 × 136, Q15 storage |                            0.0000012 |                    0.000008 |                       0.00003 |                             0.00006 |                                       > 0.9999 |

The thin-line preset is the most sensitive case. Its two-seed mean B MAE at 1,000 steps is 0.00005. No cell differs from the reference by more than 0.01 at 1,000 steps in any case, and the largest single-cell difference is 0.0033. Version 3 measured 0.00003 with the same script, so the 32-bit arithmetic of version 4 costs no precision against the reference with the folded coefficients. Against a reference with exact Da = 1 and Db = 0.5, the 0.024% fold shows as a fixed parameter offset of 0.00009 in the mean and 0.00033 in the worst case.

## FitzHugh–Nagumo, version 5

The same script runs the three FitzHugh–Nagumo presets with seeds 42 and 1234 against `lib/fhn-simulation.ts`. The Float32 fields start from x = 4 s − 2 of the `rd_get` values, and the reference uses the parameters the core computes with: du and dv folded like the Gray–Scott diffusion coefficients, k and rest rounded to Q13 as `floor((q + 2) / 4) / 8192`, the others rounded to Q15. v and u are compared in units of x, whose range is 4, and a cell counts as differing when it is off by more than 0.04, 1% of that range. The table shows u. The v errors are about 40% of the u errors.

| Preset        | u MAE after 1 step, mean of 2 seeds | u MAE after 100 steps, mean | u MAE after 1,000 steps, mean | u MAE after 1,000 steps, worst case | Largest single-cell difference at 1,000 steps | Lowest u spatial correlation after 1,000 steps |
| ------------- | ----------------------------------: | --------------------------: | ----------------------------: | ----------------------------------: | --------------------------------------------: | ---------------------------------------------: |
| `fhn-stripes` |                           0.0000044 |                    0.000172 |                       0.00207 |                             0.00226 |                                        0.0366 |                                        0.99995 |
| `fhn-hex`     |                           0.0000046 |                    0.000157 |                       0.00263 |                             0.00285 |                                        0.0728 |                                        0.99992 |
| `fhn-spiral`  |                           0.0000399 |                    0.000134 |                       0.00372 |                             0.00372 |                                        0.1125 |                                        0.99990 |

The spiral starts from the broken wave, which does not depend on the seed, so its two seeds are identical. At 1,000 steps the stripes have no cell above 0.04, and the spots 0.16% of their cells. The spiral keeps moving, so a front displaced by a fraction of a cell shows as a large difference in the cells it crosses, and 1.5% of its cells are above 0.04 at 1,000 steps. It is recorded, not held to the stationary presets. In units of the stored fraction, a quarter of x, the stationary presets are at about 0.0005 to 0.0007 after 1,000 steps, about twenty times Gray–Scott. The faster rates of the presets (1.5 times ru and rv, [Shared C core](core.md#parameter-search)) raised these errors by about half, because each step reacts more. The Q13 arithmetic floors u², u³, and av × v, and k and rest are rounded to Q13, which is a likely part of the difference. That was not separated further. The spatial correlations stay above 0.9999.

## Determinism

`scripts/check-optimization.sh` compiles the C core with `-O0` and `-O3`. Its field hashes match exactly for both models at 1, 100, and 1,000 steps with seed 42, with and without the clock mask, and with the analog face mask for Gray–Scott, and match the hashes recorded in `tests/golden-hashes.txt`. The existing native-versus-Wasm checks cover the optimized Wasm build. These are deterministic checks of the fixed-point implementation. They do not imply equality with Float32.

Float32 storage has about 24 significant binary bits. The core computes its Gray–Scott rates as exact Q30 products and rounds them once to Q24, so its remaining error is the Q15 storage step of 0.00003 per write, mostly cancelled by error diffusion. Reaction-diffusion patterns amplify small state differences over time, so similar overall shapes can still shift position over long runs. [Storage precision study](precision-optimization.md) compares the arithmetic candidates.
