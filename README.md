# Pebble Turing Lab

A local Gray–Scott pattern explorer and Emery watchface sharing one fixed-point C core.

```sh
mise trust
mise install
mise run setup
mise run sdk-install
npm ci
mise run build-wasm
npm run dev -- --host 127.0.0.1
```

- [Development and builds](docs/development.md)
- [Core architecture and numerical contract](docs/core.md)
- [Web and watchface behavior](docs/behavior.md)
- [Validation results](docs/validation.md)
- [Float32 precision comparison](docs/float-precision.md)
- [Storage precision study](docs/precision-optimization.md)

## License

MIT, see [LICENSE](LICENSE).

The clock glyphs in `public/fonts/clock-fonts.json` and `core/clock_glyphs.h` are extracted from the LECO and Bitham system fonts of [coredevices/PebbleOS](https://github.com/coredevices/PebbleOS) at revision `119cb96e`, which are distributed under the Apache License 2.0. A copy of that license is in [public/fonts/LICENSE](public/fonts/LICENSE) and the source file hashes are recorded in both generated files. The components in `components/ui/` are generated from [shadcn/ui](https://ui.shadcn.com) (MIT, Copyright (c) 2023 shadcn).
