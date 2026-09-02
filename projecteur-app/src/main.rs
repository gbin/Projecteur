mod backend;
mod control;
mod screencast;

use std::{ffi::OsString, fmt::Write as _, fs::OpenOptions, path::PathBuf};

use cxx_qt_lib::{QAnyStringView, QQmlApplicationEngine, QString};
use projecteur_core::{
    Bus, SupportedDevice,
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices, scan_devices_with},
    hidpp::{send_vibration, spotlight_device_index},
    parse_additional_device,
};

fn main() {
    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    if let Some(exit_code) = handle_information_options(&arguments) {
        std::process::exit(exit_code);
    }
    let additional_devices = match additional_devices(&arguments) {
        Ok(devices) => devices,
        Err(error) => {
            eprintln!("projecteur: {error}");
            std::process::exit(2);
        }
    };
    if arguments
        .iter()
        .any(|argument| argument == "--device-scan" || argument == "-d")
    {
        std::process::exit(run_device_scan(&additional_devices));
    }
    if let Some(command) = parse_vibration_command(&arguments) {
        std::process::exit(run_vibration(&command));
    }
    match control::forward_to_running(&arguments) {
        Ok(true) => return,
        Ok(false) => {}
        Err(error) => eprintln!("projecteur: {error}"),
    }
    cxx_qt::init_crate!(projecteur_app);
    cxx_qt::init_qml_module!("org.projecteur");

    let mut application = backend::ffi::create_widget_application();
    application
        .pin_mut()
        .set_application_name(&QString::from("Projecteur"));
    application
        .pin_mut()
        .set_application_display_name(&QString::from("Projecteur"));
    application
        .pin_mut()
        .set_application_version(&QString::from(env!("CARGO_PKG_VERSION")));
    application
        .pin_mut()
        .set_organization_domain(&QString::from("projecteur.org"));
    application
        .pin_mut()
        .set_organization_name(&QString::from("Projecteur"));
    backend::ffi::setup_application_metadata();
    backend::ffi::setup_quick_style();
    backend::ffi::setup_global_shortcuts();

    let mut engine = QQmlApplicationEngine::new();
    let _creation_failure = engine.pin_mut().on_object_creation_failed(|_, url| {
        eprintln!("projecteur: failed to load QML module from {url:?}");
    });
    eprintln!("projecteur: loading QML module");
    backend::ffi::load_qml_module(
        engine.pin_mut(),
        QAnyStringView::from("org.projecteur"),
        QAnyStringView::from("Main"),
    );
    eprintln!("projecteur: QML load call completed");

    let exit_code = application.pin_mut().exec();
    // Destroy the QML backend while QApplication and the session bus are still
    // alive so temporary desktop-effect changes can be restored cleanly.
    drop(engine);
    drop(application);
    std::process::exit(exit_code);
}

fn handle_information_options(arguments: &[OsString]) -> Option<i32> {
    if arguments
        .iter()
        .any(|argument| argument == "--help" || argument == "-h" || argument == "--help-all")
    {
        print!(
            r"Projecteur {version}
A virtual laser pointer and live magnifier for KDE Plasma.

Usage: projecteur [OPTIONS]

Options:
  -h, --help                         Show command line usage
      --help-all                     Show usage including configurable properties
  -v, --version                      Print application version
  -f, --fullversion                  Print detailed version information
      --cfg FILE                     Use a custom configuration file
  -d, --device-scan                  Print supported presenter devices
  -D, --additional-device ID[:NAME]  Accept an additional USB presenter
  -c, --command COMMAND              Apply a command or property
      --disable-uinput                Disable presenter button forwarding
      --show-dialog                   Show preferences on start
      --hide-systray-icon             Hide the fallback tray icon
      --disable-overlay               Disable the spotlight overlay

Commands: quit, spot=on|off|toggle, settings=show|hide, preset=NAME,
          preset.next, preset.previous, vibrate[=INTENSITY,LENGTH]
",
            version = env!("CARGO_PKG_VERSION")
        );
        if arguments.iter().any(|argument| argument == "--help-all") {
            println!(
                "\nProperties: spot.size, spot.size.adjust, spot.rotation, spot.shape,\n\
                 spot.shape.square.radius, spot.shape.star.points,\n\
                 spot.shape.star.innerradius, spot.shape.ngon.sides,\n\
                 spot.multi-screen, spot.overlay, shade, shade.opacity, shade.color,\n\
                 dot, dot.size, dot.color, dot.opacity, dot.mode, dot.trail,\n\
                 border, border.size, border.color, border.opacity, zoom, zoom.factor,\n\
                 zoom.mode"
            );
        }
        return Some(0);
    }
    if arguments
        .iter()
        .any(|argument| argument == "--fullversion" || argument == "-f")
    {
        println!("Projecteur {}", env!("CARGO_PKG_VERSION"));
        println!("  - backend: Rust");
        println!("  - target: {}", std::env::consts::ARCH);
        println!("  - operating-system: {}", std::env::consts::OS);
        return Some(0);
    }
    if arguments
        .iter()
        .any(|argument| argument == "--version" || argument == "-v")
    {
        println!("Projecteur {}", env!("CARGO_PKG_VERSION"));
        return Some(0);
    }
    None
}

fn additional_devices(arguments: &[OsString]) -> Result<Vec<SupportedDevice>, String> {
    let mut devices = Vec::new();
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].to_string_lossy();
        let value = if argument == "-D" || argument == "--additional-device" {
            index += 1;
            Some(
                arguments
                    .get(index)
                    .ok_or_else(|| format!("{argument} requires VENDOR:PRODUCT[:NAME]"))?
                    .to_string_lossy(),
            )
        } else {
            argument
                .strip_prefix("--additional-device=")
                .map(std::borrow::Cow::Borrowed)
        };
        if let Some(value) = value {
            devices.push(parse_additional_device(&value).map_err(|error| error.to_string())?);
        }
        index += 1;
    }
    Ok(devices)
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct VibrationCommand {
    intensity: u8,
    length: u8,
    presenter: Option<PathBuf>,
}

fn parse_vibration_command(arguments: &[OsString]) -> Option<VibrationCommand> {
    let mut value = None;
    let mut presenter = None;
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].to_str()?;
        if argument == "--vibrate" {
            value = Some("");
        } else if let Some(command) = argument.strip_prefix("--vibrate=") {
            value = Some(command);
        } else if (argument == "-c" || argument == "--command")
            && arguments.get(index + 1).is_some()
        {
            index += 1;
            let command = arguments[index].to_str()?;
            if command == "vibrate" {
                value = Some("");
            } else if let Some(command) = command.strip_prefix("vibrate=") {
                value = Some(command);
            }
        } else if argument == "--presenter" {
            index += 1;
            presenter = arguments.get(index).map(PathBuf::from);
        } else if let Some(path) = argument.strip_prefix("--presenter=") {
            presenter = Some(PathBuf::from(path));
        }
        index += 1;
    }

    let value = value?;
    let mut fields = value.split(',');
    let intensity = parse_clamped(fields.next(), 128, 255);
    let length = parse_clamped(fields.next(), 0, 10);
    Some(VibrationCommand {
        intensity,
        length,
        presenter,
    })
}

fn parse_clamped(value: Option<&str>, default: u8, maximum: u8) -> u8 {
    value
        .filter(|value| !value.is_empty())
        .and_then(|value| value.parse::<i32>().ok())
        .map_or(default, |value| {
            u8::try_from(value.clamp(0, i32::from(maximum))).unwrap_or(maximum)
        })
}

fn run_vibration(command: &VibrationCommand) -> i32 {
    let devices = match scan_devices() {
        Ok(devices) => devices,
        Err(error) => {
            eprintln!("projecteur: {error}");
            return 1;
        }
    };
    let selected = devices.iter().find_map(|device| {
        let device_index = spotlight_device_index(device.id)?;
        let node = device.nodes.iter().find(|node| {
            node.kind == DeviceNodeKind::Hidraw
                && node.readable
                && node.writable
                && command
                    .presenter
                    .as_ref()
                    .is_none_or(|path| node.path == *path)
        })?;
        Some((device_index, node.path.clone()))
    });
    let Some((device_index, path)) = selected else {
        eprintln!("projecteur: no writable Spotlight HID++ device found");
        return 1;
    };
    let Ok(mut device) = OpenOptions::new().read(true).write(true).open(&path) else {
        eprintln!(
            "projecteur: cannot open {} for HID++ commands",
            path.display()
        );
        return 1;
    };
    match send_vibration(
        &mut device,
        device_index,
        command.intensity,
        command.length,
        |_| {},
    ) {
        Ok(protocol) => {
            println!(
                "Sent vibration at intensity {} through {protocol:?} on {}.",
                command.intensity,
                path.display()
            );
            0
        }
        Err(error) => {
            eprintln!(
                "projecteur: vibration failed on {}: {error}",
                path.display()
            );
            1
        }
    }
}

fn run_device_scan(additional_devices: &[SupportedDevice]) -> i32 {
    match scan_devices_with(additional_devices) {
        Ok(devices) => {
            print!("{}", format_device_scan(&devices));
            0
        }
        Err(error) => {
            eprintln!("projecteur: {error}");
            1
        }
    }
}

fn format_device_scan(devices: &[DiscoveredDevice]) -> String {
    if devices.is_empty() {
        return "No supported presenter devices found.\n".to_owned();
    }

    let mut output = format!("Found {} supported presenter device(s):\n", devices.len());
    for device in devices {
        let bus = match device.id.bus {
            Bus::Usb => "USB",
            Bus::Bluetooth => "Bluetooth",
        };
        let _ = writeln!(
            output,
            "- {} [{:04x}:{:04x}, {bus}]",
            device.display_name(),
            device.id.vendor,
            device.id.product
        );
        if !device.physical_id.is_empty() {
            let _ = writeln!(output, "  physical-id: {}", device.physical_id);
        }
        if device.nodes.is_empty() {
            output.push_str("  no event or hidraw nodes found\n");
        }
        for node in &device.nodes {
            let kind = match node.kind {
                DeviceNodeKind::Event => "event",
                DeviceNodeKind::Hidraw => "hidraw",
            };
            let relative = if node.kind == DeviceNodeKind::Event {
                if node.has_relative_pointer {
                    ", relative-pointer=yes"
                } else {
                    ", relative-pointer=no"
                }
            } else {
                ""
            };
            let _ = writeln!(
                output,
                "  {} ({kind}, readable={}, writable={}{relative})",
                node.path.display(),
                node.readable,
                node.writable
            );
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use projecteur_core::{DeviceId, device_scan::DeviceNode};

    use super::*;

    #[test]
    fn formats_an_empty_scan() {
        assert_eq!(
            format_device_scan(&[]),
            "No supported presenter devices found.\n"
        );
    }

    #[test]
    fn formats_device_identity_nodes_and_permissions() {
        let devices = [DiscoveredDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xc53e,
                bus: Bus::Usb,
            },
            physical_id: "usb-1".to_owned(),
            kernel_name: "Kernel name".to_owned(),
            configured_name: "Logitech Spotlight (USB)".to_owned(),
            nodes: vec![DeviceNode {
                path: PathBuf::from("/dev/input/event4"),
                kind: DeviceNodeKind::Event,
                has_relative_pointer: true,
                readable: true,
                writable: false,
            }],
        }];

        let output = format_device_scan(&devices);
        assert!(output.contains("Logitech Spotlight (USB) [046d:c53e, USB]"));
        assert!(output.contains("physical-id: usb-1"));
        assert!(output.contains("relative-pointer=yes"));
        assert!(output.contains("readable=true, writable=false"));
    }

    #[test]
    fn parses_convenience_and_legacy_vibration_commands() {
        assert_eq!(
            parse_vibration_command(&[OsString::from("--vibrate=300,20")]),
            Some(VibrationCommand {
                intensity: 255,
                length: 10,
                presenter: None,
            })
        );
        assert_eq!(
            parse_vibration_command(&[
                OsString::from("-c"),
                OsString::from("vibrate=64,3"),
                OsString::from("--presenter=/dev/hidraw5"),
            ]),
            Some(VibrationCommand {
                intensity: 64,
                length: 3,
                presenter: Some(PathBuf::from("/dev/hidraw5")),
            })
        );
        assert_eq!(
            parse_vibration_command(&[OsString::from("--vibrate")])
                .unwrap()
                .intensity,
            128
        );
    }
}
