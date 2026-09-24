# Web and watchface behavior

## Web controls

The default comparison candidate is 200 × 228 / 8-bit stochastic storage. Switching to 100 × 114 / 16-bit storage frees the old Wasm allocation, allocates the queried size, and initializes the selected mode. Changing the seed or preset also resets the field. Feed, Kill, diffusion, and timestep controls apply to the current field on the next step.

Existing pause, speed, 100-step advance, pointer seeding, central seed, palettes, display scaling, PNG, and JSON controls remain. Wasm and LECO loading failures produce a visible error and stop simulation. There is no silent Float32 fallback. Linear memory is fixed and the rendering arrays are reused.

The JSON output records requested and effective Q15 parameters, mode, rounding, seed, core version, dimensions, palette, and step count. It is a settings record, not a state snapshot. Parameter history and hand-painted seeds are not serialized. `config.h` output supplies the same effective parameters and seed for a Pebble build. On-watch RGB2 output is always quantized, even if the Web's smooth-color preview option is enabled. Smartphone settings are out of scope.

## Clock

Both platforms use LECO_42_NUMBERS at y=78 and LECO_20_BOLD_NUMBERS at y=128, horizontally centered. The Web draws extracted 1-bit glyphs without antialiasing. The watch loads system fonts. White glyphs are drawn directly over the pattern without a background panel.

## Scheduling

Interactive Web mode renders up to 30 times per second. It checks elapsed compute time after each complete step and yields after approximately 8 ms. One complete step can exceed the budget.

Entering device behavior mode reinitializes the field and queues the same 2,000-step startup as the watch. It then advances 16 steps per wall-clock minute. A simulated backlight event enables animation for at most 5 seconds, up to 10 frames per second and 8 steps per frame. Blur, tab hiding, or simulated backlight-off stops that animation. Missed animation frames are not replayed.

The watch uses SDK BacklightService and AppFocusService. It does not subscribe to accelerometer data. Startup advances 2,000 steps in timer slices. Minute updates add 16 pending steps. Each callback runs at most 8 steps and checks the 8 ms budget after every complete step. Focus loss cancels timers. Focus restoration resumes pending work. Backlight-off cancels animation while pending startup or minute work remains eligible for timer execution. A separate five-second timer bounds each backlight animation window.

Timing uses the public `time_ms` API. The emulator's wall-clock seconds and tick-derived millisecond component can be discontinuous. Negative or unexpectedly large deltas end the current slice and mark timing diagnostics invalid. Such values must not be interpreted as a valid benchmark or used to select a production mode. Real-device measurements are required for the 100 ms compute-plus-draw criterion.

## Memory reporting

The UI separates these categories:

1. Core allocation, obtained directly from C, including fields, scratch rows, control data, the output row, and alignment.
2. Pebble ELF code and static data, core dynamic allocation, and measured emulator minimum free heap. Runtime minimum free heap already includes allocations and must not be added to an estimate of remaining heap.
3. Web linear-memory allocation and known pixel buffers. Core memory is contained inside linear memory. Canvas pixel-equivalent bytes are not a measurement of browser process memory or GPU allocations.

The acceptance thresholds are a 128 KiB app region and at least 16 KiB minimum free heap during normal operation. Compiler stack reports describe application functions. OS and library stack contributions are additional. The final default remains provisional until device validation completes.
