#ifndef RD_MODE
#define RD_MODE 3
#endif
#define RD_SEED 42u
#define RD_FEED 950
#define RD_KILL 1868
#define RD_DA 32768
#define RD_DB 16384
#define RD_DT 32768
#define RD_PALETTE 0
/* rd_row_rgb2 flags: 2 (RD_ROW_BILINEAR) interpolates B between cells,
 * the default for grids that are not the display or half of it. */
#ifndef RD_RENDER_FLAGS
#define RD_RENDER_FLAGS (RD_MODE >= 3 ? 2 : 0)
#endif
#define RD_CLOCK 1
/* Diagnostic logs (startup summary, progress): off in production, on with
 * RD_BUILD_LOG=1, RD_BUILD_PROFILE=1, or an RD_BENCH build. */
#ifndef RD_LOG
#if defined(RD_PROFILE) || defined(RD_BENCH) || defined(RD_FRAME_BENCH)
#define RD_LOG 1
#else
#define RD_LOG 0
#endif
#endif
/* Steps run at startup before the face settles to minute updates. */
#ifndef RD_STARTUP_STEPS
#define RD_STARTUP_STEPS 1250
#endif
/* Clock font set: 0 LECO, 1 Bitham (CM_FONT_* in core/clock_mask.h). */
#ifndef RD_FONT
#define RD_FONT 0
#endif
/* Hold B at 0 in the cells under the clock digits, widened by RD_HALO
 * pixels, and raise the kill rate toward them so the pattern fades out
 * around the digits. Off in mode 0, where a minute
 * burst would take about 24 s. */
#ifndef RD_AVOID
#define RD_AVOID (RD_MODE != 0)
#endif
#define RD_HALO 1
/* Pending steps after a minute change: with avoidance, enough to refill the
 * strokes freed by the previous digits. */
#define RD_MINUTE_STEPS (RD_AVOID ? 300 : 16)
