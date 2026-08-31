# Projecteur user guide

This guide covers the controls you are likely to set once and rely on during
every presentation. For installation and the product overview, start with the
[README](../README.md).

## Everyday workflow

Launch Projecteur from the application menu. Its Plasma system tray applet shows
connected presenters and provides quick access to:

- the current spotlight preset;
- **Test Spotlight**;
- presentation timer controls;
- preferences, About, and Quit.

Connection, battery, access-error, and timer notifications use Plasma's native
notification system. Customize them under **System Settings → Notifications →
Applications → Projecteur**.

Projecteur stores its settings in `~/.config/projecteurrc`.

## Pointer modes: Spotlight & Laser Pointer

Projecteur supports two primary on-screen pointer modes:

1. **Spotlight Mode**: Dims the background display and highlights the area of interest using configurable shapes (circle, square, star, ngon), borders, center dot, and live KPipeWire magnification.
2. **Laser Pointer Mode**: Projects a crisp, vibrant digital laser dot with an optical diode hotspot core and an optional glowing halo / aura. Does not dim the screen or hide background elements.

Switch modes easily from the Plasma applet menu, **Preferences → Active Pointer Mode**, global shortcuts, presenter button actions, or CLI/D-Bus commands.

## Spotlight and presets

Under **Preferences → Spotlight**, you can configure:

- spotlight size, shape, and rotation;
- shade color and opacity;
- center dot and border;
- cursor appearance;
- zoom level and content type;
- multi-screen behavior.

Save combinations as presets when different situations need different treatment:
for example, a small dot for slides, a large text magnifier for a code demo, a
glowing red laser pointer for annotations, and a borderless highlight for video.
Presets remember whether they are configured for Spotlight or Laser Pointer mode.

Presets are ordered alphabetically when Projecteur starts. Prefix names with
numbers if you want a fixed cycle order, such as `1 Slides`, `2 Demo`, and
`3 Questions`.

## Laser pointer customization

Under **Preferences → Laser Pointer**, you can configure:

- **Laser Dot**: Dot size (in pixels), color (any RGB/HTML color), and opacity.
- **Laser Glow / Halo**: Enable or disable the outer soft glowing aura, glow size, glow color, and glow opacity.
- **Laser Trail Mark**: Leave an optical fading persistence trace behind pointer movement for gesturing, circling, and underlining slides. Configure trail duration (100–2000 ms), stroke width (1–30 px), color, and opacity.

## Live zoom modes

Zoom uses a low-latency stream from KWin through KPipeWire. Videos, animations,
and other changing desktop content continue updating inside the magnifier.

| Mode | Best for | Behavior |
| --- | --- | --- |
| **Smooth (images)** | Photographs, video, gradients, and mixed content | Bilinear filtering produces continuous tones and few scaling artifacts. |
| **Text and UI** | Documents, terminals, diagrams, and application controls | Smooth scaling plus bounded edge enhancement makes interface details easier to read. |
| **Pixel-perfect** | Pixel art, source-pixel inspection, and debugging | Nearest-neighbor scaling preserves captured pixel values; text may look blocky. |

The selected mode is stored in each preset. **Text and UI** improves the captured
raster; it cannot recover font outlines or rerender text as vectors.

Live zoom requires Projecteur to be installed. KWin uses the installed desktop
metadata to authorize its restricted capture interfaces, so a binary launched
only from the build directory cannot use the normal zoom path.

## Global shortcuts and device-free use

Projecteur registers native KDE global actions for:

- toggling the spotlight (`toggle_spotlight`);
- toggling the laser pointer (`toggle_laser`);
- toggling between pointer modes (`toggle_pointer_mode`);
- opening preferences (`show_preferences`);
- starting or resetting the presentation timer (`start_restart_timer`, `reset_timer`);
- selecting the next or previous preset (`next_preset`, `previous_preset`).

No key combinations are assigned by default. Set them under **Preferences →
Shortcuts** or **System Settings → Keyboard → Shortcuts → Projecteur**.

This also makes Projecteur useful without presenter hardware: assign **Toggle
Spotlight** or **Toggle Laser Pointer**, then use it while sharing your screen in a meeting or recording a
demo.

## Presentation timer

The system tray applet can start the timer immediately or arm it for the next
presenter button press. While it runs, the panel icon shows the remaining
minutes and its tooltip shows the precise countdown.

When time expires, compatible presenters—including Logitech Spotlight models—
can provide configurable haptic feedback.

## Button mapping

Projecteur can map device input to:

- a keyboard sequence;
- toggling the spotlight overlay;
- toggling the laser pointer;
- toggling between pointer modes (Spotlight / Laser);
- cycling or selecting presets;
- vertical or horizontal scrolling;
- volume control;
- other built-in presentation actions.

Keyboard sequences are especially useful for presentation software shortcuts.
Projecteur grabs presenter events and forwards unmapped input through a virtual
uinput device. Starting with `--disable-uinput` disables both event grabbing and
button mapping.

### Logitech hold gestures

Logitech Spotlight devices distinguish three interactions for the Next and Back
buttons:

1. tap;
2. long press;
3. hold while moving the presenter.

On the Devices page in Preferences, record taps and long presses directly. To
map hold-and-move, wake the presenter with any button, right-click the input
sequence column, and choose the relevant hold-and-move input.

Avoid mapping both long press and hold-and-move on the same button unless you
want both actions to run when the button is held during movement.

## Command-line control

Projecteur can control an already running instance from scripts. Common examples:

```sh
projecteur --command spot=toggle
projecteur --command laser=toggle
projecteur --command pointer=laser
projecteur --command pointer=toggle
projecteur --command laser.size.adjust=+5
projecteur --command settings=show
projecteur --command preset="2 Demo"
```

Run `projecteur --help-all` or `man projecteur` for the complete command and
property reference.
