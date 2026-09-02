//! `KWin` `PipeWire` stream acquisition without a desktop-portal prompt.

use std::{
    collections::{HashMap, HashSet},
    fs::File,
    io::{BufWriter, Read, Write},
    os::unix::net::UnixStream,
    path::PathBuf,
    sync::mpsc::{self, Receiver, Sender, TryRecvError},
    thread,
    time::Duration,
};

use wayland_client::{Connection, Dispatch, QueueHandle, delegate_noop, protocol::wl_registry};

mod protocol {
    #![allow(
        dead_code,
        non_camel_case_types,
        non_upper_case_globals,
        non_snake_case,
        unused_imports,
        unused_unsafe,
        unused_variables,
        clippy::all,
        clippy::wildcard_imports
    )]

    use wayland_client;
    use wayland_client::protocol::*;

    pub mod __interfaces {
        use wayland_client::protocol::__interfaces::*;
        wayland_scanner::generate_interfaces!("../../protocols/zkde-screencast-unstable-v1.xml");
    }
    use self::__interfaces::*;

    wayland_scanner::generate_client_code!("../../protocols/zkde-screencast-unstable-v1.xml");
}

use protocol::{
    zkde_screencast_stream_unstable_v1,
    zkde_screencast_unstable_v1::{self, Pointer},
};

/// Logical desktop region corresponding to one `Qt` screen.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScreenRegion {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct StreamIdentifier {
    pub node_id: u32,
    pub object_serial: u64,
}

#[derive(Debug)]
pub enum CaptureEvent {
    Ready(ScreenRegion, StreamIdentifier),
    SnapshotReady(ScreenRegion, PathBuf),
    Closed(ScreenRegion),
    Failed(ScreenRegion, String),
    ManagerFailed(String),
}

#[derive(Debug)]
enum CaptureCommand {
    Ensure(ScreenRegion),
}

pub struct ScreencastManager {
    commands: Sender<CaptureCommand>,
    events: Receiver<CaptureEvent>,
}

impl ScreencastManager {
    pub fn start() -> Result<Self, String> {
        let (command_sender, command_receiver) = mpsc::channel();
        let (event_sender, event_receiver) = mpsc::channel();
        thread::Builder::new()
            .name("projecteur-screencast".to_owned())
            .spawn(move || run_manager(&command_receiver, &event_sender))
            .map_err(|error| format!("cannot start KWin screencast manager: {error}"))?;
        Ok(Self {
            commands: command_sender,
            events: event_receiver,
        })
    }

    pub fn ensure(&self, region: ScreenRegion) {
        let _ = self.commands.send(CaptureCommand::Ensure(region));
    }

    pub fn try_recv(&self) -> Result<CaptureEvent, TryRecvError> {
        self.events.try_recv()
    }
}

struct WaylandState {
    manager: Option<zkde_screencast_unstable_v1::ZkdeScreencastUnstableV1>,
    event_sender: Sender<CaptureEvent>,
}

fn run_manager(commands: &Receiver<CaptureCommand>, events: &Sender<CaptureEvent>) {
    if let Err(error) = run_wayland(commands, events) {
        let _ = events.send(CaptureEvent::ManagerFailed(error));
    }
}

fn run_wayland(
    commands: &Receiver<CaptureCommand>,
    events: &Sender<CaptureEvent>,
) -> Result<(), String> {
    let connection = Connection::connect_to_env()
        .map_err(|error| format!("cannot connect to the Wayland compositor: {error}"))?;
    let mut event_queue = connection.new_event_queue();
    let queue_handle = event_queue.handle();
    connection.display().get_registry(&queue_handle, ());
    let mut state = WaylandState {
        manager: None,
        event_sender: events.clone(),
    };
    event_queue
        .roundtrip(&mut state)
        .map_err(|error| format!("cannot enumerate Wayland globals: {error}"))?;
    if state.manager.is_none() {
        return run_snapshot_manager(commands, events);
    }

    let mut streams = HashMap::new();
    loop {
        match commands.recv_timeout(Duration::from_millis(25)) {
            Ok(CaptureCommand::Ensure(region)) => {
                if streams.contains_key(&region) {
                    continue;
                }
                let manager = state
                    .manager
                    .as_ref()
                    .expect("manager checked after registry roundtrip");
                let stream = manager.stream_region(
                    region.x,
                    region.y,
                    region.width,
                    region.height,
                    0.0,
                    Pointer::Hidden.into(),
                    &queue_handle,
                    region,
                );
                streams.insert(region, stream);
            }
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => return Ok(()),
        }
        event_queue
            .roundtrip(&mut state)
            .map_err(|error| format!("KWin screencast dispatch failed: {error}"))?;
    }
}

fn run_snapshot_manager(
    commands: &Receiver<CaptureCommand>,
    events: &Sender<CaptureEvent>,
) -> Result<(), String> {
    let connection = zbus::blocking::Connection::session()
        .map_err(|error| format!("cannot connect to the desktop D-Bus session: {error}"))?;
    let proxy = zbus::blocking::Proxy::new(
        &connection,
        "org.kde.KWin.ScreenShot2",
        "/org/kde/KWin/ScreenShot2",
        "org.kde.KWin.ScreenShot2",
    )
    .map_err(|error| format!("cannot access KWin ScreenShot2: {error}"))?;
    let mut captured = HashSet::new();
    while let Ok(CaptureCommand::Ensure(region)) = commands.recv() {
        if captured.contains(&region) {
            continue;
        }
        match capture_snapshot(&proxy, region) {
            Ok(path) => {
                captured.insert(region);
                let _ = events.send(CaptureEvent::SnapshotReady(region, path));
            }
            Err(error) => {
                let _ = events.send(CaptureEvent::Failed(region, error));
            }
        }
    }
    Ok(())
}

fn capture_snapshot(
    proxy: &zbus::blocking::Proxy<'_>,
    region: ScreenRegion,
) -> Result<PathBuf, String> {
    use zbus::zvariant::{Fd, OwnedValue, Value};

    let (mut read_pipe, write_pipe) = UnixStream::pair()
        .map_err(|error| format!("cannot create KWin screenshot pipe: {error}"))?;
    let options = HashMap::from([
        ("hide-caller-windows", Value::from(true)),
        ("native-resolution", Value::from(true)),
    ]);
    let attributes: HashMap<String, OwnedValue> = proxy
        .call(
            "CaptureArea",
            &(
                region.x,
                region.y,
                region.width,
                region.height,
                options,
                Fd::from(&write_pipe),
            ),
        )
        .map_err(|error| format!("KWin ScreenShot2 failed: {error}"))?;
    drop(write_pipe);

    let width = attribute_u32(&attributes, "width")?;
    let height = attribute_u32(&attributes, "height")?;
    let stride = attribute_u32(&attributes, "stride")?;
    let format = attribute_u32(&attributes, "format")?;
    let expected_length = usize::try_from(u64::from(stride) * u64::from(height))
        .map_err(|_| "KWin screenshot dimensions overflow memory limits".to_owned())?;
    let mut pixels = Vec::with_capacity(expected_length);
    read_pipe
        .read_to_end(&mut pixels)
        .map_err(|error| format!("cannot read KWin screenshot: {error}"))?;
    if pixels.len() < expected_length {
        return Err(format!(
            "KWin returned an incomplete screenshot ({} of {expected_length} bytes)",
            pixels.len()
        ));
    }

    let output = std::env::temp_dir().join(format!(
        "projecteur-rs-{}-{}-{}-{}x{}.ppm",
        std::process::id(),
        region.x,
        region.y,
        region.width,
        region.height
    ));
    write_ppm(&output, &pixels, width, height, stride, format)?;
    Ok(output)
}

fn attribute_u32(
    attributes: &HashMap<String, zbus::zvariant::OwnedValue>,
    name: &str,
) -> Result<u32, String> {
    attributes
        .get(name)
        .ok_or_else(|| format!("KWin screenshot is missing its {name} attribute"))
        .and_then(|value| {
            u32::try_from(value)
                .map_err(|error| format!("KWin screenshot has an invalid {name}: {error}"))
        })
}

fn write_ppm(
    path: &std::path::Path,
    pixels: &[u8],
    width: u32,
    height: u32,
    stride: u32,
    format: u32,
) -> Result<(), String> {
    let file = File::create(path)
        .map_err(|error| format!("cannot create screenshot {}: {error}", path.display()))?;
    let mut output = BufWriter::new(file);
    write!(output, "P6\n{width} {height}\n255\n")
        .map_err(|error| format!("cannot write screenshot header: {error}"))?;
    for y in 0..height {
        let row_start = usize::try_from(u64::from(y) * u64::from(stride))
            .map_err(|_| "KWin screenshot row offset overflowed".to_owned())?;
        for x in 0..width {
            let rgb = rgb_pixel(pixels, row_start, x, format)?;
            output
                .write_all(&rgb)
                .map_err(|error| format!("cannot write screenshot pixels: {error}"))?;
        }
    }
    output
        .flush()
        .map_err(|error| format!("cannot finish screenshot {}: {error}", path.display()))
}

fn rgb_pixel(pixels: &[u8], row_start: usize, x: u32, format: u32) -> Result<[u8; 3], String> {
    // Values are QImage::Format. KWin normally returns one of the 32-bit formats.
    let (bytes_per_pixel, red, green, blue) = match format {
        4..=6 => (4_u32, 2_usize, 1_usize, 0_usize), // RGB32 / ARGB32
        13 => (3, 0, 1, 2),                          // RGB888
        17 | 18 | 30 => (4, 0, 1, 2),                // RGBA8888 / RGBX8888
        29 => (3, 2, 1, 0),                          // BGR888
        _ => return Err(format!("KWin returned unsupported QImage format {format}")),
    };
    let offset = row_start
        .checked_add(
            usize::try_from(u64::from(x) * u64::from(bytes_per_pixel))
                .map_err(|_| "KWin screenshot pixel offset overflowed".to_owned())?,
        )
        .ok_or_else(|| "KWin screenshot pixel offset overflowed".to_owned())?;
    let pixel = pixels
        .get(offset..offset + usize::try_from(bytes_per_pixel).unwrap_or(4))
        .ok_or_else(|| "KWin screenshot ended inside a pixel".to_owned())?;
    Ok([pixel[red], pixel[green], pixel[blue]])
}

impl Dispatch<wl_registry::WlRegistry, ()> for WaylandState {
    fn event(
        state: &mut Self,
        registry: &wl_registry::WlRegistry,
        event: wl_registry::Event,
        (): &(),
        _: &Connection,
        queue_handle: &QueueHandle<Self>,
    ) {
        let wl_registry::Event::Global {
            name,
            interface,
            version,
        } = event
        else {
            return;
        };
        if interface == "zkde_screencast_unstable_v1" && state.manager.is_none() {
            state.manager = Some(registry.bind(name, version.min(6), queue_handle, ()));
        }
    }
}

delegate_noop!(WaylandState: ignore zkde_screencast_unstable_v1::ZkdeScreencastUnstableV1);

impl Dispatch<zkde_screencast_stream_unstable_v1::ZkdeScreencastStreamUnstableV1, ScreenRegion>
    for WaylandState
{
    fn event(
        state: &mut Self,
        _: &zkde_screencast_stream_unstable_v1::ZkdeScreencastStreamUnstableV1,
        event: zkde_screencast_stream_unstable_v1::Event,
        region: &ScreenRegion,
        _: &Connection,
        _: &QueueHandle<Self>,
    ) {
        use zkde_screencast_stream_unstable_v1::Event;
        let event = match event {
            Event::Created { node } => CaptureEvent::Ready(
                *region,
                StreamIdentifier {
                    node_id: node,
                    object_serial: 0,
                },
            ),
            Event::Serial {
                object_serial_hi,
                object_serial_low,
            } => CaptureEvent::Ready(
                *region,
                StreamIdentifier {
                    node_id: 0,
                    object_serial: (u64::from(object_serial_hi) << 32)
                        | u64::from(object_serial_low),
                },
            ),
            Event::Closed => CaptureEvent::Closed(*region),
            Event::Failed { error } => CaptureEvent::Failed(*region, error),
        };
        let _ = state.event_sender.send(event);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn screen_regions_are_stable_hash_keys() {
        let region = ScreenRegion {
            x: -1920,
            y: 0,
            width: 1920,
            height: 1080,
        };
        let mut streams = HashMap::new();
        streams.insert(region, StreamIdentifier::default());
        assert_eq!(streams.get(&region), Some(&StreamIdentifier::default()));
    }

    #[test]
    fn converts_common_qimage_pixel_layouts_to_rgb() {
        assert_eq!(rgb_pixel(&[3, 2, 1, 255], 0, 0, 5).unwrap(), [1, 2, 3]);
        assert_eq!(rgb_pixel(&[1, 2, 3, 255], 0, 0, 17).unwrap(), [1, 2, 3]);
        assert_eq!(rgb_pixel(&[3, 2, 1], 0, 0, 29).unwrap(), [1, 2, 3]);
        assert!(rgb_pixel(&[0; 4], 0, 0, 0).is_err());
    }
}
