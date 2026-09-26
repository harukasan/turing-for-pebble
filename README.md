# Turing Pattern Watchface for Pebble

A local reaction-diffusion pattern explorer and Emery watchface sharing one fixed-point C core, with the Gray–Scott model and, in its 16-bit Q15 modes, the FitzHugh–Nagumo model for Turing stripes and spots and rotating spirals.

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
