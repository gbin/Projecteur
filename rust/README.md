# Rust port

The maintained port is built entirely by Cargo. CXX-Qt generates the necessary
C++ glue and drives Qt's QML tooling from `build.rs`; generated C++ is never
maintained as source.

```sh
cargo build --workspace
cargo test --workspace
```

The migration executable is named `projecteur-rs` until it replaces the legacy
binary. It starts without showing a window by default. To inspect the current
QML feasibility surface:

```sh
cargo run --bin projecteur-rs -- --show-window
```

Pass the legacy `--cfg` argument to verify a specific configuration without
changing the normal desktop selection:

```sh
cargo run --bin projecteur-rs -- --show-window --cfg /path/to/projecteurrc
```
