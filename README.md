# Projecteur

[![Build Status][gh-badge]][gh-link]

Qt 6 / KDE Plasma Wayland application for the Logitech Spotlight device and
similar presenters.

## About This Fork

This repository is a fork of [jahnf/Projecteur](https://github.com/jahnf/Projecteur)
focused on supporting modern KDE Plasma on Wayland. It targets KDE Plasma 6.7
with Qt 6.11 and LayerShellQt 6.7. Wayland is the only supported windowing
platform; Qt 5 and X11 are outside the scope of this fork.

In addition to the hardware supported by the upstream project, this fork adds
support for the **Logitech Spotlight 2** through its Logi Bolt USB-C receiver
(`046d:c548`) and over Bluetooth (`046d:b506`).

This is an independent, unofficial fork. Please report problems specific to
this version in the [fork's issue tracker](https://github.com/gbin/Projecteur-kde/issues).
Only report a problem upstream when it also reproduces in the original project.

[gh-badge]: https://github.com/gbin/Projecteur-kde/actions/workflows/ci-build.yml/badge.svg?branch=develop
[gh-link]: https://github.com/gbin/Projecteur-kde/actions/workflows/ci-build.yml?query=branch%3Adevelop

## Motivation

I saw the Logitech Spotlight device in action at a conference and liked it immediately.
Unfortunately as in a lot of cases, software is only provided for Windows and Mac.
The device itself works just fine on Linux, but the cool spotlight feature is
only available using additional software.

So here it is: a Linux application for the Logitech Spotlight.

## Table of Contents

- [Projecteur](#projecteur)
  - [About This Fork](#about-this-fork)
  - [Motivation](#motivation)
  - [Table of Contents](#table-of-contents)
  - [Features](#features)
    - [Screenshots](#screenshots)
  - [Supported Environments](#supported-environments)
  - [How it works](#how-it-works)
    - [Button mapping](#button-mapping)
      - [Hold Button Mapping for Logitech Spotlight](#hold-button-mapping-for-logitech-spotlight)
  - [Building](#building)
    - [Requirements](#requirements)
    - [Build Example](#build-example)
  - [Installation/Running](#installationrunning)
    - [Pre-requisites](#pre-requisites)
      - [When building Projecteur yourself](#when-building-projecteur-yourself)
    - [Application Menu](#application-menu)
    - [Command Line Interface](#command-line-interface)
    - [Scriptability](#scriptability)
    - [Using Projecteur without a device](#using-projecteur-without-a-device)
    - [Device Support](#device-support)
      - [Compile Time](#compile-time)
      - [Runtime](#runtime)
    - [Troubleshooting](#troubleshooting)
      - [Opaque Spotlight / No Transparency](#opaque-spotlight--no-transparency)
      - [Missing System Tray](#missing-system-tray)
      - [Zoom is not updated while spotlight is shown](#zoom-is-not-updated-while-spotlight-is-shown)
      - [Wayland](#wayland)
      - [Wayland Zoom](#wayland-zoom)
      - [Device shows as not connected](#device-shows-as-not-connected)
  - [Changelog](#changelog)
  - [License](#license)

## Features

* Configurable desktop spotlight
  * _shade color_, _opacity_, _cursor_, _border_, _center dot_ and different _shapes_
  * Zoom (magnifier) functionality
* Multiple screen support
* Support of devices beyond the Logitech Spotlight (see [Device Support](#device-support))
* Button mapping:
  * Map any button on the device to (almost) any keyboard combination.
  * Switch between (cycle through) custom spotlight presets.
  * Audio Volume / Horizontal and Vertical Scrolling (Logitech Spotlight).
* Vibration (Timer) Support for the Logitech Spotlight and Spotlight 2
* Usable without a presenter device (e.g. for online presentations)

### Screenshots

[<img src="doc/screenshot-settings.png" alt="Projecteur preferences" height="300" />](./doc/screenshot-settings.png)
[<img src="doc/screenshot-spot.png" alt="Projecteur spotlight overlay" height="300" />](./doc/screenshot-spot.png)
[<img src="doc/screenshot-traymenu.png" alt="Projecteur Plasma tray popup" height="300" />](./doc/screenshot-traymenu.png)

## Supported Environments

This port targets KDE Plasma 6.7 on Wayland with Qt 6.11. X11 and Qt 5 are not
supported. The spotlight overlay and device features are Wayland-native; zoom uses
KWin's restricted `org.kde.KWin.ScreenShot2` interface.

If you build the application yourself, install both the generated desktop entry and
udev rules (see [pre-requisites](#pre-requisites)).

## How it works

With a connection via the USB Dongle Receiver or via Bluetooth, the Logitech Spotlight
device will be detected by Linux as a HID device with mouse and keyboard events.
As mouse events, the device sends relative cursor movements and left button presses.
Acting as a keyboard, the device basically just sends left and right arrow key press
events when forward or back is pressed on the device.

The mouse move events of the device are what we are mainly interested in. Since the device is
already detected as a mouse input device and able to move the cursor, we simply detect
if the Spotlight device is sending mouse move events. If it is sending mouse events,
we will 'turn on' the desktop spot (virtual laser).

For more details: Have a look at the source code ;)

### Button mapping

Button mapping works by **grabbing** all device events of connected
devices and forwarding them to a virtual _'uinput'_ device if not configured
differently by the button mapping configuration. If a mapped configuration for
a button exists, _Projecteur_ will inject the mapped action instead.
(You can still disable device grabbing with the `--disable-uinput` command
line option - button mapping will be disabled then.)

Input events from the presenter device can be mapped to different actions.
The _Key Sequence_ action is particularly powerful as it can emit any user-defined
keystroke. These keystrokes can invoke shortcut in presentation software
(or any other software) being used. Similarly, the _Cycle Preset_ action can be
used for cycling different spotlight presets. However, it should be noted that
presets are ordered alphabetically on program start. To retain a certain
order of your presets, you can prepend the preset name with a number.

#### Hold Button Mapping for Logitech Spotlight

Logitech Spotlight can send Hold event for Next and Back buttons as HID++
messages. Using this device feature, this program provides three different
usage of the Next or Hold button.

1. Button Tap
2. Long-Press Event
3. Button Hold and Move Event

On the Input Mapper tab (Devices tab in Preferences dialog box), the first two
button usages (_i.e._ tap and long-press) can be mapped directly by tapping or
long pressing the relevant button. For mapping the third button usage (_i.e._
Hold Move Event), please ensure that the device is active by pressing any button,
and then right click in first column (Input Sequence) for any entry and select
the relevant option. Additional mapped actions (e.g. _Vertical Scrolling_,
_Horizontal Scrolling_, or _Volume control_) can be selected for these hold
move events.

Please note that in case when both Long-Press event and Hold Move events are
mapped for a particular button, both actions will executed if user hold the
button and move device. To avoid this situation, do not set both Long-Press
and Hold Move actions for the same button.

## Building

### Requirements

* C++17 compiler
* CMake 3.20 or later
* Qt 6.11 with Core, DBus, Gui, LinguistTools, Quick, and Widgets
* KDE Plasma 6.7 Wayland, including Libplasma, KConfig, KCoreAddons,
  KDBusAddons, KNotifications, KWindowSystem, and KXmlGui
* LayerShellQt 6.7
* Extra CMake Modules 6.7 or later

### Build Example

```sh
git clone https://github.com/gbin/Projecteur-kde
cd Projecteur-kde
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

### Arch Linux

On Arch Linux and Arch-based distributions, the `Justfile` can install missing
build dependencies and run the complete local packaging workflow:

```sh
just build    # Compile build/projecteur
just package  # Create build/packages/projecteur-*.pkg.tar.zst
just install  # Build, package, and install the package with pacman
```

`just package` packages the current working tree, including uncommitted files,
through the checked-in `packaging/arch/PKGBUILD`. `just install` uses `sudo`
only for dependency installation and the final `pacman -U`. It stops a running
Projecteur instance, compiles, packages and installs the current tree, then
restarts `plasma-plasmashell.service`.

Installing is required for zoom: KWin authorizes the screenshot interface by matching
the running executable with the installed `org.projecteur.Projecteur.desktop` metadata. A binary run
directly from the build directory can use the normal spotlight, but KWin will reject
its zoom capture request.

## Installation/Running

Projecteur stores its KDE configuration in `~/.config/projecteurrc`.

### Pre-requisites

#### When building Projecteur yourself

The input devices detected from the Spotlight device must be readable to the
user running the application. To make this easier there is a udev rule template
file in this repository: `55-projecteur.rules.in`

* During the CMake run, the file `55-projecteur.rules` will be created from this template
  in your **build directory**. Copy that generated file to `/lib/udev/rules.d/55-projecteur.rules`
* Most recent systems (using systemd) will automatically pick up the rule.
  If not, run `sudo udevadm control --reload-rules` and `sudo udevadm trigger`
  to load the rules without a reboot.
* After that, the input devices from the Logitech USB Receiver (but also the Bluetooth device)
  in /dev/input should be readable/writable by you.
  (See also about [device detection](#device-shows-as-not-connected))

### System Tray

Projecteur provides a native Plasma system tray popup while the application is
running. It shows connected presenters and offers quick access to the overlay,
presets, spotlight test, preferences, about dialog, and quit action. Plasma owns
the popup placement and closes it when the icon is clicked again or focus moves
elsewhere.

If the system tray icon is missing, see the
[Troubleshooting](#missing-system-tray) section.

Presenter connection, battery, access-error, and presentation-timer notifications
are registered with Plasma and can be customized under System Settings →
Notifications → Applications → Projecteur.

### Command Line Interface

Additional to the standard `--help` and `--version` options, there is an option to send
commands to a running instance of _Projecteur_ and the ability to set properties.

```txt
Usage: projecteur [OPTION]...

<Options>
  -h, --help              Show command line usage.
  --help-all              Show complete command line usage with all properties.
  -v, --version           Print application version.
  -f, --fullversion       Print extended version info.
  --cfg FILE              Set custom config file.
  -d, --device-scan       Print device-scan results.
  -l, --log-level LEVEL   Set log level (dbg,inf,wrn,err), default is 'inf'.
  --show-dialog           Show preferences dialog on start.
  -m, --minimize-only     Only allow minimizing the preferences dialog.
  -D DEVICE               Additional accepted device; DEVICE=vendorId:productId
  -c COMMAND|PROPERTY     Send command/property to a running instance.

<Commands>
  spot=[on|off|toggle]     Turn spotlight on/off or toggle.
  spot.size.adjust=[+|-]N  Increase or decrease spot size by N.
  settings=[show|hide]     Show/hide preferences dialog.
  preset=NAME              Set a preset.
  quit                     Quit the running instance.
```

A complete list the properties that can be set via the command line, can be listed with the
`--help-all` option or can also be found on the man pagers with newer versions of
_Projecteur_ (`man projecteur`).

### Scriptability

_Projecteur_ allows you to set almost all aspects of the spotlight via the command line
for a running instance.

Example:

```bash
# Set showing the border to true
projecteur -c border=true
# Set the border color to red
projecteur -c border.color=#ff0000
# Send a vibrate command to the device with
# intensity=128 and length=0 (length only applies to the original Logitech Spotlight)
projecteur -c vibrate=128,0
```

While _Projecteur_ does not provide global keyboard shortcuts, command line options
can but utilized for that. For instance, if you like to use _Projecteur_ as a tool while sharing
your screen in a video call without additional presenter hardware, you can assign global
shortcuts in your window manager (e.g. GNOME) to run the commands `projecteur -c spot=on`
and `projecteur -c spot=off` or `projecteur -c spot=toggle`, and therefore
turning the spot on and off with a keyboard shortcut.

A complete list the properties that can be set via the command line, can be
listed with the `--help-all` command line option.

### Using Projecteur without a device

You can use _Projecteur_ for your online presentations and video conferences without a presenter
device. For this you can assign a global keyboard shortcut in your window manager
(e.g. KDE, GNOME...) to run the command `projecteur -c spot=toggle`. You will then be able to
turn the digital spot on and off with the assigned keyboard shortcut while sharing
your screen in an online presentation or call.

### Device Support

Besides the _Logitech Spotlight_, the following devices are currently supported out of the box:

* Logitech Spotlight 2 via Logi Bolt USB-C receiver _(046d:c548)_ or Bluetooth _(046d:b506)_
* AVATTO H100 / August WP200 _(0c45:8101)_
* August LP315 _(2312:863d)_
* AVATTO i10 Pro _(2571:4109)_
* August LP310 _(69a7:9803)_
* Norwii Wireless Presenter _(3243:0122)_
* Kensington PowerPointer _(1ea7:0002)_

#### Compile Time

Besides the Logitech Spotlight, similar devices can be used and are supported.
Additional devices can be added to `devices.conf`. At CMake configuration time,
the project will be configured to support these devices and also create entries
for them in the generated udev-rule file.

#### Runtime

_Projecteur_ will also accept devices as supported when added via the `-D`
command line option.

Example: `projecteur -D 04b3:310c`

This will enable devices within _Projecteur_ and the application will try to
connect to that device if it is detected. It is, however, up to the user to make
sure the device is accessible (via udev rules).

### Troubleshooting

#### Opaque Spotlight / No Transparency

The overlay requires the Plasma Wayland compositor. Verify that the session reports
`XDG_SESSION_TYPE=wayland` and that Projecteur logs `Qt platform plugin: wayland`.

#### Missing System Tray

If the Plasma system tray does not show the _Projecteur_ applet, commands can be sent
to the application to bring up the preferences
dialog, test the spotlight, quit the application or set spotlight properties.
See [Command Line Interface](#command-line-interface). There is also a command
line option (`-m`) to prevent the preferences dialog from hiding, allowing it
only to minimize - behaving more like a regular application window.

#### Zoom is not updated while spotlight is shown

Zoom does not update while spotlight is shown due to how the zoom currently works. A screenshot is
taken shortly before the overlay window is shown, and then a magnified section is shown wherever
the mouse/spotlight is.
If the zoom would be updated while the overlay window is shown, the overlay window it self would
show up in the magnified section. That is a general problem that other magnifier tools also face,
although they get around the problem by showing the magnified content rectangle always in the
same position on the screen.

#### Wayland

Wayland is the only supported windowing platform in this port. Do not force
`QT_QPA_PLATFORM=xcb`.

#### Wayland Zoom

Zoom is implemented for KDE Plasma through KWin's `ScreenShot2` DBus interface. The
installed desktop entry declares the required restricted interface. If the log says
the process is not authorized to take a screenshot, install Projecteur instead of
running it from an arbitrary build path.

#### Device shows as not connected

If the device shows as not connected, there are some things you can do:

* Check for devices with _Projecteur_'s command line option `-d` or `--device-scan` option.
  This will show you a list of all supported and detected devices and also if
  they are readable/writable. If a detected device is not readable/writable, it is an indicator
  that there is something wrong with the installed _udev_ rules.
* Manually on the shell: Check if the device is detected by the Linux system: Run
  `cat /proc/bus/input/devices | grep -A 5 "Vendor=046d"` \
  This should show one or multiple spotlight devices (among other Logitech devices)
  * Check that the corresponding `/dev/input/event??` device file is readable by you. \
    Example: `test -r /dev/input/event19 && echo "SUCCESS" || echo "NOT readable"`
* Make sure you don't have conflicting udev rules installed, e.g. first you installed
  the udev rule yourself and later you used the automatically built Linux packages to
  install _Projecteur_.

## Changelog

See [CHANGELOG.md](./doc/CHANGELOG.md) for a detailed changelog.

## License

Copyright 2018-2021 Jahn Fuchs

This project is distributed under the [MIT License](https://opensource.org/licenses/MIT),
see [LICENSE.md](./LICENSE.md) for more information.
