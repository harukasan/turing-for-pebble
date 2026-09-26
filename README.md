# Turing Pattern Watchface for Pebble

A local Gray–Scott pattern explorer and Emery watchface sharing one fixed-point C core.

```sh
mise trust
mise install
mise run setup
mise run sdk-install
pnpm install --frozen-lockfile
mise run build-wasm
pnpm run dev --host 127.0.0.1
```

- [Development and builds](docs/development.md)
- [Web core and standalone demo](docs/web.md)
- [Embed only the Web preview](docs/embed-preview.md)
- [Core architecture and numerical contract](docs/core.md)
- [Web and watchface behavior](docs/behavior.md)
- [Validation results](docs/validation.md)
- [Float32 precision comparison](docs/float-precision.md)
- [Storage precision study](docs/precision-optimization.md)

## License

MIT, see [LICENSE](LICENSE).

The clock glyphs in `public/fonts/clock-fonts.json` and `core/clock_glyphs.h` are extracted from the LECO and Bitham system fonts of [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS) at revision `119cb96e`, which are distributed under the Apache License 2.0. A copy of that license is in [public/fonts/LICENSE](public/fonts/LICENSE) and the source file hashes are recorded in both generated files.

The viridis, magma, plasma, inferno, cividis, and turbo palettes in `lib/palettes.ts` and `core/palettes.h` are sampled from matplotlib's colormap tables. Viridis, magma, plasma, and inferno are CC0, turbo is Copyright 2019 Google LLC under the Apache License 2.0, and cividis is Copyright 2017 Battelle Memorial Institute under a BSD-style license. The sources and license texts are in [licenses/palettes.txt](licenses/palettes.txt).
