//! Safe ownership wrappers around the Linux uinput and exclusive-grab ABIs.

use std::{
    fs::{File, OpenOptions},
    io::{self, Write},
    os::fd::{AsRawFd, RawFd},
    path::Path,
    time::Duration,
};

use linux_raw_sys::ioctl::{
    EVIOCGRAB, UI_DEV_CREATE, UI_DEV_DESTROY, UI_DEV_SETUP, UI_SET_EVBIT, UI_SET_KEYBIT,
    UI_SET_RELBIT,
};

use crate::input_event::{EV_KEY, EV_REL, EV_SYN, InputEvent, LINUX_INPUT_EVENT_SIZE};

const BUS_USB: u16 = 0x03;
const BTN_MISC: u16 = 0x100;
const KEY_OK: u16 = 0x160;
const KEY_MACRO1: u16 = 0x290;
const REL_CNT: u16 = 0x10;
const UINPUT_NAME_SIZE: usize = 80;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct InputId {
    bustype: u16,
    vendor: u16,
    product: u16,
    version: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct UinputSetup {
    id: InputId,
    name: [u8; UINPUT_NAME_SIZE],
    ff_effects_max: u32,
}

/// A virtual keyboard registered with the Linux input subsystem.
pub struct VirtualKeyboard {
    device: File,
}

impl VirtualKeyboard {
    /// Create a virtual keyboard at the standard `/dev/uinput` node.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when uinput is unavailable, inaccessible, or
    /// rejects device setup. No physical device is grabbed by this operation.
    pub fn create() -> io::Result<Self> {
        Self::create_at(Path::new("/dev/uinput"))
    }

    /// Create a virtual keyboard through an explicit uinput device node.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when the node cannot be opened or configured.
    pub fn create_at(path: &Path) -> io::Result<Self> {
        let device = OpenOptions::new().write(true).open(path)?;
        let fd = device.as_raw_fd();

        set_ioctl_value(fd, UI_SET_EVBIT, i32::from(EV_SYN))?;
        set_ioctl_value(fd, UI_SET_EVBIT, i32::from(EV_KEY))?;
        for code in 1..BTN_MISC {
            set_ioctl_value(fd, UI_SET_KEYBIT, i32::from(code))?;
        }
        for code in KEY_OK..KEY_MACRO1 {
            set_ioctl_value(fd, UI_SET_KEYBIT, i32::from(code))?;
        }

        let setup = UinputSetup {
            id: InputId {
                bustype: BUS_USB,
                vendor: 0x1209,
                product: 0x0001,
                version: 1,
            },
            name: uinput_name("Projecteur virtual keyboard"),
            ff_effects_max: 0,
        };
        ioctl_pointer(fd, UI_DEV_SETUP, &setup)?;
        ioctl_no_argument(fd, UI_DEV_CREATE)?;
        Ok(Self { device })
    }

    /// Forward a keyboard or synchronization event to the virtual device.
    /// Other event types are intentionally ignored because they were not
    /// advertised when the virtual keyboard was created.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when the kernel does not accept the event.
    pub fn emit(&mut self, event: InputEvent) -> io::Result<()> {
        if event.event_type != EV_KEY && event.event_type != EV_SYN {
            return Ok(());
        }
        self.device.write_all(&encode_input_event(event))
    }
}

impl Drop for VirtualKeyboard {
    fn drop(&mut self) {
        let _ = ioctl_no_argument(self.device.as_raw_fd(), UI_DEV_DESTROY);
    }
}

/// A virtual mouse registered with the Linux input subsystem.
pub struct VirtualMouse {
    device: File,
}

impl VirtualMouse {
    /// Create a virtual mouse at `/dev/uinput`.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when uinput is unavailable or rejects setup.
    pub fn create() -> io::Result<Self> {
        Self::create_at(Path::new("/dev/uinput"))
    }

    /// Create a virtual mouse through an explicit uinput node.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when the node cannot be opened or configured.
    pub fn create_at(path: &Path) -> io::Result<Self> {
        let device = OpenOptions::new().write(true).open(path)?;
        let fd = device.as_raw_fd();
        set_ioctl_value(fd, UI_SET_EVBIT, i32::from(EV_SYN))?;
        set_ioctl_value(fd, UI_SET_EVBIT, i32::from(EV_KEY))?;
        set_ioctl_value(fd, UI_SET_EVBIT, i32::from(EV_REL))?;
        for code in 0..REL_CNT {
            set_ioctl_value(fd, UI_SET_RELBIT, i32::from(code))?;
        }
        for code in BTN_MISC..KEY_OK {
            set_ioctl_value(fd, UI_SET_KEYBIT, i32::from(code))?;
        }
        let setup = UinputSetup {
            id: InputId {
                bustype: BUS_USB,
                vendor: 0x1209,
                product: 0x0002,
                version: 1,
            },
            name: uinput_name("Projecteur virtual mouse"),
            ff_effects_max: 0,
        };
        ioctl_pointer(fd, UI_DEV_SETUP, &setup)?;
        ioctl_no_argument(fd, UI_DEV_CREATE)?;
        Ok(Self { device })
    }

    /// Emit one mouse, relative-axis, or synchronization event.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when the kernel rejects the event.
    pub fn emit(&mut self, event: InputEvent) -> io::Result<()> {
        self.device.write_all(&encode_input_event(event))
    }
}

impl Drop for VirtualMouse {
    fn drop(&mut self) {
        let _ = ioctl_no_argument(self.device.as_raw_fd(), UI_DEV_DESTROY);
    }
}

/// A physical Linux input-event node held under an exclusive grab.
pub struct GrabbedEventDevice {
    device: File,
}

impl GrabbedEventDevice {
    /// Open and exclusively grab an input-event node.
    ///
    /// Callers should create their virtual forwarding device first. If the
    /// grab fails, the file is closed immediately and normal input continues.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when the node cannot be opened or grabbed.
    pub fn open(path: &Path) -> io::Result<Self> {
        let device = File::open(path)?;
        set_ioctl_value(device.as_raw_fd(), EVIOCGRAB, 1)?;
        Ok(Self { device })
    }

    /// Read one native Linux input event from the grabbed device.
    ///
    /// # Errors
    ///
    /// Propagates device read errors and truncated records.
    pub fn read_event(&mut self) -> io::Result<InputEvent> {
        crate::input_event::read_input_event(&mut self.device)
    }

    /// Wait up to `timeout` for and decode one native Linux input event.
    ///
    /// # Errors
    ///
    /// Propagates polling and input-event read errors.
    pub fn read_event_timeout(&mut self, timeout: Duration) -> io::Result<Option<InputEvent>> {
        let timeout_ms = i32::try_from(timeout.as_millis()).unwrap_or(i32::MAX);
        let mut descriptor = libc::pollfd {
            fd: self.device.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        loop {
            // SAFETY: `descriptor` points to one initialized `pollfd` for the
            // duration of the call, and its file descriptor remains owned.
            let result = unsafe { libc::poll(&raw mut descriptor, 1, timeout_ms) };
            if result > 0 {
                return self.read_event().map(Some);
            }
            if result == 0 {
                return Ok(None);
            }
            let error = io::Error::last_os_error();
            if error.kind() != io::ErrorKind::Interrupted {
                return Err(error);
            }
        }
    }

    /// Wait for one of several grabbed event nodes to become readable.
    ///
    /// # Errors
    ///
    /// Returns an I/O error when polling fails.
    pub fn wait_readable(devices: &[Self], timeout: Duration) -> io::Result<Option<usize>> {
        let timeout_ms = i32::try_from(timeout.as_millis()).unwrap_or(i32::MAX);
        let mut descriptors: Vec<_> = devices
            .iter()
            .map(|device| libc::pollfd {
                fd: device.device.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            })
            .collect();
        loop {
            // SAFETY: the descriptor vector owns initialized `pollfd` values
            // and is not changed while `poll` borrows its storage.
            let result = unsafe {
                libc::poll(
                    descriptors.as_mut_ptr(),
                    descriptors.len().try_into().unwrap_or(libc::nfds_t::MAX),
                    timeout_ms,
                )
            };
            if result > 0 {
                let readable = descriptors
                    .iter()
                    .position(|descriptor| descriptor.revents & libc::POLLIN != 0);
                if readable.is_some() {
                    return Ok(readable);
                }
                return Err(io::Error::new(
                    io::ErrorKind::BrokenPipe,
                    "grabbed input device disconnected",
                ));
            }
            if result == 0 {
                return Ok(None);
            }
            let error = io::Error::last_os_error();
            if error.kind() != io::ErrorKind::Interrupted {
                return Err(error);
            }
        }
    }
}

impl Drop for GrabbedEventDevice {
    fn drop(&mut self) {
        let _ = set_ioctl_value(self.device.as_raw_fd(), EVIOCGRAB, 0);
    }
}

fn uinput_name(name: &str) -> [u8; UINPUT_NAME_SIZE] {
    let mut output = [0; UINPUT_NAME_SIZE];
    let length = name.len().min(UINPUT_NAME_SIZE - 1);
    output[..length].copy_from_slice(&name.as_bytes()[..length]);
    output
}

fn encode_input_event(event: InputEvent) -> [u8; LINUX_INPUT_EVENT_SIZE] {
    let mut bytes = [0; LINUX_INPUT_EVENT_SIZE];
    let payload = &mut bytes[LINUX_INPUT_EVENT_SIZE - 8..];
    payload[..2].copy_from_slice(&event.event_type.to_ne_bytes());
    payload[2..4].copy_from_slice(&event.code.to_ne_bytes());
    payload[4..].copy_from_slice(&event.value.to_ne_bytes());
    bytes
}

fn set_ioctl_value(fd: RawFd, request: u32, value: i32) -> io::Result<()> {
    // SAFETY: `fd` is owned by a live `File`; these ioctl requests take an
    // integer value rather than a pointer, matching the Linux UAPI headers.
    let result = unsafe { libc::ioctl(fd, libc::c_ulong::from(request), value) };
    ioctl_result(result)
}

fn ioctl_pointer<T>(fd: RawFd, request: u32, value: &T) -> io::Result<()> {
    // SAFETY: `value` remains valid for the call and `request` expects a
    // pointer to the matching repr(C) Linux UAPI structure.
    let result = unsafe { libc::ioctl(fd, libc::c_ulong::from(request), value) };
    ioctl_result(result)
}

fn ioctl_no_argument(fd: RawFd, request: u32) -> io::Result<()> {
    // SAFETY: `fd` is owned by a live `File` and these requests have no third
    // argument according to the Linux UAPI headers.
    let result = unsafe { libc::ioctl(fd, libc::c_ulong::from(request)) };
    ioctl_result(result)
}

fn ioctl_result(result: libc::c_int) -> io::Result<()> {
    if result < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uinput_setup_matches_the_linux_uapi_layout() {
        assert_eq!(size_of::<InputId>(), 8);
        assert_eq!(size_of::<UinputSetup>(), 92);
        let name = uinput_name("Projecteur");
        assert_eq!(&name[..11], b"Projecteur\0");
    }

    #[test]
    fn input_event_encoding_preserves_the_stable_payload() {
        let event = InputEvent {
            event_type: EV_KEY,
            code: 106,
            value: 1,
        };
        let bytes = encode_input_event(event);

        assert_eq!(
            &bytes[..LINUX_INPUT_EVENT_SIZE - 8],
            &[0; LINUX_INPUT_EVENT_SIZE - 8]
        );
        assert_eq!(
            &bytes[LINUX_INPUT_EVENT_SIZE - 8..],
            &[1, 0, 106, 0, 1, 0, 0, 0]
        );
    }
}
