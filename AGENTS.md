# Agent guide

This repository is a local Gray–Scott reaction-diffusion experiment for Pebble Time 2. Read this file before making changes. Detailed project documentation belongs in `docs/` and must be written in English. Keep this guide short and update it when workflows or invariants change.

## Working rules

- Do not push commits or create, edit, comment on, or merge pull requests without the user's explicit approval for that specific action. Local commits are allowed when the user requests them. Do not add a `Codex-Session:` commit trailer. Keep `Co-Authored-By` only when co-authoring a commit.
- Do not use semicolons in English or Japanese prose. Semicolons in code are fine.
- Preserve the local-only scope. The watch settings page is embedded in the phone script and needs no hosting. Do not deploy the Web demo or host the settings page unless the user explicitly changes that scope.
- Before changing generated files, identify their source and regeneration command. Keep reports tied to the exact build and observation that produced them.
- Do not treat emulator timing or heap observations as physical-device acceptance evidence. The measured acceptance status is in [docs/validation.md](docs/validation.md).

## Project map

| Path                                           | Purpose                                                   |
| ---------------------------------------------- | --------------------------------------------------------- |
| `core/rd.c`, `core/rd.h`                       | Shared fixed-point C core and public API                  |
| `pebble/src/c/`                                | Emery watchface adapter and build configuration           |
| `public/wasm/rd.wasm`                          | Generated WebAssembly binary from the shared core         |
| `lib/wasm-simulation.ts`                       | Web adapter for the C core                                |
| `lib/simulation.ts`, `lib/float-simulation.ts` | Float32 reference and Web comparison adapter              |
| `lib/palettes.ts`, `core/palettes.h`           | Display palettes and the generated core table             |
| `licenses/`                                    | Origins and licenses of third-party palette data          |
| `src/core/`                                    | Reusable Canvas, Wasm, and playback API                   |
| `src/demo/`                                    | React demo UI, Panda styles, and downloads                |
| `src/settings/`                                | Watch settings page with the Wasm preview                 |
| `pebble/src/pkjs/`                             | Phone script, generated with the embedded page            |
| `tests/`, `scripts/`                           | Core, adapter, numerical comparison, and build checks     |
| `public/reports/`                              | Generated images and build or emulator measurements       |
| `docs/`                                        | Detailed design, setup, validation, and numerical results |

The Web offers 200 × 228 packed 16-bit Wasm (A 7-bit, B 9-bit), 120 × 136 Q15 Wasm (mode 3, the watch build, shown with interpolated rendering), 100 × 114 Q15 Wasm, and Float32 at each of these grid sizes. Mode 2 in the C core is a diagnostic 100 × 114 configuration with the packed cells of mode 0. Mode switching resets to the same seed. The Float32 implementation is a reference, not a Pebble build. The `細線` preset is intended to expose narrow-band differences. See [docs/behavior.md](docs/behavior.md) and [docs/float-precision.md](docs/float-precision.md).

The standalone Web demo uses React, Vite, and Panda CSS. pnpm 10.33.0 is pinned in `mise.toml` and `package.json`. Regenerate the ignored `styled-system/` directory with `pnpm run typecheck` or `pnpm run build`. The reusable Web API, the demo, and the settings page are in [docs/web.md](docs/web.md), and the watch settings in [docs/behavior.md](docs/behavior.md#watch-settings). `pnpm run build:settings` builds the settings page and the phone script, and `mise run build-pebble` and `mise run test-core` run it. Web TypeScript, JavaScript, CSS, and Markdown are formatted with Prettier. C, Python, and shell keep their existing formatters.

## Environment and checks

Use the project-local mise setup in [docs/development.md](docs/development.md). `mise.toml` pins Node, Python, lint tools, and project-local Pebble state. The Pebble SDK version is 4.33.1. Emscripten and the ARM compiler come from the existing environment.

```sh
mise trust
mise install
mise run setup
mise run sdk-install
pnpm install --frozen-lockfile
mise run build-wasm
pnpm run typecheck
pnpm test
mise run test-core
mise run lint
pnpm run build
mise run build-pebble
```

Run the checks relevant to each change. `mise run test-core` includes AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer, native-versus-Wasm checks, and the TypeScript adapters. LeakSanitizer may fail under a ptrace-based sandbox even when the core assertions pass. Record that limitation rather than reporting a full pass. The Web build is `pnpm run build`. A local preview is `pnpm run dev --host 127.0.0.1`.

When changing core arithmetic or storage, keep the C API's memory query accurate, preserve invalid-input no-mutation behavior, and verify the row-buffer algorithm against the full-screen oracle. Rebuild `public/wasm/rd.wasm`, run native-versus-Wasm hash checks, and rebuild the Pebble watchfaces (modes 0, 1, and 3) if their core changed. Update the Web method selector and memory display if a new mode is exposed. Re-run same-resolution Float32 comparisons and the thin-line case. Record code size, dynamic allocation, minimum free heap, and timing with clear measured versus estimated labels. The numerical contract is in [docs/core.md](docs/core.md).

The watchface acceptance targets are the 128 KiB app region, at least 16 KiB minimum free heap in normal operation, compute slices of at most 30 ms with redraws about every 50 ms during animation, and a startup animation of 30 s. Physical timing and heap are measured in [docs/validation.md](docs/validation.md) with builds made with `RD_BUILD_LOG=1`. Do not claim a mode is production-ready from host or emulator results alone.
