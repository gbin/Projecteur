# Contributing to Projecteur

Thanks for helping make Linux presentations better. Bug fixes, device support,
documentation, translations, and focused feature improvements are all welcome.

For a substantial change, please open an issue before investing heavily so the
approach and project scope can be agreed on. Small, self-contained fixes can go
straight to a pull request.

## Project scope

The current Projecteur development line targets:

- KDE Plasma 6.7 or newer on Wayland
- Qt 6.11 or newer
- LayerShellQt and KPipeWire 6.7 or newer
- Linux presenter devices exposed through evdev and hidraw

Qt 5, X11, and non-Plasma support are maintained only for critical fixes on the
[`legacy/qt5`](https://github.com/gbin/Projecteur/tree/legacy/qt5) branch.

## Requirements

- A C++17 compiler
- CMake 3.20 or newer
- Extra CMake Modules 6.7 or newer
- Qt 6.11 or newer with Core, DBus, Gui, Quick, ShaderTools, WaylandClient, and
  Widgets
- KDE Frameworks 6.7 or newer: Config, ConfigWidgets, CoreAddons, DBusAddons,
  GlobalAccel, I18n, Notifications, Package, KirigamiPlatform, WidgetsAddons,
  WindowSystem, and XmlGui
- Plasma 6.7 or newer
- KPipeWire 6.7 or newer
- LayerShellQt 6.7 or newer
- gettext for translations

Package names vary by distribution. The CI workflow and
[`Justfile`](./Justfile) are the canonical dependency lists for Arch Linux.

## Build from source

```sh
git clone https://github.com/gbin/Projecteur.git
cd Projecteur
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DPACKAGE_TARGETS=OFF
cmake --build build --parallel
```

The binary in `build/projecteur` can exercise most of the application, but live
zoom requires a proper installation. KWin authorizes the restricted capture
interfaces by matching the executable to Projecteur's installed desktop
metadata.

For a complete local install:

```sh
sudo cmake --install build
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The install also provides the Plasma applet, desktop entry, AppStream metadata,
udev rules, notifications, D-Bus service, shell completion, and manual page.

## Arch Linux workflow

On Arch Linux and Arch-based distributions, install `just` and use:

```sh
just build
just package
just install
```

- `just build` installs missing dependencies and compiles Projecteur.
- `just package` packages the current working tree—including uncommitted
  files—into `build/packages/`.
- `just install` builds and installs that package with `pacman`, then restarts
  Plasma Shell and Projecteur.

`sudo` is used only when dependencies or the finished package need to be
installed.

## Verify a change

At minimum, rebuild and run the same smoke checks as CI:

```sh
cmake --build build --parallel
./build/projecteur --version
./build/projecteur --help
git diff --check
```

For UI or device changes, also test the relevant workflow manually on Plasma
Wayland. In the pull request, mention the Plasma and Qt versions, connection type,
and presenter model you tested.

## Project map

| Path | Purpose |
| --- | --- |
| `src/` | Application, device handling, settings, and QWidget UI |
| `qml/` | Wayland overlay and zoom shaders |
| `plasma/` | Native Plasma system tray applet |
| `protocols/` | Wayland protocol definitions used by the zoom pipeline |
| `cmake/` | Build, packaging, desktop, AppStream, and manual templates |
| `po/` | gettext translation catalogs |
| `doc/` | User documentation, screenshots, and changelog |
| `packaging/arch/` | Local Arch package recipe |

## Adding a presenter

Add compile-time device support to [`devices.conf`](./devices.conf) using:

```text
vendorId, productId, [usb|bt], name
```

For example:

```text
0x0abc, 0x1234, usb, Example Presenter
```

CMake uses this list to generate both device definitions and udev rules. If the
device needs special event decoding or HID++ behavior, changes in `src/` may also
be required.

For quick experiments, Projecteur accepts
`--additional-device VENDOR:PRODUCT` at runtime.

## Translations

Projecteur uses KDE's KI18n/gettext system. Run `Messages.sh` through the standard
KDE translation tooling to update `projecteur.pot`. Catalogs placed at
`po/<locale>/projecteur.po` are compiled and installed automatically.

Keep user-visible strings translatable and avoid assembling sentences from
fragments.

## Pull requests

Keep each pull request focused. Include:

- what changed and why;
- how it was verified;
- screenshots for visible UI changes;
- device IDs and connection type for hardware-specific changes.

By contributing, you agree that your work is provided under the project's
[MIT License](./LICENSE.md).
