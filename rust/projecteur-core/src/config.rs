//! Reader for Projecteur's existing KConfig-compatible INI file.

use std::{
    collections::BTreeMap,
    error::Error,
    fmt, fs, io,
    path::{Path, PathBuf},
};

use crate::settings::SpotlightSettings;

/// One named `KConfig` group and its raw key/value entries.
pub type ConfigGroup = BTreeMap<String, String>;

/// An in-memory representation of a `projecteurrc` file.
///
/// Values are intentionally retained as strings. This keeps settings that the
/// Rust port does not understand yet available for later migration stages.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct ProjecteurConfig {
    groups: BTreeMap<String, ConfigGroup>,
}

impl ProjecteurConfig {
    /// Parse a KConfig-compatible document.
    ///
    /// Entries before the first explicit group belong to `General`, matching
    /// how Projecteur addresses unqualified settings in its existing backend.
    ///
    /// # Errors
    ///
    /// Returns [`ConfigError::Parse`] for an unterminated group header or for
    /// a non-comment entry without an equals sign.
    pub fn parse(input: &str) -> Result<Self, ConfigError> {
        let mut config = Self::default();
        let mut current_group = String::from("General");

        for (index, raw_line) in input.lines().enumerate() {
            let line_number = index + 1;
            let line = raw_line.trim();
            if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
                continue;
            }

            if let Some(group) = line.strip_prefix('[') {
                let Some(group) = group.strip_suffix(']') else {
                    return Err(ConfigError::Parse {
                        line: line_number,
                        message: "unterminated group header".to_owned(),
                    });
                };
                if group.is_empty() {
                    return Err(ConfigError::Parse {
                        line: line_number,
                        message: "empty group name".to_owned(),
                    });
                }
                group.clone_into(&mut current_group);
                config.groups.entry(current_group.clone()).or_default();
                continue;
            }

            let Some((key, value)) = raw_line.split_once('=') else {
                return Err(ConfigError::Parse {
                    line: line_number,
                    message: "expected key=value entry".to_owned(),
                });
            };
            let key = key.trim();
            if key.is_empty() {
                return Err(ConfigError::Parse {
                    line: line_number,
                    message: "empty key".to_owned(),
                });
            }

            config
                .groups
                .entry(current_group.clone())
                .or_default()
                .insert(key.to_owned(), value.trim().to_owned());
        }

        Ok(config)
    }

    /// Load and parse a configuration file.
    ///
    /// # Errors
    ///
    /// Returns [`ConfigError::Io`] when the file cannot be read and propagates
    /// parsing errors from [`Self::parse`].
    pub fn read(path: impl AsRef<Path>) -> Result<Self, ConfigError> {
        let path = path.as_ref();
        let input = fs::read_to_string(path).map_err(|source| ConfigError::Io {
            path: path.to_owned(),
            source,
        })?;
        Self::parse(&input)
    }

    /// Return all parsed groups, including groups unknown to the Rust port.
    #[must_use]
    pub fn groups(&self) -> &BTreeMap<String, ConfigGroup> {
        &self.groups
    }

    /// Return one raw group by name.
    #[must_use]
    pub fn group(&self, name: &str) -> Option<&ConfigGroup> {
        self.groups.get(name)
    }

    /// Return one raw value without interpreting its type.
    #[must_use]
    pub fn value(&self, group: &str, key: &str) -> Option<&str> {
        self.group(group)?.get(key).map(String::as_str)
    }

    /// Materialize overlay settings from the legacy `General` group.
    #[must_use]
    pub fn spotlight_settings(&self) -> SpotlightSettings {
        let mut settings = SpotlightSettings::default();
        if let Some(general) = self.group("General") {
            for (key, value) in general {
                settings.apply_general_entry(key, value);
            }
        }
        settings
    }
}

/// Failure to read or parse a Projecteur configuration file.
#[derive(Debug)]
pub enum ConfigError {
    /// The selected file could not be read.
    Io { path: PathBuf, source: io::Error },
    /// A document line is not valid KConfig-style INI syntax.
    Parse { line: usize, message: String },
}

impl fmt::Display for ConfigError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io { path, source } => {
                write!(formatter, "cannot read {}: {source}", path.display())
            }
            Self::Parse { line, message } => write!(formatter, "line {line}: {message}"),
        }
    }
}

impl Error for ConfigError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Io { source, .. } => Some(source),
            Self::Parse { .. } => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::settings::{DotMode, ZoomMode};

    const DOCUMENT: &str = r"
# Projecteur configuration
ungrouped=value

[General]
spotSize=48
dotColor=#123456
dotMode=diffuse
zoomMode=pixel
unknownFutureSetting=@Variant(\0\0\0)

[Preset_Talk]
spotSize=22

[Device_046d_c53e]
inputMapConfigData=@ByteArray(AQID)
";

    #[test]
    fn reads_general_settings_and_retains_unknown_data() {
        let config = ProjecteurConfig::parse(DOCUMENT).expect("fixture should parse");
        let settings = config.spotlight_settings();

        assert_eq!(settings.spot_size, 48);
        assert_eq!(settings.dot_color, "#123456");
        assert_eq!(settings.dot_mode, DotMode::Diffuse);
        assert_eq!(settings.zoom_mode, ZoomMode::Pixel);
        assert_eq!(
            config.value("General", "unknownFutureSetting"),
            Some(r"@Variant(\0\0\0)")
        );
        assert_eq!(config.value("Preset_Talk", "spotSize"), Some("22"));
        assert_eq!(
            config.value("Device_046d_c53e", "inputMapConfigData"),
            Some("@ByteArray(AQID)")
        );
    }

    #[test]
    fn ungrouped_entries_and_duplicate_keys_match_kconfig_behavior() {
        let config = ProjecteurConfig::parse("first=one\nfirst=two\n").unwrap();

        assert_eq!(config.value("General", "first"), Some("two"));
    }

    #[test]
    fn reports_the_location_of_malformed_input() {
        let error = ProjecteurConfig::parse("[General]\nnot an entry\n").unwrap_err();

        assert_eq!(error.to_string(), "line 2: expected key=value entry");
    }
}
