#![allow(clippy::unnecessary_box_returns)]
#![allow(clippy::float_cmp)] // Generated CXX-Qt property setters compare values.

use core::pin::Pin;
use std::{
    ffi::OsString,
    fmt::Write as _,
    fs::File,
    io::{ErrorKind, Read},
    path::PathBuf,
    sync::mpsc::{self, Receiver, SyncSender, TryRecvError, TrySendError},
    thread,
    time::Duration,
};

use cxx_qt::CxxQtType;
use cxx_qt_lib::QString;
use projecteur_core::{
    config::{ConfigError, ProjecteurConfig},
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices},
    hid_report::{PresenterReport, decode_presenter_report},
    settings::{DotMode, SpotlightSettings, ZoomMode},
};

#[cxx_qt::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;

        include!("cxx-qt-lib/qanystringview.h");
        type QAnyStringView<'a> = cxx_qt_lib::QAnyStringView<'a>;

        include!("cxx-qt-lib/qqmlapplicationengine.h");
        type QQmlApplicationEngine = cxx_qt_lib::QQmlApplicationEngine;
    }

    #[namespace = "projecteur::generated"]
    unsafe extern "C++" {
        include!("projecteur_qml_loader.h");

        fn load_qml_module(
            engine: Pin<&mut QQmlApplicationEngine>,
            uri: QAnyStringView,
            type_name: QAnyStringView,
        );
    }

    #[auto_cxx_name]
    extern "RustQt" {
        /// Initial Rust backend exposed to QML during the migration.
        #[qobject]
        #[qml_element]
        #[qproperty(bool, show_window)]
        #[qproperty(bool, overlay_active)]
        #[qproperty(bool, preview_timeout_enabled)]
        #[qproperty(bool, presenter_connected)]
        #[qproperty(QString, presenter_device)]
        #[qproperty(i32, pointer_delta_x)]
        #[qproperty(i32, pointer_delta_y)]
        #[qproperty(i32, motion_serial)]
        #[qproperty(QString, status)]
        #[qproperty(QString, config_path)]
        #[qproperty(bool, show_spot_shade)]
        #[qproperty(i32, spot_size)]
        #[qproperty(bool, show_center_dot)]
        #[qproperty(i32, dot_size)]
        #[qproperty(QString, dot_color)]
        #[qproperty(f64, dot_opacity)]
        #[qproperty(QString, dot_mode)]
        #[qproperty(bool, dot_trail_enabled)]
        #[qproperty(QString, shade_color)]
        #[qproperty(f64, shade_opacity)]
        #[qproperty(i32, cursor)]
        #[qproperty(QString, spot_shape)]
        #[qproperty(f64, spot_rotation)]
        #[qproperty(bool, show_border)]
        #[qproperty(QString, border_color)]
        #[qproperty(i32, border_size)]
        #[qproperty(f64, border_opacity)]
        #[qproperty(bool, zoom_enabled)]
        #[qproperty(f64, zoom_factor)]
        #[qproperty(QString, zoom_mode)]
        #[qproperty(bool, multi_screen_overlay)]
        #[qproperty(bool, presentation_timer_enabled)]
        #[qproperty(i32, presentation_timer_duration_seconds)]
        type ProjecteurBackend = super::ProjecteurBackendRust;

        #[qinvokable]
        fn confirm_qml_loaded(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn poll_presenter(self: Pin<&mut ProjecteurBackend>);
    }

    impl cxx_qt::Initialize for ProjecteurBackend {}
}

/// Rust-owned state behind the initial QML bridge.
#[allow(clippy::struct_excessive_bools)]
pub struct ProjecteurBackendRust {
    show_window: bool,
    overlay_active: bool,
    preview_timeout_enabled: bool,
    presenter_connected: bool,
    presenter_device: QString,
    pointer_delta_x: i32,
    pointer_delta_y: i32,
    motion_serial: i32,
    status: QString,
    config_path: QString,
    show_spot_shade: bool,
    spot_size: i32,
    show_center_dot: bool,
    dot_size: i32,
    dot_color: QString,
    dot_opacity: f64,
    dot_mode: QString,
    dot_trail_enabled: bool,
    shade_color: QString,
    shade_opacity: f64,
    cursor: i32,
    spot_shape: QString,
    spot_rotation: f64,
    show_border: bool,
    border_color: QString,
    border_size: i32,
    border_opacity: f64,
    zoom_enabled: bool,
    zoom_factor: f64,
    zoom_mode: QString,
    multi_screen_overlay: bool,
    presentation_timer_enabled: bool,
    presentation_timer_duration_seconds: i32,
    presenter_events: Option<Receiver<PresenterEvent>>,
}

impl Default for ProjecteurBackendRust {
    fn default() -> Self {
        eprintln!("projecteur-rs: constructing Rust backend");
        let options = match CliOptions::parse(std::env::args_os().skip(1)) {
            Ok(options) => options,
            Err(message) => {
                eprintln!("projecteur-rs: {message}");
                CliOptions {
                    startup_error: Some(message),
                    ..CliOptions::default()
                }
            }
        };
        let (settings, config_path, mut status) = load_settings(&options);
        let presenter_events = match options.presenter {
            PresenterSelection::Disabled => {
                let _ = write!(status, "; presenter monitoring disabled");
                None
            }
            selection => match start_presenter_manager(selection) {
                Ok(receiver) => {
                    let _ = write!(status, "; waiting for presenter input");
                    Some(receiver)
                }
                Err(error) => {
                    let _ = write!(status, "; {error}");
                    eprintln!("projecteur-rs: {error}");
                    None
                }
            },
        };
        Self::from_settings(
            options.show_window,
            options.overlay_preview,
            presenter_events,
            &settings,
            config_path.as_deref(),
            &status,
        )
    }
}

impl ProjecteurBackendRust {
    fn from_settings(
        show_window: bool,
        overlay_active: bool,
        presenter_events: Option<Receiver<PresenterEvent>>,
        settings: &SpotlightSettings,
        config_path: Option<&std::path::Path>,
        status: &str,
    ) -> Self {
        let dot_mode = match settings.dot_mode {
            DotMode::Solid => "solid",
            DotMode::Diffuse => "diffuse",
        };
        let zoom_mode = match settings.zoom_mode {
            ZoomMode::Smooth => "smooth",
            ZoomMode::Text => "text",
            ZoomMode::Pixel => "pixel",
        };

        Self {
            show_window,
            overlay_active,
            preview_timeout_enabled: overlay_active,
            presenter_connected: false,
            presenter_device: QString::default(),
            pointer_delta_x: 0,
            pointer_delta_y: 0,
            motion_serial: 0,
            status: QString::from(status),
            config_path: QString::from(config_path.and_then(std::path::Path::to_str).unwrap_or("")),
            show_spot_shade: settings.show_spot_shade,
            spot_size: settings.spot_size,
            show_center_dot: settings.show_center_dot,
            dot_size: settings.dot_size,
            dot_color: QString::from(&settings.dot_color),
            dot_opacity: settings.dot_opacity,
            dot_mode: QString::from(dot_mode),
            dot_trail_enabled: settings.dot_trail_enabled,
            shade_color: QString::from(&settings.shade_color),
            shade_opacity: settings.shade_opacity,
            cursor: settings.cursor,
            spot_shape: QString::from(&settings.spot_shape),
            spot_rotation: settings.spot_rotation,
            show_border: settings.show_border,
            border_color: QString::from(&settings.border_color),
            border_size: settings.border_size,
            border_opacity: settings.border_opacity,
            zoom_enabled: settings.zoom_enabled,
            zoom_factor: settings.zoom_factor,
            zoom_mode: QString::from(zoom_mode),
            multi_screen_overlay: settings.multi_screen_overlay,
            presentation_timer_enabled: settings.presentation_timer_enabled,
            presentation_timer_duration_seconds: settings.presentation_timer_duration_seconds,
            presenter_events,
        }
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq)]
enum PresenterSelection {
    #[default]
    Auto,
    Explicit(PathBuf),
    Disabled,
}

#[derive(Debug, Default, Eq, PartialEq)]
struct CliOptions {
    show_window: bool,
    overlay_preview: bool,
    config_path: Option<PathBuf>,
    config_path_is_explicit: bool,
    presenter: PresenterSelection,
    startup_error: Option<String>,
}

impl CliOptions {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut options = Self::default();
        let mut arguments = arguments.into_iter();

        while let Some(argument) = arguments.next() {
            if argument == "--show-window" {
                options.show_window = true;
            } else if argument == "--overlay-preview" {
                options.overlay_preview = true;
            } else if argument == "--presenter" {
                let path = arguments
                    .next()
                    .ok_or_else(|| "--presenter requires a hidraw path".to_owned())?;
                options.presenter = if path == "auto" {
                    PresenterSelection::Auto
                } else {
                    PresenterSelection::Explicit(PathBuf::from(path))
                };
            } else if argument == "--no-presenter" {
                options.presenter = PresenterSelection::Disabled;
            } else if argument == "--cfg" {
                let path = arguments
                    .next()
                    .ok_or_else(|| "--cfg requires a file path".to_owned())?;
                options.config_path = Some(PathBuf::from(path));
                options.config_path_is_explicit = true;
            } else if let Some(argument) = argument.to_str() {
                if let Some(path) = argument.strip_prefix("--cfg=") {
                    if path.is_empty() {
                        return Err("--cfg requires a file path".to_owned());
                    }
                    options.config_path = Some(PathBuf::from(path));
                    options.config_path_is_explicit = true;
                } else if let Some(path) = argument.strip_prefix("--presenter=") {
                    if path.is_empty() {
                        return Err("--presenter requires a hidraw path".to_owned());
                    }
                    options.presenter = if path == "auto" {
                        PresenterSelection::Auto
                    } else {
                        PresenterSelection::Explicit(PathBuf::from(path))
                    };
                }
            }
        }

        if options.config_path.is_none() {
            options.config_path = default_config_path();
        }
        Ok(options)
    }
}

#[derive(Debug)]
enum PresenterEvent {
    Connected(PathBuf),
    Disconnected,
    Report(PresenterReport),
}

const PRESENTER_RESCAN_INTERVAL: Duration = Duration::from_millis(800);

fn start_presenter_manager(
    selection: PresenterSelection,
) -> Result<Receiver<PresenterEvent>, String> {
    let (sender, receiver) = mpsc::sync_channel(256);
    thread::Builder::new()
        .name("projecteur-presenter".to_owned())
        .spawn(move || {
            loop {
                let path = match &selection {
                    PresenterSelection::Auto => scan_devices()
                        .ok()
                        .and_then(|devices| preferred_hidraw_path(&devices)),
                    PresenterSelection::Explicit(path) => Some(path.clone()),
                    PresenterSelection::Disabled => return,
                };
                let Some(path) = path else {
                    thread::sleep(PRESENTER_RESCAN_INTERVAL);
                    continue;
                };
                let Ok(mut device) = File::open(&path) else {
                    thread::sleep(PRESENTER_RESCAN_INTERVAL);
                    continue;
                };
                if sender.send(PresenterEvent::Connected(path)).is_err() {
                    return;
                }
                read_presenter(&mut device, &sender);
                if sender.send(PresenterEvent::Disconnected).is_err() {
                    return;
                }
                thread::sleep(PRESENTER_RESCAN_INTERVAL);
            }
        })
        .map_err(|error| format!("cannot start presenter monitor: {error}"))?;
    Ok(receiver)
}

fn preferred_hidraw_path(devices: &[DiscoveredDevice]) -> Option<PathBuf> {
    devices
        .iter()
        .flat_map(|device| &device.nodes)
        .find(|node| node.kind == DeviceNodeKind::Hidraw && node.readable)
        .map(|node| node.path.clone())
}

fn read_presenter(device: &mut File, sender: &SyncSender<PresenterEvent>) {
    let mut bytes = [0_u8; 256];
    loop {
        let length = match device.read(&mut bytes) {
            Ok(0) | Err(_) => return,
            Ok(length) => length,
        };
        let Ok(report) = decode_presenter_report(&bytes[..length]) else {
            continue;
        };
        match sender.try_send(PresenterEvent::Report(report)) {
            Ok(()) | Err(TrySendError::Full(_)) => {}
            Err(TrySendError::Disconnected(_)) => return,
        }
    }
}

fn default_config_path() -> Option<PathBuf> {
    if let Some(directory) = std::env::var_os("XDG_CONFIG_HOME").filter(|value| !value.is_empty()) {
        return Some(PathBuf::from(directory).join("projecteurrc"));
    }
    std::env::var_os("HOME")
        .filter(|value| !value.is_empty())
        .map(|directory| PathBuf::from(directory).join(".config/projecteurrc"))
}

fn load_settings(options: &CliOptions) -> (SpotlightSettings, Option<PathBuf>, String) {
    if let Some(error) = &options.startup_error {
        return (
            SpotlightSettings::default(),
            options.config_path.clone(),
            error.clone(),
        );
    }
    let Some(path) = options.config_path.as_ref() else {
        return (
            SpotlightSettings::default(),
            None,
            "Using built-in settings; no configuration directory was found".to_owned(),
        );
    };

    match ProjecteurConfig::read(path) {
        Ok(config) => (
            config.spotlight_settings(),
            Some(path.clone()),
            format!("Loaded settings from {}", path.display()),
        ),
        Err(ConfigError::Io { source, .. })
            if source.kind() == ErrorKind::NotFound && !options.config_path_is_explicit =>
        {
            (
                SpotlightSettings::default(),
                Some(path.clone()),
                "Using built-in settings; no existing projecteurrc was found".to_owned(),
            )
        }
        Err(error) => {
            let status = format!("Could not load settings: {error}");
            eprintln!("projecteur-rs: {status}");
            (SpotlightSettings::default(), Some(path.clone()), status)
        }
    }
}

impl cxx_qt::Initialize for ffi::ProjecteurBackend {
    fn initialize(self: Pin<&mut Self>) {}
}

impl ffi::ProjecteurBackend {
    #[allow(clippy::unused_self)]
    fn confirm_qml_loaded(self: Pin<&mut Self>) {
        eprintln!("projecteur-rs: Rust backend and QML are connected");
    }

    fn poll_presenter(mut self: Pin<&mut Self>) {
        loop {
            let event = self
                .as_ref()
                .rust()
                .presenter_events
                .as_ref()
                .map(Receiver::try_recv);
            match event {
                Some(Ok(PresenterEvent::Connected(path))) => {
                    self.as_mut().set_presenter_connected(true);
                    self.as_mut()
                        .set_presenter_device(QString::from(path.to_str().unwrap_or("presenter")));
                }
                Some(Ok(PresenterEvent::Disconnected)) => {
                    self.as_mut().set_presenter_connected(false);
                    self.as_mut().set_presenter_device(QString::default());
                }
                Some(Ok(PresenterEvent::Report(PresenterReport::Pointer(pointer)))) => {
                    if pointer.x == 0 && pointer.y == 0 {
                        continue;
                    }
                    self.as_mut().set_pointer_delta_x(i32::from(pointer.x));
                    self.as_mut().set_pointer_delta_y(i32::from(pointer.y));
                    let serial = self.as_ref().motion_serial().wrapping_add(1);
                    self.as_mut().set_motion_serial(serial);
                    self.as_mut().set_overlay_active(true);
                    self.as_mut().set_preview_timeout_enabled(false);
                }
                Some(Ok(PresenterEvent::Report(PresenterReport::Keyboard(_)))) => {}
                Some(Err(TryRecvError::Empty)) | None => break,
                Some(Err(TryRecvError::Disconnected)) => {
                    self.as_mut().set_presenter_connected(false);
                    self.as_mut().set_presenter_device(QString::default());
                    break;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_legacy_config_argument_and_diagnostic_window_flag() {
        let options = CliOptions::parse([
            OsString::from("--show-window"),
            OsString::from("--cfg"),
            OsString::from("/tmp/projecteur-test.rc"),
        ])
        .unwrap();

        assert!(options.show_window);
        assert!(!options.overlay_preview);
        assert!(options.config_path_is_explicit);
        assert_eq!(options.presenter, PresenterSelection::Auto);
        assert_eq!(
            options.config_path,
            Some(PathBuf::from("/tmp/projecteur-test.rc"))
        );
    }

    #[test]
    fn parses_overlay_preview_flag() {
        let options = CliOptions::parse([OsString::from("--overlay-preview")]).unwrap();

        assert!(options.overlay_preview);
    }

    #[test]
    fn parses_presenter_path_forms() {
        let separated = CliOptions::parse([
            OsString::from("--presenter"),
            OsString::from("/dev/hidraw5"),
        ])
        .unwrap();
        let equals = CliOptions::parse([OsString::from("--presenter=/dev/hidraw6")]).unwrap();

        assert_eq!(
            separated.presenter,
            PresenterSelection::Explicit(PathBuf::from("/dev/hidraw5"))
        );
        assert_eq!(
            equals.presenter,
            PresenterSelection::Explicit(PathBuf::from("/dev/hidraw6"))
        );
    }

    #[test]
    fn parses_presenter_auto_and_disabled_modes() {
        let automatic = CliOptions::parse([OsString::from("--presenter=auto")]).unwrap();
        let disabled = CliOptions::parse([OsString::from("--no-presenter")]).unwrap();

        assert_eq!(automatic.presenter, PresenterSelection::Auto);
        assert_eq!(disabled.presenter, PresenterSelection::Disabled);
    }

    #[test]
    fn parses_equals_form_and_rejects_a_missing_path() {
        let options = CliOptions::parse([OsString::from("--cfg=/tmp/projecteur-test.rc")]).unwrap();
        assert_eq!(
            options.config_path,
            Some(PathBuf::from("/tmp/projecteur-test.rc"))
        );

        let error = CliOptions::parse([OsString::from("--cfg")]).unwrap_err();
        assert_eq!(error, "--cfg requires a file path");
    }
}
