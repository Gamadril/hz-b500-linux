//! Vehicle media keys from the sole ubus-to-uinput action adapter.
use crate::{AndroidAutoMessage, SendableAndroidAutoMessage, Wifi, protobuf::MessageField};
use std::{
    ffi::{CString, c_char},
    fs::File,
    io,
    os::fd::{AsRawFd, FromRawFd},
};
use tokio::{
    io::unix::AsyncFd,
    sync::{mpsc, watch},
};

/// Android keycodes advertised to the phone: next, previous, play, stop.
pub const KEYCODES: [u32; 4] = [87, 88, 126, 86];
#[repr(C)]
#[derive(Default)]
struct Event {
    kind: u16,
    code: u16,
    value: i32,
    micros: u64,
}
unsafe extern "C" {
    fn aa_media_open(path: *const c_char) -> i32;
    fn aa_media_now_us() -> u64;
    fn aa_touch_read(fd: i32, event: *mut Event) -> i32;
}
fn android_key(code: u16) -> Option<u32> {
    match code {
        163 => Some(87),
        165 => Some(88),
        207 => Some(126),
        128 => Some(86),
        _ => None,
    }
}
fn encode(code: u32, micros: u64) -> Wifi::InputEventIndication {
    // One complete action carries both edges; no held key can leak across sessions.
    let mut keys = Wifi::ButtonEvents::new();
    for pressed in [true, false] {
        let mut key = Wifi::ButtonEvent::new();
        key.set_scan_code(code);
        key.set_is_pressed(pressed);
        keys.button_events.push(key);
    }
    let mut event = Wifi::InputEventIndication::new();
    event.set_timestamp(micros);
    event.button_event = MessageField::some(keys);
    event
}
fn fresh_press(event: &Event, enabled: bool, dropped: bool, enabled_after: u64, now: u64) -> bool {
    event.kind == 1
        && event.value == 1
        && enabled
        && !dropped
        && event.micros >= enabled_after
        && event.micros <= now
        && now - event.micros <= 1_000_000
}
fn open() -> io::Result<AsyncFd<File>> {
    for entry in std::fs::read_dir("/dev/input")? {
        let path = entry?.path();
        if !path
            .file_name()
            .is_some_and(|n| n.to_string_lossy().starts_with("event"))
        {
            continue;
        }
        let path = CString::new(path.as_os_str().as_encoded_bytes())?;
        // SAFETY: A valid C path is provided; a successful return transfers ownership.
        let fd = unsafe { aa_media_open(path.as_ptr()) };
        if fd >= 0 {
            return AsyncFd::new(unsafe { File::from_raw_fd(fd) });
        }
    }
    Err(io::ErrorKind::NotFound.into())
}
fn read(fd: &AsyncFd<File>) -> io::Result<Event> {
    let mut event = Event::default();
    // SAFETY: The file remains owned and the scalar output matches native layout.
    match unsafe { aa_touch_read(fd.get_ref().as_raw_fd(), &mut event) } {
        1 => Ok(event),
        0 => Err(io::ErrorKind::WouldBlock.into()),
        e => Err(io::Error::from_raw_os_error(-e)),
    }
}
/// Reopen a recreated uinput device; events received without focus are discarded.
pub async fn run(
    mut enabled: watch::Receiver<bool>,
    sender: mpsc::Sender<SendableAndroidAutoMessage>,
) -> io::Result<()> {
    loop {
        let fd = match open() {
            Ok(fd) => fd,
            Err(_) => {
                tokio::select! {
                    _ = sender.closed() => return Ok(()),
                    _ = tokio::time::sleep(std::time::Duration::from_secs(1)) => continue,
                }
            }
        };
        // Discard the device backlog before accepting a newly opened session.
        for _ in 0..256 {
            if read(&fd).is_err() {
                break;
            }
        }
        let mut ready = *enabled.borrow_and_update();
        // SAFETY: Native clock query has no pointer arguments.
        let mut enabled_after = unsafe { aa_media_now_us() };
        let mut dropped = false;
        loop {
            tokio::select! {
                biased;
                _ = sender.closed() => return Ok(()),
                changed = enabled.changed() => {
                    if changed.is_err() { return Ok(()); }
                    ready = *enabled.borrow_and_update();
                    enabled_after = unsafe { aa_media_now_us() };
                }
                result = fd.readable() => {
                    let mut guard = result?;
                    let event = match guard.try_io(|_| read(&fd)) {
                        Ok(Ok(event)) => event,
                        Ok(Err(_)) => break,
                        Err(_) => continue,
                    };
                    if event.kind == 0 {
                        if event.code == 3 { dropped = true; }
                        if event.code == 0 && dropped {
                            dropped = false;
                            enabled_after = unsafe { aa_media_now_us() };
                        }
                        continue;
                    }
                    // uinput has host receipt timestamps. Only process fresh press edges.
                    let now = unsafe { aa_media_now_us() };
                    if !fresh_press(&event, ready, dropped, enabled_after, now) { continue; }
                    let Some(code) = android_key(event.code) else { continue; };
                    let message: SendableAndroidAutoMessage = AndroidAutoMessage::Input(encode(code, event.micros)).sendable();
                    tokio::time::timeout(std::time::Duration::from_millis(250), sender.send(message)).await
                        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut,"Media input queue full"))?
                        .map_err(|_| io::Error::new(io::ErrorKind::BrokenPipe,"Media input session ended"))?;
                }
            }
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn media_mapping_and_complete_edges() {
        assert_eq!(android_key(163), Some(87));
        assert_eq!(android_key(165), Some(88));
        assert_eq!(android_key(207), Some(126));
        assert_eq!(android_key(128), Some(86));
        assert_eq!(android_key(1), None);
        let event = encode(87, 123);
        let keys = &event.button_event.as_ref().unwrap().button_events;
        assert_eq!(event.timestamp(), 123);
        assert_eq!(keys.len(), 2);
        assert!(keys[0].is_pressed());
        assert!(!keys[1].is_pressed());
        assert_eq!(keys[0].scan_code(), 87);
        assert_eq!(keys[1].scan_code(), 87);
    }
    #[test]
    fn ignore_old_unfocused_repeated_and_overrun_events() {
        let mut event = Event {
            kind: 1,
            code: 163,
            value: 1,
            micros: 100,
        };
        assert!(fresh_press(&event, true, false, 99, 101));
        assert!(!fresh_press(&event, false, false, 99, 101));
        assert!(!fresh_press(&event, true, true, 99, 101));
        assert!(!fresh_press(&event, true, false, 101, 102));
        assert!(!fresh_press(&event, true, false, 99, 99));
        assert!(!fresh_press(&event, true, false, 99, 1_000_101));
        event.value = 0;
        assert!(!fresh_press(&event, true, false, 99, 101));
        event.value = 2;
        assert!(!fresh_press(&event, true, false, 99, 101));
    }
}
