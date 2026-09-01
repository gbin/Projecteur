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

The first settings-backed LayerShell overlay is opt-in and initially activated
by `--overlay-preview`. It can then be controlled from the diagnostic window.
The initial preview automatically closes after 12 seconds, and clicking anywhere
also closes it:

```sh
cargo run --bin projecteur-rs -- --show-window --overlay-preview
```

Read-only presenter discovery runs without starting Qt or grabbing devices:

```sh
cargo run --bin projecteur-rs -- --device-scan
```

Inspect one input node without exclusively grabbing it (stop with Ctrl-C):

```sh
cargo run --bin projecteur-rs -- --event-monitor /dev/input/event16
```

Inspect raw HID notifications without writing initialization commands:

```sh
cargo run --bin projecteur-rs -- --hidraw-monitor /dev/hidraw5
```

Drive the overlay from a presenter hidraw node without an exclusive grab:

```sh
cargo run --bin projecteur-rs -- --show-window --presenter /dev/hidraw5
```
