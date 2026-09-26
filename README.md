# Pebble Turing Lab

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
- [Core architecture and numerical contract](docs/core.md)
- [Web and watchface behavior](docs/behavior.md)
- [Validation results and remaining work](docs/validation.md)
- [Float32 precision comparison](docs/float-precision.md)
- [Storage precision study](docs/precision-optimization.md)
