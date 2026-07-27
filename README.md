# Projecteur

develop: [![Build Status develop][gh-badge-dev]][gh-link-dev]
master: [![Build Status master][gh-badge-rel]][gh-link-rel]

Linux/X11 application for the Logitech Spotlight device (and similar devices). \
See **[Download](#download)** section for binary packages.

[gh-badge-dev]: https://github.com/jahnf/Projecteur/workflows/ci-build/badge.svg?branch=develop
[gh-badge-rel]: https://github.com/jahnf/Projecteur/workflows/ci-build/badge.svg?branch=master
[gh-link-dev]: https://github.com/jahnf/Projecteur/actions?query=workflow%3Aci-build+branch%3Adevelop
[gh-link-rel]: https://github.com/jahnf/Projecteur/actions?query=workflow%3Aci-build+branch%3Amaster

## Motivation

I saw the Logitech Spotlight device in action at a conference and liked it immediately.
Unfortunately as in a lot of cases, software is only provided for Windows and Mac.
The device itself works just fine on Linux, but the cool spotlight feature is
only available using additional software.

So here it is: a Linux application for the Logitech Spotlight.

## Table of Contents

- [Projecteur](#projecteur)
  - [Motivation](#motivation)
  - [Table of Contents](#table-of-contents)
  - [Features](#features)
    - [Screenshots](#screenshots)
    - [Planned features](#planned-features)
  - [Supported Environments](#supported-environments)
  - [How it works](#how-it-works)
    - [Button mapping](#button-mapping)
      - [Hold Button Mapping for Logitech Spotlight](#hold-button-mapping-for-logitech-spotlight)
  - [Download](#download)
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
* Vibration (Timer) Support for the Logitech Spotlight
* Usable without a presenter device (e.g. for online presentations)

### Screenshots

[<img src="doc/screenshot-settings.png" height="300" />](./doc/screenshot-settings.png)
[<img src="doc/screenshot-spot.png" height="300" />](./doc/screenshot-spot.png)
[<img src="doc/screenshot-button-mapping.png" height="300" />](./doc/screenshot-button-mapping.png)
[<img src="doc/screenshot-traymenu.png" />](./doc/screenshot-traymenu.png)

### Planned features

* Support for more customizable button mapping actions.
* Support of more proprietary features of the Logitech Spotlight and other devices.

## Supported Environments

The application was mostly tested on Ubuntu 18.04, Ubuntu 20.04 (GNOME) and
OpenSuse 15 (GNOME) but should work on almost any Linux/X11 Desktop. In case
you are building the application yourself, make sure you have the correct udev
rules installed (see [pre-requisites section](#pre-requisites)).

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

## Download

The latest binary packages for some Linux distributions are available for download on cloudsmith.
Currently binary packages for _Ubuntu_, _Debian_, _Fedora_, _OpenSuse_, _CentOS_ and
_Arch_ Linux are automatically built. For release version downloads you can also visit
the project's [github releases page](https://github.com/jahnf/Projecteur/releases).

* **Latest release:**
  * on cloudsmith: [![cloudsmith-rel-badge]][cloudsmith-rel-latest]
  * on secondery server: [![projecteur-rel-badge]][projecteur-rel-dl]
* Latest development version:
  * on cloudsmith: [![cloudsmith-dev-badge]][cloudsmith-dev-latest]
  * on secondary server: [![projecteur-dev-badge]][projecteur-dev-dl]

See also the **[list of Linux repositories](./doc/LinuxRepositories.md)** where _Projecteur_
is available.

[cloudsmith-rel-badge]: https://img.shields.io/badge/dynamic/json?color=blue&labelColor=12577e&logo=cloudsmith&label=Projecteur&prefix=v&query=%24.version&url=https%3A%2F%2Fprojecteur.de%2Fdownloads%2Fstable-latest.json
[cloudsmith-rel-latest]: https://cloudsmith.io/~jahnf/repos/projecteur-stable/packages/?q=format%3Araw+tag%3Alatest
[cloudsmith-dev-badge]: https://img.shields.io/badge/dynamic/json?color=blue&labelColor=12577e&logo=cloudsmith&label=Projecteur&prefix=v&query=%24.version&url=https%3A%2F%2Fprojecteur.de%2Fdownloads%2Fdevelop-latest.json
[cloudsmith-dev-latest]: https://cloudsmith.io/~jahnf/repos/projecteur-develop/packages/?q=format%3Araw+tag%3Alatest
[projecteur-rel-badge]: https://img.shields.io/badge/dynamic/json?color=blue&label=Projecteur&prefix=v&query=%24.version&url=https%3A%2F%2Fprojecteur.de%2Fdownloads%2Fstable-latest.json
[projecteur-dev-badge]: https://img.shields.io/badge/dynamic/json?color=blue&label=Projecteur&prefix=v&query=%24.version&url=https%3A%2F%2Fprojecteur.de%2Fdownloads%2Fdevelop-latest.json
[projecteur-dev-dl]: https://projecteur.de/downloads/develop/latest
[projecteur-rel-dl]: https://projecteur.de/downloads/stable/latest

## Building

### Requirements

* C++14 compiler
* CMake 3.6 or later
* Qt 5.7 and later

### Build Example

```sh
    git clone https://github.com/jahnf/Projecteur
    cd Projecteur
    mkdir build && cd build
    cmake ..
    make
```

Building against other Qt versions, than the default one from your Linux distribution
can be done by setting the `QTDIR` variable during CMake configuration.

Example: `QTDIR=/opt/Qt/5.9.6/gcc_64 cmake ..`

## Installation/Running

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
* When building against the Qt version that comes with your distribution's packages,
  you might need to install some  additional QML module packages. For example this
  is the case for Ubuntu, where you need to install the packages
  `qml-module-qtgraphicaleffects`, `qml-module-qtquick-window2`, `qml-modules-qtquick2` and
  `qtdeclarative5-dev` to satisfy the application's run time dependencies.

### Application Menu

The application menu is accessible via the system tray icon. There you will find
the preferences and the menu entry to exit the application. If the system tray icon is missing,
see the [Troubleshooting](#missing-system-tray) section.

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
# intensity=128 and length=0 (only Logitech Spotlight)
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

* Logitech Spotlight 2 via Bluetooth _(046d:b506)_
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

To be able to show transparent windows, a **compositing manager** is necessary. If there is no
compositing manager running you will see the spotlight overlay as an opaque window.

* On **KDE** it might be necessary to turn on Desktop effects to allow transparent windows.
* Depending on your Linux Desktop and configuration there might not be a compositing manager
  running by default. You can run `xcompmgr`, `compton` or others manually.
  * Examples: `xcompmgr -c -t-6 -l-6 -o.1` or `xcompmgr -c`

#### Missing System Tray

_Projecteur_ was developed and tested on GNOME and KDE Desktop environments, but should
work on most other desktop environments. If the system tray with the _Application Menu_
is not showing, commands can be send to the application to bring up the preferences
dialog, test the spotlight, quit the application or set spotlight properties.
See [Command Line Interface](#command-line-interface). There is also a command
line option (`-m`) to prevent the preferences dialog from hiding, allowing it
only to minimize - behaving more like a regular application window.

On some distributions that have a **GNOME Desktop** by default there is
**no system tray extensions** installed (_Fedora_ for example). You can install the
[KStatusNotifierItem/AppIndicator Support][appind-ext] or the [TopIcons Plus][topicon-ext]
GNOME extension to have a system tray that can show the _Projecteur_ tray icon
(and also from other applications like Dropbox or Skype).

[appind-ext]: https://extensions.gnome.org/extension/615/appindicator-support/
[topicon-ext]: https://extensions.gnome.org/extension/1031/topicons/

#### Zoom is not updated while spotlight is shown

Zoom does not update while spotlight is shown due to how the zoom currently works. A screenshot is
taken shortly before the overlay window is shown, and then a magnified section is shown wherever
the mouse/spotlight is.
If the zoom would be updated while the overlay window is shown, the overlay window it self would
show up in the magnified section. That is a general problem that other magnifier tools also face,
although they get around the problem by showing the magnified content rectangle always in the
same position on the screen.

#### Wayland

While not developed with Wayland in mind, users reported _Projecteur_ works with
Wayland. If you experience problems, you can try to set the `QT_QPA_PLATFORM` environment
variable to `wayland`, example:

```bash
user@ubuntu1904:~/Projecteur/build$ QT_QPA_PLATFORM=wayland ./projecteur
Using Wayland-EGL
```

#### Wayland Zoom

On Wayland the Zoom feature is currently only implemented on KDE and GNOME. This is done with
the help of their respective DBus interfaces for screen capturing. On other environments with
Wayland, the zoom feature is not currently supported.

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
