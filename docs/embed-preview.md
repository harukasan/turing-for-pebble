# Embed only the preview

`TuringPlayer` renders the watchface preview into a Canvas without the React demo, controls, or Panda CSS. It is the existing integration point for another browser page. The repository does not provide a published package or a preview-only iframe page, so the host site must bundle the TypeScript source and serve the runtime assets itself. The Web demo remains local only.

## Add the source and assets to the host site

For a Vite and TypeScript site, copy `src/core/` and these files into the same relative layout under the host project's root:

```text
src/core/index.ts
src/core/modes.ts
src/core/player.ts
lib/clock-fonts.ts
lib/float-simulation.ts
lib/simulation.ts
lib/wasm-simulation.ts
```

The core imports the `lib/` files through `../../lib/`, so keep that relative layout or adjust the imports. The host build must support TypeScript imports ending in `.ts`, as Vite does. No React or Panda CSS dependency is needed for this preview.

Copy these runtime assets into the host site's public asset directory:

```text
public/turing/wasm/rd.wasm
public/turing/fonts/clock-fonts.json
public/turing/fonts/LICENSE
```

`public/wasm/rd.wasm` is generated from the shared C core by `mise run build-wasm`. `public/fonts/clock-fonts.json` is generated as described in [development.md](development.md). Copy the existing assets unless changing their sources. Preserve the font license alongside the font data.

## Mount the Canvas

In the host page, provide a Canvas with a 200 × 228 drawing buffer:

```html
<canvas
  id="turing-preview"
  width="200"
  height="228"
  role="img"
  aria-label="Turing pattern watchface preview"
></canvas>
```

```css
#turing-preview {
  display: block;
  width: 200px;
  max-width: 100%;
  height: auto;
  image-rendering: pixelated;
}
```

Initialize it from the host site's TypeScript entry point:

```ts
import { TuringPlayer, defaultPlayerSettings } from "./core";

const canvas = document.querySelector<HTMLCanvasElement>("#turing-preview");
if (!canvas) throw new Error("Preview canvas is missing");

const player = new TuringPlayer(canvas, "/turing/", defaultPlayerSettings, {
  onStats() {},
  onLoading() {},
  onError(error) {
    console.error("Turing preview failed", error);
  },
});

await player.load("q15-120", 42);
if (!matchMedia("(prefers-reduced-motion: reduce)").matches) player.play();

// Call this when the host page removes the preview.
export function destroyTuringPreview() {
  player.dispose();
}
```

The asset base URL is resolved against `document.baseURI`. It must point to a directory containing `wasm/rd.wasm` and `fonts/clock-fonts.json`. For a host site under a URL prefix, include that prefix in the asset base URL. Keep the assets on the host site's origin or configure cross-origin access for both files.

`load(engine, seed)` resets the simulation. `play()` and `pause()` control animation. `setSettings()` changes rendering and simulation settings without resetting the field. `seed(x, y)` adds a seed at normalized Canvas coordinates in `[0, 1)`. Call `dispose()` when the view is removed to stop the animation loop and release the simulation. See [web.md](web.md) for the full API example.
