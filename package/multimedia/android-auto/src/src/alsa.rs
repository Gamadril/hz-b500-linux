//! Bounded ALSA PCM workers for the Cedar head unit's three output streams.
use crate::AudioChannelType;
use std::{
    ffi::{CString, c_char, c_void},
    io,
    ptr::NonNull,
};
use tokio::sync::{mpsc, oneshot};

/// ALSA device and optional private configuration, shared by all audio channels.
#[derive(Clone, Debug)]
pub struct AudioConfig {
    /// ALSA PCM name; the packaged default mixes the three streams together.
    pub device: String,
    /// ALSA configuration filename, or empty to use the system configuration.
    pub config: String,
}
impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            device: "android_auto".into(),
            config: "/usr/share/android-auto/alsa.conf".into(),
        }
    }
}

unsafe extern "C" {
    fn aa_alsa_open(
        device: *const c_char,
        config: *const c_char,
        rate: u32,
        channels: u32,
        error: *mut c_char,
        size: usize,
    ) -> *mut c_void;
    fn aa_alsa_start(sink: *mut c_void, error: *mut c_char, size: usize) -> i32;
    fn aa_alsa_write(
        sink: *mut c_void,
        data: *const u8,
        len: usize,
        error: *mut c_char,
        size: usize,
    ) -> i32;
    fn aa_alsa_stop(sink: *mut c_void, error: *mut c_char, size: usize) -> i32;
    fn aa_alsa_close(sink: *mut c_void);
}

/// Native PCM owner, created and used exclusively on one worker thread.
struct Native(NonNull<c_void>);
impl Native {
    /// Open the fixed PCM format advertised by the selected protocol channel.
    fn open(config: AudioConfig, kind: AudioChannelType) -> io::Result<Self> {
        let device = CString::new(config.device)?;
        let path = CString::new(config.config)?;
        let (rate, channels) = match kind {
            AudioChannelType::Media => (48000, 2),
            AudioChannelType::System | AudioChannelType::Speech => (16000, 1),
        };
        let mut error = [0; 256];
        // SAFETY: All strings and buffers live through the call; Native owns the result.
        let p = unsafe {
            aa_alsa_open(
                device.as_ptr(),
                path.as_ptr(),
                rate,
                channels,
                error.as_mut_ptr().cast(),
                error.len(),
            )
        };
        NonNull::new(p)
            .map(Self)
            .ok_or_else(|| native_error(&error))
    }
    /// Start a fresh stream or drain the current stream without closing the channel.
    fn control(&mut self, start: bool) -> io::Result<()> {
        let mut error = [0; 256];
        // SAFETY: The live PCM belongs exclusively to this worker.
        let rc = unsafe {
            if start {
                aa_alsa_start(self.0.as_ptr(), error.as_mut_ptr().cast(), error.len())
            } else {
                aa_alsa_stop(self.0.as_ptr(), error.as_mut_ptr().cast(), error.len())
            }
        };
        if rc == 0 {
            Ok(())
        } else {
            Err(native_error(&error))
        }
    }
    /// Submit aligned S16_LE bytes, with a bounded native progress deadline.
    fn write(&mut self, data: &[u8]) -> io::Result<()> {
        let mut error = [0; 256];
        // SAFETY: Neither the PCM nor the borrowed packet escapes this call.
        let rc = unsafe {
            aa_alsa_write(
                self.0.as_ptr(),
                data.as_ptr(),
                data.len(),
                error.as_mut_ptr().cast(),
                error.len(),
            )
        };
        if rc == 0 {
            Ok(())
        } else {
            Err(native_error(&error))
        }
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        // SAFETY: Drop consumes the sole native PCM owner, even after a failed write.
        unsafe { aa_alsa_close(self.0.as_ptr()) };
    }
}

/// Convert a bounded, NUL-terminated native error buffer.
fn native_error(bytes: &[u8]) -> io::Error {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    io::Error::other(String::from_utf8_lossy(&bytes[..end]).into_owned())
}

/// One bounded command for a worker; completion controls protocol backpressure.
enum Command {
    /// Start a new session or drain its end.
    Control(bool, oneshot::Sender<io::Result<()>>),
    /// Play one packet, acknowledging only after ALSA accepts all bytes.
    Data(Vec<u8>, oneshot::Sender<io::Result<()>>),
    /// Drop pending audio and release the PCM before replying.
    Close(oneshot::Sender<io::Result<()>>),
}

/// One independently controlled media, speech or system playback stream.
pub struct AlsaSink {
    /// Bounded queue; ALSA calls never run on Tokio's executor.
    tx: mpsc::Sender<Command>,
}
impl AlsaSink {
    /// Check and open an ALSA output for the channel's advertised PCM format.
    pub async fn open(config: AudioConfig, kind: AudioChannelType) -> io::Result<Self> {
        let (tx, mut rx) = mpsc::channel(1);
        let (ready, wait) = oneshot::channel();
        std::thread::Builder::new()
            .name(format!("aa-audio-{kind:?}"))
            .stack_size(256 * 1024)
            .spawn(move || {
                let mut native = match Native::open(config, kind) {
                    Ok(n) => {
                        let _ = ready.send(Ok(()));
                        n
                    }
                    Err(e) => {
                        let _ = ready.send(Err(e));
                        return;
                    }
                };
                while let Some(command) = rx.blocking_recv() {
                    let (result, reply) = match command {
                        Command::Control(start, reply) => (native.control(start), reply),
                        Command::Data(data, reply) => (native.write(&data), reply),
                        Command::Close(reply) => {
                            drop(native);
                            let _ = reply.send(Ok(()));
                            return;
                        }
                    };
                    let failed = result.is_err();
                    let _ = reply.send(result);
                    if failed {
                        break;
                    }
                }
            })?;
        wait.await
            .map_err(|_| io::Error::other("ALSA worker exited during setup"))??;
        Ok(Self { tx })
    }
    /// Wait for a native operation or report a stopped worker.
    async fn request(
        &self,
        command: Command,
        reply: oneshot::Receiver<io::Result<()>>,
    ) -> io::Result<()> {
        self.tx
            .send(command)
            .await
            .map_err(|_| io::Error::other("ALSA worker stopped"))?;
        reply
            .await
            .map_err(|_| io::Error::other("ALSA worker dropped its reply"))?
    }
    /// Reset a stream's PCM buffer for a new protocol session.
    pub async fn start(&self) -> io::Result<()> {
        let (tx, rx) = oneshot::channel();
        self.request(Command::Control(true, tx), rx).await
    }
    /// Accept at most 64 KiB of aligned PCM without silently discarding bytes.
    pub async fn submit(&self, data: Vec<u8>) -> io::Result<()> {
        if data.is_empty() || data.len() > 65536 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "empty or oversized PCM packet",
            ));
        }
        let (tx, rx) = oneshot::channel();
        self.request(Command::Data(data, tx), rx).await
    }
    /// Finish queued PCM at stream stop, with a bounded drain deadline.
    pub async fn stop(&self) -> io::Result<()> {
        let (tx, rx) = oneshot::channel();
        self.request(Command::Control(false, tx), rx).await
    }
    /// Discard pending audio and release this channel on disconnect/shutdown.
    pub async fn close(self) -> io::Result<()> {
        let (tx, rx) = oneshot::channel();
        self.request(Command::Close(tx), rx).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn null_pcm_lifecycle_and_formats() {
        for kind in [
            AudioChannelType::Media,
            AudioChannelType::Speech,
            AudioChannelType::System,
        ] {
            let sink = AlsaSink::open(
                AudioConfig {
                    device: "null".into(),
                    config: String::new(),
                },
                kind,
            )
            .await
            .unwrap();
            sink.start().await.unwrap();
            sink.submit(vec![0; 1920]).await.unwrap();
            sink.stop().await.unwrap();
            sink.start().await.unwrap();
            sink.submit(vec![0; 1920]).await.unwrap();
            sink.close().await.unwrap();
        }
    }
}
