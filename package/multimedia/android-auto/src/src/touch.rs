//! Linux direct-touch input, including Goodix type-A and slot-based multitouch.
use crate::{AndroidAutoMessage, SendableAndroidAutoMessage, Wifi, protobuf::MessageField};
use std::{
    collections::HashSet,
    ffi::{CString, c_char},
    fs::File,
    io,
    os::fd::{AsRawFd, FromRawFd},
    path::Path,
};
use tokio::{
    io::unix::AsyncFd,
    sync::{mpsc, watch},
};

/// Advertised AA touchscreen/video dimensions (stock BZ500 profile: 720p).
pub const VIDEO_SIZE: (u16, u16) = (1280, 720);
/// Total video margins. The phone draws its UI into the centred
/// `VIDEO_SIZE - VIDEO_MARGINS` area (1024x600, the panel size); only that area
/// is shown.
pub const VIDEO_MARGINS: (u16, u16) = (256, 120);
/// Advertised touchscreen: the visible UI area. Touches are reported relative to
/// it (full-frame coordinates offset by the margins left the right side dead).
pub const TOUCH_SIZE: (u16, u16) = (VIDEO_SIZE.0 - VIDEO_MARGINS.0, VIDEO_SIZE.1 - VIDEO_MARGINS.1);
/// Maximum simultaneous contacts accepted from evdev.
const SLOTS: usize = 16;

/// Scalar native probe result, including current slot state for resynchronization.
#[repr(C)]
#[derive(Clone)]
struct Info {
    protocol: i32,
    x_min: i32,
    x_max: i32,
    y_min: i32,
    y_max: i32,
    slot_min: i32,
    slots: i32,
    down: i32,
    current_slot: i32,
    ids: [i32; SLOTS],
    xs: [i32; SLOTS],
    ys: [i32; SLOTS],
    name: [u8; 128],
}
impl Default for Info {
    fn default() -> Self {
        Self {
            protocol: 0,
            x_min: 0,
            x_max: 0,
            y_min: 0,
            y_max: 0,
            slot_min: 0,
            slots: 0,
            down: 0,
            current_slot: 0,
            ids: [-1; SLOTS],
            xs: [0; SLOTS],
            ys: [0; SLOTS],
            name: [0; 128],
        }
    }
}
/// Normalized evdev event, independent of native timeval layout.
#[repr(C)]
#[derive(Default, Clone, Copy)]
struct Event {
    kind: u16,
    code: u16,
    value: i32,
    micros: u64,
}
unsafe extern "C" {
    fn aa_touch_open(path: *const c_char, info: *mut Info) -> i32;
    fn aa_touch_state(fd: i32, info: *mut Info) -> i32;
    fn aa_touch_read(fd: i32, event: *mut Event) -> i32;
}

/// Nonblocking evdev reader; no exclusive grab or kernel input configuration change.
pub struct TouchDevice {
    /// Owned descriptor, registered with Tokio rather than a blocking reader thread.
    fd: AsyncFd<File>,
    /// Immutable capabilities and initial state.
    info: Info,
    /// Actual LCD dimensions used by the Cedar renderer.
    display: (u32, u32),
    /// Discovered event node for diagnostics.
    path: String,
}
impl TouchDevice {
    /// Open an explicit event node, or discover the direct Goodix device for `auto`.
    pub fn open(selector: &str, screen: u32) -> io::Result<Self> {
        if selector != "auto" {
            let path = Path::new(selector);
            let (file, info) = Self::probe(path)?;
            return Self::from_probe(path, screen, file, info);
        }
        let mut paths: Vec<_> = std::fs::read_dir("/dev/input")?
            .filter_map(Result::ok)
            .filter(|e| e.file_name().to_string_lossy().starts_with("event"))
            .map(|e| e.path())
            .collect();
        paths.sort();
        for path in paths {
            if let Ok((file, info)) = Self::probe(&path) {
                let end = info
                    .name
                    .iter()
                    .position(|&b| b == 0)
                    .unwrap_or(info.name.len());
                let name = String::from_utf8_lossy(&info.name[..end]).to_ascii_lowercase();
                if name.contains("goodix") || name.contains("gt9") {
                    return Self::from_probe(&path, screen, file, info);
                }
            }
        }
        Err(io::Error::new(
            io::ErrorKind::NotFound,
            "no direct Goodix evdev device found",
        ))
    }
    /// Probe capabilities through the native Linux ABI and retain descriptor ownership.
    fn probe(path: &Path) -> io::Result<(File, Info)> {
        use std::os::unix::ffi::OsStrExt;
        let name = CString::new(path.as_os_str().as_bytes())?;
        let mut info = Info::default();
        // SAFETY: Native fills the complete scalar result; a successful fd is owned below.
        let fd = unsafe { aa_touch_open(name.as_ptr(), &mut info) };
        if fd < 0 {
            return Err(io::Error::from_raw_os_error(-fd));
        }
        // SAFETY: The successful native open returns one new, uniquely owned descriptor.
        let file = unsafe { File::from_raw_fd(fd) };
        Ok((file, info))
    }
    /// Bind a validated touchscreen to the selected display's actual dimensions.
    fn from_probe(path: &Path, screen: u32, file: File, info: Info) -> io::Result<Self> {
        let display = crate::cedar::display_size(screen)?;
        Ok(Self {
            fd: AsyncFd::new(file)?,
            info,
            display,
            path: path.display().to_string(),
        })
    }
    /// Name reported by EVIOCGNAME.
    pub fn name(&self) -> String {
        let end = self
            .info
            .name
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(self.info.name.len());
        String::from_utf8_lossy(&self.info.name[..end]).into_owned()
    }
    /// Event node selected without relying on input-device numbering.
    pub fn path(&self) -> &str {
        &self.path
    }
    /// Read one complete native input_event without blocking.
    fn read_now(&self) -> io::Result<Event> {
        let mut event = Event::default();
        // SAFETY: The fd stays owned by self and the output buffer lives through this call.
        match unsafe { aa_touch_read(self.fd.get_ref().as_raw_fd(), &mut event) } {
            1 => Ok(event),
            0 => Err(io::ErrorKind::WouldBlock.into()),
            rc => Err(io::Error::from_raw_os_error(-rc)),
        }
    }
    /// Wait for readability, correctly clearing readiness on EAGAIN.
    async fn read(&self) -> io::Result<Event> {
        loop {
            let mut ready = self.fd.readable().await?;
            if let Ok(result) = ready.try_io(|_| self.read_now()) {
                return result;
            }
        }
    }
    /// Drop old queued events and query current contacts before enabling a new session.
    fn current_state(&self) -> io::Result<Info> {
        for _ in 0..4096 {
            match self.read_now() {
                Ok(_) => continue,
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                    let mut info = self.info.clone();
                    // SAFETY: Native only queries this descriptor and updates its scalar snapshot.
                    let rc = unsafe { aa_touch_state(self.fd.get_ref().as_raw_fd(), &mut info) };
                    if rc < 0 {
                        return Err(io::Error::from_raw_os_error(-rc));
                    }
                    return Ok(info);
                }
                Err(e) => return Err(e),
            }
        }
        Err(io::Error::other(
            "evdev backlog did not drain within its bound",
        ))
    }
    /// Forward complete touch reports while input and video focus are active.
    /// A bounded send timeout ends the session instead of losing a release event.
    pub async fn run(
        &self,
        mut enabled: watch::Receiver<bool>,
        sender: mpsc::Sender<SendableAndroidAutoMessage>,
    ) -> io::Result<()> {
        let mut decoder = Decoder::new(self.current_state()?, self.display, *enabled.borrow());
        loop {
            let messages = tokio::select! {
                biased;
                _ = sender.closed() => return Ok(()),
                changed = enabled.changed() => {
                    if changed.is_err() { return Ok(()); }
                    let messages = decoder.cancel();
                    decoder.reset(self.current_state()?, *enabled.borrow_and_update());
                    messages
                }
                event = self.read() => {
                    let messages = decoder.feed(event?)?;
                    if decoder.resync { decoder.reset(self.current_state()?, *enabled.borrow()); }
                    messages
                }
            };
            for message in messages {
                let sent = tokio::time::timeout(
                    std::time::Duration::from_millis(500),
                    sender.send(AndroidAutoMessage::Input(message).sendable()),
                )
                .await
                .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "AA touch queue stalled"))?;
                // Receiver closure is normal during phone disconnect. A new
                // session gets a new producer and no old queued contacts.
                if sent.is_err() {
                    return Ok(());
                }
            }
        }
    }
}

/// Exit zone shared with the HMI's fbDOOM: top-left screen pixels per axis.
const CORNER_PX: i64 = 100;
/// Hold time before the corner exit fires.
const CORNER_HOLD_MS: u64 = 2000;
/// Exit gesture state: a contact held inside the top-left zone. Raw ranges
/// map onto the whole display, like the HMI's own touch input.
#[derive(Default)]
struct CornerHold {
    /// Last raw X of any contact.
    x: i32,
    /// Last raw Y of any contact.
    y: i32,
    /// BTN_TOUCH state.
    down: bool,
    /// Monotonic milliseconds when the current in-zone hold began.
    since: Option<u64>,
}
impl CornerHold {
    /// Track touch state and the latest position.
    fn feed(&mut self, e: &Event) {
        match (e.kind, e.code) {
            (1, 330) => self.down = e.value != 0,
            (3, 53) | (3, 0) => self.x = e.value,
            (3, 54) | (3, 1) => self.y = e.value,
            _ => (),
        }
    }
    /// Checked on a timer too: a resting finger may send no events.
    fn held(&mut self, now_ms: u64, info: &Info, display: (u32, u32)) -> bool {
        let pixel = |v: i32, min: i32, max: i32, extent: u32| {
            (v as i64 - min as i64) * (extent as i64 - 1) / (max as i64 - min as i64).max(1)
        };
        let inside = self.down
            && pixel(self.x, info.x_min, info.x_max, display.0) < CORNER_PX
            && pixel(self.y, info.y_min, info.y_max, display.1) < CORNER_PX;
        self.since = if inside { self.since.or(Some(now_ms)) } else { None };
        self.since.is_some_and(|start| now_ms - start >= CORNER_HOLD_MS)
    }
}
impl TouchDevice {
    /// Completes once the corner exit gesture is held. Use a separate
    /// `TouchDevice`: no grab, so a session reader still gets every event.
    pub async fn hold_corner(&self) -> io::Result<()> {
        let start = std::time::Instant::now();
        let mut corner = CornerHold::default();
        let mut tick = tokio::time::interval(std::time::Duration::from_millis(100));
        loop {
            tokio::select! {
                event = self.read() => corner.feed(&event?),
                _ = tick.tick() => (),
            }
            if corner.held(start.elapsed().as_millis() as u64, &self.info, self.display) {
                return Ok(());
            }
        }
    }
}

/// One physical contact; slot positions persist even when its tracking ID changes.
#[derive(Clone, Copy, Default)]
struct Raw {
    id: Option<i32>,
    x: Option<i32>,
    y: Option<i32>,
}
/// One Android pointer, retaining its stable ID and last mapped location.
#[derive(Clone, Copy)]
struct Point {
    raw: i32,
    id: u32,
    x: u32,
    y: u32,
}
/// Report decoder and gesture state, driven only by SYN_REPORT boundaries.
struct Decoder {
    info: Info,
    display: (u32, u32),
    enabled: bool,
    blocked: bool,
    slots: [Raw; SLOTS],
    slot: usize,
    partial: Raw,
    frame: Vec<Raw>,
    down: bool,
    points: Vec<Point>,
    ignored: HashSet<i32>,
    dropped: bool,
    resync: bool,
    timestamp: u64,
}
impl Decoder {
    /// Initialize from kernel state; already held fingers must lift before producing input.
    fn new(info: Info, display: (u32, u32), enabled: bool) -> Self {
        let mut d = Self {
            info: info.clone(),
            display,
            enabled,
            blocked: false,
            slots: [Raw::default(); SLOTS],
            slot: 0,
            partial: Raw::default(),
            frame: Vec::new(),
            down: false,
            points: Vec::new(),
            ignored: HashSet::new(),
            dropped: false,
            resync: false,
            timestamp: 0,
        };
        d.reset(info, enabled);
        d
    }
    /// Reset evdev state after focus changes or SYN_DROPPED without reviving old gestures.
    fn reset(&mut self, info: Info, enabled: bool) {
        self.enabled = enabled;
        self.blocked = info.down != 0;
        self.down = info.down != 0;
        self.slots = [Raw::default(); SLOTS];
        self.slot = info.current_slot as usize;
        for i in 0..SLOTS {
            self.slots[i] = Raw {
                id: (info.ids[i] >= 0).then_some(info.ids[i]),
                x: Some(info.xs[i]),
                y: Some(info.ys[i]),
            };
        }
        self.partial = Raw::default();
        self.frame.clear();
        self.points.clear();
        self.ignored.clear();
        self.dropped = false;
        self.resync = false;
        self.info = info;
    }
    /// Map raw ranges to the letterboxed visible area used by Cedar, in
    /// `TOUCH_SIZE` coordinates.
    fn map(&self, raw: Raw, clamp: bool) -> Option<(u32, u32)> {
        let (x, y) = (raw.x?, raw.y?);
        let (sw, sh) = (i64::from(self.display.0), i64::from(self.display.1));
        let (vw, vh) = (i64::from(TOUCH_SIZE.0), i64::from(TOUCH_SIZE.1));
        let mut width = sw;
        let mut height = width * vh / vw;
        if height > sh {
            height = sh;
            width = height * vw / vh;
        }
        if width < 2 || height < 2 {
            return None;
        }
        let x = (i64::from(x).clamp(self.info.x_min.into(), self.info.x_max.into())
            - i64::from(self.info.x_min))
            * (sw - 1)
            / (i64::from(self.info.x_max) - i64::from(self.info.x_min));
        let y = (i64::from(y).clamp(self.info.y_min.into(), self.info.y_max.into())
            - i64::from(self.info.y_min))
            * (sh - 1)
            / (i64::from(self.info.y_max) - i64::from(self.info.y_min));
        let (left, top) = ((sw - width) / 2, (sh - height) / 2);
        if !clamp && (x < left || x >= left + width || y < top || y >= top + height) {
            return None;
        }
        Some((
            ((x - left).clamp(0, width - 1) * (vw - 1) / (width - 1)) as u32,
            ((y - top).clamp(0, height - 1) * (vh - 1) / (height - 1)) as u32,
        ))
    }
    /// Encode the current pointer list with a monotonic microsecond timestamp.
    fn message(
        &mut self,
        action: Wifi::touch_action::Enum,
        index: usize,
    ) -> Wifi::InputEventIndication {
        self.timestamp = self.timestamp.saturating_add(1);
        let mut touch = Wifi::TouchEvent::new();
        touch.set_touch_action(action);
        touch.set_action_index(index as u32);
        touch.touch_location = self
            .points
            .iter()
            .map(|p| {
                let mut location = Wifi::TouchLocation::new();
                location.set_x(p.x);
                location.set_y(p.y);
                location.set_pointer_id(p.id);
                location
            })
            .collect();
        let mut message = Wifi::InputEventIndication::new();
        message.set_timestamp(self.timestamp);
        message.set_disp_channel(0);
        message.touch_event = MessageField::some(touch);
        message
    }
    /// Release all logical pointers, including on focus loss and input overflow.
    fn cancel(&mut self) -> Vec<Wifi::InputEventIndication> {
        let mut messages = Vec::new();
        while !self.points.is_empty() {
            let index = self.points.len() - 1;
            messages.push(self.message(
                if index == 0 {
                    Wifi::touch_action::Enum::RELEASE
                } else {
                    Wifi::touch_action::Enum::POINTER_UP
                },
                index,
            ));
            self.points.pop();
        }
        messages
    }
    /// Compare a complete physical report with the last logical pointer set.
    fn report(&mut self, contacts: Vec<Raw>) -> Vec<Wifi::InputEventIndication> {
        if self.blocked {
            self.blocked = !contacts.is_empty();
            return Vec::new();
        }
        if !self.enabled {
            return Vec::new();
        }
        self.ignored
            .retain(|id| contacts.iter().any(|p| p.id == Some(*id)));
        let mut next = Vec::new();
        for raw in contacts {
            let Some(id) = raw.id else {
                continue;
            };
            if self.ignored.contains(&id) {
                continue;
            }
            if next.iter().any(|p: &Point| p.raw == id) {
                continue;
            }
            let existing = self.points.iter().any(|p| p.raw == id);
            if let Some((x, y)) = self.map(raw, existing) {
                next.push(Point {
                    raw: id,
                    id: 0,
                    x,
                    y,
                });
            } else {
                self.ignored.insert(id);
            }
        }
        let mut changed = false;
        for p in &mut self.points {
            if let Some(n) = next.iter().find(|n| n.raw == p.raw) {
                changed |= p.x != n.x || p.y != n.y;
                p.x = n.x;
                p.y = n.y;
            }
        }
        let mut messages = Vec::new();
        for i in (0..self.points.len()).rev() {
            if !next.iter().any(|p| p.raw == self.points[i].raw) {
                messages.push(self.message(
                    if self.points.len() == 1 {
                        Wifi::touch_action::Enum::RELEASE
                    } else {
                        Wifi::touch_action::Enum::POINTER_UP
                    },
                    i,
                ));
                self.points.remove(i);
            }
        }
        for mut n in next {
            if self.points.iter().any(|p| p.raw == n.raw) {
                continue;
            }
            n.id = (0..SLOTS as u32)
                .find(|id| !self.points.iter().any(|p| p.id == *id))
                .unwrap();
            self.points.push(n);
            messages.push(self.message(
                if self.points.len() == 1 {
                    Wifi::touch_action::Enum::PRESS
                } else {
                    Wifi::touch_action::Enum::POINTER_DOWN
                },
                self.points.len() - 1,
            ));
        }
        if changed && messages.is_empty() {
            messages.push(self.message(Wifi::touch_action::Enum::DRAG, 0));
        }
        messages
    }
    /// Decode one normalized Linux event; malformed slot/contact counts terminate the reader.
    fn feed(&mut self, e: Event) -> io::Result<Vec<Wifi::InputEventIndication>> {
        self.timestamp = self.timestamp.max(e.micros);
        if e.kind == 0 && e.code == 3 {
            self.dropped = true;
            return Ok(self.cancel());
        }
        if self.dropped {
            if e.kind == 0 && e.code == 0 {
                self.resync = true;
            }
            return Ok(Vec::new());
        }
        if e.kind == 1 && e.code == 330 {
            self.down = e.value != 0;
        }
        if e.kind == 3 {
            if self.info.protocol == 2 && e.code == 47 {
                if e.value < 0 || e.value >= self.info.slots {
                    return Err(io::Error::other("invalid evdev MT slot"));
                }
                self.slot = e.value as usize;
            } else {
                let p = if self.info.protocol == 1 {
                    &mut self.partial
                } else {
                    &mut self.slots[self.slot]
                };
                match e.code {
                    57 => p.id = (e.value >= 0).then_some(e.value),
                    53 => p.x = Some(e.value),
                    54 => p.y = Some(e.value),
                    0 if self.info.protocol == 0 => p.x = Some(e.value),
                    1 if self.info.protocol == 0 => p.y = Some(e.value),
                    _ => (),
                }
            }
        }
        if self.info.protocol == 1 && e.kind == 0 && e.code == 2 {
            if self.partial.id.is_some() {
                if self.frame.len() == SLOTS {
                    return Err(io::Error::other("too many evdev contacts"));
                }
                self.frame.push(self.partial);
            }
            self.partial = Raw::default();
        }
        if e.kind != 0 || e.code != 0 {
            return Ok(Vec::new());
        }
        let contacts = match self.info.protocol {
            0 if self.down => {
                self.slots[0].id = Some(0);
                vec![self.slots[0]]
            }
            0 => Vec::new(),
            1 => {
                let frame = std::mem::take(&mut self.frame);
                self.partial = Raw::default();
                if self.down { frame } else { Vec::new() }
            }
            _ => self
                .slots
                .iter()
                .filter(|p| p.id.is_some())
                .copied()
                .collect(),
        };
        Ok(self.report(contacts))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protobuf::Message;
    use Wifi::touch_action::Enum::*;
    fn info(protocol: i32) -> Info {
        Info {
            protocol,
            x_max: 1023,
            y_max: 599,
            slots: 4,
            ..Info::default()
        }
    }
    fn feed(d: &mut Decoder, kind: u16, code: u16, value: i32) -> Vec<Wifi::InputEventIndication> {
        d.feed(Event {
            kind,
            code,
            value,
            micros: 123456,
        })
        .unwrap()
    }
    fn type_a(d: &mut Decoder, contacts: &[(i32, i32, i32)]) -> Vec<Wifi::InputEventIndication> {
        feed(d, 1, 330, i32::from(!contacts.is_empty()));
        for &(id, x, y) in contacts {
            feed(d, 3, 57, id);
            feed(d, 3, 53, x);
            feed(d, 3, 54, y);
            feed(d, 0, 2, 0);
        }
        feed(d, 0, 0, 0)
    }
    fn action(m: &Wifi::InputEventIndication) -> Wifi::touch_action::Enum {
        m.touch_event.as_ref().unwrap().touch_action()
    }
    fn position(m: &Wifi::InputEventIndication, index: usize) -> (u32, u32, u32) {
        let p = &m.touch_event.as_ref().unwrap().touch_location[index];
        (p.x(), p.y(), p.pointer_id())
    }
    #[test]
    fn goodix_type_a_press_drag_release_and_letterbox_edges() {
        let mut d = Decoder::new(info(1), (1024, 600), true);
        let first = type_a(&mut d, &[(9, 12, 0)]);
        assert_eq!(first.len(), 1);
        assert_eq!(action(&first[0]), PRESS);
        // 1024x600 panel = visible area = TOUCH_SIZE: coordinates pass through.
        assert_eq!(position(&first[0], 0), (12, 0, 0));
        let moved = type_a(&mut d, &[(9, 1023, 599)]);
        assert_eq!(action(&moved[0]), DRAG);
        assert_eq!(position(&moved[0], 0), (1023, 599, 0));
        assert!(type_a(&mut d, &[(9, 1023, 599)]).is_empty());
        let released = type_a(&mut d, &[]);
        assert_eq!(action(&released[0]), RELEASE);
        assert_eq!(position(&released[0], 0), (1023, 599, 0));
        assert!(released[0].timestamp() > moved[0].timestamp());
        assert!(first[0].is_initialized());
    }
    #[test]
    fn stable_pointer_ids_reordered_type_a_reports_and_pointer_indices() {
        let mut d = Decoder::new(info(1), (1024, 600), true);
        type_a(&mut d, &[(200, 100, 100)]);
        let added = type_a(&mut d, &[(201, 200, 200), (200, 100, 100)]);
        assert_eq!(action(&added[0]), POINTER_DOWN);
        assert_eq!(added[0].touch_event.as_ref().unwrap().action_index(), 1);
        assert_eq!(position(&added[0], 0).2, 0);
        assert_eq!(position(&added[0], 1).2, 1);
        assert!(type_a(&mut d, &[(200, 100, 100), (201, 200, 200)]).is_empty());
        let removed = type_a(&mut d, &[(201, 200, 200)]);
        assert_eq!(action(&removed[0]), POINTER_UP);
        assert_eq!(removed[0].touch_event.as_ref().unwrap().action_index(), 0);
        assert_eq!(
            removed[0]
                .touch_event
                .as_ref()
                .unwrap()
                .touch_location
                .len(),
            2
        );
        let last = type_a(&mut d, &[]);
        assert_eq!(action(&last[0]), RELEASE);
        assert_eq!(position(&last[0], 0).2, 1);
    }
    #[test]
    fn type_b_tracks_slots_and_reuses_unchanged_axis_values() {
        let mut initial = info(2);
        initial.current_slot = 2;
        initial.xs[2] = 100;
        initial.ys[2] = 200;
        let mut d = Decoder::new(initial, (1024, 600), true);
        feed(&mut d, 3, 57, 222);
        let pressed = feed(&mut d, 0, 0, 0);
        assert_eq!(action(&pressed[0]), PRESS);
        feed(&mut d, 3, 53, 700);
        assert_eq!(action(&feed(&mut d, 0, 0, 0)[0]), DRAG);
        feed(&mut d, 3, 57, -1);
        assert_eq!(action(&feed(&mut d, 0, 0, 0)[0]), RELEASE);
        feed(&mut d, 3, 57, 333);
        assert_eq!(action(&feed(&mut d, 0, 0, 0)[0]), PRESS);
        assert!(
            d.feed(Event {
                kind: 3,
                code: 47,
                value: 16,
                micros: 0
            })
            .is_err()
        );
    }
    #[test]
    fn bars_ignore_new_gestures_but_clamp_active_drags() {
        // A display taller than 1024:600 has bars above and below the visible
        // area (800x800: visible 800x468 at top 166).
        let mut d = Decoder::new(info(1), (800, 800), true);
        assert!(type_a(&mut d, &[(1, 200, 0)]).is_empty());
        assert!(type_a(&mut d, &[(1, 200, 300)]).is_empty());
        assert!(type_a(&mut d, &[]).is_empty());
        type_a(&mut d, &[(1, 200, 300)]);
        let top = type_a(&mut d, &[(1, 200, 0)]);
        assert_eq!(position(&top[0], 0).1, 0);
        let right = type_a(&mut d, &[(1, 1023, 300)]);
        assert_eq!(position(&right[0], 0).0, 1023);
        type_a(&mut d, &[]);
        let visible = type_a(&mut d, &[(1, 0, 125)]);
        assert_eq!(position(&visible[0], 0), (0, 0, 0));
    }
    #[test]
    fn focus_loss_and_overflow_release_then_wait_for_held_fingers_to_lift() {
        let mut d = Decoder::new(info(1), (1024, 600), true);
        type_a(&mut d, &[(1, 100, 100), (2, 200, 200)]);
        let cancel = d.cancel();
        assert_eq!(cancel.len(), 2);
        assert_eq!(action(&cancel[0]), POINTER_UP);
        assert_eq!(action(&cancel[1]), RELEASE);
        let mut held = info(1);
        held.down = 1;
        d.reset(held.clone(), false);
        assert!(type_a(&mut d, &[(1, 100, 100)]).is_empty());
        d.reset(held, true);
        assert!(type_a(&mut d, &[(1, 200, 200)]).is_empty());
        assert!(type_a(&mut d, &[]).is_empty());
        assert_eq!(action(&type_a(&mut d, &[(1, 100, 100)])[0]), PRESS);
        let cancelled = feed(&mut d, 0, 3, 0);
        assert_eq!(action(&cancelled[0]), RELEASE);
        assert!(type_a(&mut d, &[(1, 300, 300)]).is_empty());
        assert!(d.resync);
        d.reset(info(1), true);
        assert_eq!(action(&type_a(&mut d, &[(1, 100, 100)])[0]), PRESS);
    }
    #[test]
    fn disabled_single_touch_and_nonzero_axis_minima() {
        let mut initial = info(0);
        initial.x_min = 100;
        initial.x_max = 1123;
        initial.y_min = 50;
        initial.y_max = 649;
        let mut d = Decoder::new(initial.clone(), (1024, 600), false);
        feed(&mut d, 3, 0, 112);
        feed(&mut d, 3, 1, 50);
        feed(&mut d, 1, 330, 1);
        assert!(feed(&mut d, 0, 0, 0).is_empty());
        d.reset(initial, true);
        feed(&mut d, 3, 0, 112);
        feed(&mut d, 3, 1, 50);
        feed(&mut d, 1, 330, 1);
        assert_eq!(position(&feed(&mut d, 0, 0, 0)[0], 0), (12, 0, 0));
        feed(&mut d, 1, 330, 0);
        assert_eq!(action(&feed(&mut d, 0, 0, 0)[0]), RELEASE);
    }
    #[test]
    fn corner_hold_needs_two_seconds_inside_the_top_left_zone() {
        let (i, display) = (info(1), (1024, 600));
        let event = |kind, code, value| Event { kind, code, value, micros: 0 };
        let mut c = CornerHold::default();
        for e in [event(3, 53, 50), event(3, 54, 99), event(1, 330, 1)] {
            c.feed(&e);
        }
        assert!(!c.held(1000, &i, display));
        assert!(!c.held(2999, &i, display));
        assert!(c.held(3000, &i, display));
        // Leaving the zone or lifting restarts the hold.
        c.feed(&event(3, 53, 100));
        assert!(!c.held(3100, &i, display));
        c.feed(&event(3, 53, 10));
        assert!(!c.held(4000, &i, display));
        c.feed(&event(1, 330, 0));
        assert!(!c.held(6100, &i, display));
        c.feed(&event(1, 330, 1));
        assert!(!c.held(6200, &i, display) && c.held(8200, &i, display));
    }
}
