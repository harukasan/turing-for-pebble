import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { gzipSync } from "node:zlib";
import { gzipDataUrl } from "./gzip.ts";

test("gzipped assets come back byte for byte as data: URLs", () => {
  for (const [path, type] of [
    ["../../public/wasm/rd.wasm", "application/wasm"],
    ["../../public/fonts/clock-fonts.json", "application/json"],
  ]) {
    const bytes = readFileSync(new URL(path, import.meta.url));
    const packed = gzipSync(bytes, { level: 9 }).toString("base64");
    const url = gzipDataUrl(packed, type);
    const prefix = `data:${type};base64,`;
    assert(url.startsWith(prefix));
    assert.deepEqual(Buffer.from(url.slice(prefix.length), "base64"), bytes);
  }
});
