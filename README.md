# Turing Pattern Watchface for Pebble

A local Gray–Scott pattern explorer and Emery watchface sharing one fixed-point C core. The watchface's pattern, palette, and clock are set from the Pebble app through a settings page with a live preview of the face.

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
- [Web and watchface behavior](docs/behavior.md), including the [watch settings](docs/behavior.md#watch-settings)
- [Validation results](docs/validation.md)
- [Float32 precision comparison](docs/float-precision.md)
- [Storage precision study](docs/precision-optimization.md)

## License

MIT, see [LICENSE](LICENSE). The third-party works in the builds and their licenses are listed in [CREDITS.md](CREDITS.md).
