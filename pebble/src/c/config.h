#ifndef RD_MODE
#define RD_MODE 1
#endif
#define RD_SEED 42u
#define RD_FEED 950
#define RD_KILL 1868
#define RD_DA 32768
#define RD_DB 16384
#define RD_DT 32768
#define RD_PALETTE 0
#define RD_CLOCK 1
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
