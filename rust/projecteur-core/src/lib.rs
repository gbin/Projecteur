//! Qt-independent Projecteur domain types.
//!
//! This crate deliberately has no Qt dependency. Device I/O, configuration
//! migration, and input mapping can therefore be tested without a display
//! server as the Rust port grows.

use std::{error::Error, fmt, str::FromStr};

pub mod config;
pub mod device_scan;
pub mod hid_report;
pub mod input_event;
pub mod settings;
pub mod uinput;

/// The transport used by a supported presenter.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum Bus {
    /// A directly connected USB device or USB receiver.
    Usb,
    /// A Bluetooth HID device.
    Bluetooth,
}

impl FromStr for Bus {
    type Err = ParseDeviceError;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value.trim() {
            "usb" => Ok(Self::Usb),
            "bt" => Ok(Self::Bluetooth),
            other => Err(ParseDeviceError::UnknownBus(other.to_owned())),
        }
    }
}

/// USB/HID identity of a supported presenter.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct DeviceId {
    /// USB vendor identifier.
    pub vendor: u16,
    /// USB product identifier.
    pub product: u16,
    /// Device transport.
    pub bus: Bus,
}

/// A presenter supported by the generic HID device implementation.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SupportedDevice {
    /// Stable device identity.
    pub id: DeviceId,
    /// Human-readable model name.
    pub name: String,
}

/// Error produced while parsing the maintained device registry.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum ParseDeviceError {
    /// A non-comment line did not contain the four required columns.
    InvalidColumnCount { line: usize, found: usize },
    /// A hexadecimal USB identifier was invalid.
    InvalidIdentifier { line: usize, value: String },
    /// The bus column was neither `usb` nor `bt`.
    UnknownBus(String),
}

impl fmt::Display for ParseDeviceError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidColumnCount { line, found } => {
                write!(formatter, "line {line}: expected 4 columns, found {found}")
            }
            Self::InvalidIdentifier { line, value } => {
                write!(
                    formatter,
                    "line {line}: invalid hexadecimal identifier {value:?}"
                )
            }
            Self::UnknownBus(bus) => write!(formatter, "unknown device bus {bus:?}"),
        }
    }
}

impl Error for ParseDeviceError {}

/// Parse Projecteur's `devices.conf` format.
///
/// # Errors
///
/// Returns [`ParseDeviceError`] when a registry row has invalid columns,
/// identifiers, or a transport name other than `usb` or `bt`.
pub fn parse_supported_devices(input: &str) -> Result<Vec<SupportedDevice>, ParseDeviceError> {
    input
        .lines()
        .enumerate()
        .filter_map(|(index, raw_line)| {
            let line = raw_line.trim();
            (!line.is_empty() && !line.starts_with('#')).then_some((index + 1, line))
        })
        .map(|(line_number, line)| {
            let columns: Vec<_> = line.split(',').map(str::trim).collect();
            if columns.len() != 4 {
                return Err(ParseDeviceError::InvalidColumnCount {
                    line: line_number,
                    found: columns.len(),
                });
            }

            let parse_id = |value: &str| {
                u16::from_str_radix(
                    value
                        .strip_prefix("0x")
                        .or_else(|| value.strip_prefix("0X"))
                        .unwrap_or(value),
                    16,
                )
                .map_err(|_| ParseDeviceError::InvalidIdentifier {
                    line: line_number,
                    value: value.to_owned(),
                })
            };

            Ok(SupportedDevice {
                id: DeviceId {
                    vendor: parse_id(columns[0])?,
                    product: parse_id(columns[1])?,
                    bus: columns[2].parse()?,
                },
                name: columns[3].to_owned(),
            })
        })
        .collect()
}

/// Return the generic presenters maintained in the repository registry.
///
/// # Panics
///
/// Panics when the checked-in `devices.conf` is malformed. CI tests the same
/// parser so a malformed registry cannot produce a release build unnoticed.
#[must_use]
pub fn supported_devices() -> Vec<SupportedDevice> {
    parse_supported_devices(include_str!("../../../devices.conf"))
        .expect("the checked-in devices.conf must be valid")
}

/// Return every built-in presenter model, including Logitech Spotlight models.
#[must_use]
pub fn all_supported_devices() -> Vec<SupportedDevice> {
    let mut devices = vec![
        SupportedDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xc53e,
                bus: Bus::Usb,
            },
            name: "Logitech Spotlight (USB)".to_owned(),
        },
        SupportedDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xb503,
                bus: Bus::Bluetooth,
            },
            name: "Logitech Spotlight (Bluetooth)".to_owned(),
        },
        SupportedDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xc548,
                bus: Bus::Usb,
            },
            name: "Logitech Spotlight 2 (USB-C receiver)".to_owned(),
        },
        SupportedDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xb506,
                bus: Bus::Bluetooth,
            },
            name: "Logitech Spotlight 2 (Bluetooth)".to_owned(),
        },
    ];
    devices.extend(supported_devices());
    devices
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checked_in_device_registry_is_valid() {
        let devices = supported_devices();

        assert_eq!(devices.len(), 13);
        assert_eq!(devices[0].id.vendor, 0x0c45);
        assert_eq!(devices[0].id.product, 0x8101);
        assert_eq!(devices[0].id.bus, Bus::Usb);
        assert_eq!(devices[0].name, "AVATTO H100 / August WP200");
        assert!(devices.iter().any(|device| device.id.bus == Bus::Bluetooth));
    }

    #[test]
    fn complete_registry_includes_logitech_spotlight_models() {
        let devices = all_supported_devices();

        assert_eq!(devices.len(), 17);
        assert_eq!(devices[0].id.vendor, 0x046d);
        assert_eq!(devices[0].id.product, 0xc53e);
        assert_eq!(devices[0].name, "Logitech Spotlight (USB)");
    }

    #[test]
    fn comments_and_blank_lines_are_ignored() {
        let devices =
            parse_supported_devices("# comment\n\n0x046d, 0xc53e, usb, Logitech Spotlight\n")
                .unwrap();

        assert_eq!(devices.len(), 1);
        assert_eq!(devices[0].id.vendor, 0x046d);
    }

    #[test]
    fn malformed_lines_report_their_location() {
        let error =
            parse_supported_devices("# heading\n0x1234, usb, Missing product\n").unwrap_err();

        assert_eq!(
            error,
            ParseDeviceError::InvalidColumnCount { line: 2, found: 3 }
        );
    }
}
