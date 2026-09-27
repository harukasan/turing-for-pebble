/*! fflate (https://github.com/101arrowz/fflate), Copyright (c) 2026 Arjun
 * Barrett, MIT License. See licenses/fflate.txt. */
import { gunzipSync } from "fflate";

/** A data: URL of the given type for gzipped, base64-encoded bytes, as
 * vite.settings.config.ts embeds them. fflate unpacks them in script,
 * because the web views of iOS before 16.4 have no DecompressionStream,
 * and the data: URL keeps the loaders of lib/ unchanged. */
export function gzipDataUrl(packed: string, type: string) {
  const bytes = gunzipSync(
    Uint8Array.from(atob(packed), (c) => c.charCodeAt(0))
  );
  let binary = "";
  for (let i = 0; i < bytes.length; i += 0x8000)
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return `data:${type};base64,${btoa(binary)}`;
}
