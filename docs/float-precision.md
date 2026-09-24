# Float32 precision comparison

The optimized implementation does **not** retain the same cell-by-cell numerical precision as the original Float32 field implementation. Compiler optimization itself did not change the fixed-point result in the tested cases. The observed differences come from Q15 arithmetic and, more strongly in the 8-bit modes, concentration storage and stochastic rounding.

## Reproducible method

```sh
mise run build-wasm
npm run test:optimization
npm run compare:float
```

`scripts/compare-float.mjs` loads the production Wasm compiled with `emcc -O3` and runs the existing `lib/simulation.ts` class. For each mode, five presets including the thin-line case, and seeds 42 and 1234, it starts Float32 fields from the exact Q15 concentrations returned by `rd_get`. It uses the same grid size and the same Q15-effective feed, kill, diffusion, and timestep parameters. This removes seed placement, grid resolution, and parameter rounding as independent causes of the reported error. Both fields advance one step at a time through checkpoints 0, 1, 10, 100, and 1,000.

The reference stores its fields in `Float32Array` and uses JavaScript number arithmetic between writes, as in the original Web implementation. It is a comparison with that implementation, not with an independent all-operations-binary32 solver. Mean absolute error (MAE) is over every B cell on a concentration scale from 0 to 1. Spatial correlation compares the full B fields. All individual case results, including A errors, maxima, RMSE, fraction of cells differing by more than 0.01, effective parameters, and means are in [float-precision.json](float-precision.json).

| Core mode     | Grid and storage       | B MAE after 1 step, mean of 10 cases | B MAE after 1,000 steps, mean | B MAE after 1,000 steps, worst case | Lowest B spatial correlation after 1,000 steps |
| ------------- | ---------------------- | -----------------------------------: | ----------------------------: | ----------------------------------: | ---------------------------------------------: |
| 0             | 200 × 228, 8-bit       |                             0.000158 |                       0.01458 |                             0.03547 |                                          0.763 |
| 1             | 100 × 114, Q15 storage |                           0.00000132 |                       0.00450 |                             0.01280 |                                          0.953 |
| 2, diagnostic | 100 × 114, 8-bit       |                             0.000176 |                       0.01771 |                             0.03678 |                                          0.804 |

At 100 steps, the mean B MAE is 0.00119, 0.000190, and 0.00211 for modes 0, 1, and 2 respectively. All cases have zero error at checkpoint 0 by construction. Same-resolution modes 1 and 2 show that 8-bit storage adds substantial error beyond Q15 arithmetic. Comparing modes 0 and 1 directly also changes grid resolution and therefore cannot isolate storage precision.

The thin-line preset is more sensitive than the baseline cases in this checkpoint range. Its two-seed mean B MAE at 1,000 steps is 0.03489 for mode 0, 0.01082 for mode 1, and 0.03229 for diagnostic mode 2. These values compare each mode against Float32 at its own grid resolution. They do not measure visual similarity between the two output resolutions.

The mean includes unchanged background cells. In the worst case at 1,000 steps, 69.3% of mode 0 cells, 33.5% of mode 1 cells, and 65.7% of diagnostic mode 2 cells differ from the reference by more than 0.01. The corresponding largest single-cell absolute differences are 0.502, 0.274, and 0.431. These figures expose local pattern displacement that the mean alone would hide.

`scripts/check-optimization.sh` compiled the C core with `-O0` and `-O3`. Its field hashes matched exactly for all three modes at 1, 100, and 1,000 steps with seed 42. The existing native-versus-Wasm checks cover the optimized Wasm build. These are deterministic checks of the fixed-point implementation. They do not imply equality with Float32.

Float32 storage has about 24 significant binary bits. Q15 calculation has 15 fractional bits, while 8-bit concentration storage has 256 possible levels. Stochastic rounding reduces average quantization bias but cannot preserve an individual Float32 concentration. Reaction-diffusion patterns amplify small state differences over time, so similar overall shapes can eventually shift position. Mode 1 tracks the Float32 reference more closely in this test. Choosing a mode still requires judging visual quality, memory, and timing on a physical watch.
