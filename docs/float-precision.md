# Float32 precision comparison

The fixed-point implementation does **not** retain the same cell-by-cell numerical precision as the original Float32 field implementation, but numerical definition version 2 brings every mode far closer to it. Compiler optimization itself did not change the fixed-point result in the tested cases. The remaining differences come from the storage codes and, in the 16-bit Q15 mode, from Q24 arithmetic.

## Reproducible method

```sh
mise run build-wasm
npm run test:optimization
npm run compare:float
```

`scripts/compare-float.mjs` loads the production Wasm compiled with `emcc -O3` and runs the existing `lib/simulation.ts` class. For each mode, five presets including the thin-line case, and seeds 42 and 1234, it starts Float32 fields from the exact Q24 concentrations returned by `rd_get`. It uses the same grid size and the same Q15-effective feed, kill, diffusion, and timestep parameters. This removes seed placement, grid resolution, and parameter rounding as independent causes of the reported error. Both fields advance one step at a time through checkpoints 0, 1, 10, 100, and 1,000.

The reference stores its fields in `Float32Array` and uses JavaScript number arithmetic between writes, as in the original Web implementation. It is a comparison with that implementation, not with an independent all-operations-binary32 solver. Mean absolute error (MAE) is over every B cell on a concentration scale from 0 to 1. Spatial correlation compares the full B fields. All individual case results, including A errors, maxima, RMSE, fraction of cells differing by more than 0.01, effective parameters, and means are in [float-precision.json](float-precision.json).

## Version 2 results

| Core mode     | Grid and storage        | B MAE after 1 step, mean of 10 cases | B MAE after 100 steps, mean | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ------------- | ----------------------- | -----------------------------------: | --------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 0             | 200 × 228, packed A7/B9 |                            0.0000488 |                    0.000223 |                       0.00202 |                             0.00497 |                                          0.994 |
| 1             | 100 × 114, Q15 storage  |                            0.0000013 |                    0.000017 |                       0.00004 |                             0.00020 |                                       > 0.9999 |
| 2, diagnostic | 100 × 114, packed A7/B9 |                            0.0000557 |                    0.000488 |                       0.00450 |                             0.01835 |                                          0.880 |

The thin-line preset remains the most sensitive case for the packed modes. Its two-seed mean B MAE at 1,000 steps is 0.00449 for mode 0, 0.00003 for mode 1, and 0.0113 for diagnostic mode 2. In the worst mode 0 case at 1,000 steps, 11.2% of cells differ from the reference by more than 0.01 and the largest single-cell difference is 0.225, so local pattern displacement still occurs, only later and less often than before. Mode 1 has no cell above 0.01 at 1,000 steps in any case, and its largest single-cell difference is 0.0070.

## Change from version 1

Version 1 stored 8-bit linear codes with stochastic rounding in modes 0 and 2, and rounded every product to Q15 in every mode. The same script produced these results for it:

| Core mode     | Grid and storage       | B MAE after 1 step, mean | B MAE after 100 steps, mean | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ------------- | ---------------------- | -----------------------: | --------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 0             | 200 × 228, 8-bit       |                 0.000158 |                     0.00119 |                       0.01458 |                             0.03547 |                                          0.763 |
| 1             | 100 × 114, Q15 storage |               0.00000132 |                    0.000190 |                       0.00450 |                             0.01280 |                                          0.953 |
| 2, diagnostic | 100 × 114, 8-bit       |                 0.000176 |                     0.00211 |                       0.01771 |                             0.03678 |                                          0.804 |

Mode 0 now has 7.2 times less B error at 1,000 steps in the mean and 7.1 times less in the worst case, with the same field memory. Mode 1 has about 100 times less error, because its previous error came almost entirely from Q15 intermediate rounding rather than from its 16-bit storage. Diagnostic mode 2 has 3.9 times less error. The version 1 thin-line mean at 1,000 steps was 0.03489 for mode 0 and 0.01082 for mode 1. [Storage precision study](precision-optimization.md) explains which technique contributes what.

`scripts/check-optimization.sh` compiles the C core with `-O0` and `-O3`. Its field hashes match exactly for all three modes at 1, 100, and 1,000 steps with seed 42. The existing native-versus-Wasm checks cover the optimized Wasm build. These are deterministic checks of the fixed-point implementation. They do not imply equality with Float32.

Float32 storage has about 24 significant binary bits. Q24 calculation has 24 fractional bits, so the remaining error in mode 1 is the Q15 storage step of 0.00003 per write, mostly cancelled by error diffusion and slightly spread by its dither. The packed modes store B with a step between 0.0009 and 0.0039 depending on its value and A with a step of 0.0079. Error diffusion preserves the local average far better than the step, but reaction-diffusion patterns amplify small state differences over time, so similar overall shapes can still shift position. Choosing a mode still requires judging visual quality, memory, and timing on a physical watch.
