mod backend;

use std::fmt::Write as _;

use cxx_qt_lib::{QAnyStringView, QGuiApplication, QQmlApplicationEngine, QString};
use projecteur_core::{
    Bus,
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices},
};

fn main() {
    if std::env::args_os().any(|argument| argument == "--device-scan" || argument == "-d") {
        std::process::exit(run_device_scan());
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
}
