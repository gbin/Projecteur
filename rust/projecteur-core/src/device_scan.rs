//! Read-only discovery of supported Linux HID presenter devices.

use std::{
    collections::BTreeMap,
    error::Error,
    fmt, fs,
    fs::OpenOptions,
    io,
    path::{Path, PathBuf},
};

use crate::{Bus, DeviceId, all_supported_devices};

/// Linux device-node role discovered below a HID device.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum DeviceNodeKind {
    /// Linux input-event node used for buttons and relative pointer movement.
    Event,
    /// Raw HID node used for Logitech HID++ communication.
    Hidraw,
}

/// One device node belonging to a supported presenter.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DeviceNode {
    pub path: PathBuf,
    pub kind: DeviceNodeKind,
    pub has_relative_pointer: bool,
    pub readable: bool,
    pub writable: bool,
}

/// A supported presenter found in the Linux HID tree.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiscoveredDevice {
    pub id: DeviceId,
    pub physical_id: String,
    pub kernel_name: String,
    pub configured_name: String,
    pub nodes: Vec<DeviceNode>,
}

impl DiscoveredDevice {
    /// Prefer Projecteur's maintained model name over the kernel-provided name.
    #[must_use]
    pub fn display_name(&self) -> &str {
        if self.configured_name.is_empty() {
            &self.kernel_name
        } else {
            &self.configured_name
        }
    }
}

/// Failure to enumerate the Linux HID tree.
#[derive(Debug)]
pub struct DeviceScanError {
    path: PathBuf,
    source: io::Error,
}

impl fmt::Display for DeviceScanError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "cannot scan HID devices below {}: {}",
            self.path.display(),
            self.source
        )
    }
}

impl Error for DeviceScanError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        Some(&self.source)
    }
}

/// Discover supported presenters from the host's standard Linux paths.
///
/// This is read-only: it checks whether device nodes can be opened but does not
/// grab an input device or issue HID commands.
///
/// # Errors
///
/// Returns [`DeviceScanError`] when `/sys/bus/hid/devices` cannot be listed.
pub fn scan_devices() -> Result<Vec<DiscoveredDevice>, DeviceScanError> {
    scan_devices_below(Path::new("/sys/bus/hid/devices"), Path::new("/dev"))
}

/// Discover supported presenters below explicit sysfs and device roots.
///
/// This entry point supports deterministic tests and diagnostic tools that
/// operate on a mounted filesystem snapshot.
///
/// # Errors
///
/// Returns [`DeviceScanError`] when `hid_root` cannot be listed.
pub fn scan_devices_below(
    hid_root: &Path,
    device_root: &Path,
) -> Result<Vec<DiscoveredDevice>, DeviceScanError> {
    let entries = fs::read_dir(hid_root).map_err(|source| DeviceScanError {
        path: hid_root.to_owned(),
        source,
    })?;
    let supported = all_supported_devices();
    let mut devices = Vec::<DiscoveredDevice>::new();

    for entry in entries.flatten() {
        let hid_path = entry.path();
        let Ok(properties) = read_properties(&hid_path.join("uevent")) else {
            continue;
        };
        let Some(id) = properties
            .get("HID_ID")
            .and_then(|value| parse_hid_id(value))
        else {
            continue;
        };
        let Some(model) = supported.iter().find(|model| model.id == id) else {
            continue;
        };
        let physical_id = properties
            .get("HID_PHYS")
            .and_then(|value| value.split('/').next())
            .unwrap_or_default()
            .to_owned();
        let device_index = devices
            .iter()
            .position(|device| device.id == id && device.physical_id == physical_id);
        let device = if let Some(index) = device_index {
            &mut devices[index]
        } else {
            devices.push(DiscoveredDevice {
                id,
                physical_id,
                kernel_name: properties.get("HID_NAME").cloned().unwrap_or_default(),
                configured_name: model.name.clone(),
                nodes: Vec::new(),
            });
            let Some(device) = devices.last_mut() else {
                continue;
            };
            device
        };

        scan_event_nodes(&hid_path, device_root, &mut device.nodes);
        scan_hidraw_nodes(&hid_path, device_root, &mut device.nodes);
    }

    for device in &mut devices {
        device
            .nodes
            .sort_by(|left, right| (left.kind, &left.path).cmp(&(right.kind, &right.path)));
        device.nodes.dedup_by(|left, right| left.path == right.path);
    }
    devices.sort_by(|left, right| {
        (left.id.vendor, left.id.product, &left.physical_id).cmp(&(
            right.id.vendor,
            right.id.product,
            &right.physical_id,
        ))
    });
    Ok(devices)
}

fn parse_hid_id(value: &str) -> Option<DeviceId> {
    let mut fields = value.split(':');
    let bus = match u16::from_str_radix(fields.next()?, 16).ok()? {
        0x0003 => Bus::Usb,
        0x0005 => Bus::Bluetooth,
        _ => return None,
    };
    let vendor = u32::from_str_radix(fields.next()?, 16).ok()?;
    let product = u32::from_str_radix(fields.next()?, 16).ok()?;
    Some(DeviceId {
        vendor: u16::try_from(vendor).ok()?,
        product: u16::try_from(product).ok()?,
        bus,
    })
}

fn read_properties(path: &Path) -> io::Result<BTreeMap<String, String>> {
    let contents = fs::read_to_string(path)?;
    Ok(contents
        .lines()
        .filter_map(|line| line.split_once('='))
        .map(|(key, value)| (key.to_owned(), value.to_owned()))
        .collect())
}

fn scan_event_nodes(hid_path: &Path, device_root: &Path, nodes: &mut Vec<DeviceNode>) {
    let Ok(inputs) = fs::read_dir(hid_path.join("input")) else {
        return;
    };
    for input in inputs.flatten() {
        let input_path = input.path();
        let has_relative_pointer = supports_relative_pointer(&input_path);
        let Ok(children) = fs::read_dir(&input_path) else {
            continue;
        };
        for child in children.flatten() {
            if !child.file_name().to_string_lossy().starts_with("event") {
                continue;
            }
            let Some(device_name) = read_properties(&child.path().join("uevent"))
                .ok()
                .and_then(|properties| properties.get("DEVNAME").cloned())
            else {
                continue;
            };
            push_node(
                nodes,
                device_root.join(device_name),
                DeviceNodeKind::Event,
                has_relative_pointer,
            );
        }
    }
}

fn scan_hidraw_nodes(hid_path: &Path, device_root: &Path, nodes: &mut Vec<DeviceNode>) {
    let Ok(entries) = fs::read_dir(hid_path.join("hidraw")) else {
        return;
    };
    for entry in entries.flatten() {
        if !entry.file_name().to_string_lossy().starts_with("hidraw") {
            continue;
        }
        let Some(device_name) = read_properties(&entry.path().join("uevent"))
            .ok()
            .and_then(|properties| properties.get("DEVNAME").cloned())
        else {
            continue;
        };
        push_node(
            nodes,
            device_root.join(device_name),
            DeviceNodeKind::Hidraw,
            false,
        );
    }
}

fn push_node(
    nodes: &mut Vec<DeviceNode>,
    path: PathBuf,
    kind: DeviceNodeKind,
    has_relative_pointer: bool,
) {
    let readable = OpenOptions::new().read(true).open(&path).is_ok();
    let writable = OpenOptions::new().write(true).open(&path).is_ok();
    nodes.push(DeviceNode {
        path,
        kind,
        has_relative_pointer,
        readable,
        writable,
    });
}

fn supports_relative_pointer(input_path: &Path) -> bool {
    let read_bitmap = |name: &str| {
        fs::read_to_string(input_path.join("capabilities").join(name))
            .ok()
            .and_then(|value| {
                value
                    .split_ascii_whitespace()
                    .next_back()
                    .and_then(|word| u64::from_str_radix(word, 16).ok())
            })
            .unwrap_or(0)
    };
    let event_types = read_bitmap("ev");
    let relative_axes = read_bitmap("rel");
    event_types & (1 << 2) != 0 && relative_axes & (1 << 0) != 0 && relative_axes & (1 << 1) != 0
}

#[cfg(test)]
mod tests {
    use std::sync::atomic::{AtomicUsize, Ordering};

    use super::*;

    static NEXT_TREE: AtomicUsize = AtomicUsize::new(0);

    struct TestTree {
        root: PathBuf,
    }

    impl TestTree {
        fn new() -> Self {
            let root = std::env::temp_dir().join(format!(
                "projecteur-device-scan-{}-{}",
                std::process::id(),
                NEXT_TREE.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&root).unwrap();
            Self { root }
        }

        fn write(&self, relative: &str, contents: &str) {
            let path = self.root.join(relative);
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(path, contents).unwrap();
        }
    }

    impl Drop for TestTree {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.root).unwrap();
        }
    }

    #[test]
    fn discovers_and_merges_event_and_hidraw_nodes() {
        let tree = TestTree::new();
        tree.write(
            "sys/0003:0000046D:0000C53E/uevent",
            "HID_ID=0003:0000046D:0000C53E\nHID_NAME=Kernel Spotlight\nHID_PHYS=usb-1/input0\n",
        );
        tree.write(
            "sys/0003:0000046D:0000C53E/input/input4/event4/uevent",
            "DEVNAME=input/event4\n",
        );
        tree.write(
            "sys/0003:0000046D:0000C53E/input/input4/capabilities/ev",
            "17\n",
        );
        tree.write(
            "sys/0003:0000046D:0000C53E/input/input4/capabilities/rel",
            "3\n",
        );
        tree.write(
            "sys/0003:0000046D:0000C53E/hidraw/hidraw2/uevent",
            "DEVNAME=hidraw2\n",
        );
        tree.write("dev/input/event4", "");
        tree.write("dev/hidraw2", "");

        let devices = scan_devices_below(&tree.root.join("sys"), &tree.root.join("dev")).unwrap();

        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].display_name(), "Logitech Spotlight (USB)");
        assert_eq!(devices[0].physical_id, "usb-1");
        assert_eq!(devices[0].nodes.len(), 2);
        assert_eq!(devices[0].nodes[0].kind, DeviceNodeKind::Event);
        assert!(devices[0].nodes[0].has_relative_pointer);
        assert!(devices[0].nodes.iter().all(|node| node.readable));
    }

    #[test]
    fn ignores_unknown_and_bus_mismatched_devices() {
        let tree = TestTree::new();
        tree.write(
            "sys/unknown/uevent",
            "HID_ID=0003:00001234:00005678\nHID_NAME=Unknown\n",
        );
        tree.write(
            "sys/wrong-bus/uevent",
            "HID_ID=0005:0000046D:0000C53E\nHID_NAME=Wrong bus\n",
        );

        let devices = scan_devices_below(&tree.root.join("sys"), &tree.root.join("dev")).unwrap();

        assert!(devices.is_empty());
    }

    #[test]
    fn missing_hid_tree_is_an_error() {
        let tree = TestTree::new();
        let error = scan_devices_below(&tree.root.join("missing"), &tree.root.join("dev"))
            .expect_err("missing tree should fail");

        assert!(error.to_string().contains("cannot scan HID devices"));
    }
}
