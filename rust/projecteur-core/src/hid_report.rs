//! Decoder for standard HID reports emitted by Logitech Spotlight devices.

use std::{error::Error, fmt};

/// Relative pointer payload carried by report ID `0x02`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PointerReport {
    pub buttons: u8,
    pub x: i16,
    pub y: i16,
    pub wheel: i8,
    pub pan: i8,
}

/// Keyboard payload carried by report ID `0x01`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct KeyboardReport {
    pub modifiers: u8,
    pub usages: [u8; 6],
}

/// A standard report emitted on a Spotlight hidraw node.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PresenterReport {
    Pointer(PointerReport),
    Keyboard(KeyboardReport),
}

/// A raw report does not match a supported Spotlight report layout.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HidReportError {
    InvalidLength { report_id: Option<u8>, found: usize },
    UnknownReportId(u8),
}

impl fmt::Display for HidReportError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidLength { report_id, found } => {
                write!(
                    formatter,
                    "report {report_id:?} has {found} bytes; expected 8"
                )
            }
            Self::UnknownReportId(report_id) => {
                write!(formatter, "unsupported HID report ID {report_id:#04x}")
            }
        }
    }
}

impl Error for HidReportError {}

/// Decode one eight-byte Spotlight keyboard or packed 12-bit pointer report.
///
/// # Errors
///
/// Returns [`HidReportError`] when the packet is not eight bytes or its report
/// ID is neither `0x01` nor `0x02`.
pub fn decode_presenter_report(report: &[u8]) -> Result<PresenterReport, HidReportError> {
    if report.len() != 8 {
        return Err(HidReportError::InvalidLength {
            report_id: report.first().copied(),
            found: report.len(),
        });
    }
    match report[0] {
        0x01 => Ok(PresenterReport::Keyboard(KeyboardReport {
            modifiers: report[1],
            usages: [
                report[2], report[3], report[4], report[5], report[6], report[7],
            ],
        })),
        0x02 => {
            let x = u16::from(report[3]) | (u16::from(report[4] & 0x0f) << 8);
            let y = u16::from(report[4] >> 4) | (u16::from(report[5]) << 4);
            Ok(PresenterReport::Pointer(PointerReport {
                buttons: report[1],
                x: sign_extend_12(x),
                y: sign_extend_12(y),
                wheel: i8::from_ne_bytes([report[6]]),
                pan: i8::from_ne_bytes([report[7]]),
            }))
        }
        report_id => Err(HidReportError::UnknownReportId(report_id)),
    }
}

fn sign_extend_12(value: u16) -> i16 {
    if value & 0x0800 == 0 {
        i16::try_from(value).unwrap_or_default()
    } else {
        i16::from_ne_bytes((value | 0xf000).to_ne_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_positive_and_negative_captured_motion() {
        assert_eq!(
            decode_presenter_report(&[0x02, 0, 0, 0x0d, 0x60, 0, 0, 0]),
            Ok(PresenterReport::Pointer(PointerReport {
                buttons: 0,
                x: 13,
                y: 6,
                wheel: 0,
                pan: 0,
            }))
        );
        assert_eq!(
            decode_presenter_report(&[0x02, 0, 0, 0xf7, 0xdf, 0xff, 0, 0]),
            Ok(PresenterReport::Pointer(PointerReport {
                buttons: 0,
                x: -9,
                y: -3,
                wheel: 0,
                pan: 0,
            }))
        );
    }

    #[test]
    fn decodes_captured_next_back_and_release_reports() {
        let next = decode_presenter_report(&[0x01, 0, 0x4f, 0, 0, 0, 0, 0]).unwrap();
        let back = decode_presenter_report(&[0x01, 0, 0x50, 0, 0, 0, 0, 0]).unwrap();
        let release = decode_presenter_report(&[0x01, 0, 0, 0, 0, 0, 0, 0]).unwrap();

        assert!(matches!(
            next,
            PresenterReport::Keyboard(KeyboardReport {
                usages: [0x4f, 0, 0, 0, 0, 0],
                ..
            })
        ));
        assert!(matches!(
            back,
            PresenterReport::Keyboard(KeyboardReport {
                usages: [0x50, 0, 0, 0, 0, 0],
                ..
            })
        ));
        assert_eq!(
            release,
            PresenterReport::Keyboard(KeyboardReport {
                modifiers: 0,
                usages: [0; 6],
            })
        );
    }

    #[test]
    fn rejects_unknown_or_malformed_reports() {
        assert_eq!(
            decode_presenter_report(&[0x20; 8]),
            Err(HidReportError::UnknownReportId(0x20))
        );
        assert!(matches!(
            decode_presenter_report(&[0x02, 0]),
            Err(HidReportError::InvalidLength { found: 2, .. })
        ));
    }
}
