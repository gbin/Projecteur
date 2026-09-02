#![allow(clippy::unnecessary_box_returns)]
#![allow(clippy::float_cmp)] // Generated CXX-Qt property setters compare values.

use core::pin::Pin;
use std::{
    collections::HashMap,
    ffi::OsString,
    fmt::Write as _,
    fs::{File, OpenOptions},
    io::ErrorKind,
    path::PathBuf,
    sync::mpsc::{self, Receiver, SyncSender, TryRecvError},
    thread,
    time::{Duration, Instant},
};

use cxx_qt::CxxQtType;
use cxx_qt_lib::QString;
use projecteur_core::{
    config::{ConfigError, ProjecteurConfig},
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices},
    hid_report::{PresenterReport, decode_presenter_report},
    hidpp::{
        BatteryInfo, query_battery, read_report_with_timeout, send_vibration,
        spotlight_device_index,
    },
    settings::{DotMode, SpotlightSettings, ZoomMode},
    uinput::{GrabbedEventDevice, VirtualKeyboard},
};

use crate::control::{self, ControlCommand, ControlHandle, ControlSnapshot};
use crate::screencast::{CaptureEvent, ScreenRegion, ScreencastManager, StreamIdentifier};

#[cxx_qt::bridge]
pub mod ffi {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;

        include!("cxx-qt-lib/qanystringview.h");
        type QAnyStringView<'a> = cxx_qt_lib::QAnyStringView<'a>;

        include!("cxx-qt-lib/qqmlapplicationengine.h");
        type QQmlApplicationEngine = cxx_qt_lib::QQmlApplicationEngine;

        include!("cxx-qt-lib/qguiapplication.h");
        type QGuiApplication = cxx_qt_lib::QGuiApplication;
    }

    #[namespace = "projecteur::generated"]
    unsafe extern "C++" {
        include!("projecteur_qml_loader.h");

        fn create_widget_application() -> UniquePtr<QGuiApplication>;

        fn setup_application_metadata();

        fn show_about_dialog();

        fn send_notification(
            event_id: &QString,
            title: &QString,
            text: &QString,
            icon_name: &QString,
        );

        fn load_qml_module(
            engine: Pin<&mut QQmlApplicationEngine>,
            uri: QAnyStringView,
            type_name: QAnyStringView,
        );

        fn setup_global_shortcuts();

        fn show_global_shortcuts_editor();
    }

    #[auto_cxx_name]
    extern "RustQt" {
        /// Initial Rust backend exposed to QML during the migration.
        #[qobject]
        #[qml_element]
        #[qproperty(bool, show_window)]
        #[qproperty(bool, quit_requested)]
        #[qproperty(bool, tray_visible)]
        #[qproperty(bool, overlay_disabled)]
        #[qproperty(bool, overlay_active)]
        #[qproperty(bool, preview_timeout_enabled)]
        #[qproperty(bool, presenter_connected)]
        #[qproperty(QString, presenter_device)]
        #[qproperty(QString, presenter_details)]
        #[qproperty(i32, device_input_sequence_interval)]
        #[qproperty(i32, device_timer_haptic_strength)]
        #[qproperty(bool, button_forwarding)]
        #[qproperty(i32, battery_level)]
        #[qproperty(QString, battery_status)]
        #[qproperty(i32, pointer_delta_x)]
        #[qproperty(i32, pointer_delta_y)]
        #[qproperty(i32, motion_serial)]
        #[qproperty(i32, capture_generation)]
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
        #[qproperty(i32, square_radius)]
        #[qproperty(i32, star_points)]
        #[qproperty(i32, star_inner_radius)]
        #[qproperty(i32, ngon_sides)]
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
        #[qproperty(QString, presentation_timer_state)]
        #[qproperty(i32, presentation_timer_remaining_seconds)]
        #[qproperty(QString, preset_names)]
        type ProjecteurBackend = super::ProjecteurBackendRust;

        #[qinvokable]
        fn confirm_qml_loaded(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn poll_presenter(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn request_screen_capture(
            self: Pin<&mut ProjecteurBackend>,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
        );

        #[qinvokable]
        fn capture_node_id(
            self: &ProjecteurBackend,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
        ) -> u32;

        #[qinvokable]
        fn capture_object_serial(
            self: &ProjecteurBackend,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
        ) -> u64;

        #[qinvokable]
        fn capture_snapshot_source(
            self: &ProjecteurBackend,
            x: i32,
            y: i32,
            width: i32,
            height: i32,
        ) -> QString;

        #[qinvokable]
        fn save_settings(self: Pin<&mut ProjecteurBackend>) -> bool;

        #[qinvokable]
        fn begin_preferences(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn restore_applied_settings(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn restore_default_settings(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn show_overlay_test(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn load_preset(self: Pin<&mut ProjecteurBackend>, name: &QString) -> bool;

        #[qinvokable]
        fn save_preset(self: Pin<&mut ProjecteurBackend>, name: &QString) -> bool;

        #[qinvokable]
        fn remove_preset(self: Pin<&mut ProjecteurBackend>, name: &QString) -> bool;

        #[qinvokable]
        fn update_device_input_sequence_interval(
            self: Pin<&mut ProjecteurBackend>,
            interval_ms: i32,
        ) -> bool;

        #[qinvokable]
        fn update_device_timer_haptic_strength(
            self: Pin<&mut ProjecteurBackend>,
            strength: i32,
        ) -> bool;

        #[qinvokable]
        fn configure_global_shortcuts(self: &ProjecteurBackend);
    }

    impl cxx_qt::Initialize for ProjecteurBackend {}
}

/// Rust-owned state behind the initial QML bridge.
#[allow(clippy::struct_excessive_bools)]
pub struct ProjecteurBackendRust {
    show_window: bool,
    quit_requested: bool,
    tray_visible: bool,
    overlay_disabled: bool,
    overlay_active: bool,
    preview_timeout_enabled: bool,
    presenter_connected: bool,
    presenter_device: QString,
    presenter_details: QString,
    device_input_sequence_interval: i32,
    device_timer_haptic_strength: i32,
    connected_device_name: String,
    current_device_group: String,
    button_forwarding: bool,
    battery_level: i32,
    battery_status: QString,
    pointer_delta_x: i32,
    pointer_delta_y: i32,
    motion_serial: i32,
    capture_generation: i32,
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
    square_radius: i32,
    star_points: i32,
    star_inner_radius: i32,
    ngon_sides: i32,
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
    presentation_timer_state: QString,
    presentation_timer_remaining_seconds: i32,
    preset_names: QString,
    current_preset: QString,
    applied_settings: SpotlightSettings,
    control_tray_visible: bool,
    control_handle: ControlHandle,
    control_commands: Receiver<ControlCommand>,
    timer_deadline: Option<Instant>,
    navigation_pressed: bool,
    presenter_events: Option<Receiver<PresenterEvent>>,
    screencast_manager: Option<ScreencastManager>,
    capture_streams: HashMap<ScreenRegion, StreamIdentifier>,
    capture_snapshots: HashMap<ScreenRegion, QString>,
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
        let presenter_events = match options.presenter.clone() {
            PresenterSelection::Disabled => {
                let _ = write!(status, "; presenter monitoring disabled");
                None
            }
            selection => match start_presenter_manager(selection, options.uinput_enabled) {
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
            &options,
            presenter_events,
            &settings,
            config_path.as_deref(),
            &status,
        )
    }
}

impl ProjecteurBackendRust {
    fn from_settings(
        options: &CliOptions,
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

        let control_snapshot = ControlSnapshot {
            tray_visible: options.tray_visible,
            overlay_enabled: !options.overlay_disabled,
            spotlight_active: options.overlay_preview,
            presets: preset_names(config_path)
                .lines()
                .map(str::to_owned)
                .collect(),
            timer_enabled: settings.presentation_timer_enabled,
            timer_duration_seconds: settings.presentation_timer_duration_seconds,
            timer_remaining_seconds: settings.presentation_timer_duration_seconds,
            ..ControlSnapshot::default()
        };
        let (control_handle, control_commands) = control::start(control_snapshot);

        Self {
            show_window: options.show_window,
            quit_requested: false,
            tray_visible: options.tray_visible,
            overlay_disabled: options.overlay_disabled,
            overlay_active: options.overlay_preview,
            preview_timeout_enabled: options.overlay_preview,
            presenter_connected: false,
            presenter_device: QString::default(),
            presenter_details: QString::default(),
            device_input_sequence_interval: 250,
            device_timer_haptic_strength: 50,
            connected_device_name: String::new(),
            current_device_group: String::new(),
            button_forwarding: false,
            battery_level: -1,
            battery_status: QString::default(),
            pointer_delta_x: 0,
            pointer_delta_y: 0,
            motion_serial: 0,
            capture_generation: 0,
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
            square_radius: settings.square_radius,
            star_points: settings.star_points,
            star_inner_radius: settings.star_inner_radius,
            ngon_sides: settings.ngon_sides,
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
            presentation_timer_state: QString::from("idle"),
            presentation_timer_remaining_seconds: settings.presentation_timer_duration_seconds,
            preset_names: QString::from(preset_names(config_path)),
            current_preset: QString::default(),
            applied_settings: settings.clone(),
            control_tray_visible: options.tray_visible,
            control_handle,
            control_commands,
            timer_deadline: None,
            navigation_pressed: false,
            presenter_events,
            screencast_manager: None,
            capture_streams: HashMap::new(),
            capture_snapshots: HashMap::new(),
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

#[derive(Debug, Eq, PartialEq)]
#[allow(clippy::struct_excessive_bools)]
struct CliOptions {
    show_window: bool,
    tray_visible: bool,
    overlay_disabled: bool,
    overlay_preview: bool,
    config_path: Option<PathBuf>,
    config_path_is_explicit: bool,
    presenter: PresenterSelection,
    uinput_enabled: bool,
    startup_error: Option<String>,
}

impl Default for CliOptions {
    fn default() -> Self {
        Self {
            show_window: false,
            tray_visible: true,
            overlay_disabled: false,
            overlay_preview: false,
            config_path: None,
            config_path_is_explicit: false,
            presenter: PresenterSelection::Auto,
            uinput_enabled: true,
            startup_error: None,
        }
    }
}

impl CliOptions {
    fn parse(arguments: impl IntoIterator<Item = OsString>) -> Result<Self, String> {
        let mut options = Self::default();
        let mut arguments = arguments.into_iter();

        while let Some(argument) = arguments.next() {
            if argument == "--show-window" || argument == "--show-dialog" {
                options.show_window = true;
            } else if argument == "--hide-systray-icon" {
                options.tray_visible = false;
            } else if argument == "--disable-overlay" {
                options.overlay_disabled = true;
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
            } else if argument == "--disable-uinput" || argument == "--no-uinput" {
                options.uinput_enabled = false;
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
    ButtonForwarding(bool),
    Battery(BatteryInfo),
}

const PRESENTER_RESCAN_INTERVAL: Duration = Duration::from_millis(800);

fn start_presenter_manager(
    selection: PresenterSelection,
    uinput_enabled: bool,
) -> Result<Receiver<PresenterEvent>, String> {
    let (sender, receiver) = mpsc::sync_channel(256);
    let presenter_selection = selection.clone();
    let presenter_sender = sender.clone();
    thread::Builder::new()
        .name("projecteur-presenter".to_owned())
        .spawn(move || {
            loop {
                let path = match &presenter_selection {
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
                let battery_device_index = scan_devices()
                    .ok()
                    .and_then(|devices| battery_device_index_for_path(&path, &devices));
                let writable_device = OpenOptions::new().read(true).write(true).open(&path);
                let (mut device, battery_device_index) = if let Ok(device) = writable_device {
                    (device, battery_device_index)
                } else {
                    let Ok(device) = File::open(&path) else {
                        thread::sleep(PRESENTER_RESCAN_INTERVAL);
                        continue;
                    };
                    (device, None)
                };
                if presenter_sender
                    .send(PresenterEvent::Connected(path.clone()))
                    .is_err()
                {
                    return;
                }
                read_presenter(&mut device, &presenter_sender, &path, battery_device_index);
                if presenter_sender.send(PresenterEvent::Disconnected).is_err() {
                    return;
                }
                thread::sleep(PRESENTER_RESCAN_INTERVAL);
            }
        })
        .map_err(|error| format!("cannot start presenter monitor: {error}"))?;
    if uinput_enabled {
        thread::Builder::new()
            .name("projecteur-presenter-buttons".to_owned())
            .spawn(move || run_button_manager(&selection, &sender))
            .map_err(|error| format!("cannot start presenter button forwarding: {error}"))?;
    }
    Ok(receiver)
}

fn preferred_hidraw_path(devices: &[DiscoveredDevice]) -> Option<PathBuf> {
    devices
        .iter()
        .flat_map(|device| &device.nodes)
        .find(|node| node.kind == DeviceNodeKind::Hidraw && node.readable)
        .map(|node| node.path.clone())
}

fn battery_device_index_for_path(
    path: &std::path::Path,
    devices: &[DiscoveredDevice],
) -> Option<u8> {
    let device = devices
        .iter()
        .find(|device| device.nodes.iter().any(|node| node.path == path))?;
    spotlight_device_index(device.id)
}

fn preferred_button_path(
    selection: &PresenterSelection,
    devices: &[DiscoveredDevice],
) -> Option<PathBuf> {
    let device = match selection {
        PresenterSelection::Auto => devices.iter().find(|device| {
            device
                .nodes
                .iter()
                .any(|node| node.kind == DeviceNodeKind::Hidraw && node.readable)
        }),
        PresenterSelection::Explicit(path) => devices
            .iter()
            .find(|device| device.nodes.iter().any(|node| node.path == *path)),
        PresenterSelection::Disabled => None,
    }?;
    device
        .nodes
        .iter()
        .find(|node| {
            node.kind == DeviceNodeKind::Event && node.readable && !node.has_relative_pointer
        })
        .map(|node| node.path.clone())
}

fn run_button_manager(selection: &PresenterSelection, sender: &SyncSender<PresenterEvent>) {
    let mut reported_uinput_error = false;
    loop {
        let path = scan_devices()
            .ok()
            .and_then(|devices| preferred_button_path(selection, &devices));
        let Some(path) = path else {
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        };
        let Ok(mut keyboard) = VirtualKeyboard::create() else {
            if !reported_uinput_error {
                eprintln!("projecteur-rs: uinput unavailable; presenter buttons remain ungrabbed");
                reported_uinput_error = true;
            }
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        };
        let Ok(mut source) = GrabbedEventDevice::open(&path) else {
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        };
        if sender.send(PresenterEvent::ButtonForwarding(true)).is_err() {
            return;
        }
        while let Ok(event) = source.read_event() {
            if keyboard.emit(event).is_err() {
                break;
            }
        }
        if sender
            .send(PresenterEvent::ButtonForwarding(false))
            .is_err()
        {
            return;
        }
        thread::sleep(PRESENTER_RESCAN_INTERVAL);
    }
}

const BATTERY_REFRESH_INTERVAL: Duration = Duration::from_secs(5 * 60);

fn read_presenter(
    device: &mut File,
    sender: &SyncSender<PresenterEvent>,
    path: &std::path::Path,
    battery_device_index: Option<u8>,
) {
    let mut bytes = [0_u8; 256];
    let mut next_battery_refresh = battery_device_index.map(|_| Instant::now());
    loop {
        let timeout = next_battery_refresh.map_or(Duration::from_secs(60), |deadline| {
            deadline.saturating_duration_since(Instant::now())
        });
        match read_report_with_timeout(device, &mut bytes, timeout) {
            Ok(Some(0)) | Err(_) => return,
            Ok(Some(length)) => forward_presenter_report(&bytes[..length], sender),
            Ok(None) => {}
        }

        let (Some(device_index), Some(deadline)) = (battery_device_index, next_battery_refresh)
        else {
            continue;
        };
        if Instant::now() < deadline {
            continue;
        }
        match query_battery(device, device_index, |report| {
            forward_presenter_report(report, sender);
        }) {
            Ok(info) => {
                if sender.send(PresenterEvent::Battery(info)).is_err() {
                    return;
                }
            }
            Err(error) => eprintln!(
                "projecteur-rs: battery status unavailable on {}: {error}",
                path.display()
            ),
        }
        next_battery_refresh = Some(Instant::now() + BATTERY_REFRESH_INTERVAL);
    }
}

fn forward_presenter_report(report: &[u8], sender: &SyncSender<PresenterEvent>) {
    let Ok(report) = decode_presenter_report(report) else {
        return;
    };
    let _ = sender.try_send(PresenterEvent::Report(report));
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

fn preset_names(path: Option<&std::path::Path>) -> String {
    let Some(path) = path else {
        return String::new();
    };
    let Ok(config) = ProjecteurConfig::read(path) else {
        return String::new();
    };
    config
        .group_names()
        .filter_map(|name| name.strip_prefix("Preset_"))
        .collect::<Vec<_>>()
        .join("\n")
}

fn screen_region(x: i32, y: i32, width: i32, height: i32) -> Option<ScreenRegion> {
    Some(ScreenRegion {
        x,
        y,
        width: u32::try_from(width).ok().filter(|width| *width > 0)?,
        height: u32::try_from(height).ok().filter(|height| *height > 0)?,
    })
}

fn load_device_settings(config_path: &std::path::Path, group: &str) -> (i32, i32) {
    let config = ProjecteurConfig::read(config_path).unwrap_or_default();
    let interval = config
        .value(group, "inputSequenceInterval")
        .and_then(|value| value.parse::<i32>().ok())
        .unwrap_or(250)
        .clamp(
            SpotlightSettings::INPUT_SEQUENCE_INTERVAL_RANGE.minimum,
            SpotlightSettings::INPUT_SEQUENCE_INTERVAL_RANGE.maximum,
        );
    let strength = config
        .value(group, "presentationTimerHapticStrength")
        .and_then(|value| value.parse::<i32>().ok())
        .unwrap_or(50)
        .clamp(0, 100);
    (interval, strength)
}

fn format_device_details(device: &DiscoveredDevice) -> String {
    let bus = match device.id.bus {
        projecteur_core::Bus::Usb => "Usb",
        projecteur_core::Bus::Bluetooth => "Bluetooth",
    };
    let mut details = format!(
        "Name\t{}\nVendorId\t{:04x}\nProductId\t{:04x}\nPhys\t{}\nBus Type\t{bus}\n\nSub devices:\n",
        device.display_name(),
        device.id.vendor,
        device.id.product,
        device.physical_id
    );
    for node in &device.nodes {
        let access = match (node.readable, node.writable) {
            (true, true) => "ReadWrite",
            (true, false) => "ReadOnly",
            (false, true) => "WriteOnly",
            (false, false) => "Inaccessible",
        };
        let kind = match node.kind {
            DeviceNodeKind::Event => "Event",
            DeviceNodeKind::Hidraw => "Hidraw, HID++",
        };
        let _ = writeln!(details, "• {}: [{access}, {kind}]", node.path.display());
    }
    details
}

fn send_timer_completion_feedback(config_path: &std::path::Path) {
    let config = ProjecteurConfig::read(config_path).unwrap_or_default();
    let Ok(devices) = scan_devices() else {
        return;
    };
    for device in devices {
        let Some(device_index) = spotlight_device_index(device.id) else {
            continue;
        };
        let group = format!("Device_{:04x}_{:04x}", device.id.vendor, device.id.product);
        let strength = config
            .value(&group, "presentationTimerHapticStrength")
            .and_then(|value| value.parse::<i32>().ok())
            .unwrap_or(50)
            .clamp(0, 100);
        if strength == 0 {
            continue;
        }
        let intensity = u8::try_from((strength * 255 + 50) / 100).unwrap_or(255);
        let Some(node) = device
            .nodes
            .iter()
            .find(|node| node.kind == DeviceNodeKind::Hidraw && node.readable && node.writable)
        else {
            continue;
        };
        let Ok(mut file) = OpenOptions::new().read(true).write(true).open(&node.path) else {
            continue;
        };
        if let Err(error) = send_vibration(&mut file, device_index, intensity, 0, |_| {}) {
            eprintln!(
                "projecteur-rs: timer feedback failed on {}: {error}",
                node.path.display()
            );
        }
    }
}

impl cxx_qt::Initialize for ffi::ProjecteurBackend {
    fn initialize(self: Pin<&mut Self>) {}
}

impl ffi::ProjecteurBackend {
    #[allow(clippy::unused_self)]
    fn configure_global_shortcuts(&self) {
        ffi::show_global_shortcuts_editor();
    }

    #[allow(clippy::unused_self)]
    fn confirm_qml_loaded(self: Pin<&mut Self>) {
        eprintln!("projecteur-rs: Rust backend and QML are connected");
    }

    fn save_settings(mut self: Pin<&mut Self>) -> bool {
        let path = PathBuf::from(String::from(&self.as_ref().rust().config_path));
        let settings = self.as_ref().current_spotlight_settings();
        if path.as_os_str().is_empty() {
            self.as_mut()
                .set_status(QString::from("Cannot save settings without a config path"));
            return false;
        }
        let mut config = match ProjecteurConfig::read(&path) {
            Ok(config) => config,
            Err(ConfigError::Io { source, .. }) if source.kind() == ErrorKind::NotFound => {
                ProjecteurConfig::default()
            }
            Err(error) => {
                self.as_mut()
                    .set_status(QString::from(format!("Could not read settings: {error}")));
                return false;
            }
        };
        config.set_spotlight_settings(&settings);
        match config.write(&path) {
            Ok(()) => {
                self.as_mut().rust_mut().applied_settings = settings;
                self.as_mut().set_status(QString::from(format!(
                    "Saved settings to {}",
                    path.display()
                )));
                true
            }
            Err(error) => {
                self.as_mut()
                    .set_status(QString::from(format!("Could not save settings: {error}")));
                false
            }
        }
    }

    fn current_spotlight_settings(self: Pin<&Self>) -> SpotlightSettings {
        let rust = self.rust();
        SpotlightSettings {
            show_spot_shade: rust.show_spot_shade,
            spot_size: rust.spot_size,
            show_center_dot: rust.show_center_dot,
            dot_size: rust.dot_size,
            dot_color: String::from(&rust.dot_color),
            dot_opacity: rust.dot_opacity,
            dot_mode: String::from(&rust.dot_mode).parse().unwrap_or_default(),
            dot_trail_enabled: rust.dot_trail_enabled,
            shade_color: String::from(&rust.shade_color),
            shade_opacity: rust.shade_opacity,
            cursor: rust.cursor,
            spot_shape: String::from(&rust.spot_shape),
            spot_rotation: rust.spot_rotation,
            square_radius: rust.square_radius,
            star_points: rust.star_points,
            star_inner_radius: rust.star_inner_radius,
            ngon_sides: rust.ngon_sides,
            show_border: rust.show_border,
            border_color: String::from(&rust.border_color),
            border_size: rust.border_size,
            border_opacity: rust.border_opacity,
            zoom_enabled: rust.zoom_enabled,
            zoom_factor: rust.zoom_factor,
            zoom_mode: String::from(&rust.zoom_mode).parse().unwrap_or_default(),
            multi_screen_overlay: rust.multi_screen_overlay,
            presentation_timer_enabled: rust.presentation_timer_enabled,
            presentation_timer_duration_seconds: rust.presentation_timer_duration_seconds,
        }
    }

    fn apply_spotlight_settings(mut self: Pin<&mut Self>, settings: &SpotlightSettings) {
        self.as_mut().set_show_spot_shade(settings.show_spot_shade);
        self.as_mut().set_spot_size(settings.spot_size);
        self.as_mut().set_show_center_dot(settings.show_center_dot);
        self.as_mut().set_dot_size(settings.dot_size);
        self.as_mut()
            .set_dot_color(QString::from(&settings.dot_color));
        self.as_mut().set_dot_opacity(settings.dot_opacity);
        self.as_mut()
            .set_dot_mode(QString::from(match settings.dot_mode {
                DotMode::Solid => "solid",
                DotMode::Diffuse => "diffuse",
            }));
        self.as_mut()
            .set_dot_trail_enabled(settings.dot_trail_enabled);
        self.as_mut()
            .set_shade_color(QString::from(&settings.shade_color));
        self.as_mut().set_shade_opacity(settings.shade_opacity);
        self.as_mut().set_cursor(settings.cursor);
        self.as_mut()
            .set_spot_shape(QString::from(&settings.spot_shape));
        self.as_mut().set_spot_rotation(settings.spot_rotation);
        self.as_mut().set_square_radius(settings.square_radius);
        self.as_mut().set_star_points(settings.star_points);
        self.as_mut()
            .set_star_inner_radius(settings.star_inner_radius);
        self.as_mut().set_ngon_sides(settings.ngon_sides);
        self.as_mut().set_show_border(settings.show_border);
        self.as_mut()
            .set_border_color(QString::from(&settings.border_color));
        self.as_mut().set_border_size(settings.border_size);
        self.as_mut().set_border_opacity(settings.border_opacity);
        self.as_mut().set_zoom_enabled(settings.zoom_enabled);
        self.as_mut().set_zoom_factor(settings.zoom_factor);
        self.as_mut()
            .set_zoom_mode(QString::from(match settings.zoom_mode {
                ZoomMode::Smooth => "smooth",
                ZoomMode::Text => "text",
                ZoomMode::Pixel => "pixel",
            }));
        self.as_mut()
            .set_multi_screen_overlay(settings.multi_screen_overlay);
        self.as_mut()
            .set_presentation_timer_enabled(settings.presentation_timer_enabled);
        self.as_mut()
            .set_presentation_timer_duration_seconds(settings.presentation_timer_duration_seconds);
    }

    fn begin_preferences(mut self: Pin<&mut Self>) {
        let current = self.as_ref().current_spotlight_settings();
        self.as_mut().rust_mut().applied_settings = current;
    }

    fn restore_applied_settings(mut self: Pin<&mut Self>) {
        let settings = self.as_ref().rust().applied_settings.clone();
        self.as_mut().apply_spotlight_settings(&settings);
    }

    fn restore_default_settings(mut self: Pin<&mut Self>) {
        self.as_mut()
            .apply_spotlight_settings(&SpotlightSettings::default());
    }

    fn show_overlay_test(mut self: Pin<&mut Self>) {
        self.as_mut().set_preview_timeout_enabled(true);
        self.as_mut().set_overlay_active(true);
    }

    fn load_preset(mut self: Pin<&mut Self>, name: &QString) -> bool {
        let name = String::from(name).trim().to_owned();
        if name.is_empty() {
            return false;
        }
        let path = PathBuf::from(String::from(&self.as_ref().rust().config_path));
        let Ok(config) = ProjecteurConfig::read(&path) else {
            return false;
        };
        let group = format!("Preset_{name}");
        if config.group(&group).is_none() {
            return false;
        }
        let settings = config.spotlight_settings_from_group(&group);
        self.as_mut().apply_spotlight_settings(&settings);
        self.as_mut().rust_mut().current_preset = QString::from(&name);
        true
    }

    fn load_relative_preset(mut self: Pin<&mut Self>, offset: i32) {
        let names = String::from(&self.as_ref().rust().preset_names)
            .lines()
            .map(str::to_owned)
            .collect::<Vec<_>>();
        if names.is_empty() {
            return;
        }

        let current = String::from(&self.as_ref().rust().current_preset);
        let current_index = names.iter().position(|name| name == &current);
        let target_index = if offset < 0 {
            current_index
                .filter(|index| *index > 0)
                .map_or(names.len() - 1, |index| index - 1)
        } else {
            current_index
                .filter(|index| *index + 1 < names.len())
                .map_or(0, |index| index + 1)
        };
        let _ = self
            .as_mut()
            .load_preset(&QString::from(&names[target_index]));
    }

    fn save_preset(mut self: Pin<&mut Self>, name: &QString) -> bool {
        let name = String::from(name).trim().to_owned();
        if name.is_empty() {
            return false;
        }
        let path = PathBuf::from(String::from(&self.as_ref().rust().config_path));
        let mut config = match ProjecteurConfig::read(&path) {
            Ok(config) => config,
            Err(ConfigError::Io { source, .. }) if source.kind() == ErrorKind::NotFound => {
                ProjecteurConfig::default()
            }
            Err(_) => return false,
        };
        let settings = self.as_ref().current_spotlight_settings();
        config.set_spotlight_settings_in_group(&format!("Preset_{name}"), &settings);
        if config.write(&path).is_err() {
            return false;
        }
        self.as_mut()
            .set_preset_names(QString::from(preset_names(Some(&path))));
        true
    }

    fn remove_preset(mut self: Pin<&mut Self>, name: &QString) -> bool {
        let name = String::from(name).trim().to_owned();
        if name.is_empty() {
            return false;
        }
        let path = PathBuf::from(String::from(&self.as_ref().rust().config_path));
        let Ok(mut config) = ProjecteurConfig::read(&path) else {
            return false;
        };
        config.remove_group(&format!("Preset_{name}"));
        if config.write(&path).is_err() {
            return false;
        }
        self.as_mut()
            .set_preset_names(QString::from(preset_names(Some(&path))));
        true
    }

    fn update_device_input_sequence_interval(mut self: Pin<&mut Self>, interval_ms: i32) -> bool {
        let interval_ms = interval_ms.clamp(
            SpotlightSettings::INPUT_SEQUENCE_INTERVAL_RANGE.minimum,
            SpotlightSettings::INPUT_SEQUENCE_INTERVAL_RANGE.maximum,
        );
        if !self
            .as_mut()
            .save_device_setting("inputSequenceInterval", interval_ms)
        {
            return false;
        }
        self.as_mut()
            .set_device_input_sequence_interval(interval_ms);
        true
    }

    fn update_device_timer_haptic_strength(mut self: Pin<&mut Self>, strength: i32) -> bool {
        let strength = strength.clamp(0, 100);
        if !self
            .as_mut()
            .save_device_setting("presentationTimerHapticStrength", strength)
        {
            return false;
        }
        self.as_mut().set_device_timer_haptic_strength(strength);
        true
    }

    fn save_device_setting(mut self: Pin<&mut Self>, key: &str, value: i32) -> bool {
        let (path, group) = {
            let this = self.as_ref();
            let rust = this.rust();
            (
                PathBuf::from(String::from(&rust.config_path)),
                rust.current_device_group.clone(),
            )
        };
        if path.as_os_str().is_empty() || group.is_empty() {
            return false;
        }
        let mut config = match ProjecteurConfig::read(&path) {
            Ok(config) => config,
            Err(ConfigError::Io { source, .. }) if source.kind() == ErrorKind::NotFound => {
                ProjecteurConfig::default()
            }
            Err(error) => {
                self.as_mut().set_status(QString::from(format!(
                    "Could not read device settings: {error}"
                )));
                return false;
            }
        };
        config.set_value(&group, key, value.to_string());
        match config.write(&path) {
            Ok(()) => true,
            Err(error) => {
                self.as_mut().set_status(QString::from(format!(
                    "Could not save device settings: {error}"
                )));
                false
            }
        }
    }

    fn request_screen_capture(mut self: Pin<&mut Self>, x: i32, y: i32, width: i32, height: i32) {
        let Some(region) = screen_region(x, y, width, height) else {
            return;
        };
        if self.as_ref().rust().capture_streams.contains_key(&region) {
            return;
        }
        if self.as_ref().rust().screencast_manager.is_none() {
            match ScreencastManager::start() {
                Ok(manager) => self.as_mut().rust_mut().screencast_manager = Some(manager),
                Err(error) => {
                    self.as_mut().set_status(QString::from(error));
                    return;
                }
            }
        }
        if let Some(manager) = &self.as_ref().rust().screencast_manager {
            manager.ensure(region);
        }
    }

    fn capture_node_id(&self, x: i32, y: i32, width: i32, height: i32) -> u32 {
        screen_region(x, y, width, height)
            .and_then(|region| self.rust().capture_streams.get(&region))
            .map_or(0, |identifier| identifier.node_id)
    }

    fn capture_object_serial(&self, x: i32, y: i32, width: i32, height: i32) -> u64 {
        screen_region(x, y, width, height)
            .and_then(|region| self.rust().capture_streams.get(&region))
            .map_or(0, |identifier| identifier.object_serial)
    }

    fn capture_snapshot_source(&self, x: i32, y: i32, width: i32, height: i32) -> QString {
        screen_region(x, y, width, height)
            .and_then(|region| self.rust().capture_snapshots.get(&region))
            .cloned()
            .unwrap_or_default()
    }

    fn handle_presenter_connected(mut self: Pin<&mut Self>, path: &std::path::Path) {
        let device = scan_devices().ok().and_then(|devices| {
            devices
                .into_iter()
                .find(|device| device.nodes.iter().any(|node| node.path == path))
        });
        self.as_mut().set_presenter_connected(true);
        if let Some(device) = device {
            let description = format!(
                "{} ({:04x}:{:04x}) [{}]",
                device.display_name(),
                device.id.vendor,
                device.id.product,
                device.physical_id
            );
            self.as_mut()
                .set_presenter_device(QString::from(description));
            self.as_mut()
                .set_presenter_details(QString::from(format_device_details(&device)));
            device
                .display_name()
                .clone_into(&mut self.as_mut().rust_mut().connected_device_name);
            let group = format!("Device_{:04x}_{:04x}", device.id.vendor, device.id.product);
            let (interval, strength) = load_device_settings(
                &PathBuf::from(String::from(&self.as_ref().rust().config_path)),
                &group,
            );
            self.as_mut().rust_mut().current_device_group = group;
            self.as_mut().set_device_input_sequence_interval(interval);
            self.as_mut().set_device_timer_haptic_strength(strength);
        } else {
            self.as_mut()
                .set_presenter_device(QString::from(path.to_str().unwrap_or("presenter")));
            self.as_mut().rust_mut().connected_device_name = path.display().to_string();
        }
        let name = self.as_ref().rust().connected_device_name.clone();
        ffi::send_notification(
            &QString::from("presenterConnected"),
            &QString::from("Presenter connected"),
            &QString::from(format!("{name} is ready.")),
            &QString::from("input-mouse"),
        );
    }

    fn handle_presenter_disconnected(mut self: Pin<&mut Self>) {
        let name = self.as_ref().rust().connected_device_name.clone();
        if !name.is_empty() {
            ffi::send_notification(
                &QString::from("presenterDisconnected"),
                &QString::from("Presenter disconnected"),
                &QString::from(format!("{name} is no longer available.")),
                &QString::from("input-mouse"),
            );
        }
        self.as_mut().set_presenter_connected(false);
        self.as_mut().set_presenter_device(QString::default());
        self.as_mut().set_presenter_details(QString::default());
        self.as_mut().rust_mut().connected_device_name.clear();
        self.as_mut().rust_mut().current_device_group.clear();
        self.as_mut().set_battery_level(-1);
        self.as_mut().set_battery_status(QString::default());
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
                    self.as_mut().handle_presenter_connected(&path);
                }
                Some(Ok(PresenterEvent::Disconnected)) => {
                    self.as_mut().handle_presenter_disconnected();
                }
                Some(Ok(PresenterEvent::Report(PresenterReport::Pointer(pointer)))) => {
                    if pointer.x == 0 && pointer.y == 0 {
                        continue;
                    }
                    self.as_mut().set_pointer_delta_x(i32::from(pointer.x));
                    self.as_mut().set_pointer_delta_y(i32::from(pointer.y));
                    let serial = self.as_ref().motion_serial().wrapping_add(1);
                    self.as_mut().set_motion_serial(serial);
                    if !self.as_ref().overlay_disabled() {
                        self.as_mut().set_overlay_active(true);
                    }
                    self.as_mut().set_preview_timeout_enabled(false);
                }
                Some(Ok(PresenterEvent::Report(PresenterReport::Keyboard(keyboard)))) => {
                    let navigation_pressed = keyboard
                        .usages
                        .iter()
                        .any(|usage| matches!(usage, 0x4f | 0x50));
                    if navigation_pressed && !self.as_ref().rust().navigation_pressed {
                        self.as_mut().start_presentation_timer(false);
                    }
                    self.as_mut().rust_mut().navigation_pressed = navigation_pressed;
                }
                Some(Ok(PresenterEvent::ButtonForwarding(active))) => {
                    self.as_mut().set_button_forwarding(active);
                }
                Some(Ok(PresenterEvent::Battery(info))) => {
                    self.as_mut()
                        .set_battery_level(i32::from(info.current_level));
                    self.as_mut()
                        .set_battery_status(QString::from(info.status.as_str()));
                }
                Some(Err(TryRecvError::Empty)) | None => break,
                Some(Err(TryRecvError::Disconnected)) => {
                    self.as_mut().set_presenter_connected(false);
                    self.as_mut().set_presenter_device(QString::default());
                    self.as_mut().set_presenter_details(QString::default());
                    self.as_mut().rust_mut().connected_device_name.clear();
                    self.as_mut().rust_mut().current_device_group.clear();
                    break;
                }
            }
        }
        self.as_mut().poll_control_commands();
        self.as_mut().update_presentation_timer();
        self.as_mut().poll_screencast();
        let snapshot = self.as_ref().control_snapshot();
        self.as_ref().rust().control_handle.publish(snapshot);
    }

    fn control_snapshot(self: Pin<&Self>) -> ControlSnapshot {
        let rust = self.rust();
        let presenter = rust
            .presenter_connected
            .then(|| rust.connected_device_name.clone());
        ControlSnapshot {
            tray_visible: rust.control_tray_visible,
            overlay_enabled: !rust.overlay_disabled,
            spotlight_active: rust.overlay_active,
            connected_devices: presenter.into_iter().collect(),
            battery_levels: rust
                .presenter_connected
                .then_some(rust.battery_level)
                .into_iter()
                .collect(),
            battery_statuses: rust
                .presenter_connected
                .then(|| String::from(&rust.battery_status))
                .into_iter()
                .collect(),
            presets: String::from(&rust.preset_names)
                .lines()
                .map(str::to_owned)
                .collect(),
            current_preset: String::from(&rust.current_preset),
            timer_enabled: rust.presentation_timer_enabled,
            timer_state: String::from(&rust.presentation_timer_state),
            timer_duration_seconds: rust.presentation_timer_duration_seconds,
            timer_remaining_seconds: rust.presentation_timer_remaining_seconds,
        }
    }

    fn poll_control_commands(mut self: Pin<&mut Self>) {
        loop {
            let command = self.as_ref().rust().control_commands.try_recv();
            match command {
                Ok(ControlCommand::ServiceReady) => {
                    // The Plasma applet owns the notification-area icon once it can
                    // reach this service. Keeping Qt's fallback icon would duplicate it.
                    self.as_mut().set_tray_visible(false);
                }
                Ok(ControlCommand::ServiceError(error)) => {
                    eprintln!("projecteur-rs: {error}; using the native tray icon");
                }
                Ok(ControlCommand::SetOverlayEnabled(enabled)) => {
                    self.as_mut().set_overlay_disabled(!enabled);
                    let _ = self.as_mut().save_settings();
                }
                Ok(ControlCommand::SetSpotlightActive(active)) => {
                    if !self.as_ref().overlay_disabled() {
                        self.as_mut().set_overlay_active(active);
                        self.as_mut().set_preview_timeout_enabled(false);
                    }
                }
                Ok(ControlCommand::LoadPreset(name, result)) => {
                    let loaded = self.as_mut().load_preset(&QString::from(name));
                    let _ = result.send(loaded);
                }
                Ok(ControlCommand::SetTimerEnabled(enabled)) => {
                    self.as_mut().set_presentation_timer_enabled_state(enabled);
                    let _ = self.as_mut().save_settings();
                }
                Ok(ControlCommand::StartTimer) => self.as_mut().start_presentation_timer(false),
                Ok(ControlCommand::RestartTimer) => self.as_mut().start_presentation_timer(true),
                Ok(ControlCommand::ResetTimer) => self.as_mut().reset_presentation_timer(),
                Ok(ControlCommand::SetTimerDurationSeconds(seconds)) => {
                    self.as_mut().set_presentation_timer_duration(seconds);
                    let _ = self.as_mut().save_settings();
                }
                Ok(ControlCommand::ShowPreferences) => self.as_mut().set_show_window(true),
                Ok(ControlCommand::ShowAbout) => ffi::show_about_dialog(),
                Ok(ControlCommand::ApplyCommands(commands)) => {
                    self.as_mut().apply_control_commands(&commands);
                }
                Ok(ControlCommand::Quit) => self.as_mut().set_quit_requested(true),
                Err(TryRecvError::Empty | TryRecvError::Disconnected) => break,
            }
        }
    }

    fn set_presentation_timer_enabled_state(mut self: Pin<&mut Self>, enabled: bool) {
        if *self.as_ref().presentation_timer_enabled() == enabled {
            return;
        }
        self.as_mut().set_presentation_timer_enabled(enabled);
        if !enabled {
            self.as_mut().reset_presentation_timer();
        }
    }

    fn set_presentation_timer_duration(mut self: Pin<&mut Self>, seconds: i32) {
        let seconds = seconds.clamp(60, 180 * 60);
        if *self.as_ref().presentation_timer_duration_seconds() == seconds {
            return;
        }
        self.as_mut()
            .set_presentation_timer_duration_seconds(seconds);
        if self.as_ref().presentation_timer_state().to_string() == "idle" {
            self.as_mut()
                .set_presentation_timer_remaining_seconds(seconds);
        }
    }

    fn start_presentation_timer(mut self: Pin<&mut Self>, restart: bool) {
        if !self.as_ref().presentation_timer_enabled() {
            return;
        }
        if !restart && self.as_ref().presentation_timer_state().to_string() != "idle" {
            return;
        }
        let seconds = *self.as_ref().presentation_timer_duration_seconds();
        let seconds = u64::try_from(seconds).unwrap_or(60);
        self.as_mut().rust_mut().timer_deadline =
            Some(Instant::now() + Duration::from_secs(seconds));
        self.as_mut()
            .set_presentation_timer_remaining_seconds(i32::try_from(seconds).unwrap_or(i32::MAX));
        self.as_mut()
            .set_presentation_timer_state(QString::from("running"));
    }

    fn reset_presentation_timer(mut self: Pin<&mut Self>) {
        self.as_mut().rust_mut().timer_deadline = None;
        let duration = *self.as_ref().presentation_timer_duration_seconds();
        self.as_mut()
            .set_presentation_timer_remaining_seconds(duration);
        self.as_mut()
            .set_presentation_timer_state(QString::from("idle"));
    }

    fn update_presentation_timer(mut self: Pin<&mut Self>) {
        let Some(deadline) = self.as_ref().rust().timer_deadline else {
            return;
        };
        let now = Instant::now();
        if now < deadline {
            let millis = deadline.duration_since(now).as_millis();
            let remaining = i32::try_from(millis.div_ceil(1000)).unwrap_or(i32::MAX);
            self.as_mut()
                .set_presentation_timer_remaining_seconds(remaining);
            return;
        }
        self.as_mut().rust_mut().timer_deadline = None;
        self.as_mut().set_presentation_timer_remaining_seconds(0);
        self.as_mut()
            .set_presentation_timer_state(QString::from("completed"));
        ffi::send_notification(
            &QString::from("presentationTimerFinished"),
            &QString::from("Presentation timer finished"),
            &QString::from("The configured presentation time has elapsed."),
            &QString::from("chronometer"),
        );
        let config_path = PathBuf::from(String::from(&self.as_ref().rust().config_path));
        let _ = thread::Builder::new()
            .name("projecteur-timer-feedback".to_owned())
            .spawn(move || send_timer_completion_feedback(&config_path));
    }

    fn apply_control_commands(mut self: Pin<&mut Self>, commands: &[String]) {
        for command in commands {
            let (key, value) = command.split_once('=').unwrap_or((command, ""));
            let key = key.trim();
            let value = value.trim();
            match key {
                "quit" => self.as_mut().set_quit_requested(true),
                "spot" if value.eq_ignore_ascii_case("toggle") => {
                    let active = !self.as_ref().overlay_active();
                    self.as_mut().set_overlay_active(active);
                }
                "spot" => {
                    let active = value.eq_ignore_ascii_case("on")
                        || value.eq_ignore_ascii_case("true")
                        || value == "1";
                    self.as_mut().set_overlay_active(active);
                }
                "settings" | "preferences" => {
                    let show = !(value.eq_ignore_ascii_case("hide") || value == "0");
                    self.as_mut().set_show_window(show);
                }
                "preset" if !value.is_empty() => {
                    let _ = self.as_mut().load_preset(&QString::from(value));
                }
                "preset.next" => self.as_mut().load_relative_preset(1),
                "preset.previous" => self.as_mut().load_relative_preset(-1),
                "spot.size.adjust" => {
                    if let Ok(adjustment) = value.parse::<i32>() {
                        let size = (*self.as_ref().spot_size() + adjustment).clamp(
                            SpotlightSettings::SPOT_SIZE_RANGE.minimum,
                            SpotlightSettings::SPOT_SIZE_RANGE.maximum,
                        );
                        self.as_mut().set_spot_size(size);
                        let _ = self.as_mut().save_settings();
                    }
                }
                _ if !value.is_empty() && self.as_mut().apply_string_property(key, value) => {
                    let _ = self.as_mut().save_settings();
                }
                _ => {}
            }
        }
    }

    fn apply_string_property(mut self: Pin<&mut Self>, key: &str, value: &str) -> bool {
        if key == "spot.overlay" {
            self.as_mut().set_overlay_disabled(!command_bool(value));
            return true;
        }

        let general_key = match key {
            "spot.multi-screen" => "multiScreenOverlay",
            "spot.size" => "spotSize",
            "spot.rotation" => "spotRotation",
            "spot.shape.square.radius" => "Shape.Square/radius",
            "spot.shape.star.points" => "Shape.Star/points",
            "spot.shape.star.innerradius" => "Shape.Star/innerRadius",
            "spot.shape.ngon.sides" => "Shape.Ngon/sides",
            "shade" => "showSpotShade",
            "shade.opacity" => "shadeOpacity",
            "shade.color" => "shadeColor",
            "dot" => "showCenterDot",
            "dot.size" => "dotSize",
            "dot.color" => "dotColor",
            "dot.opacity" => "dotOpacity",
            "dot.mode" => "dotMode",
            "dot.trail" => "dotTrailEnabled",
            "border" => "showBorder",
            "border.size" => "borderSize",
            "border.color" => "borderColor",
            "border.opacity" => "borderOpacity",
            "zoom" => "enableZoom",
            "zoom.factor" => "zoomFactor",
            "zoom.mode" => "zoomMode",
            "spot.shape" => {
                let shape = match value.to_ascii_lowercase().as_str() {
                    "circle" => "spotshapes/Circle.qml",
                    "square" => "spotshapes/Square.qml",
                    "star" => "spotshapes/Star.qml",
                    "ngon" | "n-gon" => "spotshapes/Ngon.qml",
                    _ => return false,
                };
                self.as_mut().set_spot_shape(QString::from(shape));
                return true;
            }
            _ => return false,
        };
        let mut settings = self.as_ref().current_spotlight_settings();
        settings.apply_general_entry(general_key, value);
        self.as_mut().apply_spotlight_settings(&settings);
        true
    }

    fn poll_screencast(mut self: Pin<&mut Self>) {
        loop {
            let event = self
                .as_ref()
                .rust()
                .screencast_manager
                .as_ref()
                .map(ScreencastManager::try_recv);
            match event {
                Some(Ok(CaptureEvent::Ready(region, identifier))) => {
                    let changed = {
                        let mut rust = self.as_mut().rust_mut();
                        let current = rust.capture_streams.entry(region).or_default();
                        let previous = *current;
                        if identifier.node_id != 0 {
                            current.node_id = identifier.node_id;
                        }
                        if identifier.object_serial != 0 {
                            current.object_serial = identifier.object_serial;
                        }
                        previous != *current
                    };
                    if changed {
                        let generation = self.as_ref().capture_generation().wrapping_add(1);
                        self.as_mut().set_capture_generation(generation);
                    }
                }
                Some(Ok(CaptureEvent::SnapshotReady(region, path))) => {
                    self.as_mut()
                        .rust_mut()
                        .capture_snapshots
                        .insert(region, QString::from(path.to_str().unwrap_or_default()));
                    let generation = self.as_ref().capture_generation().wrapping_add(1);
                    self.as_mut().set_capture_generation(generation);
                }
                Some(Ok(CaptureEvent::Closed(region))) => {
                    if self
                        .as_mut()
                        .rust_mut()
                        .capture_streams
                        .remove(&region)
                        .is_some()
                    {
                        let generation = self.as_ref().capture_generation().wrapping_add(1);
                        self.as_mut().set_capture_generation(generation);
                    }
                }
                Some(Ok(CaptureEvent::Failed(region, error))) => {
                    eprintln!("projecteur-rs: {error}");
                    self.as_mut().rust_mut().capture_streams.remove(&region);
                    self.as_mut().set_status(QString::from(format!(
                        "KWin screencast failed for {}x{} at {},{}: {error}",
                        region.width, region.height, region.x, region.y
                    )));
                }
                Some(Ok(CaptureEvent::ManagerFailed(error))) => {
                    eprintln!("projecteur-rs: {error}");
                    self.as_mut().rust_mut().screencast_manager = None;
                    self.as_mut().set_status(QString::from(error));
                    break;
                }
                Some(Err(TryRecvError::Disconnected)) => {
                    self.as_mut().rust_mut().screencast_manager = None;
                    break;
                }
                Some(Err(TryRecvError::Empty)) | None => break,
            }
        }
    }
}

fn command_bool(value: &str) -> bool {
    value.eq_ignore_ascii_case("true") || value.eq_ignore_ascii_case("on") || value == "1"
}

#[cfg(test)]
mod tests {
    use projecteur_core::{Bus, DeviceId, device_scan::DeviceNode};

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
        assert!(options.uinput_enabled);
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
    fn parses_legacy_desktop_flags() {
        let options = CliOptions::parse([
            OsString::from("--show-dialog"),
            OsString::from("--hide-systray-icon"),
            OsString::from("--disable-overlay"),
        ])
        .unwrap();

        assert!(options.show_window);
        assert!(!options.tray_visible);
        assert!(options.overlay_disabled);
    }

    #[test]
    fn validates_screen_capture_regions() {
        assert_eq!(
            screen_region(-1920, 0, 1920, 1080),
            Some(ScreenRegion {
                x: -1920,
                y: 0,
                width: 1920,
                height: 1080,
            })
        );
        assert_eq!(screen_region(0, 0, 0, 1080), None);
        assert_eq!(screen_region(0, 0, 1920, -1), None);
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
    fn parses_uinput_disable_aliases() {
        let legacy = CliOptions::parse([OsString::from("--disable-uinput")]).unwrap();
        let concise = CliOptions::parse([OsString::from("--no-uinput")]).unwrap();

        assert!(!legacy.uinput_enabled);
        assert!(!concise.uinput_enabled);
    }

    #[test]
    fn button_forwarding_uses_only_the_selected_devices_non_pointer_node() {
        let devices = [DiscoveredDevice {
            id: DeviceId {
                vendor: 0x046d,
                product: 0xb506,
                bus: Bus::Bluetooth,
            },
            physical_id: "presenter".to_owned(),
            kernel_name: String::new(),
            configured_name: "Spotlight".to_owned(),
            nodes: vec![
                DeviceNode {
                    path: PathBuf::from("/dev/input/event16"),
                    kind: DeviceNodeKind::Event,
                    has_relative_pointer: true,
                    readable: true,
                    writable: true,
                },
                DeviceNode {
                    path: PathBuf::from("/dev/input/event15"),
                    kind: DeviceNodeKind::Event,
                    has_relative_pointer: false,
                    readable: true,
                    writable: true,
                },
                DeviceNode {
                    path: PathBuf::from("/dev/hidraw5"),
                    kind: DeviceNodeKind::Hidraw,
                    has_relative_pointer: false,
                    readable: true,
                    writable: true,
                },
            ],
        }];

        assert_eq!(
            preferred_button_path(&PresenterSelection::Auto, &devices),
            Some(PathBuf::from("/dev/input/event15"))
        );
        assert_eq!(
            preferred_button_path(
                &PresenterSelection::Explicit(PathBuf::from("/dev/hidraw5")),
                &devices
            ),
            Some(PathBuf::from("/dev/input/event15"))
        );
        assert_eq!(
            battery_device_index_for_path(std::path::Path::new("/dev/hidraw5"), &devices),
            Some(1)
        );
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
