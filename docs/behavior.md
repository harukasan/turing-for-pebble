# Web and watchface behavior

## Web controls

The default implementation is the production candidate, Q15 16-bit storage at 100 × 114. The implementation selector also offers 200 × 228 with one packed 16-bit word per cell (A as a 7-bit linear code and B as a 9-bit square-root companded code, written with dithered error diffusion), Float32 at 200 × 228, and Float32 at 100 × 114. The same-resolution pairs isolate numerical and storage differences from grid resolution. Switching implementations frees the old Wasm allocation when applicable and initializes the selected mode with the same seed. Changing the seed or preset also resets the field. Switching does not retain or synchronize elapsed steps, so compare the same seed, parameters, and displayed step count. Feed, Kill, diffusion, and timestep controls apply to the current field on the next step.

The Float32 options run the original `Simulation.step` implementation. Their initial 24 disks are placed using the C core's display-coordinate LCG, periodic distance, and radius range. The Q15 mode has exactly the same initial concentrations. The packed mode starts from A = 64/127 instead of 0.5, while its B = 0.25 is exact. Interactive seeds also use display coordinates. Float32 computes with JavaScript number intermediates and stores each field in `Float32Array`. Its result is a reference for visual comparison, not a numerically identical Pebble build.

Existing pause, speed, 100-step advance, pointer seeding, central seed, palettes, display scaling, PNG, and JSON controls remain. Wasm and LECO loading failures produce a visible error and stop simulation. Selecting Float32 is explicit, never a silent fallback. Wasm linear memory is fixed and the rendering arrays are reused. The Float32 renderer writes into the same reusable `ImageData`.

The JSON output records requested and effective parameters, implementation, storage, rounding, seed, version, dimensions, palette, and step count. Float32 records its requested parameters as effective because Q15 conversion is not applied. It is a settings record, not a state snapshot. Parameter history and hand-painted seeds are not serialized. `config.h` output is enabled for Wasm modes and supplies the same effective parameters and seed for a Pebble build. On-watch RGB2 output is always quantized, even if the Web's smooth-color preview option is enabled. Smartphone settings are out of scope.

The new **細線** preset uses Feed 0.023 and Kill 0.052. With seed 42 at 10,000 steps, the 200 × 228 packed mode develops narrow curved bands. The 100 × 114 Q15 mode retains the structure with visibly wider bands. [The comparison image](../public/reports/thin-lines.png) shows these two outputs side by side at native display size, high resolution on the left. This is a deliberately difficult case for a coarse grid, not a guarantee of the same geometry across implementations.

## Clock

Both platforms use LECO_42_NUMBERS at y=78 and LECO_20_BOLD_NUMBERS at y=128, horizontally centered. The Web draws extracted 1-bit glyphs without antialiasing. The watch loads system fonts. White glyphs are drawn directly over the pattern without a background panel.

## Scheduling

Interactive Web mode renders up to 30 times per second. It checks elapsed compute time after each complete step and yields after approximately 8 ms. One complete step can exceed the budget.

Entering device behavior mode reinitializes the field and queues the same 1,800-step startup as the watch. Pending startup or minute work runs in consecutive slices of at most 120 ms on every animation frame, and the screen is painted at most every 200 ms and when the queue empties. It then advances 16 steps per wall-clock minute. A simulated backlight event enables animation for at most 5 seconds, up to 10 frames per second and 8 steps per frame. Blur, tab hiding, or simulated backlight-off stops that animation. Missed animation frames are not replayed.

The watch uses SDK BacklightService and AppFocusService. It does not subscribe to accelerometer data. Startup advances 1,800 steps in timer slices, which the physical Pebble Time 2 completes in about 30 s in mode 1. Minute updates add 16 pending steps. While work is pending, each callback runs steps until a 120 ms budget is used, checked after every complete step, then reschedules itself 1 ms later, and the layer is marked dirty at most every 200 ms and when the queue empties. Backlight animation runs at most 8 steps within 8 ms per 100 ms frame. Focus loss cancels timers. Focus restoration resumes pending work. Backlight-off cancels animation while pending startup or minute work remains eligible for timer execution. A separate five-second timer bounds each backlight animation window. The field is blitted into the framebuffer as ARGB8 rows from `rd_row_rgb2` within the unobstructed bounds, and the clock fonts are loaded once at startup.

Timing uses the public `time_ms` API. The emulator's wall-clock seconds and tick-derived millisecond component can be discontinuous. Negative or unexpectedly large deltas end the current slice and mark timing diagnostics invalid. Such values must not be interpreted as a valid benchmark or used to select a production mode. Real-device measurements are required for the 100 ms compute-plus-draw criterion. When the startup queue empties, the watch logs `RD startup` lines with the validated wall time, steps per second times 10, the average step, blit, and text times, the minimum free heap, and whether a focus loss or the clock invalidated the run. A build with `RD_BUILD_PROFILE=1` adds a clock calibration loop and a `RD prof` line every 256 steps.

## Memory reporting

The UI separates these categories:

1. Core allocation, obtained directly from C for Wasm modes, including fields, scratch rows, control data, the output row, and alignment. Float32 instead reports four JavaScript concentration arrays.
2. Pebble ELF code and static data, core dynamic allocation, and measured emulator minimum free heap. Runtime minimum free heap already includes allocations and must not be added to an estimate of remaining heap.
3. Web linear-memory allocation when Wasm is selected and known pixel buffers for both implementations. Core memory is contained inside linear memory. Canvas pixel-equivalent bytes are not a measurement of browser process memory or GPU allocations.

The acceptance thresholds are a 128 KiB app region and at least 16 KiB minimum free heap during normal operation. Compiler stack reports describe application functions. OS and library stack contributions are additional. The final default remains provisional until device validation completes.
