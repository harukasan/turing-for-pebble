# Storage precision study

The core stores each species of the 120 × 136 grid as a Q15 word and computes a step on those codes. This study measures how the rounding of the stored codes and the arithmetic of a step affect the distance from the Float32 reference. [Shared C core](core.md) is the resulting contract and [Float32 precision comparison](float-precision.md) reports the production Wasm build.

## Method

`tests/precision.c` is a self-contained simulator with the version 1 arithmetic (every product rounded to Q15), the integer arithmetic of versions 2 and 3 (concentrations with P fraction bits, 24 in the core, and 64-bit products), the version 4 32-bit candidates (`q15x32c1` to `q15x32c3`, below), and double arithmetic as selectable paths, plus storage codes and rounding schemes. Each candidate starts from the same 24-disk initial field as `rd_init`, quantized to its own nearest codes. The Float32 reference (float storage, double intermediates, the operation order of `lib/simulation.ts`) starts from the candidate's decoded initial values, so checkpoint 0 has no error. The five Gray–Scott parameter sets of `tests/precision.c` and seeds 42 and 1234 give ten cases per candidate. The reported numbers are the B mean absolute error over all cells at 1,000 steps and its worst case.

```sh
sh scripts/precision-sweep.sh
```

Candidate names follow `tests/precision.c`: `q15` is a Q15 word, `near` nearest rounding, `fs` Floyd–Steinberg error diffusion, `i=q15` the version 1 arithmetic, `i=q24` the arithmetic of versions 2 and 3 with 24 fraction bits, and `i=q15x32c<n>` the version 4 candidates. `scripts/precision-configs.txt` lists the candidates of the sweep.

## Error diffusion

Nearest rounding without diffusion creates a dead zone: rates below half a code step never move a cell. Stochastic rounding is unbiased on average but injects independent noise of up to one code step into every cell at every step, and pattern position is a neutral direction of the dynamics, so this noise accumulates as a random walk of the pattern. Floyd–Steinberg error diffusion instead carries each cell's rounding error into the cells written next, which conserves the local average and moves the error to high spatial frequencies that diffusion removes within a few steps. At the Q15 step the nearest code with diffusion measured a smaller error than a dithered threshold, so the core rounds to the nearest code.

## Version 4 arithmetic

Version 3 computed every cell with 64-bit products, sums, and rounding, which the watch measured as the largest phase of a step. Three 32-bit candidates on the Q15 codes were added to the harness and swept over the five presets and seeds 42 and 1234 against version 3 (`i=q24`) and the version 1 arithmetic. C1 rounds the Laplacian and A × B to Q15 before the products. C2 keeps the exact 20-fold Laplacian sum, folds each diffusion coefficient with the 1/20 into `round(D / 20)`, and computes A × B² exactly to the Q30 unit with the product split at bit 15. C3 rounds the Laplacian like C1 and computes the reaction like C2. All three round the Q30 rate once to Q24 and encode as version 3 does. The C2 reference runs with the folded coefficients, as the Float32 comparison of the core does.

| Candidate                  | B MAE at 1,000 steps |   Worst |
| -------------------------- | -------------------: | ------: |
| version 1 (`r=near,i=q15`) |              0.00512 | 0.02943 |
| version 3 (`i=q24`)        |              0.00003 | 0.00005 |
| C1 (`i=q15x32c1`)          |              0.00009 | 0.00037 |
| C2 (`i=q15x32c2`)          |              0.00003 | 0.00006 |
| C3 (`i=q15x32c3`)          |              0.00008 | 0.00021 |

The per-step Q15 rounding of C1 and C3 costs three to six times the error of version 3, while C2 matches it. Over seeds 1 through 8 of feed 0.035 and kill 0.065 at 10,000 steps, every candidate reproduces the reference B mean within 0.1% with correlations above 0.9997, so none of them shifts the marginal case. C2 became numerical definition version 4. On the watch a step fell from 18.0 ms to 14.25 ms with the row loop of that time, and to 13.1 ms with the later three-pass loop.
