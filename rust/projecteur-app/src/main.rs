mod backend;

use std::{ffi::OsString, fmt::Write as _, fs::File, path::Path};

use cxx_qt_lib::{QAnyStringView, QGuiApplication, QQmlApplicationEngine, QString};
use projecteur_core::{
    Bus,
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices},
    input_event::{EV_KEY, EV_MSC, EV_REL, EV_SYN, InputEvent, read_input_event},
};

fn main() {
    if std::env::args_os().any(|argument| argument == "--device-scan" || argument == "-d") {
        std::process::exit(run_device_scan());
    }
    if let Some(result) = event_monitor_argument(std::env::args_os().skip(1)) {
        std::process::exit(match result {
            Ok(path) => run_event_monitor(Path::new(&path)),
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

fn event_monitor_argument(
    arguments: impl IntoIterator<Item = OsString>,
) -> Option<Result<OsString, String>> {
    let mut arguments = arguments.into_iter();
    while let Some(argument) = arguments.next() {
        if argument == "--event-monitor" {
            return Some(
                arguments
                    .next()
                    .ok_or_else(|| "--event-monitor requires a /dev/input/event path".to_owned()),
            );
        }
        if let Some(argument) = argument.to_str() {
            if let Some(path) = argument.strip_prefix("--event-monitor=") {
                return Some(if path.is_empty() {
                    Err("--event-monitor requires a /dev/input/event path".to_owned())
                } else {
                    Ok(OsString::from(path))
                });
            }
        }
    }
    None
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
    fn parses_event_monitor_argument_forms() {
        assert_eq!(
            event_monitor_argument([
                OsString::from("--event-monitor"),
                OsString::from("/dev/input/event16")
            ]),
            Some(Ok(OsString::from("/dev/input/event16")))
        );
        assert_eq!(
            event_monitor_argument([OsString::from("--event-monitor=/dev/input/event15")]),
            Some(Ok(OsString::from("/dev/input/event15")))
        );
        assert!(matches!(
            event_monitor_argument([OsString::from("--event-monitor")]),
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
}
