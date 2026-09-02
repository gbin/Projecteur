//! D-Bus control surface consumed by Projecteur's Plasma applet.

use std::{
    ffi::OsString,
    sync::{
        Arc, RwLock,
        mpsc::{self, Receiver, SyncSender},
    },
    thread,
};

use zbus::object_server::SignalEmitter;

pub const SERVICE_NAME: &str = "org.projecteur.Projecteur";
pub const OBJECT_PATH: &str = "/org/projecteur/Projecteur/Control";

/// Forward launch-time actions to an already running instance.
///
/// Returns `true` when the well-known service exists, including launches
/// that do not contain an action.
pub fn forward_to_running(arguments: &[OsString]) -> Result<bool, String> {
    let connection = zbus::blocking::Connection::session()
        .map_err(|error| format!("cannot connect to the session bus: {error}"))?;
    let owner_reply = connection
        .call_method(
            Some("org.freedesktop.DBus"),
            "/org/freedesktop/DBus",
            Some("org.freedesktop.DBus"),
            "NameHasOwner",
            &(SERVICE_NAME,),
        )
        .map_err(|error| format!("cannot query the session bus: {error}"))?;
    let has_owner = owner_reply
        .body()
        .deserialize::<bool>()
        .map_err(|error| format!("cannot decode the session bus reply: {error}"))?;
    if !has_owner {
        return Ok(false);
    }

    let mut commands = Vec::new();
    let mut show_preferences = false;
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].to_string_lossy();
        if argument == "--show-window" || argument == "--show-dialog" {
            show_preferences = true;
        } else if argument == "-c" || argument == "--command" {
            index += 1;
            if let Some(command) = arguments.get(index) {
                let command = command.to_string_lossy().trim().to_owned();
                if !command.is_empty() {
                    commands.push(command);
                }
            }
        } else if let Some(command) = argument.strip_prefix("--command=") {
            let command = command.trim();
            if !command.is_empty() {
                commands.push(command.to_owned());
            }
        }
        index += 1;
    }

    if !commands.is_empty() {
        connection
            .call_method(
                Some(SERVICE_NAME),
                OBJECT_PATH,
                Some(SERVICE_NAME),
                "ApplyCommands",
                &(commands,),
            )
            .map_err(|error| format!("cannot forward commands: {error}"))?;
    } else if show_preferences {
        connection
            .call_method(
                Some(SERVICE_NAME),
                OBJECT_PATH,
                Some(SERVICE_NAME),
                "ShowPreferences",
                &(),
            )
            .map_err(|error| format!("cannot show preferences: {error}"))?;
    }
    Ok(true)
}

#[allow(clippy::struct_excessive_bools)]
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ControlSnapshot {
    pub tray_visible: bool,
    pub overlay_enabled: bool,
    pub spotlight_active: bool,
    pub connected_devices: Vec<String>,
    pub battery_levels: Vec<i32>,
    pub battery_statuses: Vec<String>,
    pub presets: Vec<String>,
    pub current_preset: String,
    pub timer_enabled: bool,
    pub timer_state: String,
    pub timer_duration_seconds: i32,
    pub timer_remaining_seconds: i32,
}

impl Default for ControlSnapshot {
    fn default() -> Self {
        Self {
            tray_visible: true,
            overlay_enabled: true,
            spotlight_active: false,
            connected_devices: Vec::new(),
            battery_levels: Vec::new(),
            battery_statuses: Vec::new(),
            presets: Vec::new(),
            current_preset: String::new(),
            timer_enabled: false,
            timer_state: "idle".to_owned(),
            timer_duration_seconds: 15 * 60,
            timer_remaining_seconds: 15 * 60,
        }
    }
}

#[derive(Debug)]
pub enum ControlCommand {
    ServiceReady,
    ServiceError(String),
    SetOverlayEnabled(bool),
    SetSpotlightActive(bool),
    LoadPreset(String, SyncSender<bool>),
    SetTimerEnabled(bool),
    StartTimer,
    RestartTimer,
    ResetTimer,
    SetTimerDurationSeconds(i32),
    ShowPreferences,
    ShowAbout,
    ApplyCommands(Vec<String>),
    Quit,
}

#[derive(Clone)]
pub struct ControlHandle {
    state: Arc<RwLock<ControlSnapshot>>,
    updates: SyncSender<(ControlSnapshot, ControlSnapshot)>,
}

impl ControlHandle {
    pub fn publish(&self, snapshot: ControlSnapshot) {
        let previous = {
            let mut state = self
                .state
                .write()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            if *state == snapshot {
                return;
            }
            std::mem::replace(&mut *state, snapshot.clone())
        };
        let _ = self.updates.try_send((previous, snapshot));
    }
}

pub fn start(initial: ControlSnapshot) -> (ControlHandle, Receiver<ControlCommand>) {
    let state = Arc::new(RwLock::new(initial));
    let (commands, command_receiver) = mpsc::sync_channel(64);
    let (updates, update_receiver) = mpsc::sync_channel(64);
    let service_state = Arc::clone(&state);
    let service_commands = commands.clone();
    let spawn_result = thread::Builder::new()
        .name("projecteur-dbus".to_owned())
        .spawn(move || run_service(service_state, &service_commands, &update_receiver));
    if let Err(error) = spawn_result {
        let _ = commands.try_send(ControlCommand::ServiceError(format!(
            "cannot start D-Bus control thread: {error}"
        )));
    }
    (ControlHandle { state, updates }, command_receiver)
}

fn run_service(
    state: Arc<RwLock<ControlSnapshot>>,
    commands: &SyncSender<ControlCommand>,
    updates: &Receiver<(ControlSnapshot, ControlSnapshot)>,
) {
    let interface = ProjecteurControl {
        state,
        commands: commands.clone(),
    };
    let connection = zbus::blocking::connection::Builder::session()
        .and_then(|builder| builder.name(SERVICE_NAME))
        .and_then(|builder| builder.serve_at(OBJECT_PATH, interface))
        .and_then(zbus::blocking::connection::Builder::build);
    let connection = match connection {
        Ok(connection) => connection,
        Err(error) => {
            let _ = commands.try_send(ControlCommand::ServiceError(format!(
                "cannot register {SERVICE_NAME}: {error}"
            )));
            return;
        }
    };
    let _ = commands.try_send(ControlCommand::ServiceReady);

    while let Ok((previous, current)) = updates.recv() {
        let Ok(interface) = connection
            .object_server()
            .interface::<_, ProjecteurControl>(OBJECT_PATH)
        else {
            continue;
        };
        let emitter = interface.signal_emitter();
        let control = interface.get();
        zbus::block_on(emit_changes(&control, emitter, &previous, &current));
    }
}

async fn emit_changes(
    control: &ProjecteurControl,
    emitter: &SignalEmitter<'_>,
    previous: &ControlSnapshot,
    current: &ControlSnapshot,
) {
    if previous.overlay_enabled != current.overlay_enabled {
        let _ =
            ProjecteurControl::emit_overlay_enabled_changed(emitter, current.overlay_enabled).await;
        let _ = control.overlay_enabled_changed(emitter).await;
    }
    if previous.spotlight_active != current.spotlight_active {
        let _ = ProjecteurControl::emit_spotlight_active_changed(emitter, current.spotlight_active)
            .await;
        let _ = control.spotlight_active_changed(emitter).await;
    }
    if previous.connected_devices != current.connected_devices {
        let _ =
            ProjecteurControl::emit_connected_devices_changed(emitter, &current.connected_devices)
                .await;
        let _ = control.connected_devices_changed(emitter).await;
    }
    if previous.battery_levels != current.battery_levels {
        let _ = ProjecteurControl::emit_connected_device_battery_levels_changed(
            emitter,
            &current.battery_levels,
        )
        .await;
        let _ = control
            .connected_device_battery_levels_changed(emitter)
            .await;
    }
    if previous.battery_statuses != current.battery_statuses {
        let _ = ProjecteurControl::emit_connected_device_battery_statuses_changed(
            emitter,
            &current.battery_statuses,
        )
        .await;
        let _ = control
            .connected_device_battery_statuses_changed(emitter)
            .await;
    }
    if previous.presets != current.presets {
        let _ = ProjecteurControl::emit_presets_changed(emitter, &current.presets).await;
        let _ = control.presets_changed(emitter).await;
    }
    if previous.current_preset != current.current_preset {
        let _ =
            ProjecteurControl::emit_current_preset_changed(emitter, &current.current_preset).await;
        let _ = control.current_preset_changed(emitter).await;
    }
    if previous.timer_enabled != current.timer_enabled {
        let _ = ProjecteurControl::emit_timer_enabled_changed(emitter, current.timer_enabled).await;
        let _ = control.timer_enabled_changed(emitter).await;
    }
    if previous.timer_state != current.timer_state {
        let _ = ProjecteurControl::emit_timer_state_changed(emitter, &current.timer_state).await;
        let _ = control.timer_state_changed(emitter).await;
    }
    if previous.timer_duration_seconds != current.timer_duration_seconds {
        let _ = ProjecteurControl::emit_timer_duration_seconds_changed(
            emitter,
            current.timer_duration_seconds,
        )
        .await;
        let _ = control.timer_duration_seconds_changed(emitter).await;
    }
    if previous.timer_remaining_seconds != current.timer_remaining_seconds {
        let _ = ProjecteurControl::emit_timer_remaining_seconds_changed(
            emitter,
            current.timer_remaining_seconds,
        )
        .await;
        let _ = control.timer_remaining_seconds_changed(emitter).await;
    }
}

struct ProjecteurControl {
    state: Arc<RwLock<ControlSnapshot>>,
    commands: SyncSender<ControlCommand>,
}

impl ProjecteurControl {
    fn snapshot(&self) -> ControlSnapshot {
        self.state
            .read()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .clone()
    }

    fn send(&self, command: ControlCommand) {
        let _ = self.commands.try_send(command);
    }
}

#[zbus::interface(name = "org.projecteur.Projecteur")]
impl ProjecteurControl {
    #[zbus(property(emits_changed_signal = "const"), name = "TrayVisible")]
    fn tray_visible(&self) -> bool {
        self.snapshot().tray_visible
    }

    #[zbus(property, name = "OverlayEnabled")]
    fn overlay_enabled(&self) -> bool {
        self.snapshot().overlay_enabled
    }

    #[zbus(property, name = "SpotlightActive")]
    fn spotlight_active(&self) -> bool {
        self.snapshot().spotlight_active
    }

    #[zbus(property, name = "ConnectedDevices")]
    fn connected_devices(&self) -> Vec<String> {
        self.snapshot().connected_devices
    }

    #[zbus(property, name = "ConnectedDeviceBatteryLevels")]
    fn connected_device_battery_levels(&self) -> Vec<i32> {
        self.snapshot().battery_levels
    }

    #[zbus(property, name = "ConnectedDeviceBatteryStatuses")]
    fn connected_device_battery_statuses(&self) -> Vec<String> {
        self.snapshot().battery_statuses
    }

    #[zbus(property, name = "Presets")]
    fn presets(&self) -> Vec<String> {
        self.snapshot().presets
    }

    #[zbus(property, name = "CurrentPreset")]
    fn current_preset(&self) -> String {
        self.snapshot().current_preset
    }

    #[zbus(property, name = "TimerEnabled")]
    fn timer_enabled(&self) -> bool {
        self.snapshot().timer_enabled
    }

    #[zbus(property, name = "TimerState")]
    fn timer_state(&self) -> String {
        self.snapshot().timer_state
    }

    #[zbus(property, name = "TimerDurationSeconds")]
    fn timer_duration_seconds(&self) -> i32 {
        self.snapshot().timer_duration_seconds
    }

    #[zbus(property, name = "TimerRemainingSeconds")]
    fn timer_remaining_seconds(&self) -> i32 {
        self.snapshot().timer_remaining_seconds
    }

    fn set_overlay_enabled(&self, enabled: bool) {
        self.send(ControlCommand::SetOverlayEnabled(enabled));
    }

    fn set_spotlight_active(&self, active: bool) {
        self.send(ControlCommand::SetSpotlightActive(active));
    }

    fn load_preset(&self, preset: String) -> bool {
        let (sender, receiver) = mpsc::sync_channel(1);
        self.send(ControlCommand::LoadPreset(preset, sender));
        receiver.recv().unwrap_or(false)
    }

    fn set_timer_enabled(&self, enabled: bool) {
        self.send(ControlCommand::SetTimerEnabled(enabled));
    }

    fn start_timer(&self) {
        self.send(ControlCommand::StartTimer);
    }

    fn restart_timer(&self) {
        self.send(ControlCommand::RestartTimer);
    }

    fn reset_timer(&self) {
        self.send(ControlCommand::ResetTimer);
    }

    fn set_timer_duration_seconds(&self, seconds: i32) {
        self.send(ControlCommand::SetTimerDurationSeconds(seconds));
    }

    fn show_preferences(&self) {
        self.send(ControlCommand::ShowPreferences);
    }

    fn show_about(&self) {
        self.send(ControlCommand::ShowAbout);
    }

    fn apply_commands(&self, commands: Vec<String>) {
        self.send(ControlCommand::ApplyCommands(commands));
    }

    fn quit(&self) {
        self.send(ControlCommand::Quit);
    }

    #[zbus(signal, name = "overlayEnabledChanged")]
    async fn emit_overlay_enabled_changed(
        emitter: &SignalEmitter<'_>,
        enabled: bool,
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "spotlightActiveChanged")]
    async fn emit_spotlight_active_changed(
        emitter: &SignalEmitter<'_>,
        active: bool,
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "connectedDevicesChanged")]
    async fn emit_connected_devices_changed(
        emitter: &SignalEmitter<'_>,
        devices: &[String],
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "connectedDeviceBatteryLevelsChanged")]
    async fn emit_connected_device_battery_levels_changed(
        emitter: &SignalEmitter<'_>,
        levels: &[i32],
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "connectedDeviceBatteryStatusesChanged")]
    async fn emit_connected_device_battery_statuses_changed(
        emitter: &SignalEmitter<'_>,
        statuses: &[String],
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "presetsChanged")]
    async fn emit_presets_changed(
        emitter: &SignalEmitter<'_>,
        presets: &[String],
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "currentPresetChanged")]
    async fn emit_current_preset_changed(
        emitter: &SignalEmitter<'_>,
        preset: &str,
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "timerEnabledChanged")]
    async fn emit_timer_enabled_changed(
        emitter: &SignalEmitter<'_>,
        enabled: bool,
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "timerStateChanged")]
    async fn emit_timer_state_changed(emitter: &SignalEmitter<'_>, state: &str)
    -> zbus::Result<()>;
    #[zbus(signal, name = "timerDurationSecondsChanged")]
    async fn emit_timer_duration_seconds_changed(
        emitter: &SignalEmitter<'_>,
        seconds: i32,
    ) -> zbus::Result<()>;
    #[zbus(signal, name = "timerRemainingSecondsChanged")]
    async fn emit_timer_remaining_seconds_changed(
        emitter: &SignalEmitter<'_>,
        seconds: i32,
    ) -> zbus::Result<()>;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn snapshot_defaults_match_the_cpp_control_contract() {
        let state = ControlSnapshot::default();
        assert_eq!(state.timer_state, "idle");
        assert_eq!(state.timer_duration_seconds, 15 * 60);
        assert_eq!(state.timer_remaining_seconds, 15 * 60);
        assert!(state.tray_visible);
        assert!(state.overlay_enabled);
    }

    #[test]
    fn launch_argument_parser_is_exercised_by_the_backend_cli_tests() {
        // The live forwarding path is deliberately integration-tested over
        // D-Bus. Keep this unit assertion as a guard for its public constants.
        assert_eq!(SERVICE_NAME, "org.projecteur.Projecteur");
        assert!(OBJECT_PATH.ends_with("/Control"));
    }
}
