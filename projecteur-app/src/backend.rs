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
    SupportedDevice,
    config::{ConfigError, ProjecteurConfig},
    device_scan::{DeviceNodeKind, DiscoveredDevice, scan_devices, scan_devices_with},
    hid_report::{PresenterReport, decode_presenter_report},
    hidpp::{
        BatteryInfo, Message, enable_hold_notifications, query_battery, read_report_with_timeout,
        send_vibration, spotlight_device_index,
    },
    input_event::{
        EV_KEY, EV_REL, EV_SYN, InputEvent, REL_HWHEEL, REL_WHEEL, REL_X, REL_Y, SYN_REPORT,
    },
    input_mapping::{
        InputMapConfig, InputMapper, InputMapping, KeyEvent, KeyEventSequence, MappedAction,
        MapperOutput, NativeKeySequence,
    },
    parse_additional_device,
    settings::{DotMode, SpotlightSettings, ZoomMode},
    uinput::{GrabbedEventDevice, VirtualKeyboard, VirtualMouse},
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

        fn suppress_shake_cursor_effect() -> bool;

        fn restore_shake_cursor_effect() -> bool;

        fn load_qml_module(
            engine: Pin<&mut QQmlApplicationEngine>,
            uri: QAnyStringView,
            type_name: QAnyStringView,
        );

        fn setup_global_shortcuts();

        fn show_global_shortcuts_editor();

        fn format_key_combination(key: i32) -> QString;
    }

    #[auto_cxx_name]
    extern "RustQt" {
        /// Application backend exposed to QML.
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
        #[qproperty(QString, input_mapping_rows)]
        #[qproperty(i32, input_mapping_recording_row)]
        #[qproperty(QString, input_mapping_recording_preview)]
        #[qproperty(QString, native_mapping_recording_preview)]
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
        fn mark_settings_changed(self: Pin<&mut ProjecteurBackend>);

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
        fn add_input_mapping(self: Pin<&mut ProjecteurBackend>) -> i32;

        #[qinvokable]
        fn remove_input_mapping(self: Pin<&mut ProjecteurBackend>, row: i32) -> bool;

        #[qinvokable]
        fn set_input_mapping_action(
            self: Pin<&mut ProjecteurBackend>,
            row: i32,
            action_type: i32,
        ) -> bool;

        #[qinvokable]
        fn set_input_mapping_predefined_key(
            self: Pin<&mut ProjecteurBackend>,
            row: i32,
            name: &QString,
        ) -> bool;

        #[qinvokable]
        fn start_input_mapping_recording(self: Pin<&mut ProjecteurBackend>, row: i32) -> bool;

        #[qinvokable]
        fn set_special_input_mapping(
            self: Pin<&mut ProjecteurBackend>,
            row: i32,
            name: &QString,
        ) -> bool;

        #[qinvokable]
        fn cancel_input_mapping_recording(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn record_native_mapping_key(
            self: Pin<&mut ProjecteurBackend>,
            row: i32,
            qt_key: i32,
            native_scan_code: i32,
            modifiers: i32,
        ) -> i32;

        #[qinvokable]
        fn begin_native_mapping_recording(self: Pin<&mut ProjecteurBackend>, row: i32) -> bool;

        #[qinvokable]
        fn finish_native_mapping_recording(self: Pin<&mut ProjecteurBackend>) -> bool;

        #[qinvokable]
        fn cancel_native_mapping_recording(self: Pin<&mut ProjecteurBackend>);

        #[qinvokable]
        fn update_device_timer_haptic_strength(
            self: Pin<&mut ProjecteurBackend>,
            strength: i32,
        ) -> bool;

        #[qinvokable]
        fn configure_global_shortcuts(self: &ProjecteurBackend);

        #[qsignal]
        fn show_preferences_requested(self: Pin<&mut ProjecteurBackend>);
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
    input_mapping_rows: QString,
    input_mapping_recording_row: i32,
    input_mapping_recording_preview: QString,
    native_mapping_recording_preview: QString,
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
    shake_cursor_effect_suppressed: bool,
    presenter_events: Option<Receiver<PresenterEvent>>,
    button_commands: Option<SyncSender<ButtonManagerCommand>>,
    input_mapping_items: Vec<InputMapping>,
    recorded_input_sequence: KeyEventSequence,
    native_key_recording: Option<(usize, NativeKeySequence)>,
    screencast_manager: Option<ScreencastManager>,
    capture_streams: HashMap<ScreenRegion, StreamIdentifier>,
    capture_snapshots: HashMap<ScreenRegion, QString>,
    additional_devices: Vec<SupportedDevice>,
    startup_commands: Vec<String>,
}

impl Default for ProjecteurBackendRust {
    fn default() -> Self {
        eprintln!("projecteur: constructing backend");
        let options = match CliOptions::parse(std::env::args_os().skip(1)) {
            Ok(options) => options,
            Err(message) => {
                eprintln!("projecteur: {message}");
                CliOptions {
                    startup_error: Some(message),
                    ..CliOptions::default()
                }
            }
        };
        let (settings, config_path, mut status) = load_settings(&options);
        let (presenter_events, button_commands) = match options.presenter.clone() {
            PresenterSelection::Disabled => {
                let _ = write!(status, "; presenter monitoring disabled");
                (None, None)
            }
            selection => match start_presenter_manager(
                selection,
                options.uinput_enabled,
                config_path.clone(),
                options.additional_devices.clone(),
            ) {
                Ok(manager) => {
                    let _ = write!(status, "; waiting for presenter input");
                    (Some(manager.events), manager.button_commands)
                }
                Err(error) => {
                    let _ = write!(status, "; {error}");
                    eprintln!("projecteur: {error}");
                    (None, None)
                }
            },
        };
        Self::from_settings(
            &options,
            presenter_events,
            button_commands,
            &settings,
            config_path.as_deref(),
            &status,
        )
    }
}

impl Drop for ProjecteurBackendRust {
    fn drop(&mut self) {
        if self.shake_cursor_effect_suppressed {
            let _ = ffi::restore_shake_cursor_effect();
        }
    }
}

impl ProjecteurBackendRust {
    fn from_settings(
        options: &CliOptions,
        presenter_events: Option<Receiver<PresenterEvent>>,
        button_commands: Option<SyncSender<ButtonManagerCommand>>,
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
            input_mapping_rows: QString::from("[]"),
            input_mapping_recording_row: -1,
            input_mapping_recording_preview: QString::default(),
            native_mapping_recording_preview: QString::default(),
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
            shake_cursor_effect_suppressed: false,
            presenter_events,
            button_commands,
            input_mapping_items: Vec::new(),
            recorded_input_sequence: Vec::new(),
            native_key_recording: None,
            screencast_manager: None,
            capture_streams: HashMap::new(),
            capture_snapshots: HashMap::new(),
            additional_devices: options.additional_devices.clone(),
            startup_commands: options.startup_commands.clone(),
        }
    }

    fn send_button_command(&self, command: ButtonManagerCommand) -> bool {
        self.button_commands
            .as_ref()
            .is_some_and(|sender| sender.try_send(command).is_ok())
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
    additional_devices: Vec<SupportedDevice>,
    startup_commands: Vec<String>,
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
            additional_devices: Vec::new(),
            startup_commands: Vec::new(),
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
            } else if argument == "-D" || argument == "--additional-device" {
                let value = arguments.next().ok_or_else(|| {
                    format!(
                        "{} requires VENDOR:PRODUCT[:NAME]",
                        argument.to_string_lossy()
                    )
                })?;
                let value = value
                    .to_str()
                    .ok_or_else(|| "additional device must be valid UTF-8".to_owned())?;
                options
                    .additional_devices
                    .push(parse_additional_device(value).map_err(|error| error.to_string())?);
            } else if argument == "-c" || argument == "--command" {
                let command = arguments
                    .next()
                    .ok_or_else(|| format!("{} requires a command", argument.to_string_lossy()))?;
                options
                    .startup_commands
                    .push(command.to_string_lossy().into_owned());
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
                } else if let Some(value) = argument.strip_prefix("--additional-device=") {
                    options
                        .additional_devices
                        .push(parse_additional_device(value).map_err(|error| error.to_string())?);
                } else if let Some(command) = argument.strip_prefix("--command=") {
                    if !command.is_empty() {
                        options.startup_commands.push(command.to_owned());
                    }
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
    Motion { x: i32, y: i32 },
    ButtonForwarding(bool),
    Battery(BatteryInfo),
    MappedAction(MappedAction),
    InputRecorded(KeyEvent),
    InputRecordingFinished,
    SpecialInput(SpecialInput),
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum SpecialInput {
    Hold(u16),
    Move { code: u16, x: i32, y: i32 },
}

#[derive(Debug)]
enum ButtonManagerCommand {
    Config(InputMapConfig),
    Interval(Duration),
    Recording(bool),
    SpecialInput(SpecialInput),
}

struct PresenterManager {
    events: Receiver<PresenterEvent>,
    button_commands: Option<SyncSender<ButtonManagerCommand>>,
}

const PRESENTER_RESCAN_INTERVAL: Duration = Duration::from_millis(800);

fn start_presenter_manager(
    selection: PresenterSelection,
    uinput_enabled: bool,
    config_path: Option<PathBuf>,
    additional_devices: Vec<SupportedDevice>,
) -> Result<PresenterManager, String> {
    let (sender, receiver) = mpsc::sync_channel(256);
    let presenter_selection = selection.clone();
    let presenter_sender = sender.clone();
    let presenter_additional_devices = additional_devices.clone();
    thread::Builder::new()
        .name("projecteur-presenter".to_owned())
        .spawn(move || {
            loop {
                let path = match &presenter_selection {
                    PresenterSelection::Auto => scan_devices_with(&presenter_additional_devices)
                        .ok()
                        .and_then(|devices| preferred_hidraw_path(&devices)),
                    PresenterSelection::Explicit(path) => Some(path.clone()),
                    PresenterSelection::Disabled => return,
                };
                let Some(path) = path else {
                    thread::sleep(PRESENTER_RESCAN_INTERVAL);
                    continue;
                };
                let battery_device_index = scan_devices_with(&presenter_additional_devices)
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
    let button_commands = if uinput_enabled {
        let (command_sender, command_receiver) = mpsc::sync_channel(32);
        let button_additional_devices = additional_devices;
        thread::Builder::new()
            .name("projecteur-presenter-buttons".to_owned())
            .spawn(move || {
                run_button_manager(
                    &selection,
                    &sender,
                    &command_receiver,
                    config_path.as_deref(),
                    &button_additional_devices,
                );
            })
            .map_err(|error| format!("cannot start presenter button forwarding: {error}"))?;
        Some(command_sender)
    } else {
        None
    };
    Ok(PresenterManager {
        events: receiver,
        button_commands,
    })
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

fn preferred_button_paths(
    selection: &PresenterSelection,
    devices: &[DiscoveredDevice],
) -> Vec<PathBuf> {
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
    };
    let Some(device) = device else {
        return Vec::new();
    };
    device
        .nodes
        .iter()
        .filter(|node| node.kind == DeviceNodeKind::Event && node.readable)
        .map(|node| node.path.clone())
        .collect()
}

#[allow(clippy::too_many_lines)]
fn run_button_manager(
    selection: &PresenterSelection,
    sender: &SyncSender<PresenterEvent>,
    commands: &Receiver<ButtonManagerCommand>,
    config_path: Option<&std::path::Path>,
    additional_devices: &[SupportedDevice],
) {
    let mut reported_uinput_error = false;
    loop {
        let devices = scan_devices_with(additional_devices).unwrap_or_default();
        let paths = preferred_button_paths(selection, &devices);
        if paths.is_empty() {
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        }
        let Ok(mut keyboard) = VirtualKeyboard::create() else {
            if !reported_uinput_error {
                eprintln!("projecteur: uinput unavailable; presenter buttons remain ungrabbed");
                reported_uinput_error = true;
            }
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        };
        let Ok(mut mouse) = VirtualMouse::create() else {
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        };
        let mut sources: Vec<_> = paths
            .iter()
            .filter_map(|path| GrabbedEventDevice::open(path).ok())
            .collect();
        if sources.is_empty() {
            thread::sleep(PRESENTER_RESCAN_INTERVAL);
            continue;
        }
        if sender.send(PresenterEvent::ButtonForwarding(true)).is_err() {
            return;
        }
        let device = devices.iter().find(|device| {
            paths
                .iter()
                .any(|path| device.nodes.iter().any(|node| node.path == *path))
        });
        let device_group = device
            .map(|device| format!("Device_{:04x}_{:04x}", device.id.vendor, device.id.product));
        let motion_from_evdev =
            device.is_some_and(|device| spotlight_device_index(device.id).is_none());
        let config = config_path
            .and_then(|path| ProjecteurConfig::read(path).ok())
            .and_then(|config| {
                config
                    .device_input_map(device_group.as_deref()?)
                    .map_err(|error| {
                        eprintln!("projecteur: ignoring invalid presenter input map: {error}");
                    })
                    .ok()
            })
            .unwrap_or_default();
        let sequence_interval = config_path
            .zip(device_group.as_deref())
            .map_or(250, |(path, group)| load_device_settings(path, group).0);
        let mut sequence_interval = Duration::from_millis(
            u64::try_from(sequence_interval).expect("clamped input interval is positive"),
        );
        let mut mapper = InputMapper::new(config);
        let mut frames = vec![Vec::new(); sources.len()];
        let mut recording = false;
        let mut recorded_frames = 0_u8;
        let mut recording_deadline = None;
        let mut mapping_deadline = None;
        loop {
            while let Ok(command) = commands.try_recv() {
                match command {
                    ButtonManagerCommand::Config(config) => {
                        mapper.set_config(config);
                        mapping_deadline = None;
                    }
                    ButtonManagerCommand::Interval(interval) => {
                        sequence_interval = interval;
                        mapping_deadline = mapper
                            .has_pending_input()
                            .then(|| Instant::now() + sequence_interval);
                    }
                    ButtonManagerCommand::Recording(active) => {
                        recording = active;
                        recorded_frames = 0;
                        recording_deadline = None;
                        mapping_deadline = None;
                        mapper.reset();
                    }
                    ButtonManagerCommand::SpecialInput(input) => {
                        if !dispatch_special_input(
                            input,
                            recording,
                            &mut recorded_frames,
                            &mut recording_deadline,
                            sequence_interval,
                            &mut mapper,
                            &mut keyboard,
                            &mut mouse,
                            sender,
                        ) {
                            return;
                        }
                        mapping_deadline = mapper
                            .has_pending_input()
                            .then(|| Instant::now() + sequence_interval);
                    }
                }
            }
            let mapping_timeout = mapping_deadline
                .map(|deadline: Instant| deadline.saturating_duration_since(Instant::now()));
            let timeout = [
                mapping_timeout,
                recording_deadline
                    .map(|deadline: Instant| deadline.saturating_duration_since(Instant::now())),
            ]
            .into_iter()
            .flatten()
            .min()
            .unwrap_or(Duration::from_millis(25))
            .min(Duration::from_millis(25));
            match GrabbedEventDevice::wait_readable(&sources, timeout) {
                Ok(Some(source_index)) => {
                    let Ok(event) = sources[source_index].read_event() else {
                        break;
                    };
                    let frame = &mut frames[source_index];
                    frame.push(event);
                    if event.is_sync_report() {
                        if frame
                            .first()
                            .is_some_and(|event| event.is_relative_motion())
                        {
                            if motion_from_evdev {
                                let (x, y) = relative_motion(frame);
                                if (x != 0 || y != 0)
                                    && sender.send(PresenterEvent::Motion { x, y }).is_err()
                                {
                                    return;
                                }
                            }
                            if !frame.drain(..).all(|event| mouse.emit(event).is_ok()) {
                                break;
                            }
                            continue;
                        }
                        if recording {
                            if let Some(recorded) = normalized_recording_frame(frame) {
                                if sender
                                    .send(PresenterEvent::InputRecorded(recorded))
                                    .is_err()
                                {
                                    return;
                                }
                                recorded_frames += 1;
                                recording_deadline = Some(Instant::now() + sequence_interval);
                                if recorded_frames >= 8 {
                                    recording = false;
                                    recording_deadline = None;
                                    if sender.send(PresenterEvent::InputRecordingFinished).is_err()
                                    {
                                        return;
                                    }
                                }
                            }
                            frame.clear();
                            continue;
                        }
                        let output = mapper.feed_frame(frame);
                        frame.clear();
                        let Ok(output) = output else { break };
                        if !dispatch_mapper_output(output, &mut keyboard, &mut mouse, sender) {
                            break;
                        }
                        mapping_deadline = mapper
                            .has_pending_input()
                            .then(|| Instant::now() + sequence_interval);
                    }
                }
                Ok(None)
                    if recording
                        && recording_deadline
                            .is_some_and(|deadline| Instant::now() >= deadline) =>
                {
                    recording = false;
                    recording_deadline = None;
                    if sender.send(PresenterEvent::InputRecordingFinished).is_err() {
                        return;
                    }
                }
                Ok(None)
                    if mapper.has_pending_input()
                        && mapping_deadline.is_some_and(|deadline| Instant::now() >= deadline) =>
                {
                    if let Some(output) = mapper.sequence_timeout()
                        && !dispatch_mapper_output(output, &mut keyboard, &mut mouse, sender)
                    {
                        break;
                    }
                    mapping_deadline = None;
                }
                Ok(None) => {}
                Err(_) => break,
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

fn normalized_recording_frame(frame: &[InputEvent]) -> Option<KeyEvent> {
    let mut frame = frame.strip_suffix(&[InputEvent {
        event_type: EV_SYN,
        code: SYN_REPORT,
        value: 0,
    }])?;
    if frame.len() == 2
        && frame[0].event_type == 4
        && frame[0].code == 4
        && frame[1].event_type == EV_KEY
        && (0x110..=0x112).contains(&frame[1].code)
    {
        frame = &frame[1..];
    }
    (!frame.is_empty()).then(|| frame.to_vec())
}

fn canonical_input_mappings(items: &[InputMapping]) -> Vec<InputMapping> {
    let mut mappings = Vec::new();
    for item in items {
        if item.input.is_empty()
            || mappings
                .iter()
                .any(|mapping: &InputMapping| mapping.input == item.input)
        {
            continue;
        }
        mappings.push(item.clone());
    }
    mappings.sort_by(|left, right| left.input.cmp(&right.input));
    mappings
}

fn input_mapping_rows_json(device_group: &str, items: &[InputMapping]) -> String {
    let rows: Vec<_> = items
        .iter()
        .enumerate()
        .map(|(index, item)| {
            let duplicate = items
                .iter()
                .filter(|other| other.input == item.input)
                .count()
                > 1;
            let move_input = special_move_name(&item.input).is_some();
            serde_json::json!({
                "row": index,
                "input": input_sequence_label(device_group, &item.input),
                "duplicate": duplicate,
                "moveInput": move_input,
                "actionType": item.action.type_id(),
                "action": mapped_action_label(&item.action),
            })
        })
        .collect();
    serde_json::to_string(&rows).unwrap_or_else(|_| "[]".to_owned())
}

fn input_sequence_label(device_group: &str, sequence: &[KeyEvent]) -> String {
    if sequence.is_empty() {
        return "None".to_owned();
    }
    if let Some(name) = special_move_name(sequence) {
        return name.to_owned();
    }
    let mut labels = Vec::new();
    let mut index = 0;
    while index < sequence.len() {
        let frame = &sequence[index];
        let Some(event) = frame.last() else {
            index += 1;
            continue;
        };
        let is_tap = sequence.get(index + 1).is_some_and(|next| {
            frame.len() == next.len()
                && frame.iter().zip(next).all(|(pressed, released)| {
                    if pressed.event_type == EV_KEY {
                        pressed.event_type == released.event_type
                            && pressed.code == released.code
                            && pressed.value == 1
                            && released.value == 0
                    } else {
                        pressed == released
                    }
                })
        });
        let arrow = if is_tap {
            "↓↑"
        } else if event.value == 0 {
            "↑"
        } else {
            "↓"
        };
        labels.push(format!(
            "[{}{arrow}]",
            input_event_name(device_group, *event)
        ));
        index += if is_tap { 2 } else { 1 };
    }
    if labels.is_empty() {
        "None".to_owned()
    } else {
        labels.join(" ")
    }
}

fn input_event_name(device_group: &str, event: InputEvent) -> String {
    let spotlight = matches!(
        device_group,
        "Device_046d_c53e" | "Device_046d_b503" | "Device_046d_b506"
    );
    let avatto = device_group == "Device_0c45_8101";
    match (event.event_type, event.code) {
        (EV_KEY, 0x110) if spotlight || avatto => "Click".to_owned(),
        (EV_KEY, 106) if spotlight => "Next".to_owned(),
        (EV_KEY, 105) if spotlight => "Back".to_owned(),
        (EV_KEY, 109) if avatto => "Down".to_owned(),
        (EV_KEY, 104) if avatto => "Up".to_owned(),
        (EV_KEY, 0x0e10) => "Next Hold".to_owned(),
        (EV_KEY, 0x0e11) => "Back Hold".to_owned(),
        (_, code) => format!("{code:x}"),
    }
}

fn special_move_name(sequence: &[KeyEvent]) -> Option<&'static str> {
    match sequence {
        [events]
            if events.len() == 3
                && events.iter().all(|event| {
                    *event
                        == InputEvent {
                            event_type: EV_KEY,
                            code: 0x0ff0,
                            value: 1,
                        }
                }) =>
        {
            Some("Next Hold Move")
        }
        [events]
            if events.len() == 3
                && events.iter().all(|event| {
                    *event
                        == InputEvent {
                            event_type: EV_KEY,
                            code: 0x0ff1,
                            value: 1,
                        }
                }) =>
        {
            Some("Back Hold Move")
        }
        _ => None,
    }
}

fn mapped_action_label(action: &MappedAction) -> String {
    match action {
        MappedAction::KeySequence(sequence) => native_key_label(sequence),
        MappedAction::CyclePresets => "Cycle Presets".to_owned(),
        MappedAction::ToggleSpotlight => "Toggle Spotlight".to_owned(),
        MappedAction::ScrollHorizontal => "Scroll Horizontal".to_owned(),
        MappedAction::ScrollVertical => "Scroll Vertical".to_owned(),
        MappedAction::VolumeControl => "Volume Control".to_owned(),
    }
}

fn native_key_label(sequence: &NativeKeySequence) -> String {
    if sequence.native_sequence.is_empty() {
        return "None".to_owned();
    }
    if *sequence == predefined_native_key(15, 0x0900_0001, 4) {
        return "Alt+Tab".to_owned();
    }
    if *sequence == predefined_native_key(62, 0x0900_0033, 4) {
        return "Alt+F4".to_owned();
    }
    if *sequence == modifier_only_native_key(125, 64) {
        return "Meta".to_owned();
    }
    sequence
        .qt_keys
        .iter()
        .map(|key| String::from(&ffi::format_key_combination(*key)))
        .collect::<Vec<_>>()
        .join(", ")
}

fn predefined_native_key(code: u16, qt_key: i32, native_modifier: u16) -> NativeKeySequence {
    let modifier_code = 56;
    NativeKeySequence {
        qt_keys: vec![qt_key],
        native_modifiers: vec![native_modifier],
        native_sequence: vec![
            vec![
                InputEvent {
                    event_type: EV_KEY,
                    code: modifier_code,
                    value: 1,
                },
                InputEvent {
                    event_type: EV_KEY,
                    code,
                    value: 1,
                },
                sync_event(),
            ],
            vec![
                InputEvent {
                    event_type: EV_KEY,
                    code: modifier_code,
                    value: 0,
                },
                InputEvent {
                    event_type: EV_KEY,
                    code,
                    value: 0,
                },
                sync_event(),
            ],
        ],
    }
}

fn modifier_only_native_key(code: u16, native_modifier: u16) -> NativeKeySequence {
    NativeKeySequence {
        qt_keys: Vec::new(),
        native_modifiers: vec![native_modifier],
        native_sequence: vec![
            vec![
                InputEvent {
                    event_type: EV_KEY,
                    code,
                    value: 1,
                },
                sync_event(),
            ],
            vec![
                InputEvent {
                    event_type: EV_KEY,
                    code,
                    value: 0,
                },
                sync_event(),
            ],
        ],
    }
}

const fn sync_event() -> InputEvent {
    InputEvent {
        event_type: EV_SYN,
        code: SYN_REPORT,
        value: 0,
    }
}

fn native_modifier_keys(modifiers: i32) -> (Vec<u16>, u16) {
    let mut codes = Vec::new();
    let mut native = 0;
    if modifiers & 0x0400_0000 != 0 {
        codes.push(29);
        native |= 1;
    }
    if modifiers & 0x0800_0000 != 0 {
        codes.push(56);
        native |= 4;
    }
    if modifiers & 0x0200_0000 != 0 {
        codes.push(42);
        native |= 16;
    }
    if modifiers & 0x1000_0000 != 0 {
        codes.push(125);
        native |= 64;
    }
    codes.sort_unstable();
    (codes, native)
}

fn dispatch_mapper_output(
    output: MapperOutput,
    keyboard: &mut VirtualKeyboard,
    mouse: &mut VirtualMouse,
    sender: &SyncSender<PresenterEvent>,
) -> bool {
    match output {
        MapperOutput::Pending | MapperOutput::Consumed => true,
        MapperOutput::Forward(events) => emit_forwarded_frames(&events, keyboard, mouse),
        MapperOutput::Action(MappedAction::KeySequence(sequence)) => sequence
            .native_sequence
            .into_iter()
            .flatten()
            .all(|event| keyboard.emit(event).is_ok()),
        MapperOutput::Action(action) => sender.send(PresenterEvent::MappedAction(action)).is_ok(),
    }
}

fn emit_forwarded_frames(
    events: &[InputEvent],
    keyboard: &mut VirtualKeyboard,
    mouse: &mut VirtualMouse,
) -> bool {
    events
        .split_inclusive(|event| event.is_sync_report())
        .all(|frame| {
            if frame
                .iter()
                .any(|event| event.event_type == EV_KEY && event.code >= 0x0e00)
            {
                return true;
            }
            let is_mouse = frame.iter().any(|event| {
                event.event_type == EV_REL || (event.event_type == EV_KEY && event.code >= 0x100)
            });
            if is_mouse {
                frame.iter().all(|event| mouse.emit(*event).is_ok())
            } else {
                frame.iter().all(|event| keyboard.emit(*event).is_ok())
            }
        })
}

#[allow(clippy::too_many_arguments)]
fn dispatch_special_input(
    input: SpecialInput,
    recording: bool,
    recorded_frames: &mut u8,
    recording_deadline: &mut Option<Instant>,
    sequence_interval: Duration,
    mapper: &mut InputMapper,
    keyboard: &mut VirtualKeyboard,
    mouse: &mut VirtualMouse,
    sender: &SyncSender<PresenterEvent>,
) -> bool {
    let (code, motion) = match input {
        SpecialInput::Hold(code) => (code, None),
        SpecialInput::Move { code, x, y } => (code, Some((x, y))),
    };
    if recording {
        if motion.is_some() {
            return true;
        }
        if sender
            .send(PresenterEvent::InputRecorded(vec![InputEvent {
                event_type: EV_KEY,
                code,
                value: 1,
            }]))
            .is_err()
        {
            return false;
        }
        *recorded_frames += 1;
        *recording_deadline = Some(Instant::now() + sequence_interval);
        return true;
    }
    let repetitions = if motion.is_some() { 3 } else { 1 };
    let mut frame = vec![
        InputEvent {
            event_type: EV_KEY,
            code,
            value: 1,
        };
        repetitions
    ];
    frame.push(sync_event());
    let Ok(output) = mapper.feed_frame(&frame) else {
        return true;
    };
    let Some((x, y)) = motion else {
        return dispatch_mapper_output(output, keyboard, mouse, sender);
    };
    match output {
        MapperOutput::Action(MappedAction::ScrollHorizontal) => emit_wheel(mouse, REL_HWHEEL, -x),
        MapperOutput::Action(MappedAction::ScrollVertical) => emit_wheel(mouse, REL_WHEEL, y),
        MapperOutput::Action(MappedAction::VolumeControl) => emit_volume(keyboard, -y),
        other => dispatch_mapper_output(other, keyboard, mouse, sender),
    }
}

fn emit_wheel(mouse: &mut VirtualMouse, code: u16, value: i32) -> bool {
    value == 0
        || [
            InputEvent {
                event_type: EV_REL,
                code,
                value,
            },
            sync_event(),
        ]
        .into_iter()
        .all(|event| mouse.emit(event).is_ok())
}

fn emit_volume(keyboard: &mut VirtualKeyboard, value: i32) -> bool {
    if value == 0 {
        return true;
    }
    let code = if value > 0 { 115 } else { 114 };
    [
        InputEvent {
            event_type: EV_KEY,
            code,
            value: 1,
        },
        sync_event(),
        InputEvent {
            event_type: EV_KEY,
            code,
            value: 0,
        },
        sync_event(),
    ]
    .into_iter()
    .all(|event| keyboard.emit(event).is_ok())
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
    let mut hold_state = HoldNotificationState::default();
    let hold_feature_index = battery_device_index.and_then(|device_index| {
        match enable_hold_notifications(device, device_index, |report| {
            forward_presenter_report(report, sender);
        }) {
            Ok(index) => index,
            Err(error) => {
                eprintln!(
                    "projecteur: hold notifications unavailable on {}: {error}",
                    path.display()
                );
                None
            }
        }
    });
    loop {
        let timeout = next_battery_refresh.map_or(Duration::from_secs(60), |deadline| {
            deadline.saturating_duration_since(Instant::now())
        });
        match read_report_with_timeout(device, &mut bytes, timeout) {
            Ok(Some(0)) | Err(_) => return,
            Ok(Some(length)) => forward_presenter_or_hold(
                &bytes[..length],
                sender,
                hold_feature_index,
                &mut hold_state,
            ),
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
            forward_presenter_or_hold(report, sender, hold_feature_index, &mut hold_state);
        }) {
            Ok(info) => {
                if sender.send(PresenterEvent::Battery(info)).is_err() {
                    return;
                }
            }
            Err(error) => eprintln!(
                "projecteur: battery status unavailable on {}: {error}",
                path.display()
            ),
        }
        next_battery_refresh = Some(Instant::now() + BATTERY_REFRESH_INTERVAL);
    }
}

#[derive(Debug, Default)]
struct HoldNotificationState {
    next_pressed: bool,
    back_pressed: bool,
    move_code: Option<u16>,
    last_move: Option<Instant>,
}

fn forward_presenter_or_hold(
    report: &[u8],
    sender: &SyncSender<PresenterEvent>,
    feature_index: Option<u8>,
    state: &mut HoldNotificationState,
) {
    if let Some(feature_index) = feature_index
        && let Ok(message) = Message::parse(report)
        && message.feature_index() == feature_index
    {
        for input in decode_hold_notification(&message, state) {
            let _ = sender.try_send(PresenterEvent::SpecialInput(input));
        }
        return;
    }
    forward_presenter_report(report, sender);
}

fn decode_hold_notification(
    message: &Message,
    state: &mut HoldNotificationState,
) -> Vec<SpecialInput> {
    let payload = message.payload();
    if payload.len() < 4 {
        return Vec::new();
    }
    if message.function() == 0 {
        let next_pressed = payload[1] == 0xda || payload[3] == 0xda;
        let back_pressed = payload[1] == 0xdc || payload[3] == 0xdc;
        let mut inputs = Vec::new();
        if !state.next_pressed && next_pressed {
            inputs.push(SpecialInput::Hold(0x0e10));
        }
        if !state.back_pressed && back_pressed {
            inputs.push(SpecialInput::Hold(0x0e11));
        }
        if !state.next_pressed && next_pressed {
            state.move_code = Some(0x0ff0);
        } else if back_pressed && (!state.back_pressed || (state.next_pressed && !next_pressed)) {
            state.move_code = Some(0x0ff1);
        } else if state.back_pressed && !back_pressed && next_pressed {
            state.move_code = Some(0x0ff0);
        }
        state.next_pressed = next_pressed;
        state.back_pressed = back_pressed;
        if !next_pressed && !back_pressed {
            state.move_code = None;
        }
        return inputs;
    }
    if message.function() != 1 || state.move_code.is_none() {
        return Vec::new();
    }
    let now = Instant::now();
    if state
        .last_move
        .is_some_and(|last_move| now.duration_since(last_move) < Duration::from_millis(30))
    {
        return Vec::new();
    }
    state.last_move = Some(now);
    let reduce = |value: i32| {
        if value.abs() < 5 {
            0
        } else {
            value.clamp(-10, 10).div_euclid(5)
        }
    };
    let x = reduce(i32::from(i8::from_ne_bytes([payload[1]])));
    let y = reduce(i32::from(i8::from_ne_bytes([payload[3]])));
    if x == 0 && y == 0 {
        Vec::new()
    } else {
        vec![SpecialInput::Move {
            code: state.move_code.expect("checked above"),
            x,
            y,
        }]
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
            eprintln!("projecteur: {status}");
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
                "projecteur: timer feedback failed on {}: {error}",
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

    fn confirm_qml_loaded(mut self: Pin<&mut Self>) {
        eprintln!("projecteur: backend and QML are connected");
        let commands = std::mem::take(&mut self.as_mut().rust_mut().startup_commands);
        self.as_mut().apply_control_commands(&commands);
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
        self.as_mut().rust_mut().current_preset = QString::default();
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

    fn mark_settings_changed(mut self: Pin<&mut Self>) {
        self.as_mut().rust_mut().current_preset = QString::default();
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
        self.as_mut().rust_mut().current_preset = QString::from(name);
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
        if String::from(&self.as_ref().rust().current_preset) == name {
            self.as_mut().rust_mut().current_preset = QString::default();
        }
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
        let _ = self
            .as_ref()
            .rust()
            .send_button_command(ButtonManagerCommand::Interval(Duration::from_millis(
                u64::try_from(interval_ms).expect("clamped interval is positive"),
            )));
        true
    }

    fn add_input_mapping(mut self: Pin<&mut Self>) -> i32 {
        let row = self.as_ref().rust().input_mapping_items.len();
        self.as_mut()
            .rust_mut()
            .input_mapping_items
            .push(InputMapping {
                input: Vec::new(),
                action: MappedAction::KeySequence(NativeKeySequence::default()),
            });
        self.as_mut().refresh_input_mapping_rows();
        i32::try_from(row).unwrap_or(-1)
    }

    fn remove_input_mapping(mut self: Pin<&mut Self>, row: i32) -> bool {
        let Ok(row) = usize::try_from(row) else {
            return false;
        };
        if row >= self.as_ref().rust().input_mapping_items.len() {
            return false;
        }
        if *self.as_ref().input_mapping_recording_row() == i32::try_from(row).unwrap_or(-1) {
            self.as_mut().cancel_input_mapping_recording();
        }
        self.as_mut().rust_mut().input_mapping_items.remove(row);
        let saved = self.as_mut().persist_input_mappings();
        self.as_mut().refresh_input_mapping_rows();
        saved
    }

    fn set_input_mapping_action(mut self: Pin<&mut Self>, row: i32, action_type: i32) -> bool {
        let Ok(row) = usize::try_from(row) else {
            return false;
        };
        {
            let this = self.as_mut();
            let mut rust = this.rust_mut();
            let Some(item) = rust.input_mapping_items.get_mut(row) else {
                return false;
            };
            let move_input = special_move_name(&item.input).is_some();
            item.action = match action_type {
                1 if !move_input => MappedAction::KeySequence(NativeKeySequence::default()),
                2 if !move_input => MappedAction::CyclePresets,
                3 if !move_input => MappedAction::ToggleSpotlight,
                11 if move_input => MappedAction::ScrollHorizontal,
                12 if move_input => MappedAction::ScrollVertical,
                13 if move_input => MappedAction::VolumeControl,
                _ => return false,
            };
        }
        let saved = self.as_mut().persist_input_mappings();
        self.as_mut().refresh_input_mapping_rows();
        saved
    }

    fn set_input_mapping_predefined_key(
        mut self: Pin<&mut Self>,
        row: i32,
        name: &QString,
    ) -> bool {
        let Ok(row) = usize::try_from(row) else {
            return false;
        };
        let name = String::from(name);
        let sequence = match name.as_str() {
            "Alt+Tab" => predefined_native_key(15, 0x0900_0001, 4),
            "Alt+F4" => predefined_native_key(62, 0x0900_0033, 4),
            "Meta" => modifier_only_native_key(125, 64),
            "None" => NativeKeySequence::default(),
            _ => return false,
        };
        {
            let this = self.as_mut();
            let mut rust = this.rust_mut();
            let Some(item) = rust.input_mapping_items.get_mut(row) else {
                return false;
            };
            if !matches!(item.action, MappedAction::KeySequence(_)) {
                return false;
            }
            item.action = MappedAction::KeySequence(sequence);
        }
        let saved = self.as_mut().persist_input_mappings();
        self.as_mut().refresh_input_mapping_rows();
        saved
    }

    fn start_input_mapping_recording(mut self: Pin<&mut Self>, row: i32) -> bool {
        let Ok(index) = usize::try_from(row) else {
            return false;
        };
        if index >= self.as_ref().rust().input_mapping_items.len()
            || !self.as_ref().rust().button_forwarding
        {
            return false;
        }
        if !self
            .as_ref()
            .rust()
            .send_button_command(ButtonManagerCommand::Recording(true))
        {
            return false;
        }
        self.as_mut().rust_mut().recorded_input_sequence.clear();
        self.as_mut()
            .set_input_mapping_recording_preview(QString::from("Press device button(s)..."));
        self.as_mut().set_input_mapping_recording_row(row);
        true
    }

    fn set_special_input_mapping(mut self: Pin<&mut Self>, row: i32, name: &QString) -> bool {
        let Ok(row) = usize::try_from(row) else {
            return false;
        };
        let code = match String::from(name).as_str() {
            "Next Hold Move" => 0x0ff0,
            "Back Hold Move" => 0x0ff1,
            _ => return false,
        };
        let event = InputEvent {
            event_type: EV_KEY,
            code,
            value: 1,
        };
        {
            let this = self.as_mut();
            let mut rust = this.rust_mut();
            let Some(item) = rust.input_mapping_items.get_mut(row) else {
                return false;
            };
            item.input = vec![vec![event; 3]];
            if !matches!(
                item.action,
                MappedAction::ScrollHorizontal
                    | MappedAction::ScrollVertical
                    | MappedAction::VolumeControl
            ) {
                item.action = MappedAction::ScrollVertical;
            }
        }
        let saved = self.as_mut().persist_input_mappings();
        self.as_mut().refresh_input_mapping_rows();
        saved
    }

    fn cancel_input_mapping_recording(mut self: Pin<&mut Self>) {
        let _ = self
            .as_ref()
            .rust()
            .send_button_command(ButtonManagerCommand::Recording(false));
        self.as_mut().rust_mut().recorded_input_sequence.clear();
        self.as_mut().set_input_mapping_recording_row(-1);
        self.as_mut()
            .set_input_mapping_recording_preview(QString::default());
    }

    fn begin_native_mapping_recording(mut self: Pin<&mut Self>, row: i32) -> bool {
        let Ok(row) = usize::try_from(row) else {
            return false;
        };
        let valid = self
            .as_ref()
            .rust()
            .input_mapping_items
            .get(row)
            .is_some_and(|item| matches!(item.action, MappedAction::KeySequence(_)));
        if !valid {
            return false;
        }
        self.as_mut().rust_mut().native_key_recording = Some((row, NativeKeySequence::default()));
        self.as_mut()
            .set_native_mapping_recording_preview(QString::from("Press a keyboard shortcut..."));
        true
    }

    fn record_native_mapping_key(
        mut self: Pin<&mut Self>,
        row: i32,
        qt_key: i32,
        native_scan_code: i32,
        modifiers: i32,
    ) -> i32 {
        let Ok(row) = usize::try_from(row) else {
            return -1;
        };
        let Some(code) = native_scan_code
            .checked_sub(8)
            .and_then(|code| u16::try_from(code).ok())
        else {
            return -1;
        };
        let (modifier_codes, native_modifiers) = native_modifier_keys(modifiers);
        let mut pressed: KeyEvent = modifier_codes
            .iter()
            .map(|code| InputEvent {
                event_type: EV_KEY,
                code: *code,
                value: 1,
            })
            .collect();
        pressed.push(InputEvent {
            event_type: EV_KEY,
            code,
            value: 1,
        });
        pressed.push(sync_event());
        let mut released: KeyEvent = modifier_codes
            .iter()
            .map(|code| InputEvent {
                event_type: EV_KEY,
                code: *code,
                value: 0,
            })
            .collect();
        released.push(InputEvent {
            event_type: EV_KEY,
            code,
            value: 0,
        });
        released.push(sync_event());
        let (count, label) = {
            let this = self.as_mut();
            let mut rust = this.rust_mut();
            let Some((recording_row, sequence)) = rust.native_key_recording.as_mut() else {
                return -1;
            };
            if *recording_row != row || sequence.qt_keys.len() >= 4 {
                return -1;
            }
            sequence.qt_keys.push(qt_key | modifiers);
            sequence.native_sequence.extend([pressed, released]);
            sequence.native_modifiers.push(native_modifiers);
            (
                i32::try_from(sequence.qt_keys.len()).unwrap_or(4),
                native_key_label(sequence),
            )
        };
        self.as_mut()
            .set_native_mapping_recording_preview(QString::from(label));
        count
    }

    fn finish_native_mapping_recording(mut self: Pin<&mut Self>) -> bool {
        let Some((row, sequence)) = self.as_mut().rust_mut().native_key_recording.take() else {
            return false;
        };
        if sequence.qt_keys.is_empty() {
            self.as_mut()
                .set_native_mapping_recording_preview(QString::default());
            return false;
        }
        {
            let this = self.as_mut();
            let mut rust = this.rust_mut();
            let Some(item) = rust.input_mapping_items.get_mut(row) else {
                return false;
            };
            item.action = MappedAction::KeySequence(sequence);
        }
        let saved = self.as_mut().persist_input_mappings();
        self.as_mut().refresh_input_mapping_rows();
        self.as_mut()
            .set_native_mapping_recording_preview(QString::default());
        saved
    }

    fn cancel_native_mapping_recording(mut self: Pin<&mut Self>) {
        self.as_mut().rust_mut().native_key_recording = None;
        self.as_mut()
            .set_native_mapping_recording_preview(QString::default());
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

    fn refresh_input_mapping_rows(mut self: Pin<&mut Self>) {
        let rows = input_mapping_rows_json(
            &self.as_ref().rust().current_device_group,
            &self.as_ref().rust().input_mapping_items,
        );
        self.as_mut().set_input_mapping_rows(QString::from(rows));
    }

    fn persist_input_mappings(mut self: Pin<&mut Self>) -> bool {
        let (path, group, config) = {
            let this = self.as_ref();
            let rust = this.rust();
            let mappings = canonical_input_mappings(&rust.input_mapping_items);
            (
                PathBuf::from(String::from(&rust.config_path)),
                rust.current_device_group.clone(),
                InputMapConfig { mappings },
            )
        };
        if path.as_os_str().is_empty() || group.is_empty() {
            return false;
        }
        let mut file_config = match ProjecteurConfig::read(&path) {
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
        file_config.set_device_input_map(&group, &config);
        if let Err(error) = file_config.write(&path) {
            self.as_mut().set_status(QString::from(format!(
                "Could not save input mappings: {error}"
            )));
            return false;
        }
        let _ = self
            .as_ref()
            .rust()
            .send_button_command(ButtonManagerCommand::Config(config));
        true
    }

    fn handle_presenter_connected(mut self: Pin<&mut Self>, path: &std::path::Path) {
        let device = scan_devices_with(&self.as_ref().rust().additional_devices)
            .ok()
            .and_then(|devices| {
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
            let input_map = ProjecteurConfig::read(PathBuf::from(String::from(
                &self.as_ref().rust().config_path,
            )))
            .ok()
            .and_then(|config| {
                config
                    .device_input_map(&self.as_ref().rust().current_device_group)
                    .ok()
            })
            .unwrap_or_default();
            self.as_mut().rust_mut().input_mapping_items = input_map.mappings;
            self.as_mut().refresh_input_mapping_rows();
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
        self.as_mut().rust_mut().input_mapping_items.clear();
        self.as_mut().set_input_mapping_rows(QString::from("[]"));
        self.as_mut().set_input_mapping_recording_row(-1);
        self.as_mut().set_battery_level(-1);
        self.as_mut().set_battery_status(QString::default());
    }

    fn handle_input_recorded(mut self: Pin<&mut Self>, frame: KeyEvent) {
        if *self.as_ref().input_mapping_recording_row() < 0 {
            return;
        }
        self.as_mut().rust_mut().recorded_input_sequence.push(frame);
        let preview = input_sequence_label(
            &self.as_ref().rust().current_device_group,
            &self.as_ref().rust().recorded_input_sequence,
        );
        self.as_mut()
            .set_input_mapping_recording_preview(QString::from(preview));
    }

    fn finish_input_mapping_recording(mut self: Pin<&mut Self>) {
        let row = *self.as_ref().input_mapping_recording_row();
        let sequence = std::mem::take(&mut self.as_mut().rust_mut().recorded_input_sequence);
        if let Ok(row) = usize::try_from(row)
            && !sequence.is_empty()
            && let Some(item) = self.as_mut().rust_mut().input_mapping_items.get_mut(row)
        {
            item.input = sequence;
            let move_input = special_move_name(&item.input).is_some();
            let is_move_action = matches!(
                item.action,
                MappedAction::ScrollHorizontal
                    | MappedAction::ScrollVertical
                    | MappedAction::VolumeControl
            );
            if move_input && !is_move_action {
                item.action = MappedAction::ScrollVertical;
            } else if !move_input && is_move_action {
                item.action = MappedAction::KeySequence(NativeKeySequence::default());
            }
            let _ = self.as_mut().persist_input_mappings();
        }
        self.as_mut().set_input_mapping_recording_row(-1);
        self.as_mut()
            .set_input_mapping_recording_preview(QString::default());
        self.as_mut().refresh_input_mapping_rows();
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
                Some(Ok(PresenterEvent::Motion { x, y })) => {
                    self.as_mut().set_pointer_delta_x(x);
                    self.as_mut().set_pointer_delta_y(y);
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
                Some(Ok(PresenterEvent::MappedAction(action))) => match action {
                    MappedAction::CyclePresets => self.as_mut().load_relative_preset(1),
                    MappedAction::ToggleSpotlight => {
                        let active = !self.as_ref().overlay_active();
                        self.as_mut().set_overlay_active(active);
                    }
                    MappedAction::KeySequence(_)
                    | MappedAction::ScrollHorizontal
                    | MappedAction::ScrollVertical
                    | MappedAction::VolumeControl => {}
                },
                Some(Ok(PresenterEvent::InputRecorded(frame))) => {
                    self.as_mut().handle_input_recorded(frame);
                }
                Some(Ok(PresenterEvent::InputRecordingFinished)) => {
                    self.as_mut().finish_input_mapping_recording();
                }
                Some(Ok(PresenterEvent::SpecialInput(input))) => {
                    let _ = self
                        .as_ref()
                        .rust()
                        .send_button_command(ButtonManagerCommand::SpecialInput(input));
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
        self.as_mut().sync_shake_cursor_effect();
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
                    eprintln!("projecteur: {error}; using the native tray icon");
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
                Ok(ControlCommand::ShowPreferences) => {
                    self.as_mut().set_show_window(true);
                    self.as_mut().show_preferences_requested();
                }
                Ok(ControlCommand::ShowAbout) => ffi::show_about_dialog(),
                Ok(ControlCommand::ApplyCommands(commands)) => {
                    self.as_mut().apply_control_commands(&commands);
                }
                Ok(ControlCommand::Quit) => {
                    self.as_mut().restore_shake_cursor_effect();
                    self.as_mut().set_quit_requested(true);
                }
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

    fn sync_shake_cursor_effect(mut self: Pin<&mut Self>) {
        let should_suppress = *self.as_ref().overlay_active() && !self.as_ref().overlay_disabled();
        let is_suppressed = self.as_ref().rust().shake_cursor_effect_suppressed;
        if should_suppress && !is_suppressed {
            if ffi::suppress_shake_cursor_effect() {
                self.as_mut().rust_mut().shake_cursor_effect_suppressed = true;
            }
        } else if !should_suppress && is_suppressed {
            self.as_mut().restore_shake_cursor_effect();
        }
    }

    fn restore_shake_cursor_effect(mut self: Pin<&mut Self>) {
        if !self.as_ref().rust().shake_cursor_effect_suppressed {
            return;
        }
        if ffi::restore_shake_cursor_effect() {
            self.as_mut().rust_mut().shake_cursor_effect_suppressed = false;
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
                "quit" => {
                    self.as_mut().restore_shake_cursor_effect();
                    self.as_mut().set_quit_requested(true);
                }
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
                    eprintln!("projecteur: {error}");
                    self.as_mut().rust_mut().capture_streams.remove(&region);
                    self.as_mut().set_status(QString::from(format!(
                        "KWin screencast failed for {}x{} at {},{}: {error}",
                        region.width, region.height, region.x, region.y
                    )));
                }
                Some(Ok(CaptureEvent::ManagerFailed(error))) => {
                    eprintln!("projecteur: {error}");
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

fn relative_motion(frame: &[InputEvent]) -> (i32, i32) {
    let axis = |code| {
        frame
            .iter()
            .filter(|event| event.event_type == EV_REL && event.code == code)
            .map(|event| event.value)
            .sum()
    };
    (axis(REL_X), axis(REL_Y))
}

#[cfg(test)]
mod tests {
    use projecteur_core::{Bus, DeviceId, device_scan::DeviceNode};

    use super::*;

    #[test]
    fn parses_config_argument_and_window_flag() {
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
    fn parses_additional_devices_and_startup_commands() {
        let options = CliOptions::parse([
            OsString::from("--additional-device=04b3:310c:Demo"),
            OsString::from("--command"),
            OsString::from("spot=on"),
        ])
        .unwrap();

        assert_eq!(options.additional_devices.len(), 1);
        assert_eq!(options.additional_devices[0].name, "Demo");
        assert_eq!(options.startup_commands, ["spot=on"]);
    }

    #[test]
    fn combines_evdev_motion_for_generic_presenters() {
        let frame = [
            InputEvent {
                event_type: EV_REL,
                code: REL_X,
                value: 7,
            },
            InputEvent {
                event_type: EV_REL,
                code: REL_X,
                value: -2,
            },
            InputEvent {
                event_type: EV_REL,
                code: REL_Y,
                value: -4,
            },
        ];

        assert_eq!(relative_motion(&frame), (5, -4));
    }

    #[test]
    fn button_forwarding_uses_all_selected_device_event_nodes() {
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
            preferred_button_paths(&PresenterSelection::Auto, &devices),
            vec![
                PathBuf::from("/dev/input/event16"),
                PathBuf::from("/dev/input/event15")
            ]
        );
        assert_eq!(
            preferred_button_paths(
                &PresenterSelection::Explicit(PathBuf::from("/dev/hidraw5")),
                &devices
            ),
            vec![
                PathBuf::from("/dev/input/event16"),
                PathBuf::from("/dev/input/event15")
            ]
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

    fn mapping(code: u16, action: MappedAction) -> InputMapping {
        InputMapping {
            input: vec![vec![InputEvent {
                event_type: EV_KEY,
                code,
                value: 1,
            }]],
            action,
        }
    }

    #[test]
    fn editor_configuration_skips_empty_and_duplicate_rows_and_sorts_like_cpp_map() {
        let first = mapping(106, MappedAction::CyclePresets);
        let duplicate = mapping(106, MappedAction::ToggleSpotlight);
        let earlier = mapping(105, MappedAction::ToggleSpotlight);
        let empty = InputMapping {
            input: Vec::new(),
            action: MappedAction::CyclePresets,
        };

        let result = canonical_input_mappings(&[first.clone(), duplicate, empty, earlier.clone()]);

        assert_eq!(result, vec![earlier, first]);
    }

    #[test]
    fn recording_normalizes_mouse_scan_and_names_spotlight_taps() {
        let press = normalized_recording_frame(&[
            InputEvent {
                event_type: 4,
                code: 4,
                value: 0x90001,
            },
            InputEvent {
                event_type: EV_KEY,
                code: 0x110,
                value: 1,
            },
            sync_event(),
        ])
        .unwrap();
        let release = vec![InputEvent {
            event_type: EV_KEY,
            code: 0x110,
            value: 0,
        }];

        assert_eq!(press.len(), 1);
        assert_eq!(
            input_sequence_label("Device_046d_c53e", &[press, release]),
            "[Click↓↑]"
        );
    }

    #[test]
    fn predefined_shortcuts_match_cpp_native_event_order() {
        let alt_tab = predefined_native_key(15, 0x0900_0001, 4);

        assert_eq!(native_key_label(&alt_tab), "Alt+Tab");
        assert_eq!(alt_tab.native_modifiers, vec![4]);
        assert_eq!(
            alt_tab.native_sequence[0],
            vec![
                InputEvent {
                    event_type: EV_KEY,
                    code: 56,
                    value: 1,
                },
                InputEvent {
                    event_type: EV_KEY,
                    code: 15,
                    value: 1,
                },
                sync_event(),
            ]
        );
        assert_eq!(alt_tab.native_sequence[1][0].value, 0);
        assert_eq!(alt_tab.native_sequence[1][1].value, 0);
    }

    #[test]
    fn decodes_spotlight_hold_and_throttled_move_notifications() {
        let pressed = Message::parse(&[
            0x11, 1, 0x0d, 0x00, 0, 0xda, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();
        let movement = Message::parse(&[
            0x11, 1, 0x0d, 0x10, 0xff, 9, 0xff, 0xf7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ])
        .unwrap();
        let mut state = HoldNotificationState::default();

        assert_eq!(
            decode_hold_notification(&pressed, &mut state),
            vec![SpecialInput::Hold(0x0e10)]
        );
        assert_eq!(state.move_code, Some(0x0ff0));
        assert_eq!(
            decode_hold_notification(&movement, &mut state),
            vec![SpecialInput::Move {
                code: 0x0ff0,
                x: 1,
                y: -2,
            }]
        );
        assert!(decode_hold_notification(&movement, &mut state).is_empty());
    }
}
