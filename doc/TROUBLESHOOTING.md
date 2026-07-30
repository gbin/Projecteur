# Troubleshooting Projecteur

Start with the checks below. If you still need help, open an issue in the
[Projecteur-kde issue tracker](https://github.com/gbin/Projecteur-kde/issues) and
include:

```sh
projecteur --fullversion
projecteur --device-scan
```

Also mention your Linux distribution, Plasma version, Qt version, presenter
model, and whether it is connected over USB or Bluetooth.

## Confirm the supported desktop

This edition requires KDE Plasma on Wayland. Check the session:

```sh
printf '%s\n' "$XDG_CURRENT_DESKTOP"
printf '%s\n' "$XDG_SESSION_TYPE"
```

The session type must report `wayland`. Do not force
`QT_QPA_PLATFORM=xcb`; X11 is not supported by this fork.

## Presenter is not connected

Ask Projecteur to list supported devices and their access state:

```sh
projecteur --device-scan
```

If the presenter is detected but is not readable or writable, reload the
installed udev rules and reconnect it:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger
```

You can also confirm that Linux sees a Logitech input device:

```sh
grep -A 5 "Vendor=046d" /proc/bus/input/devices
```

For the relevant `/dev/input/eventN` path, check access:

```sh
test -r /dev/input/eventN && echo readable || echo not-readable
test -w /dev/input/eventN && echo writable || echo not-writable
```

Conflicting hand-written and package-installed Projecteur rules can produce
surprising permissions. Check for duplicate `55-projecteur.rules` files under
`/etc/udev/rules.d`, `/run/udev/rules.d`, `/usr/lib/udev/rules.d`, and
`/lib/udev/rules.d`.

## Zoom does not work

Live zoom requires all of the following:

- KDE Plasma on Wayland;
- KWin's restricted screencast protocol;
- KPipeWire;
- an installed Projecteur desktop entry that matches the running executable.

Install Projecteur instead of launching only `build/projecteur`. KWin authorizes
the capture interface using the installed
`org.projecteur.Projecteur.desktop` metadata and can reject an executable from an
arbitrary build path.

Projecteur uses KWin's low-latency stream for normal live zoom and falls back to
KWin's screenshot interface when streaming is unavailable. KWin excludes
Projecteur's own windows from capture to prevent recursive overlay images.

## Spotlight is opaque

Transparency requires the Plasma Wayland compositor. Confirm that
`XDG_SESSION_TYPE` is `wayland` and that the Projecteur log reports the Wayland
Qt platform plugin.

## System tray applet is missing

Confirm that Projecteur is running, then open the Plasma system tray
configuration and enable the Projecteur entry. Plasma owns the popup placement
and visibility.

If the application was just upgraded, restart Plasma Shell or sign out and back
in so the updated applet package is loaded.

## Reset the configuration

Projecteur stores its KDE configuration in:

```text
~/.config/projecteurrc
```

To test with clean settings without destroying your existing configuration,
start Projecteur with a separate file:

```sh
projecteur --cfg /tmp/projecteur-clean-test.rc --show-dialog
```
