//! Minimal HID++ 2.0 protocol support used by Logitech Spotlight devices.

use std::{error::Error, fmt};

/// Projecteur's HID++ software identifier.
pub const SOFTWARE_ID: u8 = 7;

/// HID++ feature codes needed for battery reporting.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u16)]
pub enum FeatureCode {
    BatteryStatus = 0x1000,
    UnifiedBattery = 0x1004,
}

/// Battery state reported by HID++ battery features.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BatteryStatus {
    Discharging,
    Charging,
    AlmostFull,
    Full,
    SlowCharging,
    InvalidBattery,
    ThermalError,
    ChargingError,
    Unknown(u8),
}

impl BatteryStatus {
    #[must_use]
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Discharging => "discharging",
            Self::Charging => "charging",
            Self::AlmostFull => "almost-full",
            Self::Full => "full",
            Self::SlowCharging => "slow-charging",
            Self::InvalidBattery => "invalid-battery",
            Self::ThermalError => "thermal-error",
            Self::ChargingError => "charging-error",
            Self::Unknown(_) => "unknown",
        }
    }
}

impl From<u8> for BatteryStatus {
    fn from(value: u8) -> Self {
        match value {
            0 => Self::Discharging,
            1 => Self::Charging,
            2 => Self::AlmostFull,
            3 => Self::Full,
            4 => Self::SlowCharging,
            5 => Self::InvalidBattery,
            6 => Self::ThermalError,
            7 => Self::ChargingError,
            other => Self::Unknown(other),
        }
    }
}

/// Battery information common to HID++ `BATTERY_STATUS` and `UNIFIED_BATTERY`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BatteryInfo {
    pub current_level: u8,
    pub next_reported_level: u8,
    pub status: BatteryStatus,
}

/// A validated HID++ short or long message.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Message {
    bytes: [u8; Self::LONG_LEN],
    len: usize,
}

impl Message {
    pub const SHORT_LEN: usize = 7;
    pub const LONG_LEN: usize = 20;

    /// Build a long HID++ 2.0 request.
    #[must_use]
    pub fn long_request(device_index: u8, feature_index: u8, function: u8, payload: &[u8]) -> Self {
        let mut bytes = [0_u8; Self::LONG_LEN];
        bytes[0] = 0x11;
        bytes[1] = device_index;
        bytes[2] = feature_index;
        bytes[3] = ((function & 0x0f) << 4) | SOFTWARE_ID;
        let payload_len = payload.len().min(Self::LONG_LEN - 4);
        bytes[4..4 + payload_len].copy_from_slice(&payload[..payload_len]);
        Self {
            bytes,
            len: Self::LONG_LEN,
        }
    }

    /// Parse one complete HID++ short or long report.
    ///
    /// # Errors
    ///
    /// Returns [`HidppError`] if the report identifier or length is invalid.
    pub fn parse(report: &[u8]) -> Result<Self, HidppError> {
        let expected = match report.first().copied() {
            Some(0x10) => Self::SHORT_LEN,
            Some(0x11) => Self::LONG_LEN,
            report_id => return Err(HidppError::UnknownReportId(report_id)),
        };
        if report.len() != expected {
            return Err(HidppError::InvalidLength {
                report_id: report[0],
                expected,
                found: report.len(),
            });
        }
        let mut bytes = [0_u8; Self::LONG_LEN];
        bytes[..expected].copy_from_slice(report);
        Ok(Self {
            bytes,
            len: expected,
        })
    }

    #[must_use]
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes[..self.len]
    }

    #[must_use]
    pub fn device_index(&self) -> u8 {
        self.bytes[1]
    }

    #[must_use]
    pub fn feature_index(&self) -> u8 {
        self.bytes[2]
    }

    #[must_use]
    pub fn function(&self) -> u8 {
        self.bytes[3] >> 4
    }

    #[must_use]
    pub fn software_id(&self) -> u8 {
        self.bytes[3] & 0x0f
    }

    #[must_use]
    pub fn payload(&self) -> &[u8] {
        &self.bytes[4..self.len]
    }

    /// Whether this message is the normal response to `request`.
    #[must_use]
    pub fn is_response_to(&self, request: &Self) -> bool {
        self.device_index() == request.device_index()
            && self.feature_index() == request.feature_index()
            && self.bytes[3] == request.bytes[3]
    }

    /// Return a HID++ 2.0 error code when this is an error response to `request`.
    #[must_use]
    pub fn error_response_to(&self, request: &Self) -> Option<u8> {
        (self.bytes[0] == 0x11
            && self.feature_index() == 0xff
            && self.device_index() == request.device_index()
            && self.bytes[3] == request.feature_index()
            && self.bytes[4] == request.bytes[3])
            .then_some(self.bytes[5])
    }
}

/// Construct a root-feature lookup request.
#[must_use]
pub fn feature_index_request(device_index: u8, feature: FeatureCode) -> Message {
    Message::long_request(device_index, 0, 0, &(feature as u16).to_be_bytes())
}

/// Decode the feature index returned by a root-feature lookup.
///
/// An index of zero means the feature is not supported.
///
/// # Errors
///
/// Returns [`HidppError`] when the message is an error, unrelated to the
/// request, or lacks its feature-index payload.
pub fn decode_feature_index(
    request: &Message,
    response: &Message,
) -> Result<Option<u8>, HidppError> {
    ensure_response(request, response)?;
    Ok(response
        .payload()
        .first()
        .copied()
        .filter(|index| *index != 0))
}

/// Construct a battery information request for the selected feature.
#[must_use]
pub fn battery_request(device_index: u8, feature_index: u8, feature: FeatureCode) -> Message {
    let function = match feature {
        FeatureCode::BatteryStatus => 0,
        FeatureCode::UnifiedBattery => 1,
    };
    Message::long_request(device_index, feature_index, function, &[])
}

/// Decode a battery feature response.
///
/// # Errors
///
/// Returns [`HidppError`] when the message is an error, unrelated to the
/// request, or lacks its battery payload.
pub fn decode_battery_info(
    feature: FeatureCode,
    request: &Message,
    response: &Message,
) -> Result<BatteryInfo, HidppError> {
    ensure_response(request, response)?;
    let payload = response.payload();
    if payload.len() < 3 {
        return Err(HidppError::MissingPayload);
    }
    Ok(BatteryInfo {
        current_level: payload[0],
        next_reported_level: match feature {
            FeatureCode::BatteryStatus => payload[1],
            FeatureCode::UnifiedBattery => payload[0],
        },
        status: BatteryStatus::from(payload[2]),
    })
}

fn ensure_response(request: &Message, response: &Message) -> Result<(), HidppError> {
    if let Some(code) = response.error_response_to(request) {
        return Err(HidppError::Device(code));
    }
    if response.is_response_to(request) {
        Ok(())
    } else {
        Err(HidppError::UnrelatedResponse)
    }
}

/// Failure to encode or decode a HID++ message exchange.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum HidppError {
    UnknownReportId(Option<u8>),
    InvalidLength {
        report_id: u8,
        expected: usize,
        found: usize,
    },
    MissingPayload,
    UnrelatedResponse,
    Device(u8),
}

impl fmt::Display for HidppError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnknownReportId(id) => write!(formatter, "not a HID++ report: {id:?}"),
            Self::InvalidLength {
                report_id,
                expected,
                found,
            } => write!(
                formatter,
                "HID++ report {report_id:#04x} has {found} bytes; expected {expected}"
            ),
            Self::MissingPayload => formatter.write_str("HID++ response payload is incomplete"),
            Self::UnrelatedResponse => formatter.write_str("unrelated HID++ response"),
            Self::Device(code) => write!(formatter, "HID++ device error {code:#04x}"),
        }
    }
}

impl Error for HidppError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_root_feature_requests_in_network_byte_order() {
        assert_eq!(
            feature_index_request(1, FeatureCode::UnifiedBattery).as_bytes(),
            &[
                0x11, 0x01, 0x00, 0x07, 0x10, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            ]
        );
    }

    #[test]
    fn correlates_normal_and_error_responses() {
        let request = feature_index_request(1, FeatureCode::BatteryStatus);
        let response = Message::parse(&[
            0x11, 1, 0, 0x07, 0x0d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();
        let error = Message::parse(&[
            0x11, 1, 0xff, 0, 0x07, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();

        assert_eq!(decode_feature_index(&request, &response), Ok(Some(0x0d)));
        assert_eq!(
            decode_feature_index(&request, &error),
            Err(HidppError::Device(9))
        );
    }

    #[test]
    fn decodes_both_battery_feature_layouts() {
        let legacy_request = battery_request(1, 0x0d, FeatureCode::BatteryStatus);
        let legacy_response = Message::parse(&[
            0x10, 1, 0x0d, 0x07, 67, 60, 0, // current, next, discharging
        ])
        .unwrap();
        assert_eq!(
            decode_battery_info(
                FeatureCode::BatteryStatus,
                &legacy_request,
                &legacy_response
            ),
            Ok(BatteryInfo {
                current_level: 67,
                next_reported_level: 60,
                status: BatteryStatus::Discharging,
            })
        );

        let unified_request = battery_request(1, 0x0e, FeatureCode::UnifiedBattery);
        let unified_response = Message::parse(&[
            0x11, 1, 0x0e, 0x17, 82, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();
        assert_eq!(
            decode_battery_info(
                FeatureCode::UnifiedBattery,
                &unified_request,
                &unified_response
            ),
            Ok(BatteryInfo {
                current_level: 82,
                next_reported_level: 82,
                status: BatteryStatus::Full,
            })
        );
    }

    #[test]
    fn rejects_other_reports_and_unrelated_replies() {
        assert_eq!(
            Message::parse(&[0x02, 0, 0, 0, 0, 0, 0, 0]),
            Err(HidppError::UnknownReportId(Some(0x02)))
        );
        assert!(matches!(
            Message::parse(&[0x11, 0]),
            Err(HidppError::InvalidLength { found: 2, .. })
        ));

        let request = feature_index_request(1, FeatureCode::BatteryStatus);
        let response = Message::parse(&[
            0x11, 2, 0, 0x07, 0x0d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();
        assert_eq!(
            decode_feature_index(&request, &response),
            Err(HidppError::UnrelatedResponse)
        );
    }
}
