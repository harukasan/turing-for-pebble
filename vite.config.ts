import { defineConfig } from "vite";

export default defineConfig({
  base: process.env.TURING_BASE_URL || "/",
  build: { outDir: "dist" },
});
