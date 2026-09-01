//! Spotlight settings and compatibility defaults.

use std::str::FromStr;

/// Inclusive range used to validate a numeric setting.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Range<T> {
    /// Smallest accepted value.
    pub minimum: T,
    /// Largest accepted value.
    pub maximum: T,
}

impl Range<i32> {
    fn clamp(self, value: i32) -> i32 {
        value.clamp(self.minimum, self.maximum)
    }
}

impl Range<f64> {
    fn clamp(self, value: f64) -> f64 {
        value.clamp(self.minimum, self.maximum)
    }
}

/// Center-dot rendering mode understood by the existing shaders.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum DotMode {
    /// A sharply bounded solid dot.
    #[default]
    Solid,
    /// A soft, diffused dot.
    Diffuse,
}

impl FromStr for DotMode {
    type Err = ();

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "solid" => Ok(Self::Solid),
            "diffuse" => Ok(Self::Diffuse),
            _ => Err(()),
        }
    }
}

/// Magnification filter used by the spotlight zoom effect.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ZoomMode {
    /// Smooth image scaling.
    #[default]
    Smooth,
    /// Scaling optimized for text.
    Text,
    /// Nearest-neighbor pixel scaling.
    Pixel,
}

impl FromStr for ZoomMode {
    type Err = ();

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "smooth" => Ok(Self::Smooth),
            "text" => Ok(Self::Text),
            "pixel" => Ok(Self::Pixel),
            _ => Err(()),
        }
    }
}

/// Settings that define the spotlight overlay.
#[allow(clippy::struct_excessive_bools)]
#[derive(Clone, Debug, PartialEq)]
pub struct SpotlightSettings {
    pub show_spot_shade: bool,
    pub spot_size: i32,
    pub show_center_dot: bool,
    pub dot_size: i32,
    pub dot_color: String,
    pub dot_opacity: f64,
    pub dot_mode: DotMode,
    pub dot_trail_enabled: bool,
    pub shade_color: String,
    pub shade_opacity: f64,
    pub cursor: i32,
    pub spot_shape: String,
    pub spot_rotation: f64,
    pub square_radius: i32,
    pub star_points: i32,
    pub star_inner_radius: i32,
    pub ngon_sides: i32,
    pub show_border: bool,
    pub border_color: String,
    pub border_size: i32,
    pub border_opacity: f64,
    pub zoom_enabled: bool,
    pub zoom_factor: f64,
    pub zoom_mode: ZoomMode,
    pub multi_screen_overlay: bool,
    pub presentation_timer_enabled: bool,
    pub presentation_timer_duration_seconds: i32,
}

impl SpotlightSettings {
    pub const SPOT_SIZE_RANGE: Range<i32> = Range {
        minimum: 5,
        maximum: 100,
    };
    pub const DOT_SIZE_RANGE: Range<i32> = Range {
        minimum: 3,
        maximum: 100,
    };
    pub const OPACITY_RANGE: Range<f64> = Range {
        minimum: 0.0,
        maximum: 1.0,
    };
    pub const SPOT_ROTATION_RANGE: Range<f64> = Range {
        minimum: 0.0,
        maximum: 360.0,
    };
    pub const BORDER_SIZE_RANGE: Range<i32> = Range {
        minimum: 0,
        maximum: 100,
    };
    pub const ZOOM_FACTOR_RANGE: Range<f64> = Range {
        minimum: 1.5,
        maximum: 20.0,
    };
    pub const INPUT_SEQUENCE_INTERVAL_RANGE: Range<i32> = Range {
        minimum: 100,
        maximum: 950,
    };

    /// Apply one key/value pair from the legacy `KConfig` `General` group.
    ///
    /// Unknown keys and malformed values are ignored. Numeric values are
    /// clamped exactly as the existing setters clamp them.
    pub fn apply_general_entry(&mut self, key: &str, value: &str) {
        match key {
            "showSpotShade" => set_bool(&mut self.show_spot_shade, value),
            "spotSize" => set_i32(&mut self.spot_size, value, Self::SPOT_SIZE_RANGE),
            "showCenterDot" => set_bool(&mut self.show_center_dot, value),
            "dotSize" => set_i32(&mut self.dot_size, value, Self::DOT_SIZE_RANGE),
            "dotColor" => set_color(&mut self.dot_color, value),
            "dotOpacity" => set_f64(&mut self.dot_opacity, value, Self::OPACITY_RANGE),
            "dotMode" => set_parsed(&mut self.dot_mode, value),
            "dotTrailEnabled" => set_bool(&mut self.dot_trail_enabled, value),
            "shadeColor" => set_color(&mut self.shade_color, value),
            "shadeOpacity" => set_f64(&mut self.shade_opacity, value, Self::OPACITY_RANGE),
            "cursor" => set_unbounded_i32(&mut self.cursor, value),
            "spotShape" => set_nonempty(&mut self.spot_shape, value),
            "spotRotation" => {
                set_f64(&mut self.spot_rotation, value, Self::SPOT_ROTATION_RANGE);
            }
            "Shape.Square/radius" => set_i32(
                &mut self.square_radius,
                value,
                Range {
                    minimum: 0,
                    maximum: 100,
                },
            ),
            "Shape.Star/points" => set_i32(
                &mut self.star_points,
                value,
                Range {
                    minimum: 3,
                    maximum: 100,
                },
            ),
            "Shape.Star/innerRadius" => set_i32(
                &mut self.star_inner_radius,
                value,
                Range {
                    minimum: 5,
                    maximum: 100,
                },
            ),
            "Shape.Ngon/sides" => set_i32(
                &mut self.ngon_sides,
                value,
                Range {
                    minimum: 3,
                    maximum: 100,
                },
            ),
            "showBorder" => set_bool(&mut self.show_border, value),
            "borderColor" => set_color(&mut self.border_color, value),
            "borderSize" => set_i32(&mut self.border_size, value, Self::BORDER_SIZE_RANGE),
            "borderOpacity" => set_f64(&mut self.border_opacity, value, Self::OPACITY_RANGE),
            "enableZoom" => set_bool(&mut self.zoom_enabled, value),
            "zoomFactor" => set_f64(&mut self.zoom_factor, value, Self::ZOOM_FACTOR_RANGE),
            "zoomMode" => set_parsed(&mut self.zoom_mode, value),
            "multiScreenOverlay" => set_bool(&mut self.multi_screen_overlay, value),
            "presentationTimerEnabled" => {
                set_bool(&mut self.presentation_timer_enabled, value);
            }
            "presentationTimerDurationSeconds" => {
                if let Ok(seconds) = value.parse::<i32>() {
                    self.presentation_timer_duration_seconds = seconds.max(1);
                }
            }
            _ => {}
        }
    }

    /// Return every persisted `General` key owned by the Rust port.
    #[must_use]
    pub fn general_entries(&self) -> Vec<(&'static str, String)> {
        let dot_mode = match self.dot_mode {
            DotMode::Solid => "solid",
            DotMode::Diffuse => "diffuse",
        };
        let zoom_mode = match self.zoom_mode {
            ZoomMode::Smooth => "smooth",
            ZoomMode::Text => "text",
            ZoomMode::Pixel => "pixel",
        };
        vec![
            ("showSpotShade", self.show_spot_shade.to_string()),
            ("spotSize", self.spot_size.to_string()),
            ("showCenterDot", self.show_center_dot.to_string()),
            ("dotSize", self.dot_size.to_string()),
            ("dotColor", self.dot_color.clone()),
            ("dotOpacity", self.dot_opacity.to_string()),
            ("dotMode", dot_mode.to_owned()),
            ("dotTrailEnabled", self.dot_trail_enabled.to_string()),
            ("shadeColor", self.shade_color.clone()),
            ("shadeOpacity", self.shade_opacity.to_string()),
            ("cursor", self.cursor.to_string()),
            ("spotShape", self.spot_shape.clone()),
            ("spotRotation", self.spot_rotation.to_string()),
            ("Shape.Square/radius", self.square_radius.to_string()),
            ("Shape.Star/points", self.star_points.to_string()),
            ("Shape.Star/innerRadius", self.star_inner_radius.to_string()),
            ("Shape.Ngon/sides", self.ngon_sides.to_string()),
            ("showBorder", self.show_border.to_string()),
            ("borderColor", self.border_color.clone()),
            ("borderSize", self.border_size.to_string()),
            ("borderOpacity", self.border_opacity.to_string()),
            ("enableZoom", self.zoom_enabled.to_string()),
            ("zoomFactor", self.zoom_factor.to_string()),
            ("zoomMode", zoom_mode.to_owned()),
            ("multiScreenOverlay", self.multi_screen_overlay.to_string()),
            (
                "presentationTimerEnabled",
                self.presentation_timer_enabled.to_string(),
            ),
            (
                "presentationTimerDurationSeconds",
                self.presentation_timer_duration_seconds.to_string(),
            ),
        ]
    }
}

impl Default for SpotlightSettings {
    fn default() -> Self {
        Self {
            show_spot_shade: true,
            spot_size: 32,
            show_center_dot: false,
            dot_size: 5,
            dot_color: "#ff0000".to_owned(),
            dot_opacity: 0.8,
            dot_mode: DotMode::Solid,
            dot_trail_enabled: false,
            shade_color: "#222222".to_owned(),
            shade_opacity: 0.3,
            cursor: 10,
            spot_shape: "spotshapes/Circle.qml".to_owned(),
            spot_rotation: 0.0,
            square_radius: 20,
            star_points: 5,
            star_inner_radius: 50,
            ngon_sides: 3,
            show_border: true,
            border_color: "#73d216".to_owned(),
            border_size: 4,
            border_opacity: 0.8,
            zoom_enabled: false,
            zoom_factor: 2.0,
            zoom_mode: ZoomMode::Smooth,
            multi_screen_overlay: false,
            presentation_timer_enabled: false,
            presentation_timer_duration_seconds: 15 * 60,
        }
    }
}

fn set_bool(target: &mut bool, value: &str) {
    let lower = value.to_ascii_lowercase();
    *target =
        lower == "true" || lower == "on" || lower.parse::<i64>().is_ok_and(|number| number > 0);
}

fn set_i32(target: &mut i32, value: &str, range: Range<i32>) {
    if let Ok(value) = value.parse() {
        *target = range.clamp(value);
    }
}

fn set_unbounded_i32(target: &mut i32, value: &str) {
    if let Ok(value) = value.parse() {
        *target = value;
    }
}

fn set_f64(target: &mut f64, value: &str, range: Range<f64>) {
    if let Ok(value) = value.parse() {
        *target = range.clamp(value);
    }
}

fn set_nonempty(target: &mut String, value: &str) {
    if !value.is_empty() {
        value.clone_into(target);
    }
}

fn set_color(target: &mut String, value: &str) {
    if value.is_empty() {
        return;
    }

    let channels: Vec<_> = value.split(',').map(str::trim).collect();
    if channels.len() == 3 || channels.len() == 4 {
        let Some(channels) = channels
            .iter()
            .map(|channel| channel.parse::<u8>().ok())
            .collect::<Option<Vec<_>>>()
        else {
            return;
        };
        *target = if channels.len() == 3 {
            format!("#{:02x}{:02x}{:02x}", channels[0], channels[1], channels[2])
        } else {
            format!(
                "#{:02x}{:02x}{:02x}{:02x}",
                channels[3], channels[0], channels[1], channels[2]
            )
        };
        return;
    }

    if !value.contains(',') {
        value.clone_into(target);
    }
}

fn set_parsed<T: FromStr>(target: &mut T, value: &str) {
    if let Ok(value) = value.parse() {
        *target = value;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_match_the_kconfig_schema() {
        let settings = SpotlightSettings::default();

        assert!(settings.show_spot_shade);
        assert_eq!(settings.spot_size, 32);
        assert_eq!(settings.dot_color, "#ff0000");
        assert_eq!(settings.cursor, 10);
        assert_eq!(settings.spot_shape, "spotshapes/Circle.qml");
        assert_eq!(settings.square_radius, 20);
        assert_eq!(settings.star_points, 5);
        assert_eq!(settings.presentation_timer_duration_seconds, 900);
    }

    #[test]
    fn legacy_values_are_parsed_and_clamped() {
        let mut settings = SpotlightSettings::default();
        for (key, value) in [
            ("showSpotShade", "off"),
            ("spotSize", "999"),
            ("dotOpacity", "-2"),
            ("dotColor", "0, 255, 0"),
            ("shadeColor", "34,34,34,128"),
            ("zoomMode", "pixel"),
            ("presentationTimerDurationSeconds", "0"),
            ("Shape.Star/points", "999"),
        ] {
            settings.apply_general_entry(key, value);
        }

        assert!(!settings.show_spot_shade);
        assert_eq!(settings.spot_size, 100);
        assert!(settings.dot_opacity.abs() < f64::EPSILON);
        assert_eq!(settings.dot_color, "#00ff00");
        assert_eq!(settings.shade_color, "#80222222");
        assert_eq!(settings.zoom_mode, ZoomMode::Pixel);
        assert_eq!(settings.presentation_timer_duration_seconds, 1);
        assert_eq!(settings.star_points, 100);
    }

    #[test]
    fn malformed_and_unknown_values_do_not_replace_defaults() {
        let mut settings = SpotlightSettings::default();
        settings.apply_general_entry("zoomFactor", "wat");
        settings.apply_general_entry("zoomMode", "nearest");
        settings.apply_general_entry("futureSetting", "42");
        settings.apply_general_entry("borderColor", "1,broken,3");

        assert!((settings.zoom_factor - 2.0).abs() < f64::EPSILON);
        assert_eq!(settings.zoom_mode, ZoomMode::Smooth);
        assert_eq!(settings.border_color, "#73d216");
    }
}
