/*
 * Build configuration of the watchface. The RD_DEFAULT_* values are the
 * settings the face shows until the phone sends its own (settings.h).
 * Everything else is fixed at build time.
 */
#ifndef RD_MODE
#define RD_MODE 3
#endif
#define RD_SEED 42u
/* rd_row_rgb2 flags: 2 (RD_ROW_BILINEAR) interpolates B between cells,
 * the default for grids that are not the display or half of it. */
#ifndef RD_RENDER_FLAGS
#define RD_RENDER_FLAGS (RD_MODE >= 3 ? 2 : 0)
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
/* Digit avoidance holds B at 0 in the cells under the clock digits, widened
 * by RD_HALO pixels, and raises the kill rate toward them so the pattern
 * fades out around the digits. RD_AVOID 0 leaves the mask area out of the
 * core and ignores the avoidance setting, as in mode 0, where a minute
 * burst would take about 24 s. */
#ifndef RD_AVOID
#define RD_AVOID (RD_MODE != 0)
#endif
#define RD_HALO 1
/* Pending steps after a minute change: with avoidance, enough to refill the
 * strokes freed by the previous digits. A settings change that moves the
 * digits or the mask also raises the pending steps to
 * RD_MINUTE_STEPS_AVOID. */
#define RD_MINUTE_STEPS_AVOID 300
#define RD_MINUTE_STEPS_PLAIN 16

/* Settings defaults: the Q15 feed, kill, diffusion rates of A and B, and
 * time step, the palette (RD_PALETTE_*), the dark and light stops of the
 * custom palette as 0xRRGGBB, the clock font set, digit avoidance, and the
 * clock. */
#define RD_DEFAULT_FEED 950
#define RD_DEFAULT_KILL 1868
#define RD_DEFAULT_DA 22938
#define RD_DEFAULT_DB 11469
#define RD_DEFAULT_DT 32768
#define RD_DEFAULT_PALETTE 0
#define RD_DEFAULT_LOW 0x001e12
#define RD_DEFAULT_HIGH 0xd2ff55
#define RD_DEFAULT_FONT 0
#define RD_DEFAULT_AVOID 1
#define RD_DEFAULT_CLOCK 1
