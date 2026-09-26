# Development and builds

## Project-local environment

`mise.toml` pins Python 3.13.15, Node 24.19.0, and pnpm 10.33.0. `requirements-tools.lock` pins the Python environment, including pebble-tool 5.0.40. The SDK installation task installs Pebble SDK 4.33.1, which includes Emery and BacklightService.

```sh
mise trust
mise install
mise run setup
mise run sdk-install
mise exec -- pebble sdk list
pnpm install --frozen-lockfile
```

| Location                   | Content                                         |
| -------------------------- | ----------------------------------------------- |
| `.venv/`                   | Python virtual environment managed through mise |
| `.local/share/pebble-sdk/` | SDKs, emulator flash, tooling state             |
| `.local/cache/`            | Project-local caches                            |
| `requirements-tools.lock`  | Resolved Python dependencies                    |
| `.tools/`                  | Earlier bootstrap environment, no longer used   |

The project allowlists only Python, Node, pnpm, ruff, shellcheck, and shfmt in mise, preventing unrelated globally configured tools from being installed for project tasks. Mise trusted-config registration remains in its user state directory. Runtime installations follow mise's data directory, including the active XDG environment. Existing runtime installations can be reused. This arrangement isolates Python dependencies and Pebble state. It is not a container or an operating-system sandbox. Pebble's legacy `~/.pebble-sdk` path takes precedence over `XDG_DATA_HOME` if it exists. Verify the resolved path on another machine before installing:

```sh
mise exec -- python -c 'from pebble_tool.util import get_persist_dir; print(get_persist_dir())'
```

The installed SDK also creates its own Python build environment and downloads an ARM toolchain. The SDK version is fixed, but its independently installed dependency environment is not fully locked by `requirements-tools.lock`.

## WebAssembly and Web

The build uses the existing Emscripten installation on `PATH`. Its cache must be writable. The generated Wasm has 1 MiB of fixed linear memory and a 64 KiB stack. It is served as a static asset and does not use Emscripten's generated JavaScript glue.

```sh
mise run build-wasm
pnpm run typecheck
pnpm test
mise run test-core
pnpm run build
pnpm run dev --host 127.0.0.1
```

The local URL is normally `http://127.0.0.1:5173/`. No deployment is required. The development server is not an on-watch performance benchmark. The standalone Web build uses Vite and Panda CSS. `pnpm run typecheck`, `pnpm run dev`, and `pnpm run build` regenerate ignored `styled-system/` with `panda codegen`. See [Web core and demo](web.md).

`mise run test-core` runs `tests/core.c` at `-O2`, then under AddressSanitizer and UndefinedBehaviorSanitizer, then `tests/wasm.mjs` (native versus Wasm field hashes and rendered rows of both models) and `tests/adapter.mjs` (the TypeScript adapters of both models, the default parameter vectors, and the clock masks). `pnpm run test:optimization` (`scripts/check-optimization.sh`) compiles the core at `-O0` and `-O3` and compares every field hash with `tests/golden-hashes.txt`, whose lines hold model, mode, steps, hash, and optionally the seven clock-mask arguments of `build/core-test`. `pnpm run compare:float` (`scripts/compare-float.mjs`) measures the Wasm core against the Float32 references of both models at the effective parameters and writes `docs/float-precision.json`.

`build/core-test`, built by `test-core` from `tests/core.c`, runs every check without arguments and otherwise prints one value for the scripts, always with seed 42 and the default parameters of the model (maze for model 0, Gray–Scott, and fhn-stripes for model 1, FitzHugh–Nagumo):

```sh
build/core-test MODEL MODE STEPS [font hour minute year month day halo]  # field hash
build/core-test render MODEL MODE STEPS                                  # hash of the interpolated rows
build/core-test defaults MODEL                                           # the default parameter vector
```

The presets live in `lib/presets.ts`, each with a stable id (`maze`, `coral`, `mitosis`, `spots`, `thin-line`, `fhn-stripes`, `fhn-hex`, `fhn-spiral`), a model, and its parameters. `node --experimental-transform-types scripts/list-presets.mjs` prints them as JSON with the model number and the Q15 parameter vector of `RD_PARAM_MAX` entries, which the Python scripts pass to the C harnesses.

`scripts/fhn-explore.mjs` runs one FitzHugh–Nagumo parameter set on the Float32 prototype, under the LECO clock mask unless `--no-mask` is given, and prints fill, amplitude, feature size, motion, chessboard, halo, and connected-region measures per checkpoint while writing `build/fhn/<label>-<step>.ppm`. Parameters start from `--preset` and are replaced one by one, negative values with `=`:

```sh
node --experimental-transform-types scripts/fhn-explore.mjs --preset fhn-hex --k=-0.25 --steps 1500,5000
node --experimental-transform-types scripts/fhn-explore.mjs --preset fhn-spiral --width 100 --pull 0.0625
```

## Lint and format

`mise run lint` runs every check and fails on any lint error or unformatted file. `mise run format` rewrites all sources with the project formatters. Both aggregate the per-language tasks below, which can also be run alone.

| Task                    | Tool                                                 | Scope                                       |
| ----------------------- | ---------------------------------------------------- | ------------------------------------------- |
| `lint-js` / `format-js` | ESLint and Prettier                                  | Web TypeScript, JavaScript, CSS, Markdown   |
| `lint-c` / `format-c`   | clang-format (system) and `cc -Wall -Wextra -Werror` | `core/`, `pebble/src/c/`, `tests/*.c`       |
| `lint-py` / `format-py` | ruff 0.16.8 (mise)                                   | `scripts/*.py`                              |
| `lint-sh` / `format-sh` | shellcheck 0.11.0 and shfmt 3.14.1 (mise)            | `scripts/*.sh`                              |

`lint-c` runs `scripts/lint-c.sh`: a clang-format check, then `cc -fsyntax-only -Wall -Wextra -Werror` over `tests/core.c`, `tests/precision.c`, `tests/bench.c`, `tests/fill.c`, `core/rd.c` and `core/clock_mask.c` with `tests/compare.c`, and `pebble/src/c/core.c` with `RD_MODE` 0 to 3 and `RD_FONT` 0 and 1 under C99 to mirror the watch build. `main.c` needs the SDK include tree, so it is only checked by `mise run build-pebble`, which compiles with the SDK's `-Werror`. Every other C build (`test-core`, `build-wasm`, `scripts/compare.py`) also passes `-Wall -Wextra`.

ruff, shellcheck, and shfmt are installed by `mise install`. clang-format is not managed by mise. Install it from the system package manager. The `.clang-format` configuration needs clang-format 15 or newer for `InsertBraces`, and the sources were formatted with 22.1.8. Other major versions may place braces or blank lines slightly differently.

Style settings live in `.editorconfig`, `.clang-format`, `ruff.toml`, `.prettierrc`, `.prettierignore`, and `eslint.config.mjs`. The shfmt tasks pass `-i 2 -ci` explicitly, so keep `.editorconfig` in sync with those flags. C sources keep the LLVM base style with blank lines between definitions and braces on every control statement. Include order is never sorted, because `pebble/src/c/core.c` relies on `config.h` preceding its textual include of `core/rd.c`.

Exclusions:

- `styled-system/` is generated by Panda CSS and ignored by ESLint and Prettier.
- Generated files are `public/reports/` (`scripts/compare.py` and `scripts/build-report.py`), `public/fonts/clock-fonts.json` (`scripts/extract-fonts.py`), `core/clock_glyphs.h` (`scripts/gen-clock-glyphs.py`), `public/wasm/rd.wasm` (`mise run build-wasm`), and `docs/float-precision.json` (`pnpm run compare:float`). Prettier skips the generated reports and font JSON. `docs/hardware-measurements.json` is the hand-maintained input of the report builder.
- `pebble/wscript` is the SDK build template with the mode, font, and log flags added. It also copies `pebble.shortName` of `pebble/package.json` (`Turing`, the name the watch menu shows) into the bundle, because the SDK otherwise uses `displayName` for both names. ruff skips it.

## Emery builds

```sh
mise run build-pebble
```

This builds modes 0, 1, and 3 and saves their `.pbw`, `.elf`, and compiler stack-usage files under `build/pebble/`. Only the selected storage implementation is compiled into each watchface. Modes 1 and 3 include both models, Gray–Scott and FitzHugh–Nagumo, and mode 0 folds to Gray–Scott, the only model of the packed storage. `RD_BUILD_MODEL=0` or `1` folds the core to one model (`RD_MODEL`). The script then builds mode 3 folded to each model once more and prints those load sizes for reference without keeping the builds. The sources are compiled with `-O3` after the SDK's own flags (`RD_BUILD_OPT` overrides it, and `-O3` measured 6% faster steps than `-O2` on the watch), `RD_BUILD_PROFILE=1` adds the clock calibration loop and the periodic profile log, and the script fails if an ELF's load size exceeds 20,480 bytes (mode 3 of numerical definition version 4, Gray–Scott only, was about 15.6 KB without logs: the row passes are duplicated so that rows away from the digits run without the mask levels, while the glyph traversal, the renderer, and the rare color fallback are kept out of line), because code shares the 128 KiB app region with the core allocation. Production builds leave out the diagnostic logs. `RD_BUILD_LOG=1` keeps them (the `RD init`, `RD startup`, `RD light`, and progress lines used for measurements), and `RD_BUILD_PROFILE=1`, `RD_BENCH`, and `RD_FRAME_BENCH` builds include them too. `RD_BUILD_MODE` selects the mode (default 3), `RD_BUILD_MODEL` folds the model, `RD_BUILD_FONT` the clock font set (0 LECO, 1 Bitham), `RD_BUILD_RENDER` overrides the `rd_row_rgb2` flags (2 interpolates, the default for mode 3 and 0 for the others), and `RD_BUILD_DEFINES` adds space-separated defines such as `RD_STARTUP_MS=20000` for study builds. The shared UUID means installing one mode replaces another in an emulator or watch.

```sh
pnpm run emulator              # build if needed, then install mode 3
pnpm run emulator:mode0        # the same for mode 0
pnpm run emulator:mode1        # the same for mode 1
pnpm run emulator:screenshot   # build/emery.png
pnpm run emulator:logs         # stream the watchface logs, Ctrl-C to stop
pnpm run emulator:kill         # stop the emulator
PEBBLE_PHONE=<ip> pnpm run device             # install mode 3 on the watch and stream its logs
PEBBLE_PHONE=<ip> pnpm run device:mode0       # the same for mode 0
PEBBLE_PHONE=<ip> pnpm run device:mode1       # the same for mode 1
PEBBLE_PHONE=<ip> pnpm run device:logs        # stream watch logs
PEBBLE_PHONE=<ip> pnpm run device:screenshot  # build/watch.png
```

These run `scripts/emulator.sh`, which re-executes itself under `mise exec` when `pebble` is not on `PATH`. `screenshot` and `device-screenshot` take an output path as a second argument, and `device-install` streams the watch logs after the install. The `device` commands use the phone's Pebble app developer connection (`--phone`), so enable it in the app and set `PEBBLE_PHONE` to the IP it shows. Start `pnpm run device` before the face launches so the startup summary is captured. The underlying commands are:

```sh
mise exec -- pebble install --emulator emery --vnc build/pebble/mode-3.pbw
mise exec -- pebble screenshot --emulator emery --vnc --no-open build/emery.png
```

Keep `--vnc` consistent across emulator commands. Changing emulator launch options can restart it and interrupt the current log connection.

Mode 0 is 200 × 228 with packed 16-bit storage (A 7-bit, B 9-bit). Mode 1 is 100 × 114 and mode 3, the default, 120 × 136, both with 16-bit storage. The build script sets `RD_BUILD_MODE` explicitly. To build the mode selected in an exported `config.h`, copy it to `pebble/src/c/config.h` and run `mise exec -- pebble build --sdk 4.33.1` from `pebble/` after setting the corresponding `RD_BUILD_MODE`. The mode override takes precedence over the header's default.

The face starts with `RD_DEFAULT_MODEL` and `RD_DEFAULT_PARAMS` of `config.h`, Gray–Scott with the maze preset by default, until the planned settings mechanism sends a model and a parameter vector from the phone. A FitzHugh–Nagumo measurement build overrides both through `RD_BUILD_DEFINES`, with the vector written without spaces. `config.h` stops such a build in modes 0 and 2, so build mode 3 directly instead of through `build-pebble`:

```sh
cd pebble
RD_BUILD_MODE=3 RD_BUILD_LOG=1 \
  RD_BUILD_DEFINES="RD_DEFAULT_MODEL=1 RD_DEFAULT_PARAMS=6554,0,8192,410,32768,-9830,32768,-21935,1" \
  mise exec -- pebble build --sdk 4.33.1
mise exec -- pebble install --emulator emery --vnc build/pebble.pbw
```

The vector is `fhn-spiral`. `scripts/list-presets.mjs` prints the others, `fhn-stripes` is `1638,32768,328,819,19661,0,32768,0,0` and `fhn-hex` `1311,32768,573,1434,19661,-7209,32768,-9585,0`. The `RD init` log line reports the model.

`RD_BUILD_DEFINES=RD_BENCH` builds a watchface that logs `RD bench` with the time of each phase of a step 3 s after launch. `RD_BUILD_DEFINES="RD_FRAME_BENCH RD_STARTUP_MS=0"` builds one that logs `RD frames`, the frames the OS renders in 5 s with the normal draw and then with an empty draw. `mkdir -p build && cc -O3 -std=c11 -DRD_MODE=3 tests/bench.c -o build/bench && build/bench 3 500` runs the same timing on the host (`build/bench MODE [COUNT]`, and a binary built without `RD_MODE` accepts every mode). The host ranks the phases differently from the watch, so only the watch numbers decide.

`mise exec -- python scripts/fill.py grids` and `fill.py seeds` rerun the growth comparison of the resolution study and write contact sheets and `build/fill/<study>.jsonl` under `build/fill/` (`grids` is the default study). `--presets` takes preset ids (default `maze,thin-line`), and FitzHugh–Nagumo presets run only in the Q15 configurations. They drive `tests/fill.c` as `build/fill-run mode model p0 ... p9 seed disks min_radius radius_range out_prefix checkpoint...`, with the model and the vector of `scripts/list-presets.mjs`.

## Regenerating comparison artifacts

```sh
mise exec -- python scripts/compare.py
mise exec -- python scripts/build-report.py
```

`compare.py` runs 10,000 steps for every preset of `scripts/list-presets.mjs` with two seeds, the Gray–Scott presets in the four modes 0 to 3 and the FitzHugh–Nagumo presets in modes 1 and 3, through `tests/compare.c` (`build/compare mode model p0 ... p9 seed steps out.ppm`). It creates `mode-M-preset-<id>-seed-S.png` and `comparison.json` under `public/reports/`, with `comparison.png` (maze, coral, mitosis, and spots in every mode), `thin-lines.png` for the thin-line preset described in `docs/behavior.md` (modes 0, 1, and 3), and `fhn.png` (the three FitzHugh–Nagumo presets in modes 1 and 3). Mode 2 is a diagnostic 100 × 114 configuration with the packed cells of mode 0 that the Web UI does not expose. `compare.py` rewrites `comparison.json` from scratch, so run `build-report.py` after it to add the ARM, stack, and physical sections back.

The report builder reads ARM ELF section sizes and `.su` stack reports and `docs/hardware-measurements.json`, and rewrites `public/reports/comparison.json` in place, so run `mise run build-pebble` and `compare.py` first with `arm-none-eabi-size` from the SDK toolchain on `PATH`. Runtime measurements must retain their measured build and observation scope. Do not substitute theoretical remaining RAM for measured minimum free heap.

## Storage precision experiment

```sh
sh scripts/precision-sweep.sh
sh scripts/precision-sweep.sh 100 1000 3000
```

`tests/precision.c` is a self-contained candidate simulator that compares storage codes, rounding schemes, and arithmetic precision against the Float32 reference. `scripts/precision-configs.txt` lists the candidates and `scripts/precision-aggregate.mjs` prints the Markdown summary. The sweep uses every core and takes a few minutes. `PRECISION_CONFIGS` names another candidate list and `PRECISION_OUT` another result file, `g=120` runs the watch grid, and `i=q15x32c2` is the version 4 arithmetic of the Q15 modes. [Storage precision study](precision-optimization.md) records its results.

## Font provenance

The glyphs in `public/fonts/clock-fonts.json` and `core/clock_glyphs.h` were extracted from official PebbleOS revision `119cb96e3f47be0f61191c0b90a502bb01f2d9bc`, from:

- `resources/normal/base/pbf/LECO_42_NUMBERS.pbf`
- `resources/normal/base/pbf/LECO_20_BOLD_NUMBERS.pbf`
- `resources/normal/base/pbf/BITHAM_42_BOLD.pbf`
- `resources/normal/base/pbf/BITHAM_30_BLACK.pbf`

The font sets are listed in `SETS` of `scripts/extract-fonts.py`. Only the digits and the separator of each line are kept. `extract-fonts.py /path/to/PebbleOS --list 'BITHAM_*'` prints the height and the printable characters of the matching PBFs. BITHAM_18_LIGHT_SUBSET and BITHAM_34_LIGHT_SUBSET contain no digits.

Source: [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS/tree/119cb96e3f47be0f61191c0b90a502bb01f2d9bc). License: Apache-2.0, copied to `public/fonts/LICENSE`. Per-file SHA-256 values and the source revision are embedded in the JSON. Glyph dimensions, bearings, advances, and monochrome pixels are preserved. The extractor requires a local checkout of that revision and its official `pbf_extract.py`:

```sh
mise exec -- python scripts/extract-fonts.py /path/to/PebbleOS
mise exec -- python scripts/gen-clock-glyphs.py
mise exec -- python scripts/verify-fonts.py build/emery.png 13:16 2026.09.25 --font leco --halo 1
```

A sparse clone of `tools/font` and `resources/normal/base/pbf` at that revision is enough for the extractor. The last command checks a screenshot with the time and date it shows: its white pixels must be exactly the glyph pixels of the set named by `--font` (`leco` by default or `bitham`), and with `--halo N` every other pixel within N pixels of a glyph must be black. `RD_BUILD_FONT=1 mise run build-pebble` builds the Bitham watchface.

Mise tool allowlisting follows the official [enable_tools setting](https://mise.jdx.dev/configuration/settings.html#enable_tools). The project does not change the global tool configuration.
