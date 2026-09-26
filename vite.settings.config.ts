import { defineConfig } from "vite";

/** The watch settings page: one HTML page whose script, style, Wasm core,
 * and clock glyphs scripts/embed-config.mjs inlines into a single file for
 * the phone. */
export default defineConfig({
  root: "src/settings",
  base: "./",
  publicDir: false,
  css: { postcss: {} },
  build: {
    outDir: "../../build/config",
    emptyOutDir: true,
    assetsInlineLimit: () => true,
    modulePreload: false,
    rollupOptions: { output: { inlineDynamicImports: true } },
  },
  server: { port: 5174 },
});
