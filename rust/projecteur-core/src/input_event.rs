//! Decoder for Linux `input_event` records without grabbing the device.

use std::{io, io::Read, mem::size_of};

/// Synchronization event type from `linux/input-event-codes.h`.
pub const EV_SYN: u16 = 0x00;
/// Key/button event type from `linux/input-event-codes.h`.
pub const EV_KEY: u16 = 0x01;
/// Relative-axis event type from `linux/input-event-codes.h`.
pub const EV_REL: u16 = 0x02;
/// Miscellaneous event type from `linux/input-event-codes.h`.
pub const EV_MSC: u16 = 0x04;
/// End-of-frame synchronization code.
pub const SYN_REPORT: u16 = 0;
/// Relative horizontal motion code.
pub const REL_X: u16 = 0;
/// Relative vertical motion code.
pub const REL_Y: u16 = 1;

/// Size of `struct input_event` on the current Linux architecture.
///
/// The kernel ABI stores two C `long` timestamp fields followed by the stable
/// eight-byte type/code/value payload.
pub const LINUX_INPUT_EVENT_SIZE: usize = 2 * size_of::<isize>() + 8;

/// Timestamp-independent payload from one Linux input event.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct InputEvent {
    pub event_type: u16,
    pub code: u16,
    pub value: i32,
}

impl InputEvent {
    /// Whether this record terminates one kernel input frame.
    #[must_use]
    pub fn is_sync_report(self) -> bool {
        self.event_type == EV_SYN && self.code == SYN_REPORT
    }

    /// Whether this is relative pointer motion on either primary axis.
    #[must_use]
    pub fn is_relative_motion(self) -> bool {
        self.event_type == EV_REL && (self.code == REL_X || self.code == REL_Y)
    }
}

/// Decode exactly one native Linux `input_event` record.
///
/// Timestamps are intentionally discarded because Projecteur only needs event
/// ordering, type, code, and value.
///
/// # Errors
///
/// Propagates read errors, including [`io::ErrorKind::UnexpectedEof`] for a
/// truncated record.
pub fn read_input_event(reader: &mut impl Read) -> io::Result<InputEvent> {
    let mut record = [0_u8; LINUX_INPUT_EVENT_SIZE];
    reader.read_exact(&mut record)?;
    let payload = &record[LINUX_INPUT_EVENT_SIZE - 8..];
    Ok(InputEvent {
        event_type: u16::from_ne_bytes([payload[0], payload[1]]),
        code: u16::from_ne_bytes([payload[2], payload[3]]),
        value: i32::from_ne_bytes([payload[4], payload[5], payload[6], payload[7]]),
    })
}

#[cfg(test)]
mod tests {
    use std::io::Cursor;

    use super::*;

    fn record(event: InputEvent) -> Vec<u8> {
        let mut bytes = vec![0; LINUX_INPUT_EVENT_SIZE];
        let offset = LINUX_INPUT_EVENT_SIZE - 8;
        bytes[offset..offset + 2].copy_from_slice(&event.event_type.to_ne_bytes());
        bytes[offset + 2..offset + 4].copy_from_slice(&event.code.to_ne_bytes());
        bytes[offset + 4..offset + 8].copy_from_slice(&event.value.to_ne_bytes());
        bytes
    }

    #[test]
    fn decodes_native_linux_event_payload() {
        let expected = InputEvent {
            event_type: EV_REL,
            code: REL_Y,
            value: -17,
        };

        let actual = read_input_event(&mut Cursor::new(record(expected))).unwrap();

        assert_eq!(actual, expected);
        assert!(actual.is_relative_motion());
        assert!(!actual.is_sync_report());
    }

    #[test]
    fn identifies_sync_frames_and_rejects_truncated_records() {
        let sync = InputEvent {
            event_type: EV_SYN,
            code: SYN_REPORT,
            value: 0,
        };
        assert!(
            read_input_event(&mut Cursor::new(record(sync)))
                .unwrap()
                .is_sync_report()
        );

        let error =
            read_input_event(&mut Cursor::new(vec![0; LINUX_INPUT_EVENT_SIZE - 1])).unwrap_err();
        assert_eq!(error.kind(), io::ErrorKind::UnexpectedEof);
    }
}
