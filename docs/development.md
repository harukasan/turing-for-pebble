# Development and builds

## Project-local environment

`mise.toml` pins Python 3.13.15 and Node 24.19.0. `requirements-tools.lock` pins the Python environment, including pebble-tool 5.0.40. The SDK installation task installs Pebble SDK 4.33.1, which includes Emery and BacklightService.

```sh
mise trust
mise install
mise run setup
mise run sdk-install
mise exec -- pebble sdk list
npm ci
```

| Location                   | Content                                         |
| -------------------------- | ----------------------------------------------- |
| `.venv/`                   | Python virtual environment managed through mise |
| `.local/share/pebble-sdk/` | SDKs, emulator flash, tooling state             |
| `.local/cache/`            | Project-local caches                            |
| `requirements-tools.lock`  | Resolved Python dependencies                    |
| `.tools/`                  | Earlier bootstrap environment, no longer used   |

The project allowlists only Python, Node, ruff, shellcheck, and shfmt in mise, preventing unrelated globally configured tools from being installed for project tasks. Mise trusted-config registration remains in its user state directory. Runtime installations follow mise's data directory, including the active XDG environment. Existing runtime installations can be reused. This arrangement isolates Python dependencies and Pebble state. It is not a container or an operating-system sandbox. Pebble's legacy `~/.pebble-sdk` path takes precedence over `XDG_DATA_HOME` if it exists. Verify the resolved path on another machine before installing:

```sh
mise exec -- python -c 'from pebble_tool.util import get_persist_dir; print(get_persist_dir())'
```

The installed SDK also creates its own Python build environment and downloads an ARM toolchain. The SDK version is fixed, but its independently installed dependency environment is not fully locked by `requirements-tools.lock`.

## WebAssembly and Web

The build uses the existing Emscripten installation on `PATH`. Its cache must be writable. The generated Wasm has 1 MiB of fixed linear memory and a 64 KiB stack. It is served as a static asset and does not use Emscripten's generated JavaScript glue.

```sh
mise run build-wasm
npm run typecheck
npm test
mise run test-core
npm run build
npm run dev -- --host 127.0.0.1
```

The local URL is normally `http://localhost:3000/`. No deployment is required. The development server is not an on-watch performance benchmark.

## Lint and format

`mise run lint` runs every check and fails on any lint error or unformatted file. `mise run format` rewrites all sources with the project formatters. Both aggregate the per-language tasks below, which can also be run alone.

| Task                    | Tool                                                 | Scope                                       |
| ----------------------- | ---------------------------------------------------- | ------------------------------------------- |
| `lint-js` / `format-js` | oxlint 1.76 and oxfmt 0.61 (npm)                     | TypeScript, JavaScript, CSS, JSON, Markdown |
| `lint-c` / `format-c`   | clang-format (system) and `cc -Wall -Wextra -Werror` | `core/`, `pebble/src/c/`, `tests/*.c`       |
| `lint-py` / `format-py` | ruff 0.16.8 (mise)                                   | `scripts/*.py`                              |
| `lint-sh` / `format-sh` | shellcheck 0.11.0 and shfmt 3.14.1 (mise)            | `scripts/*.sh`                              |

`lint-c` runs `scripts/lint-c.sh`: a clang-format check, then `cc -fsyntax-only -Wall -Wextra -Werror` over `tests/core.c`, `tests/precision.c`, `core/rd.c` with `tests/compare.c`, and `pebble/src/c/core.c` with `RD_MODE` 0, 1, and 2 under C99 to mirror the watch build. `main.c` needs the SDK include tree, so it is only checked by `mise run build-pebble`, which compiles with the SDK's `-Werror`. Every other C build (`test-core`, `build-wasm`, `scripts/compare.py`) also passes `-Wall -Wextra`.

ruff, shellcheck, and shfmt are installed by `mise install`. clang-format is not managed by mise. Install it from the system package manager. The `.clang-format` configuration needs clang-format 15 or newer for `InsertBraces`, and the sources were formatted with 22.1.8. Other major versions may place braces or blank lines slightly differently.

Style settings live in `.editorconfig`, `.clang-format`, `ruff.toml`, `.oxlintrc.json`, and `.oxfmtrc.json`. The shfmt tasks pass `-i 2 -ci` explicitly, so keep `.editorconfig` in sync with those flags. C sources keep the LLVM base style with blank lines between definitions and braces on every control statement. Include order is never sorted, because `pebble/src/c/core.c` relies on `config.h` preceding its textual include of `core/rd.c`.

Exclusions:

- `components/ui/` is vendored shadcn output. oxlint skips it, oxfmt still formats it.
- `public/reports/`, `public/fonts/leco.json`, and `docs/emulator-measurements.json` are generated. oxfmt skips them.
- `pebble/wscript` is the SDK build template. ruff skips it.

## Emery builds

```sh
mise run build-pebble
```

This builds both modes and saves their `.pbw`, `.elf`, and compiler stack-usage files under `build/pebble/`. Only the selected storage implementation is compiled into each watchface. The shared UUID means installing the second mode replaces the first in an emulator or watch.

```sh
npm run emulator              # build if needed, then install mode 0
npm run emulator:mode1        # the same for mode 1
npm run emulator:screenshot   # build/emery.png
npm run emulator:logs         # stream the watchface logs, Ctrl-C to stop
npm run emulator:kill         # stop the emulator
```

These run `scripts/emulator.sh`, which re-executes itself under `mise exec` when `pebble` is not on `PATH`. The underlying commands are:

```sh
mise exec -- pebble install --emulator emery --vnc build/pebble/mode-0.pbw
mise exec -- pebble screenshot --emulator emery --vnc --no-open build/emery.png
```

Keep `--vnc` consistent across emulator commands. Changing emulator launch options can restart it and interrupt the current log connection.

Mode 0 is 200 × 228 with 8-bit storage. Mode 1 is 100 × 114 with 16-bit storage. The build script sets `RD_BUILD_MODE` explicitly. To build the mode selected in an exported `config.h`, copy it to `pebble/src/c/config.h` and run `mise exec -- pebble build --sdk 4.33.1` from `pebble/` after setting the corresponding `RD_BUILD_MODE`. The mode override used for the two-mode comparison takes precedence over the header's default.

## Regenerating comparison artifacts

```sh
mise exec -- python scripts/compare.py
mise exec -- python scripts/build-report.py
```

`compare.py` runs 10,000 steps for the five presets, two seeds, and three modes. It creates PNGs and JSON under `public/reports/`, including `thin-lines.png` for the thin-line preset described in `docs/behavior.md`. The third mode is a diagnostic 100 × 114 / 8-bit configuration. It is not exposed as a production choice in the Web UI.

The report builder reads ARM ELF section sizes and `.su` stack reports, so run `mise run build-pebble` first with `arm-none-eabi-size` from the SDK toolchain on `PATH`. Runtime measurements must retain their measured build and observation scope. Do not substitute theoretical remaining RAM for measured minimum free heap.

## Storage precision experiment

```sh
sh scripts/precision-sweep.sh
sh scripts/precision-sweep.sh 100 1000 3000
```

`tests/precision.c` is a self-contained candidate simulator that compares storage codes, rounding schemes, and arithmetic precision against the Float32 reference. `scripts/precision-configs.txt` lists the candidates and `scripts/precision-aggregate.mjs` prints the Markdown summary. The sweep uses every core and takes a few minutes. [Storage precision study](precision-optimization.md) records its results.

## Font provenance

The glyphs in `public/fonts/leco.json` were extracted from official PebbleOS revision `119cb96e3f47be0f61191c0b90a502bb01f2d9bc`, from:

- `resources/normal/base/pbf/LECO_42_NUMBERS.pbf`
- `resources/normal/base/pbf/LECO_20_BOLD_NUMBERS.pbf`

Source: [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS/tree/119cb96e3f47be0f61191c0b90a502bb01f2d9bc). License: Apache-2.0, copied to `public/fonts/LICENSE`. Per-file SHA-256 values and the source revision are embedded in the JSON. Glyph dimensions, bearings, advances, and monochrome pixels are preserved. The extractor requires a local checkout of that revision and its official `pbf_extract.py`:

```sh
mise exec -- python scripts/extract-fonts.py /path/to/PebbleOS
mise exec -- python scripts/verify-fonts.py build/emery-mode-0.png 14:50 2026.09.24
```

The second command is specific to the recorded screenshot and its displayed timestamp.

Mise tool allowlisting follows the official [enable_tools setting](https://mise.jdx.dev/configuration/settings.html#enable_tools). The project does not change the global tool configuration.
