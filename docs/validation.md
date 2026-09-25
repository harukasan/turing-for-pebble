# Validation and adoption status

The implementation is available for local Web use and Emery builds. Production adoption is **pending**, because no physical Pebble Time 2 is connected and interactive Web browser verification is unavailable in this environment.

## Completed checks

- Native C row-buffer updates match a full-screen double-buffer oracle for all three modes.
- Native and Wasm field hashes match at steps 0, 1, and 100 with seed 42, including dithered error diffusion.
- Equilibrium, periodic seeding, coefficient endpoints, rounding ties, the integer square root over its whole domain, floor codes, the dithered threshold range, error diffusion mass conservation, invalid arguments, undersized allocation, alignment, and sentinel boundaries are tested.
- AddressSanitizer, UndefinedBehaviorSanitizer, and LeakSanitizer passed. LeakSanitizer requires execution outside this environment's ptrace-based sandbox.
- 300 allocation/reset cycles retain fixed Wasm linear memory. The TypeScript adapter is tested with real Wasm for loading, mode recreation, parameter conversion, stepping, seeding, RGBA output, and disposal.
- The original Float32 reference tests passed.
- TypeScript checking and the production Web build passed. The repository-wide `mise run lint` passes. Vendored `components/ui/` is excluded from oxlint, as described in `docs/development.md`.
- Wasm was served with HTTP 200 and `application/wasm`.
- Both Emery `.pbw` variants were built and installed in the emulator. Mode 0 was also observed after a back-button backlight animation and subsequent minute updates without a lower minimum free heap.
- The Emery screenshot's 2,427 white clock/date pixels exactly match the extracted PBF glyph positions and bitmap pixels. There are zero missing or extra white pixels. This verifies font geometry against the emulator, not browser Canvas interaction.
- With digit avoidance (B held at 0 under the digits, graded kill around them) and a 1-pixel halo, mode 1 emulator screenshots matched both font sets exactly: LECO at 13:31 2026.09.25 (2,358 glyph pixels) and Bitham at 13:32 2026.09.25 (3,453 glyph pixels). Every halo pixel was black. A Bitham screenshot 40 s after the change to 13:33 showed no trace of the previous digit. `tests/adapter.mjs` checks that the core's compiled glyphs equal the JSON glyphs for both fonts. The minute burst timing on the physical watch and its battery effect are not measured yet.

Machine-readable results and comparison images are in `public/reports/`. These are also linked from the local Web UI. Source for all numerical checks is under `tests/`.

## Pattern comparison

For the cell-by-cell comparison with the original Float32 implementation, see [Float32 precision comparison](float-precision.md). The fixed-point implementation is not numerically identical to the Float32 reference. Numerical definition version 3 (packed 7-bit A and 9-bit square-root B codes, Floyd–Steinberg error diffusion with a dithered threshold for the packed codes, and Q24 arithmetic) reduced the mode 0 error at 1,000 steps by about 7 times and the mode 1 error by more than 100 times. [Storage precision study](precision-optimization.md) records the candidates that were measured.

The Web now exposes Float32 reference runs at both grid resolutions alongside the two production Wasm modes. The adapter test checks that its initial disk occupancy matches the C core in both grids and that Q15 initial values match exactly on the 100 × 114 grid. Its step, seed, and RGB2 rendering paths are also exercised. The UI switches by resetting to the same seed and preserves the original controls.

The [thin-line comparison image](../public/reports/thin-lines.png) records seed 42, Feed 0.023, Kill 0.052, Da 1, Db 0.5, dt 1, and 10,000 steps. The left half is 200 × 228 / packed and the right half is 100 × 114 / Q15, both displayed at 200 × 228. The high-resolution result has narrow curved bands while the low-resolution result has wider bands. The separate [Float32 precision report](float-precision.md) quantifies this preset at 1,000 steps.

Each of five presets was run for 10,000 steps with seeds 42 and 1234 in modes 0, 1, and 2. All 30 runs retain spatial variance and changing concentration cells at the end of the run. None was fully frozen or uniform at that checkpoint. This is a bounded observation, not a guarantee for every setting or an arbitrarily long run.

The 200 × 228 / packed version has finer pattern geometry. The 100 × 114 / Q15 version has larger visible structures due to its grid scale. The diagnostic 100 × 114 / packed images isolate storage precision at the same resolution.

At step 10,000, a further single step changes 0.11–0.17% of displayed pixels for mode 0 in the four baseline presets and 0.5–0.6% in the thin-line preset, compared with 0.8–1.4% for the version 1 8-bit candidates. Mode 1 changes 0–0.04% and 0.4–0.6% respectively, because its thin-line pattern keeps evolving instead of freezing as the version 1 Q15 rounding made it do. This is a one-step RGB2 pixel-change statistic, not a perceptual flicker score. On-watch animation and visibility still need assessment.

![Seed 42 comparison, columns: high-resolution u8, low-resolution Q15, diagnostic low-resolution u8](../public/reports/comparison.png)

## Memory

Core allocation is 100,567 B for mode 0 and 50,967 B for mode 1. These include decoded scratch rows, error diffusion residual rows, the rendering row, the display lookup table, control state, and alignment. Concentration planes plus scratch rows alone occupy 96.875 KiB and 48.438 KiB respectively. Version 2 added 6,920 B to mode 0 and 2,920 B to mode 1 for the decoded 32-bit rows, the residual rows, and the display lookup table. The watchface is now compiled with `-O3`, which grew its load size from 4,705 B to about 10.5 KB (mode 0) and 11.3 KB (mode 1); `scripts/build-pebble.sh` fails above 12,288 B.

The current `public/reports/comparison.json` records measured ELF text/data/BSS and compiler stack reports. Application RAM estimates use the 128 KiB app region and subtract both the load footprint and the core allocation. They do not include additional OS allocations.

Observed emulator heap results are recorded with their build/observation scope. Version 1 mode 0 showed 32,888 B after startup and two subsequent minute updates. Version 1 mode 1 showed 78,688 B after startup and a subsequent minute update. The version 2 allocation and the larger code are together about 11.2 KB (mode 0) and 6.7 KB (mode 1) more than those binaries had, so the expected corresponding values are about 21.7 KiB and 72 KiB before remeasurement. The observation metadata is retained in `docs/emulator-measurements.json`. These exceed 16 KiB in the observed emulator intervals, but they are not a substitute for a sustained normal-operation and backlight workload on hardware. Use the current report for subsequent measurements.

The two `.pbw` files were rebuilt for numerical definition version 2. Their current ELF section sizes are refreshed in `public/reports/comparison.json`. The heap observations above belong to the earlier version 1 binaries listed in `docs/emulator-measurements.json` and must be remeasured on the final binaries before an acceptance decision.

Compiler `.su` files report bounded, static frames and no application recursion. The report includes their sum as a deliberately conservative application-only stack bound. This is not a full stack high-water measurement. The OS and library call paths contribute additional stack usage.

## Physical Pebble Time 2 measurements

The first physical measurements were taken on 2026-09-25 through the phone's developer connection, with the scheduling, drawing, and bit-exact core changes described in [Shared C core](core.md). `docs/hardware-measurements.json` holds the raw entries and `scripts/build-report.py` copies them into `public/reports/comparison.json`.

| Mode                        | Steps per second |    Step | Blit per frame | Text per frame | Minimum free heap | 2,000 startup steps |
| --------------------------- | ---------------: | ------: | -------------: | -------------: | ----------------: | ------------------: |
| 0                           |             11.4 |   81 ms |         3.2 ms |         2.8 ms |          21,104 B |         about 175 s |
| 1                           |             56.2 | 16.4 ms |         3.3 ms |         2.0 ms |          68,376 B |          about 36 s |
| 1, version 3                |             58.3 | 15.2 ms |         2.9 ms |         3.1 ms |          68,680 B |          about 34 s |
| 1, production (1,800 steps) |             63.0 | 15.1 ms |         3.5 ms |         3.0 ms |          69,176 B |     28.8 s measured |

The hardware clock was valid throughout (`clock_invalid=0`). The 1,000,000-iteration calibration loop of the profile build took 46–47 ms. A 256-step log interval reproduced the mode 0 rate as 256 steps per 22.5 s. Neither mode reached the 30 s target for 2,000 steps with bit-exact code: mode 1 needed about 18% more speed and mode 0 about six times more. Version 3 (nearest rounding for the Q15 codes, no per-cell hash in mode 1) raised mode 1 to 58.3 steps/s at 15.2 ms per step, and the startup was set to 1,800 steps with 120 ms slices and one redraw per 200 ms, which brings mode 1's startup to a measured 28.8 s for 1,816 steps (1,800 plus one minute tick) at 63.0 steps/s.

## Timing limitation

The emulator RTC implementation in the inspected official PebbleOS source (`src/fw/drivers/qemu/qemu_rtc_hal.c`) obtains whole seconds and fractional milliseconds from different clock sources. The observed combined timestamp can move backwards across a tick wrap. Diagnostics now abort that compute slice, set `clock_invalid=1`, and report a 1,000 ms sentinel. Once invalid, maximum timing fields cannot be used as performance measurements. The sentinel is not a measured 1,000 ms step.

Backlight windows use a separate five-second AppTimer, so they do not depend on this wall-clock combination. Compute time is checked only after a complete step. A slow single step can exceed the nominal 8 ms slice budget, which makes physical-device timing an essential remaining check.

## Remaining acceptance work

- Verify Web mode switching, repeated initialization, pause/advance behavior, simulated backlight/focus behavior, PNG export, and configuration downloads interactively. No connected browser is available to this agent.
- Observe real-device backlight-on/off and focus loss, without an accelerometer subscription.
- Measure minimum free heap throughout startup, minute updates, and backlight animation. Require at least 16 KiB during normal operation.
- Measure compute and draw times on a physical Emery watch with `PEBBLE_PHONE=<ip> npm run device` and `device:mode1`, record the `RD startup` line in `docs/hardware-measurements.json`, and rerun `scripts/build-report.py`. Require 2,000 startup steps within 30 s and combined animation compute and drawing within 100 ms.
- Validate full stack headroom including OS and library contributions.
- Compare perceived flicker, legibility, and pattern quality on the physical display.

Mode 1 is the production build: it is the only candidate that completes its startup in about 30 s on the physical watch, so `pebble/src/c/config.h`, the build default, and the Web's initial selection use it. Mode 0 remains available as the high-resolution comparison build.

## Emulator rendering after startup

![Mode 0 after startup, with transparent-background LECO clock](../public/reports/emery-mode-0-grown.png)
