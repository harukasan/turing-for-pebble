/*
 * Build configuration of the watchface. The RD_DEFAULT_* values are the
 * settings the face shows until the phone sends its own (settings.h).
 * Everything else is fixed at build time.
 */
#define RD_SEED 42u
/* rd_row_rgb2 flags: 2 (RD_ROW_BILINEAR) interpolates B between cells, and
 * 0 shows the cell that covers each pixel. */
#ifndef RD_RENDER_FLAGS
#define RD_RENDER_FLAGS 2
#endif
/* Diagnostic logs (startup summary, progress): off in production, on with
 * RD_BUILD_LOG=1, RD_BUILD_PROFILE=1, or an RD_BENCH build. */
#ifndef RD_LOG
#if defined(RD_PROFILE) || defined(RD_BENCH) || defined(RD_FRAME_BENCH)
#define RD_LOG 1
#else
#define RD_LOG 0
#endif
#endif
/* The startup animation runs for RD_STARTUP_MS after launch or a new
 * pattern, or until RD_STARTUP_STEPS_MAX steps, about 45 s at the measured
 * 55 steps per second, whichever comes first. */
#ifndef RD_STARTUP_MS
#define RD_STARTUP_MS 30000
#endif
#define RD_STARTUP_STEPS_MAX 2500
/* Clock font sets: a build that defines RD_FONT (0 LECO, 1 Bitham, the
 * CM_FONT_* of core/clock_mask.h) with RD_BUILD_FONT compiles the glyphs of
 * that set only and ignores the font setting. Otherwise both are compiled. */
/* Clock faces: a build that defines RD_FACE (0 digital HH:MM, 1 analog
 * hands) with RD_BUILD_FACE compiles that face only and ignores the face
 * setting. Otherwise both are compiled. */
/* Duration of the sweep of the analog hands to a new minute. */
#define RD_SWEEP_MS 1000
/* Digit avoidance holds B at 0 in the cells under the clock digits, widened
 * by RD_HALO pixels, and raises the kill rate toward them so the pattern
 * fades out around the digits. */
#define RD_HALO 1
/* Pending steps after a minute change: with avoidance, enough to refill the
 * strokes freed by the previous digits. A settings change that moves the
 * digits or the mask also raises the pending steps to
 * RD_MINUTE_STEPS_AVOID. */
#define RD_MINUTE_STEPS_AVOID 300
#define RD_MINUTE_STEPS_PLAIN 16

/* Settings defaults: the model (RD_MODEL_* of core/rd.h) and its Q15
 * parameter vector, 0 Gray-Scott (feed, kill, da, db, dt) or 1
 * FitzHugh-Nagumo (du, dv, ru, rv, av, k, dt, rest, init), then the
 * palette (RD_PALETTE_*), the dark and light stops of the custom palette as
 * 0xRRGGBB, its number of stops and the middle stops at a third and two
 * thirds of the way from dark to light, the clock font set, digit
 * avoidance, the clock, the date line, and the clock face. The build
 * includes both models unless RD_MODEL (RD_BUILD_MODEL) folds the core to
 * one, and the settings accept only the models of the build. */
/* A build folded to FitzHugh-Nagumo starts with the fhn-stripes preset of
 * lib/presets.ts, and every other build with the Gray-Scott maze. */
#ifndef RD_DEFAULT_MODEL
#if defined(RD_MODEL) && RD_MODEL == 1
#define RD_DEFAULT_MODEL 1
#else
#define RD_DEFAULT_MODEL 0
#endif
#endif
#ifndef RD_DEFAULT_PARAMS
#if RD_DEFAULT_MODEL == 1
#define RD_DEFAULT_PARAMS 1638, 32768, 492, 1229, 19661, 0, 32768, 0, 0
#else
#define RD_DEFAULT_PARAMS 950, 1868, 22938, 11469, 32768
#endif
#endif
#if defined(RD_MODEL) && RD_MODEL != RD_DEFAULT_MODEL
#error "RD_DEFAULT_MODEL must be the model the build is folded to (RD_MODEL)"
#endif
#define RD_DEFAULT_PALETTE 0
#define RD_DEFAULT_LOW 0x001e12
#define RD_DEFAULT_HIGH 0xd2ff55
#define RD_DEFAULT_STOPS 2
#define RD_DEFAULT_MID1 0x466928
#define RD_DEFAULT_MID2 0x8cb43f
#define RD_DEFAULT_FONT 0
#define RD_DEFAULT_AVOID 1
#define RD_DEFAULT_CLOCK 1
#define RD_DEFAULT_DATE 1
#define RD_DEFAULT_FACE 0
