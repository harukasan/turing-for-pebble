import { readFileSync } from "node:fs";
import { gzipSync } from "node:zlib";
import { defineConfig, type Plugin } from "vite";

/** Imports ending in `?gzip` become the file gzipped and base64 encoded, a
 * string that src/settings/gzip.ts turns back into a data: URL. The Wasm
 * core and the clock glyphs shrink to less than half. */
function gzipAssets(): Plugin {
  // A virtual id with a suffix of its own, which the JSON and asset
  // plugins leave alone.
  const PREFIX = "\0gzip:";
  const SUFFIX = ".gz.js";
  return {
    name: "gzip-assets",
    enforce: "pre",
    async resolveId(source, importer) {
      if (!source.endsWith("?gzip")) return null;
      const resolved = await this.resolve(source.slice(0, -5), importer);
      return resolved && PREFIX + resolved.id + SUFFIX;
    },
    load(id) {
      if (!id.startsWith(PREFIX)) return null;
      const file = id.slice(PREFIX.length, -SUFFIX.length);
      this.addWatchFile(file);
      const packed = gzipSync(readFileSync(file), { level: 9 });
      return `export default ${JSON.stringify(packed.toString("base64"))};`;
    },
  };
}

/** The watch settings page: one HTML page whose script, style, and gzipped
 * Wasm core and clock glyphs scripts/embed-config.mjs inlines into a single
 * file for the phone. */
export default defineConfig({
  root: "src/settings",
  base: "./",
  publicDir: false,
  css: { postcss: {} },
  plugins: [gzipAssets()],
  build: {
    outDir: "../../build/config",
    emptyOutDir: true,
    assetsInlineLimit: () => true,
    modulePreload: false,
    rollupOptions: { output: { inlineDynamicImports: true } },
  },
  server: { port: 5174 },
});
