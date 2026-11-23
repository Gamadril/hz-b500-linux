//! CedarC H.264 sink for the Allwinner Tina Linux BSP.
//!
//! The dedicated worker owns every vendor object and display fence. No vendor
//! pointer or ABI structure crosses into async tasks. See `docs/cedar.md`.
use std::{ffi::c_char, io, ptr::NonNull};
use tokio::sync::{mpsc, oneshot};

/// Display selection and bounded decoder resources for the 720p/30 stream (margins cropped).
#[derive(Clone, Debug)]
pub struct CedarConfig {
    /// Display index (LCD is normally zero).
    pub screen: u32,
    /// Dedicated, initially disabled video channel.
    pub channel: u32,
    /// Layer within the video channel.
    pub layer: u32,
    /// Maximum compressed packet and Cedar bitstream-buffer capacity.
    pub bitstream_bytes: u32,
    /// Deadline for decoder progress and display retirement.
    pub timeout_ms: u32,
}
impl Default for CedarConfig {
    fn default() -> Self {
        Self {
            screen: 0,
            channel: 0,
            layer: 0,
            bitstream_bytes: 1024 * 1024,
            timeout_ms: 500,
        }
    }
}

#[repr(C)]
struct NativeConfig {
    screen: u32,
    channel: u32,
    layer: u32,
    width: u32,
    height: u32,
    vbv_bytes: u32,
    timeout_ms: u32,
    margin_width: u32,
    margin_height: u32,
}
unsafe extern "C" {
    fn aa_cedar_display_size(
        screen: u32,
        width: *mut u32,
        height: *mut u32,
        error: *mut c_char,
        size: usize,
    ) -> i32;
    fn aa_cedar_open(
        config: *const NativeConfig,
        error: *mut c_char,
        size: usize,
    ) -> *mut std::ffi::c_void;
    fn aa_cedar_push(
        sink: *mut std::ffi::c_void,
        data: *const u8,
        len: usize,
        pts: i64,
        error: *mut c_char,
        size: usize,
    ) -> i32;
    fn aa_cedar_close(sink: *mut std::ffi::c_void, error: *mut c_char, size: usize) -> i32;
    fn aa_cedar_set_visible(
        sink: *mut std::ffi::c_void,
        visible: i32,
        error: *mut c_char,
        size: usize,
    ) -> i32;
}

/// Query the selected display without claiming a layer, for input coordinate mapping.
pub fn display_size(screen: u32) -> io::Result<(u32, u32)> {
    let (mut width, mut height) = (0, 0);
    let mut error = [0; 256];
    // SAFETY: All output buffers are valid for this synchronous ioctl-only call.
    let rc = unsafe {
        aa_cedar_display_size(
            screen,
            &mut width,
            &mut height,
            error.as_mut_ptr().cast(),
            error.len(),
        )
    };
    if rc == 0 {
        Ok((width, height))
    } else {
        Err(native_error(&error))
    }
}

fn native_error(error: &[u8]) -> io::Error {
    let end = error.iter().position(|b| *b == 0).unwrap_or(error.len());
    io::Error::other(String::from_utf8_lossy(&error[..end]).into_owned())
}

/// Opaque native owner; constructed, used and destroyed on the worker thread.
struct Native(NonNull<std::ffi::c_void>);
impl Native {
    fn open(config: &CedarConfig) -> io::Result<Self> {
        let config = NativeConfig {
            screen: config.screen,
            channel: config.channel,
            layer: config.layer,
            width: crate::touch::VIDEO_SIZE.0.into(),
            height: crate::touch::VIDEO_SIZE.1.into(),
            vbv_bytes: config.bitstream_bytes,
            timeout_ms: config.timeout_ms,
            margin_width: crate::touch::VIDEO_MARGINS.0.into(),
            margin_height: crate::touch::VIDEO_MARGINS.1.into(),
        };
        let mut error = [0u8; 256];
        // SAFETY: Both buffers live for the call. The returned object stays on this thread.
        let p = unsafe { aa_cedar_open(&config, error.as_mut_ptr().cast(), error.len()) };
        NonNull::new(p)
            .map(Self)
            .ok_or_else(|| native_error(&error))
    }
    fn push(&mut self, data: &[u8], timestamp: Option<u64>) -> io::Result<()> {
        let pts = timestamp
            .map(i64::try_from)
            .transpose()
            .map_err(|_| {
                io::Error::new(io::ErrorKind::InvalidInput, "video timestamp exceeds i64")
            })?
            .unwrap_or(-1);
        let mut error = [0u8; 256];
        // SAFETY: Native owns a live context; the C function copies the borrowed input.
        let r = unsafe {
            aa_cedar_push(
                self.0.as_ptr(),
                data.as_ptr(),
                data.len(),
                pts,
                error.as_mut_ptr().cast(),
                error.len(),
            )
        };
        if r == 0 {
            Ok(())
        } else {
            Err(native_error(&error))
        }
    }
    fn close(self) -> io::Result<()> {
        let mut error = [0u8; 256];
        // SAFETY: Consumes the sole owner. On error C deliberately retains scanout buffers.
        let r = unsafe { aa_cedar_close(self.0.as_ptr(), error.as_mut_ptr().cast(), error.len()) };
        if r == 0 {
            Ok(())
        } else {
            Err(native_error(&error))
        }
    }
    fn set_visible(&mut self, visible: bool) -> io::Result<()> {
        let mut error = [0u8; 256];
        // SAFETY: The live context belongs to this worker and is never accessed concurrently.
        let r = unsafe {
            aa_cedar_set_visible(
                self.0.as_ptr(),
                i32::from(visible),
                error.as_mut_ptr().cast(),
                error.len(),
            )
        };
        if r == 0 {
            Ok(())
        } else {
            Err(native_error(&error))
        }
    }
}

enum Command {
    /// Fire-and-forget: a failed push ends the worker, so the next submit fails.
    Frame(Vec<u8>, Option<u64>),
    Visible(bool, oneshot::Sender<io::Result<()>>),
    Stop(oneshot::Sender<io::Result<()>>),
}

/// Hardware decoder and direct YUV display sink, with bounded backpressure.
///
/// One worker serializes CedarC and display ioctls. `submit` returns once its
/// compressed packet is queued (at most 8), so the single protocol loop never
/// waits for decoding while audio frames queue behind video; a full queue
/// still bounds memory and slows the phone. Call `close` before process exit.
pub struct CedarSink {
    tx: mpsc::Sender<Command>,
    max_packet: usize,
}
impl CedarSink {
    /// Load the matching SDK libraries and claim an unused display layer.
    pub async fn open(config: CedarConfig) -> io::Result<Self> {
        let max_packet = config.bitstream_bytes as usize;
        // 8 compressed packets absorb bursts (map scrolling) without stalling
        // the protocol loop; a full queue still applies backpressure.
        let (tx, mut rx) = mpsc::channel(8);
        let (ready_tx, ready_rx) = oneshot::channel();
        std::thread::Builder::new()
            .name("cedar-video".into())
            .stack_size(512 * 1024)
            .spawn(move || {
                let mut native = match Native::open(&config) {
                    Ok(n) => {
                        let _ = ready_tx.send(Ok(()));
                        n
                    }
                    Err(e) => {
                        let _ = ready_tx.send(Err(e));
                        return;
                    }
                };
                while let Some(command) = rx.blocking_recv() {
                    match command {
                        Command::Frame(data, timestamp) => {
                            if let Err(e) = native.push(&data, timestamp) {
                                log::error!("Cedar decode failed: {e}");
                                break;
                            }
                        }
                        Command::Stop(reply) => {
                            let _ = reply.send(native.close());
                            return;
                        }
                        Command::Visible(visible, reply) => {
                            let result = native.set_visible(visible);
                            let failed = result.is_err();
                            let _ = reply.send(result);
                            if failed {
                                break;
                            }
                        }
                    }
                }
                if let Err(e) = native.close() {
                    log::error!("Cedar shutdown failed; native buffers retained: {e}");
                }
            })?;
        ready_rx
            .await
            .map_err(|_| io::Error::other("Cedar worker exited during setup"))??;
        Ok(Self { tx, max_packet })
    }

    /// Submit elementary H.264 bytes (including SPS/PPS); timestamps are microseconds.
    ///
    /// Input may span protocol packets. No Melis/Robin length prefix is added.
    /// On a decoder/display error the worker stops and the receiver should end
    /// the AA session rather than acknowledge and discard subsequent frames.
    pub async fn submit(&self, data: Vec<u8>, timestamp: Option<u64>) -> io::Result<()> {
        if data.is_empty() || data.len() > self.max_packet {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "empty or oversized H.264 packet",
            ));
        }
        self.tx
            .send(Command::Frame(data, timestamp))
            .await
            .map_err(|_| io::Error::new(io::ErrorKind::BrokenPipe, "Cedar worker stopped"))
    }

    /// Disable the video layer, wait for release fences and free the decoder.
    pub async fn close(self) -> io::Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx
            .send(Command::Stop(tx))
            .await
            .map_err(|_| io::Error::new(io::ErrorKind::BrokenPipe, "Cedar worker stopped"))?;
        rx.await
            .map_err(|_| io::Error::other("Cedar worker exited during shutdown"))?
    }

    /// Hide or show the video layer while retaining decoder state.
    ///
    /// Hiding waits for display retirement; hidden frames continue decoding and
    /// are immediately returned. Showing presents the next decoded picture.
    pub async fn set_visible(&self, visible: bool) -> io::Result<()> {
        let (tx, rx) = oneshot::channel();
        self.tx
            .send(Command::Visible(visible, tx))
            .await
            .map_err(|_| io::Error::new(io::ErrorKind::BrokenPipe, "Cedar worker stopped"))?;
        rx.await
            .map_err(|_| io::Error::other("Cedar worker exited during focus change"))?
    }
}
