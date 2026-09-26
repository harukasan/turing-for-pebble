# Float32 precision comparison

The fixed-point implementation does **not** retain the same cell-by-cell numerical precision as the original Float32 field implementation, but numerical definition version 4 brings every mode far closer to it. Compiler optimization itself did not change the fixed-point result in the tested cases. The remaining differences come from the storage codes and, in the Q15 modes, from the Q24 update of the 32-bit arithmetic.

## Reproducible method

```sh
mise run build-wasm
npm run test:optimization
npm run compare:float
```

`scripts/compare-float.mjs` loads the production Wasm compiled with `emcc -O3` and runs the existing `lib/simulation.ts` class. For each mode, five presets including the thin-line case, and seeds 42 and 1234, it starts Float32 fields from the exact Q24 concentrations returned by `rd_get`. It uses the same grid size and the same Q15-effective feed, kill, diffusion, and timestep parameters. In the Q15 modes the diffusion coefficients are the folded values the core computes with, `round(D × 32768 / 20) × 20 / 32768` (0.99976 of the request for Da = 1 and Db = 0.5). This removes seed placement, grid resolution, and parameter rounding as independent causes of the reported error. Both fields advance one step at a time through checkpoints 0, 1, 10, 100, and 1,000.

The reference stores its fields in `Float32Array` and uses JavaScript number arithmetic between writes, as in the original Web implementation. It is a comparison with that implementation, not with an independent all-operations-binary32 solver. Mean absolute error (MAE) is over every B cell on a concentration scale from 0 to 1. Spatial correlation compares the full B fields. All individual case results, including A errors, maxima, RMSE, fraction of cells differing by more than 0.01, effective parameters, and means are in [float-precision.json](float-precision.json).

## Version 4 results

| Core mode     | Grid and storage        | B MAE after 1 step, mean of 10 cases | B MAE after 100 steps, mean | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ------------- | ----------------------- | -----------------------------------: | --------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 0             | 200 × 228, packed A7/B9 |                            0.0000488 |                    0.000223 |                       0.00202 |                             0.00497 |                                          0.994 |
| 1             | 100 × 114, Q15 storage  |                            0.0000012 |                    0.000014 |                       0.00004 |                             0.00015 |                                       > 0.9999 |
| 2, diagnostic | 100 × 114, packed A7/B9 |                            0.0000557 |                    0.000488 |                       0.00450 |                             0.01835 |                                          0.879 |
| 3, watch      | 120 × 136, Q15 storage  |                            0.0000012 |                    0.000008 |                       0.00003 |                             0.00006 |                                       > 0.9999 |

The thin-line preset remains the most sensitive case for the packed modes. Its two-seed mean B MAE at 1,000 steps is 0.00449 for mode 0, 0.00003 for mode 1, 0.0113 for diagnostic mode 2, and 0.00005 for mode 3. In the worst mode 0 case at 1,000 steps, 11.2% of cells differ from the reference by more than 0.01 and the largest single-cell difference is 0.225, so local pattern displacement still occurs, only later and less often than before. Modes 1 and 3 have no cell above 0.01 at 1,000 steps in any case, and their largest single-cell differences are 0.0028 and 0.0033. Version 3 measured mode 1 and mode 3 at 0.00003 with the same script, so the 32-bit arithmetic of version 4 costs no precision against the reference with the folded coefficients. Against a reference with exact Da = 1 and Db = 0.5, the 0.024% fold shows as a fixed parameter offset: mode 1 then measures 0.00025 in the mean and 0.0017 in its worst case, the maze preset with seed 1234, and mode 3 0.00009 and 0.00033.

## Change from version 1

Version 1 stored 8-bit linear codes with stochastic rounding in modes 0 and 2, and rounded every product to Q15 in every mode. The same script produced these results for it:

| Core mode     | Grid and storage       | B MAE after 1 step, mean | B MAE after 100 steps, mean | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ------------- | ---------------------- | -----------------------: | --------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 0             | 200 × 228, 8-bit       |                 0.000158 |                     0.00119 |                       0.01458 |                             0.03547 |                                          0.763 |
| 1             | 100 × 114, Q15 storage |               0.00000132 |                    0.000190 |                       0.00450 |                             0.01280 |                                          0.953 |
| 2, diagnostic | 100 × 114, 8-bit       |                 0.000176 |                     0.00211 |                       0.01771 |                             0.03678 |                                          0.804 |

Mode 0 now has 7.2 times less B error at 1,000 steps in the mean and 7.1 times less in the worst case, with the same field memory. Mode 1 has about 114 times less error, because its previous error came almost entirely from Q15 intermediate rounding rather than from its 16-bit storage. Diagnostic mode 2 has 3.9 times less error. The version 1 thin-line mean at 1,000 steps was 0.03489 for mode 0 and 0.01082 for mode 1. [Storage precision study](precision-optimization.md) explains which technique contributes what.

`scripts/check-optimization.sh` compiles the C core with `-O0` and `-O3`. Its field hashes match exactly for all four modes at 1, 100, and 1,000 steps with seed 42, with and without the clock mask, and match the hashes recorded in `tests/golden-hashes.txt`. The existing native-versus-Wasm checks cover the optimized Wasm build. These are deterministic checks of the fixed-point implementation. They do not imply equality with Float32.

Float32 storage has about 24 significant binary bits. The Q15 modes compute their rates as exact Q30 products and round them once to Q24, so their remaining error is the Q15 storage step of 0.00003 per write, mostly cancelled by error diffusion. The packed modes store B with a step between 0.0009 and 0.0039 depending on its value and A with a step of 0.0079. Error diffusion preserves the local average far better than the step, but reaction-diffusion patterns amplify small state differences over time, so similar overall shapes can still shift position. Choosing a mode still requires judging visual quality, memory, and timing on a physical watch.
