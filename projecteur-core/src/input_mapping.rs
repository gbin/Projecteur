//! C++-compatible presenter input-mapping data and serialization.

use std::{error::Error, fmt};

use crate::input_event::{EV_KEY, EV_MSC, InputEvent};

const FORMAT_VERSION: u32 = 1;
const MAX_MAPPINGS: usize = 1024;
const MAX_SEQUENCE_ITEMS: usize = 4096;
const MSC_SCAN: u16 = 4;
const BTN_LEFT: u16 = 0x110;
const BTN_MIDDLE: u16 = 0x112;

/// One kernel input frame, without its trailing `SYN_REPORT` event.
pub type KeyEvent = Vec<InputEvent>;

/// A sequence of input frames used as the key of one mapping.
pub type KeyEventSequence = Vec<KeyEvent>;

/// Native output key sequence stored by the C++ `NativeKeySequence` type.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct NativeKeySequence {
    /// Qt key combinations used for display in the editor.
    pub qt_keys: Vec<i32>,
    /// Linux input frames emitted through the virtual keyboard.
    pub native_sequence: KeyEventSequence,
    /// C++ `NativeKeySequence::Modifier` bit masks, one per key chord.
    pub native_modifiers: Vec<u16>,
}

/// Action executed when its presenter input sequence matches.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MappedAction {
    /// Emit a native keyboard sequence.
    KeySequence(NativeKeySequence),
    /// Select the next spotlight preset.
    CyclePresets,
    /// Toggle the spotlight overlay.
    ToggleSpotlight,
    /// Convert Spotlight hold motion to horizontal scrolling.
    ScrollHorizontal,
    /// Convert Spotlight hold motion to vertical scrolling.
    ScrollVertical,
    /// Convert Spotlight hold motion to volume adjustment.
    VolumeControl,
}

impl MappedAction {
    /// Numeric value persisted by the C++ `Action::Type` enum.
    #[must_use]
    pub const fn type_id(&self) -> i32 {
        match self {
            Self::KeySequence(_) => 1,
            Self::CyclePresets => 2,
            Self::ToggleSpotlight => 3,
            Self::ScrollHorizontal => 11,
            Self::ScrollVertical => 12,
            Self::VolumeControl => 13,
        }
    }
}

/// One input sequence and its mapped action.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct InputMapping {
    pub input: KeyEventSequence,
    pub action: MappedAction,
}

/// Per-device input mapping configuration.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct InputMapConfig {
    pub mappings: Vec<InputMapping>,
}

impl InputMapConfig {
    /// Decode the `QDataStream::Qt_6_0` payload written by the C++ application.
    ///
    /// # Errors
    ///
    /// Returns an error for unsupported versions, invalid action identifiers,
    /// unreasonable collection sizes, truncated fields, or trailing data.
    pub fn from_qt_bytes(bytes: &[u8]) -> Result<Self, InputMapError> {
        let mut reader = Reader::new(bytes);
        let version = reader.u32()?;
        if version != FORMAT_VERSION {
            return Err(InputMapError::UnsupportedVersion(version));
        }

        let count = reader.length(MAX_MAPPINGS, "mapping count")?;
        let mut mappings = Vec::with_capacity(count);
        for _ in 0..count {
            let input = reader.event_sequence()?;
            let action = reader.action()?;
            mappings.push(InputMapping { input, action });
        }
        if !reader.remaining().is_empty() {
            return Err(InputMapError::TrailingData(reader.remaining().len()));
        }
        Ok(Self { mappings })
    }

    /// Encode exactly the `QDataStream::Qt_6_0` layout used by C++ Projecteur.
    #[must_use]
    pub fn to_qt_bytes(&self) -> Vec<u8> {
        let mut writer = Writer::default();
        writer.u32(FORMAT_VERSION);
        writer.length(self.mappings.len());
        for mapping in &self.mappings {
            writer.event_sequence(&mapping.input);
            writer.action(&mapping.action);
        }
        writer.bytes
    }

    /// Decode a complete `KConfig` `@ByteArray(...)` value.
    ///
    /// # Errors
    ///
    /// Returns an error if the wrapper or an escape sequence is malformed, or
    /// if its decoded Qt stream is invalid.
    pub fn from_kconfig_value(value: &str) -> Result<Self, InputMapError> {
        let bytes = decode_kconfig_byte_array(value)?;
        Self::from_qt_bytes(&bytes)
    }

    /// Encode a value suitable for `inputMapConfigData` in `projecteurrc`.
    #[must_use]
    pub fn to_kconfig_value(&self) -> String {
        encode_kconfig_byte_array(&self.to_qt_bytes())
    }
}

/// Result produced after feeding one complete presenter input frame.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MapperOutput {
    /// More frames may complete a configured sequence; wait for the interval.
    Pending,
    /// No mapping matched, so these physical events must be forwarded.
    Forward(Vec<InputEvent>),
    /// A complete configured action matched; physical events are consumed.
    Action(MappedAction),
    /// A complete mapping with an empty native key sequence consumed the input.
    Consumed,
}

/// Stateful prefix matcher matching the behavior of C++ `InputMapper`.
#[derive(Clone, Debug, Default)]
pub struct InputMapper {
    config: InputMapConfig,
    input: KeyEventSequence,
    buffered: Vec<InputEvent>,
    partial_action: Option<MappedAction>,
}

impl InputMapper {
    /// Create a matcher for one device's current configuration.
    #[must_use]
    pub const fn new(config: InputMapConfig) -> Self {
        Self {
            config,
            input: Vec::new(),
            buffered: Vec::new(),
            partial_action: None,
        }
    }

    /// Replace the configuration and discard any incomplete sequence.
    pub fn set_config(&mut self, config: InputMapConfig) {
        self.config = config;
        self.reset();
    }

    /// Return the active configuration.
    #[must_use]
    pub const fn config(&self) -> &InputMapConfig {
        &self.config
    }

    /// Whether a prefix is waiting for another frame or the sequence timeout.
    #[must_use]
    pub fn has_pending_input(&self) -> bool {
        !self.input.is_empty()
    }

    /// Feed one frame including its final `SYN_REPORT` event.
    ///
    /// # Errors
    ///
    /// Returns an error if the frame is empty, contains only synchronization,
    /// or does not end in `SYN_REPORT`.
    pub fn feed_frame(&mut self, frame: &[InputEvent]) -> Result<MapperOutput, InputMapError> {
        let Some(sync) = frame.last().copied() else {
            return Err(InputMapError::InvalidInputFrame);
        };
        if frame.len() == 1 || !sync.is_sync_report() {
            return Err(InputMapError::InvalidInputFrame);
        }

        if self.config.mappings.is_empty() {
            return Ok(MapperOutput::Forward(frame.to_vec()));
        }

        let mut normalized = frame;
        if frame.len() == 3
            && frame[0].event_type == EV_MSC
            && frame[0].code == MSC_SCAN
            && frame[1].event_type == EV_KEY
            && (BTN_LEFT..=BTN_MIDDLE).contains(&frame[1].code)
        {
            normalized = &frame[1..];
        }

        let key_event = normalized[..normalized.len() - 1].to_vec();
        self.input.push(key_event);
        self.buffered.extend_from_slice(normalized);

        let candidates: Vec<_> = self
            .config
            .mappings
            .iter()
            .filter(|mapping| mapping.input.starts_with(&self.input))
            .collect();
        if candidates.is_empty() {
            let buffered = std::mem::take(&mut self.buffered);
            self.reset();
            return Ok(MapperOutput::Forward(buffered));
        }

        let exact = candidates
            .iter()
            .find(|mapping| mapping.input.len() == self.input.len());
        let has_longer = candidates
            .iter()
            .any(|mapping| mapping.input.len() > self.input.len());
        if let Some(mapping) = exact {
            if has_longer {
                self.partial_action = Some(mapping.action.clone());
                return Ok(MapperOutput::Pending);
            }
            let action = mapping.action.clone();
            self.reset();
            return Ok(action_output(action));
        }

        self.partial_action = None;
        Ok(MapperOutput::Pending)
    }

    /// Resolve a pending prefix when the configured sequence interval expires.
    #[must_use]
    pub fn sequence_timeout(&mut self) -> Option<MapperOutput> {
        if self.input.is_empty() {
            return None;
        }
        let output = self.partial_action.take().map_or_else(
            || MapperOutput::Forward(std::mem::take(&mut self.buffered)),
            action_output,
        );
        self.reset();
        Some(output)
    }

    /// Discard an incomplete sequence without forwarding it.
    pub fn reset(&mut self) {
        self.input.clear();
        self.buffered.clear();
        self.partial_action = None;
    }
}

fn action_output(action: MappedAction) -> MapperOutput {
    if matches!(&action, MappedAction::KeySequence(sequence) if sequence.native_sequence.is_empty())
    {
        MapperOutput::Consumed
    } else {
        MapperOutput::Action(action)
    }
}

/// Input-map configuration or compatibility failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum InputMapError {
    InvalidByteArrayWrapper,
    InvalidEscape { offset: usize },
    UnexpectedEnd { offset: usize },
    UnsupportedVersion(u32),
    InvalidAction(i32),
    CollectionTooLarge { field: &'static str, size: u32 },
    TrailingData(usize),
    InvalidInputFrame,
}

impl fmt::Display for InputMapError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidByteArrayWrapper => {
                formatter.write_str("expected a KConfig @ByteArray(...) value")
            }
            Self::InvalidEscape { offset } => {
                write!(
                    formatter,
                    "invalid KConfig byte-array escape at byte {offset}"
                )
            }
            Self::UnexpectedEnd { offset } => {
                write!(formatter, "truncated Qt data stream at byte {offset}")
            }
            Self::UnsupportedVersion(version) => {
                write!(formatter, "unsupported input-map format version {version}")
            }
            Self::InvalidAction(action) => write!(formatter, "invalid input-map action {action}"),
            Self::CollectionTooLarge { field, size } => {
                write!(
                    formatter,
                    "input-map {field} is unreasonably large ({size})"
                )
            }
            Self::TrailingData(length) => {
                write!(formatter, "input-map stream has {length} trailing bytes")
            }
            Self::InvalidInputFrame => formatter
                .write_str("input-mapping frame must contain events followed by SYN_REPORT"),
        }
    }
}

impl Error for InputMapError {}

/// Decode Qt's INI representation of a `QByteArray`.
///
/// # Errors
///
/// Returns an error for a missing wrapper or malformed backslash escape.
pub fn decode_kconfig_byte_array(value: &str) -> Result<Vec<u8>, InputMapError> {
    let Some(body) = value
        .strip_prefix("@ByteArray(")
        .and_then(|value| value.strip_suffix(')'))
    else {
        return Err(InputMapError::InvalidByteArrayWrapper);
    };
    let input = body.as_bytes();
    let mut output = Vec::with_capacity(input.len());
    let mut index = 0;
    while index < input.len() {
        if input[index] != b'\\' {
            output.push(input[index]);
            index += 1;
            continue;
        }
        let escape_offset = index;
        index += 1;
        let Some(&escaped) = input.get(index) else {
            return Err(InputMapError::InvalidEscape {
                offset: escape_offset,
            });
        };
        index += 1;
        match escaped {
            b'0' => output.push(0),
            b'a' => output.push(0x07),
            b'b' => output.push(0x08),
            b't' => output.push(b'\t'),
            b'n' => output.push(b'\n'),
            b'v' => output.push(0x0b),
            b'f' => output.push(0x0c),
            b'r' => output.push(b'\r'),
            b'\\' | b')' => output.push(escaped),
            b'x' => {
                let Some(&first) = input.get(index) else {
                    return Err(InputMapError::InvalidEscape {
                        offset: escape_offset,
                    });
                };
                let Some(mut byte) = hex_value(first) else {
                    return Err(InputMapError::InvalidEscape {
                        offset: escape_offset,
                    });
                };
                index += 1;
                if let Some(value) = input.get(index).copied().and_then(hex_value) {
                    byte = byte * 16 + value;
                    index += 1;
                }
                output.push(byte);
            }
            _ => {
                return Err(InputMapError::InvalidEscape {
                    offset: escape_offset,
                });
            }
        }
    }
    Ok(output)
}

/// Encode bytes using the escape syntax accepted by Qt's INI backend.
#[must_use]
pub fn encode_kconfig_byte_array(bytes: &[u8]) -> String {
    let mut output = String::from("@ByteArray(");
    for &byte in bytes {
        if byte == 0 {
            output.push_str("\\0");
        } else {
            // Escaping every non-zero byte prevents a following hexadecimal
            // ASCII character from becoming part of the previous `\x` escape.
            use fmt::Write as _;
            let _ = write!(output, "\\x{byte:x}");
        }
    }
    output.push(')');
    output
}

const fn hex_value(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        b'A'..=b'F' => Some(byte - b'A' + 10),
        _ => None,
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Reader<'a> {
    const fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, offset: 0 }
    }

    fn remaining(&self) -> &'a [u8] {
        &self.bytes[self.offset..]
    }

    fn take<const N: usize>(&mut self) -> Result<[u8; N], InputMapError> {
        let end = self.offset.saturating_add(N);
        let Some(value) = self.bytes.get(self.offset..end) else {
            return Err(InputMapError::UnexpectedEnd {
                offset: self.offset,
            });
        };
        self.offset = end;
        Ok(value.try_into().expect("slice has requested fixed length"))
    }

    fn u8(&mut self) -> Result<u8, InputMapError> {
        Ok(self.take::<1>()?[0])
    }

    fn u16(&mut self) -> Result<u16, InputMapError> {
        Ok(u16::from_be_bytes(self.take()?))
    }

    fn u32(&mut self) -> Result<u32, InputMapError> {
        Ok(u32::from_be_bytes(self.take()?))
    }

    fn i32(&mut self) -> Result<i32, InputMapError> {
        Ok(i32::from_be_bytes(self.take()?))
    }

    fn length(&mut self, limit: usize, field: &'static str) -> Result<usize, InputMapError> {
        let size = self.u32()?;
        let length = usize::try_from(size).unwrap_or(usize::MAX);
        if length > limit {
            return Err(InputMapError::CollectionTooLarge { field, size });
        }
        Ok(length)
    }

    fn input_event(&mut self) -> Result<InputEvent, InputMapError> {
        Ok(InputEvent {
            event_type: self.u16()?,
            code: self.u16()?,
            value: self.i32()?,
        })
    }

    fn key_event(&mut self) -> Result<KeyEvent, InputMapError> {
        let count = self.length(MAX_SEQUENCE_ITEMS, "input event count")?;
        (0..count).map(|_| self.input_event()).collect()
    }

    fn event_sequence(&mut self) -> Result<KeyEventSequence, InputMapError> {
        let count = self.length(MAX_SEQUENCE_ITEMS, "input sequence length")?;
        (0..count).map(|_| self.key_event()).collect()
    }

    fn i32_vector(&mut self, field: &'static str) -> Result<Vec<i32>, InputMapError> {
        let count = self.length(MAX_SEQUENCE_ITEMS, field)?;
        (0..count).map(|_| self.i32()).collect()
    }

    fn u16_vector(&mut self, field: &'static str) -> Result<Vec<u16>, InputMapError> {
        let count = self.length(MAX_SEQUENCE_ITEMS, field)?;
        (0..count).map(|_| self.u16()).collect()
    }

    fn action(&mut self) -> Result<MappedAction, InputMapError> {
        let action = self.i32()?;
        match action {
            1 => Ok(MappedAction::KeySequence(NativeKeySequence {
                qt_keys: self.i32_vector("Qt key count")?,
                native_sequence: self.event_sequence()?,
                native_modifiers: self.u16_vector("native modifier count")?,
            })),
            2 => {
                self.u8()?;
                Ok(MappedAction::CyclePresets)
            }
            3 => {
                self.u8()?;
                Ok(MappedAction::ToggleSpotlight)
            }
            11 => {
                self.u8()?;
                Ok(MappedAction::ScrollHorizontal)
            }
            12 => {
                self.u8()?;
                Ok(MappedAction::ScrollVertical)
            }
            13 => {
                self.u8()?;
                Ok(MappedAction::VolumeControl)
            }
            _ => Err(InputMapError::InvalidAction(action)),
        }
    }
}

#[derive(Default)]
struct Writer {
    bytes: Vec<u8>,
}

impl Writer {
    fn u8(&mut self, value: u8) {
        self.bytes.push(value);
    }

    fn u16(&mut self, value: u16) {
        self.bytes.extend(value.to_be_bytes());
    }

    fn u32(&mut self, value: u32) {
        self.bytes.extend(value.to_be_bytes());
    }

    fn i32(&mut self, value: i32) {
        self.bytes.extend(value.to_be_bytes());
    }

    fn length(&mut self, value: usize) {
        self.u32(u32::try_from(value).expect("input map collection length exceeds u32"));
    }

    fn input_event(&mut self, event: InputEvent) {
        self.u16(event.event_type);
        self.u16(event.code);
        self.i32(event.value);
    }

    fn key_event(&mut self, event: &KeyEvent) {
        self.length(event.len());
        for &input in event {
            self.input_event(input);
        }
    }

    fn event_sequence(&mut self, sequence: &KeyEventSequence) {
        self.length(sequence.len());
        for event in sequence {
            self.key_event(event);
        }
    }

    fn action(&mut self, action: &MappedAction) {
        self.i32(action.type_id());
        match action {
            MappedAction::KeySequence(sequence) => {
                self.length(sequence.qt_keys.len());
                for &key in &sequence.qt_keys {
                    self.i32(key);
                }
                self.event_sequence(&sequence.native_sequence);
                self.length(sequence.native_modifiers.len());
                for &modifier in &sequence.native_modifiers {
                    self.u16(modifier);
                }
            }
            MappedAction::CyclePresets
            | MappedAction::ToggleSpotlight
            | MappedAction::ScrollHorizontal
            | MappedAction::ScrollVertical
            | MappedAction::VolumeControl => self.u8(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input_event::{EV_KEY, EV_SYN, SYN_REPORT};

    const CPP_FIXTURE_HEX: &str = concat!(
        "000000010000000300000002000000010001006a00000001000000010001006a0000000000000003",
        "00000000020000000100010069000000010000000100010069000000000000000100000001090000",
        "01000000020000000300010038000000010001000f00000001000000000000000000000003000100",
        "38000000000001000f00000000000000000000000000000001000400000001000000010001006d00",
        "0000010000000c00"
    );

    fn decode_hex(value: &str) -> Vec<u8> {
        value
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| hex_value(pair[0]).unwrap() * 16 + hex_value(pair[1]).unwrap())
            .collect()
    }

    #[test]
    fn decodes_and_reencodes_cpp_qdatastream_fixture() {
        let bytes = decode_hex(CPP_FIXTURE_HEX);
        let config = InputMapConfig::from_qt_bytes(&bytes).unwrap();

        assert_eq!(config.mappings.len(), 3);
        assert_eq!(config.mappings[0].action, MappedAction::ToggleSpotlight);
        assert_eq!(config.mappings[0].input[0][0].code, 106);
        let MappedAction::KeySequence(keys) = &config.mappings[1].action else {
            panic!("second fixture action should be a key sequence");
        };
        assert_eq!(keys.qt_keys, [0x0900_0001]);
        assert_eq!(keys.native_modifiers, [4]);
        assert_eq!(keys.native_sequence[0][0].code, 56);
        assert_eq!(config.mappings[2].action, MappedAction::ScrollVertical);
        assert_eq!(config.to_qt_bytes(), bytes);
    }

    #[test]
    fn round_trips_kconfig_byte_array_escapes() {
        let bytes = [0, 1, 15, b' ', b')', b'\\', b'z', 0xff];
        let encoded = encode_kconfig_byte_array(&bytes);

        assert_eq!(decode_kconfig_byte_array(&encoded).unwrap(), bytes);
        assert_eq!(
            decode_kconfig_byte_array(r"@ByteArray(\0\x1\xf \)\\z\xff)").unwrap(),
            bytes
        );
    }

    #[test]
    fn writes_native_key_sequence_in_cpp_layout() {
        let press = vec![
            InputEvent {
                event_type: EV_KEY,
                code: 56,
                value: 1,
            },
            InputEvent {
                event_type: EV_SYN,
                code: SYN_REPORT,
                value: 0,
            },
        ];
        let config = InputMapConfig {
            mappings: vec![InputMapping {
                input: vec![vec![InputEvent {
                    event_type: EV_KEY,
                    code: 106,
                    value: 1,
                }]],
                action: MappedAction::KeySequence(NativeKeySequence {
                    qt_keys: vec![0x0900_0001],
                    native_sequence: vec![press],
                    native_modifiers: vec![4],
                }),
            }],
        };

        assert_eq!(
            InputMapConfig::from_kconfig_value(&config.to_kconfig_value()).unwrap(),
            config
        );
    }

    #[test]
    fn rejects_invalid_or_unbounded_streams() {
        assert_eq!(
            InputMapConfig::from_qt_bytes(&[0, 0, 0, 2]),
            Err(InputMapError::UnsupportedVersion(2))
        );
        assert!(matches!(
            InputMapConfig::from_qt_bytes(&[0, 0, 0, 1, 0, 0, 4, 1]),
            Err(InputMapError::CollectionTooLarge {
                field: "mapping count",
                ..
            })
        ));
    }

    fn frame(code: u16, value: i32) -> Vec<InputEvent> {
        vec![
            InputEvent {
                event_type: EV_KEY,
                code,
                value,
            },
            InputEvent {
                event_type: EV_SYN,
                code: SYN_REPORT,
                value: 0,
            },
        ]
    }

    #[test]
    fn matches_actions_and_forwards_misses() {
        let mut mapper = InputMapper::new(InputMapConfig {
            mappings: vec![InputMapping {
                input: vec![vec![frame(106, 1)[0]]],
                action: MappedAction::ToggleSpotlight,
            }],
        });

        assert_eq!(
            mapper.feed_frame(&frame(106, 1)).unwrap(),
            MapperOutput::Action(MappedAction::ToggleSpotlight)
        );
        assert_eq!(
            mapper.feed_frame(&frame(105, 1)).unwrap(),
            MapperOutput::Forward(frame(105, 1))
        );
    }

    #[test]
    fn delays_prefixes_and_resolves_partial_hits_on_timeout() {
        let first = vec![frame(106, 1)[0]];
        let second = vec![frame(106, 0)[0]];
        let mut mapper = InputMapper::new(InputMapConfig {
            mappings: vec![
                InputMapping {
                    input: vec![first.clone()],
                    action: MappedAction::CyclePresets,
                },
                InputMapping {
                    input: vec![first, second],
                    action: MappedAction::ToggleSpotlight,
                },
            ],
        });

        assert_eq!(
            mapper.feed_frame(&frame(106, 1)).unwrap(),
            MapperOutput::Pending
        );
        assert_eq!(
            mapper.sequence_timeout(),
            Some(MapperOutput::Action(MappedAction::CyclePresets))
        );

        assert_eq!(
            mapper.feed_frame(&frame(106, 1)).unwrap(),
            MapperOutput::Pending
        );
        assert_eq!(
            mapper.feed_frame(&frame(106, 0)).unwrap(),
            MapperOutput::Action(MappedAction::ToggleSpotlight)
        );
    }

    #[test]
    fn forwards_an_unfinished_sequence_after_timeout() {
        let mut mapper = InputMapper::new(InputMapConfig {
            mappings: vec![InputMapping {
                input: vec![vec![frame(106, 1)[0]], vec![frame(106, 0)[0]]],
                action: MappedAction::ToggleSpotlight,
            }],
        });

        assert_eq!(
            mapper.feed_frame(&frame(106, 1)).unwrap(),
            MapperOutput::Pending
        );
        assert_eq!(
            mapper.sequence_timeout(),
            Some(MapperOutput::Forward(frame(106, 1)))
        );
    }
}
