mod backend;

use std::{
    ffi::OsString,
    fmt::Write as _,
    fs::File,
    io::{self, Read},
    path::Path,
};

use cxx_qt_lib::{QAnyStringView, QGuiApplication, QQmlApplicationEngine, QString};
use projecteur_core::{
    Bus,
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices},
    hid_report::{PresenterReport, decode_presenter_report},
    input_event::{EV_KEY, EV_MSC, EV_REL, EV_SYN, InputEvent, read_input_event},
    uinput::{GrabbedEventDevice, VirtualKeyboard},
};

fn main() {
    if std::env::args_os().any(|argument| argument == "--device-scan" || argument == "-d") {
        std::process::exit(run_device_scan());
    }
    if let Some(result) = path_argument(
        std::env::args_os().skip(1),
        "--event-monitor",
        "a /dev/input/event path",
    ) {
        std::process::exit(match result {
            Ok(path) => run_event_monitor(Path::new(&path)),
            Err(message) => {
                eprintln!("projecteur-rs: {message}");
                2
            }
        });
    }
    if let Some(result) = path_argument(
        std::env::args_os().skip(1),
        "--hidraw-monitor",
        "a /dev/hidraw path",
    ) {
        std::process::exit(match result {
            Ok(path) => run_hidraw_monitor(Path::new(&path)),
            Err(message) => {
                eprintln!("projecteur-rs: {message}");
                2
            }
        });
    }
    if let Some(result) = path_argument(
        std::env::args_os().skip(1),
        "--event-forward",
        "a /dev/input/event path",
    ) {
        std::process::exit(match result {
            Ok(path) => run_event_forward(Path::new(&path)),
            Err(message) => {
                eprintln!("projecteur-rs: {message}");
                2
            }
        });
    }

    cxx_qt::init_crate!(projecteur_app);
    cxx_qt::init_qml_module!("org.projecteur.rust");

    let mut application = QGuiApplication::new();
    application
        .pin_mut()
        .set_application_name(&QString::from("projecteur-rs"));
    application
        .pin_mut()
        .set_application_display_name(&QString::from("Projecteur (Rust port)"));
    application
        .pin_mut()
        .set_application_version(&QString::from(env!("CARGO_PKG_VERSION")));
    application
        .pin_mut()
        .set_organization_domain(&QString::from("projecteur.org"));
    application
        .pin_mut()
        .set_organization_name(&QString::from("Projecteur"));

    let mut engine = QQmlApplicationEngine::new();
    let _creation_failure = engine.pin_mut().on_object_creation_failed(|_, url| {
        eprintln!("projecteur-rs: failed to load QML module from {url:?}");
    });
    eprintln!("projecteur-rs: loading QML module");
    backend::ffi::load_qml_module(
        engine.pin_mut(),
        QAnyStringView::from("org.projecteur.rust"),
        QAnyStringView::from("Main"),
    );
    eprintln!("projecteur-rs: QML load call completed");

    std::process::exit(application.pin_mut().exec());
}

fn path_argument(
    arguments: impl IntoIterator<Item = OsString>,
    option: &str,
    expected: &str,
) -> Option<Result<OsString, String>> {
    let mut arguments = arguments.into_iter();
    while let Some(argument) = arguments.next() {
        if argument == option {
            return Some(
                arguments
                    .next()
                    .ok_or_else(|| format!("{option} requires {expected}")),
            );
        }
        if let Some(argument) = argument.to_str() {
            if let Some(path) = argument
                .strip_prefix(option)
                .and_then(|value| value.strip_prefix('='))
            {
                return Some(if path.is_empty() {
                    Err(format!("{option} requires {expected}"))
                } else {
                    Ok(OsString::from(path))
                });
            }
        }
    }
    None
}

fn run_hidraw_monitor(path: &Path) -> i32 {
    let mut device = match File::open(path) {
        Ok(device) => device,
        Err(error) => {
            eprintln!("projecteur-rs: cannot open {}: {error}", path.display());
            return 1;
        }
    };
    println!(
        "Monitoring {} without writing HID commands; press Ctrl-C to stop.",
        path.display()
    );
    let mut report = [0_u8; 256];
    loop {
        match device.read(&mut report) {
            Ok(0) => {
                eprintln!("projecteur-rs: {} reached end of file", path.display());
                return 1;
            }
            Ok(length) => println!(
                "HIDRAW {length:3}: {}",
                format_hid_report(&report[..length])
            ),
            Err(error) if error.kind() == io::ErrorKind::Interrupted => {}
            Err(error) => {
                eprintln!("projecteur-rs: cannot read {}: {error}", path.display());
                return 1;
            }
        }
    }
}

fn format_hex_report(report: &[u8]) -> String {
    let mut output = String::with_capacity(report.len().saturating_mul(3));
    for (index, byte) in report.iter().enumerate() {
        if index > 0 {
            output.push(' ');
        }
        let _ = write!(output, "{byte:02x}");
    }
    output
}

fn format_hid_report(report: &[u8]) -> String {
    match decode_presenter_report(report) {
        Ok(PresenterReport::Pointer(pointer)) => format!(
            "pointer x={:+5} y={:+5} buttons={:#04x} wheel={:+4} pan={:+4}",
            pointer.x, pointer.y, pointer.buttons, pointer.wheel, pointer.pan
        ),
        Ok(PresenterReport::Keyboard(keyboard)) => {
            let usages = keyboard
                .usages
                .into_iter()
                .filter(|usage| *usage != 0)
                .map(|usage| format!("{usage:#04x}"))
                .collect::<Vec<_>>()
                .join(",");
            format!(
                "keyboard modifiers={:#04x} usages=[{usages}]",
                keyboard.modifiers
            )
        }
        Err(_) => format_hex_report(report),
    }
}

fn run_event_monitor(path: &Path) -> i32 {
    let mut device = match File::open(path) {
        Ok(device) => device,
        Err(error) => {
            eprintln!("projecteur-rs: cannot open {}: {error}", path.display());
            return 1;
        }
    };
    println!(
        "Monitoring {} without an exclusive grab; press Ctrl-C to stop.",
        path.display()
    );
    loop {
        match read_input_event(&mut device) {
            Ok(event) => println!("{}", format_input_event(event)),
            Err(error) => {
                eprintln!("projecteur-rs: cannot read {}: {error}", path.display());
                return 1;
            }
        }
    }
}

fn run_event_forward(path: &Path) -> i32 {
    let mut keyboard = match VirtualKeyboard::create() {
        Ok(keyboard) => keyboard,
        Err(error) => {
            eprintln!("projecteur-rs: cannot create virtual keyboard through /dev/uinput: {error}");
            return 1;
        }
    };
    let mut source = match GrabbedEventDevice::open(path) {
        Ok(source) => source,
        Err(error) => {
            eprintln!(
                "projecteur-rs: cannot exclusively grab {}: {error}",
                path.display()
            );
            return 1;
        }
    };
    println!(
        "Forwarding {} through a virtual keyboard under an exclusive grab; press Ctrl-C to stop.",
        path.display()
    );
    loop {
        let event = match source.read_event() {
            Ok(event) => event,
            Err(error) => {
                eprintln!("projecteur-rs: cannot read {}: {error}", path.display());
                return 1;
            }
        };
        if let Err(error) = keyboard.emit(event) {
            eprintln!("projecteur-rs: cannot forward input event: {error}");
            return 1;
        }
    }
}

fn format_input_event(event: InputEvent) -> String {
    let event_type = match event.event_type {
        EV_SYN => "SYN",
        EV_KEY => "KEY",
        EV_REL => "REL",
        EV_MSC => "MSC",
        _ => "OTHER",
    };
    let code = match (event.event_type, event.code) {
        (EV_SYN, 0) => "REPORT",
        (EV_REL, 0) => "X",
        (EV_REL, 1) => "Y",
        (EV_REL, 8) => "WHEEL",
        (EV_KEY, 104) => "PAGE_UP",
        (EV_KEY, 105) => "LEFT",
        (EV_KEY, 106) => "RIGHT",
        (EV_KEY, 109) => "PAGE_DOWN",
        _ => "",
    };
    if code.is_empty() {
        format!("{event_type} code={} value={}", event.code, event.value)
    } else {
        format!("{event_type} {code} value={}", event.value)
    }
}

fn run_device_scan() -> i32 {
    match scan_devices() {
        Ok(devices) => {
            print!("{}", format_device_scan(&devices));
            0
        }
        Err(error) => {
            eprintln!("projecteur-rs: {error}");
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
    fn parses_monitor_argument_forms() {
        assert_eq!(
            path_argument(
                [
                    OsString::from("--event-monitor"),
                    OsString::from("/dev/input/event16")
                ],
                "--event-monitor",
                "an event path"
            ),
            Some(Ok(OsString::from("/dev/input/event16")))
        );
        assert_eq!(
            path_argument(
                [OsString::from("--hidraw-monitor=/dev/hidraw5")],
                "--hidraw-monitor",
                "a hidraw path"
            ),
            Some(Ok(OsString::from("/dev/hidraw5")))
        );
        assert_eq!(
            path_argument(
                [OsString::from("--event-monitor=/dev/input/event15")],
                "--event-monitor",
                "an event path"
            ),
            Some(Ok(OsString::from("/dev/input/event15")))
        );
        assert_eq!(
            path_argument(
                [OsString::from("--event-forward=/dev/input/event15")],
                "--event-forward",
                "an event path"
            ),
            Some(Ok(OsString::from("/dev/input/event15")))
        );
        assert!(matches!(
            path_argument(
                [OsString::from("--event-monitor")],
                "--event-monitor",
                "an event path"
            ),
            Some(Err(_))
        ));
    }

    #[test]
    fn formats_known_and_unknown_input_events() {
        assert_eq!(
            format_input_event(InputEvent {
                event_type: EV_REL,
                code: 0,
                value: -3,
            }),
            "REL X value=-3"
        );
        assert_eq!(
            format_input_event(InputEvent {
                event_type: EV_KEY,
                code: 999,
                value: 1,
            }),
            "KEY code=999 value=1"
        );
    }

    #[test]
    fn formats_raw_hid_reports_as_hex() {
        assert_eq!(format_hex_report(&[0x20, 0xff, 0x01]), "20 ff 01");
        assert!(format_hex_report(&[]).is_empty());
    }

    #[test]
    fn formats_decoded_presenter_reports() {
        assert_eq!(
            format_hid_report(&[0x02, 0, 0, 0x0d, 0x60, 0, 0, 0]),
            "pointer x=  +13 y=   +6 buttons=0x00 wheel=  +0 pan=  +0"
        );
        assert_eq!(
            format_hid_report(&[0x01, 0, 0x4f, 0, 0, 0, 0, 0]),
            "keyboard modifiers=0x00 usages=[0x4f]"
        );
    }
}
