/** The analog clock face of the core (core/clock_mask.c): hands at angles
 * in 1/TURN of a turn clockwise from 12 o'clock, a center disk, and the
 * date, drawn from the same bitmap that masks the field. */
import { buildAnalogMask, type CoreAPI } from "./wasm-simulation.ts";

/** CM_TURN of core/clock_mask.h. */
export const TURN = 1440;
/** Angle of the hour hand at an hour 0-23 and minute (cm_hour_angle). */
export const hourAngle = (hour: number, minute: number) =>
  (hour % 12) * (TURN / 12) + minute * (TURN / 720);
/** Angle of the minute hand at a minute (cm_minute_angle). */
export const minuteAngle = (minute: number) => minute * (TURN / 60);
/** A hand eased from one angle to another the shorter way round, elapsedMs
 * into a sweep of durationMs (cm_sweep_angle). */
export function sweepAngle(
  api: CoreAPI,
  from: number,
  to: number,
  elapsedMs: number,
  durationMs: number
) {
  const angle = api.cm_sweep_angle(from, to, Math.round(elapsedMs), durationMs);
  if (angle < 0) throw new Error("Invalid sweep");
  return angle;
}
/** The display pixels of the analog face as a 200 × 228 bitmap in rows of
 * 25 bytes, the halo 0 mask of the display grid, the date line only with
 * showDate. */
export const analogPixels = (
  api: CoreAPI,
  font: number,
  hour: number,
  minute: number,
  date: Date,
  showDate = true
) => buildAnalogMask(api, 200, 228, font, hour, minute, date, 0, showDate);
/** Draw the set pixels of a face bitmap in white, one run per row span. */
export function drawAnalog(ctx: CanvasRenderingContext2D, bitmap: Uint8Array) {
  ctx.fillStyle = "#fff";
  for (let y = 0; y < 228; y++)
    for (let x = 0; x < 200; x++) {
      if (!((bitmap[y * 25 + (x >> 3)] >> (x & 7)) & 1)) continue;
      const start = x;
      while (
        x + 1 < 200 &&
        (bitmap[y * 25 + ((x + 1) >> 3)] >> ((x + 1) & 7)) & 1
      )
        x++;
      ctx.fillRect(start, y, x - start + 1, 1);
    }
}
/** Today at a minute of the day. */
export function dateAtMinutes(minutes: number) {
  const date = new Date();
  date.setHours(Math.floor(minutes / 60), minutes % 60, 0, 0);
  return date;
}
