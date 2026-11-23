//! USB or stock AB5301B wireless AA with Cedar video and ALSA playback.
use android_auto::{
    AndroidAutoAudioInputTrait, AndroidAutoAudioOutputTrait, AndroidAutoConfiguration,
    AndroidAutoInputChannelTrait, AndroidAutoMainTrait, AndroidAutoSensorTrait,
    AndroidAutoVideoChannelTrait, AndroidAutoWiredTrait, AudioChannelType, HeadUnitInfo,
    InputConfiguration, SendableAndroidAutoMessage, SensorInformation, VideoConfiguration, Wifi,
    alsa::{AlsaSink, AudioConfig},
    cedar::{CedarConfig, CedarSink},
    media_keys,
    touch::{TOUCH_SIZE, TouchDevice, VIDEO_MARGINS},
};
use std::{
    collections::HashSet,
    io,
    sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    },
};
use tokio::sync::{Mutex, mpsc, watch};

struct Logger;
impl log::Log for Logger {
    fn enabled(&self, m: &log::Metadata<'_>) -> bool {
        m.level() <= log::Level::Info
    }
    fn log(&self, r: &log::Record<'_>) {
        if self.enabled(r.metadata()) {
            eprintln!("{} {}", r.level(), r.args());
        }
    }
    fn flush(&self) {}
}
static LOGGER: Logger = Logger;

#[derive(Clone, Copy, PartialEq)]
enum Transport {
    Usb,
    Wireless,
    Auto,
}

#[derive(Clone)]
struct Unit(Arc<Inner>);
struct Inner {
    transport: Transport,
    #[cfg(feature = "ab5301b")]
    wireless: Option<android_auto::ab5301b::Configuration>,
    config: CedarConfig,
    sink: Mutex<Option<CedarSink>>,
    audio_config: AudioConfig,
    audio: [Mutex<Option<AlsaSink>>; 3],
    video: VideoConfiguration,
    input: InputConfiguration,
    sensors: SensorInformation,
    fatal: mpsc::Sender<String>,
    touch: Option<Arc<TouchDevice>>,
    touch_task: Mutex<Option<tokio::task::JoinHandle<()>>>,
    media_task: Mutex<Option<tokio::task::JoinHandle<()>>>,
    touch_enabled: watch::Sender<bool>,
    media_enabled: watch::Sender<bool>,
    input_open: AtomicBool,
    video_focused: AtomicBool,
}
impl Unit {
    fn update_touch_focus(&self) {
        let media_enabled = self.0.input_open.load(Ordering::Relaxed);
        self.0.media_enabled.send_if_modified(|current| {
            if *current == media_enabled {
                false
            } else {
                *current = media_enabled;
                true
            }
        });
        let enabled = self.0.input_open.load(Ordering::Relaxed)
            && self.0.video_focused.load(Ordering::Relaxed);
        self.0.touch_enabled.send_if_modified(|current| {
            if *current == enabled {
                false
            } else {
                *current = enabled;
                true
            }
        });
    }
    fn fault(&self, e: io::Error) {
        let _ = self.0.fatal.try_send(e.to_string());
    }
    async fn close_video(&self) -> io::Result<()> {
        self.0.video_focused.store(false, Ordering::Relaxed);
        self.update_touch_focus();
        if let Some(sink) = self.0.sink.lock().await.take() {
            sink.close().await?;
        }
        Ok(())
    }
    fn output(&self, kind: &AudioChannelType) -> &Mutex<Option<AlsaSink>> {
        &self.0.audio[match kind {
            AudioChannelType::Media => 0,
            AudioChannelType::System => 1,
            AudioChannelType::Speech => 2,
        }]
    }
    async fn close(&self) -> io::Result<()> {
        self.0.input_open.store(false, Ordering::Relaxed);
        self.update_touch_focus();
        if let Some(task) = self.0.touch_task.lock().await.take() {
            task.abort();
            let _ = task.await;
        }
        if let Some(task) = self.0.media_task.lock().await.take() {
            task.abort();
            let _ = task.await;
        }
        let mut result = self.close_video().await;
        for channel in &self.0.audio {
            if let Some(sink) = channel.lock().await.take() {
                if let Err(error) = sink.close().await {
                    if result.is_ok() {
                        result = Err(error);
                    }
                }
            }
        }
        result
    }
}
#[async_trait::async_trait]
impl AndroidAutoVideoChannelTrait for Unit {
    async fn setup_video(&self) -> Result<(), ()> {
        let mut sink = self.0.sink.lock().await;
        if sink.is_none() {
            match CedarSink::open(self.0.config.clone()).await {
                Ok(s) => *sink = Some(s),
                Err(e) => {
                    self.fault(e);
                    return Err(());
                }
            }
        }
        self.0.video_focused.store(true, Ordering::Relaxed);
        self.update_touch_focus();
        Ok(())
    }
    async fn receive_video(&self, data: Vec<u8>, timestamp: Option<u64>) {
        let sink = self.0.sink.lock().await;
        let result = if let Some(sink) = sink.as_ref() {
            sink.submit(data, timestamp).await
        } else {
            Err(io::Error::other(
                "video arrived without a configured Cedar sink",
            ))
        };
        if let Err(e) = result {
            self.fault(e);
        }
    }
    async fn teardown_video(&self) {
        if let Err(e) = self.close_video().await {
            self.fault(e);
        }
    }
    async fn wait_for_focus(&self) {}
    async fn set_focus(&self, focused: bool) {
        self.0.video_focused.store(focused, Ordering::Relaxed);
        self.update_touch_focus();
        if let Some(s) = self.0.sink.lock().await.as_ref() {
            if let Err(e) = s.set_visible(focused).await {
                self.fault(e);
            }
        }
    }
    fn retrieve_video_configuration(&self) -> &VideoConfiguration {
        &self.0.video
    }
}
#[async_trait::async_trait]
impl AndroidAutoAudioOutputTrait for Unit {
    async fn open_output_channel(&self, kind: AudioChannelType) -> Result<(), ()> {
        let mut output = self.output(&kind).lock().await;
        if output.is_none() {
            match AlsaSink::open(self.0.audio_config.clone(), kind).await {
                Ok(sink) => *output = Some(sink),
                Err(error) => {
                    log::error!("Cannot open audio output: {error}");
                    return Err(());
                }
            }
        }
        Ok(())
    }
    async fn close_output_channel(&self, kind: AudioChannelType) -> Result<(), ()> {
        if let Some(sink) = self.output(&kind).lock().await.take() {
            sink.close().await.map_err(|e| self.fault(e))?;
        }
        Ok(())
    }
    async fn receive_output_audio(&self, kind: AudioChannelType, data: Vec<u8>) {
        let _ = self.write_output_audio(kind, data).await;
    }
    async fn write_output_audio(&self, kind: AudioChannelType, data: Vec<u8>) -> Result<(), ()> {
        let output = self.output(&kind).lock().await;
        let result = match output.as_ref() {
            Some(sink) => sink.submit(data).await,
            None => Err(io::Error::other(
                "PCM arrived without an open audio channel",
            )),
        };
        result.map_err(|e| self.fault(e))
    }
    async fn start_output_audio(&self, kind: AudioChannelType) {
        let output = self.output(&kind).lock().await;
        match output.as_ref() {
            Some(sink) => {
                if let Err(e) = sink.start().await {
                    self.fault(e);
                }
            }
            None => self.fault(io::Error::other("Audio start without an open channel")),
        }
    }
    async fn stop_output_audio(&self, kind: AudioChannelType) {
        if let Some(sink) = self.output(&kind).lock().await.as_ref() {
            if let Err(e) = sink.stop().await {
                self.fault(e);
            }
        }
    }
}
#[async_trait::async_trait]
impl AndroidAutoAudioInputTrait for Unit {
    async fn open_input_channel(&self) -> Result<(), ()> {
        Err(())
    }
    async fn close_input_channel(&self) -> Result<(), ()> {
        Ok(())
    }
    async fn start_input_audio(&self) {}
    async fn stop_input_audio(&self) {}
    async fn audio_input_ack(&self, _: u8, _: Wifi::AVMediaAckIndication) {}
}
#[async_trait::async_trait]
impl AndroidAutoInputChannelTrait for Unit {
    async fn input_channel_opened(&self) {
        self.0.input_open.store(true, Ordering::Relaxed);
        self.update_touch_focus();
    }
    async fn binding_request(&self, _: u32) -> Result<(), ()> {
        Ok(())
    }
    fn retrieve_input_configuration(&self) -> &InputConfiguration {
        &self.0.input
    }
}
#[async_trait::async_trait]
impl AndroidAutoSensorTrait for Unit {
    fn get_supported_sensors(&self) -> &SensorInformation {
        &self.0.sensors
    }
    async fn start_sensor(&self, sensor: Wifi::sensor_type::Enum) -> Result<(), ()> {
        (sensor == Wifi::sensor_type::Enum::NIGHT_DATA).then_some(()).ok_or(())
    }
    /// Vehicle service night state (headlights on, like the HMI LCD dimming).
    fn night_mode(&self) -> Option<bool> {
        match std::fs::read_to_string("/var/run/vehicle/night").ok()?.trim() {
            "1" => Some(true),
            "0" => Some(false),
            _ => None,
        }
    }
}
#[async_trait::async_trait]
impl AndroidAutoWiredTrait for Unit {}
#[async_trait::async_trait]
impl AndroidAutoMainTrait for Unit {
    async fn connect(&self) {
        log::info!("Transport connected; negotiating Android Auto");
    }
    async fn disconnect(&self) {
        if let Err(e) = self.close().await {
            self.fault(e);
        }
    }
    async fn get_receiver(&self) -> Option<mpsc::Receiver<SendableAndroidAutoMessage>> {
        let device = self.0.touch.clone();
        self.0.input_open.store(false, Ordering::Relaxed);
        self.update_touch_focus();
        let mut task = self.0.touch_task.lock().await;
        if let Some(old) = task.take() {
            old.abort();
            let _ = old.await;
        }
        let (sender, receiver) = mpsc::channel(16);
        let enabled = self.0.touch_enabled.subscribe();
        let fatal = self.0.fatal.clone();
        if let Some(device) = device {
            let touch_sender = sender.clone();
            *task = Some(tokio::spawn(async move {
                if let Err(error) = device.run(enabled, touch_sender).await {
                    let _ = fatal.try_send(format!("Touch input failed: {error}"));
                }
            }));
        }
        let mut media_task = self.0.media_task.lock().await;
        if let Some(old) = media_task.take() {
            old.abort();
            let _ = old.await;
        }
        let media_enabled = self.0.media_enabled.subscribe();
        let media_fatal = self.0.fatal.clone();
        *media_task = Some(tokio::spawn(async move {
            if let Err(error) = media_keys::run(media_enabled, sender).await {
                let _ = media_fatal.try_send(format!("Media input failed: {error}"));
            }
        }));
        Some(receiver)
    }
    fn supports_wired(&self) -> Option<Arc<dyn AndroidAutoWiredTrait>> {
        (self.0.transport != Transport::Wireless)
            .then(|| Arc::new(self.clone()) as Arc<dyn AndroidAutoWiredTrait>)
    }
    #[cfg(feature = "ab5301b")]
    fn supports_ab5301b(&self) -> Option<android_auto::ab5301b::Configuration> {
        self.0.wireless.clone()
    }
}

fn options() -> io::Result<(
    CedarConfig,
    AudioConfig,
    String,
    Option<String>,
    Transport,
    Option<String>,
    bool,
)> {
    let mut config = CedarConfig::default();
    let mut audio = AudioConfig::default();
    let mut file = None;
    let mut touch = "auto".to_string();
    let mut transport = Transport::Usb;
    let mut wireless = None;
    let mut once = false;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        if arg == "--help" {
            println!(
                "cedar-headunit [--screen N] [--channel N] [--layer N] [--file elementary.h264]\n\
                [--audio-device PCM] [--audio-config FILE] [--touch-device auto|none|/dev/input/eventN]\n\
                [--transport usb|wireless|auto] [--wireless-config FILE] [--once]\n\
                720p/30 video (1024x600 shown 1:1), ALSA playback, evdev touchscreen and vehicle media keys; no microphone.\n\
                USB is the default. Wireless requires an active WPA2 AP and the AB5301B companion service.\n\
                Stop other display clients first; audio uses the existing board mixer/companion setup.\n\
                Use an empty --audio-config to load the system ALSA configuration.\n\
                --file exercises hardware decoding/display without a phone (runs unpaced).\n\
                --once (HMI launch) returns after one phone session, or when the top-left 100 px\n\
                corner is held for 2 s, also while still waiting for the phone."
            );
            std::process::exit(0);
        }
        if arg == "--once" {
            once = true;
            continue;
        }
        let value = args
            .next()
            .ok_or_else(|| io::Error::other(format!("missing value for {arg}")))?;
        if arg == "--file" {
            file = Some(value);
            continue;
        }
        if arg == "--audio-device" {
            audio.device = value;
            continue;
        }
        if arg == "--audio-config" {
            audio.config = value;
            continue;
        }
        if arg == "--touch-device" {
            touch = value;
            continue;
        }
        if arg == "--transport" {
            transport = match value.as_str() {
                "usb" => Transport::Usb,
                "wireless" => Transport::Wireless,
                "auto" => Transport::Auto,
                _ => return Err(io::Error::other("expected transport usb, wireless or auto")),
            };
            continue;
        }
        if arg == "--wireless-config" {
            wireless = Some(value);
            continue;
        }
        let n = value
            .parse::<u32>()
            .map_err(|_| io::Error::other("expected an unsigned integer"))?;
        match arg.as_str() {
            "--screen" => config.screen = n,
            "--channel" => config.channel = n,
            "--layer" => config.layer = n,
            _ => return Err(io::Error::other(format!("unknown argument {arg}"))),
        }
    }
    if file.is_none() && (transport != Transport::Usb) != wireless.is_some() {
        return Err(io::Error::other(
            "wireless/auto transport requires --wireless-config; USB transport does not use it",
        ));
    }
    Ok((config, audio, touch, file, transport, wireless, once))
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    log::set_logger(&LOGGER).map_err(|e| io::Error::other(e.to_string()))?;
    log::set_max_level(log::LevelFilter::Info);
    let (config, audio_config, touch_selector, file, transport, wireless_path, once) = options()?;
    if let Some(path) = file {
        use tokio::io::AsyncReadExt;
        let mut file = tokio::fs::File::open(path).await?;
        let sink = CedarSink::open(config).await?;
        let playback = async {
            loop {
                let mut bytes = vec![0; 64 * 1024];
                let n = file.read(&mut bytes).await?;
                if n == 0 {
                    break;
                }
                bytes.truncate(n);
                sink.submit(bytes, None).await?;
            }
            Ok::<(), io::Error>(())
        };
        let result = tokio::select! { r = playback => r, _ = tokio::signal::ctrl_c() => Ok(()) };
        sink.close().await?;
        result?;
        return Ok(());
    }
    #[cfg(feature = "ab5301b")]
    let wireless = wireless_path
        .map(android_auto::ab5301b::Configuration::load)
        .transpose()?;
    #[cfg(not(feature = "ab5301b"))]
    if wireless_path.is_some() {
        return Err(io::Error::other("binary was built without the ab5301b feature").into());
    }
    let touch = if touch_selector == "none" {
        None
    } else {
        match TouchDevice::open(&touch_selector, config.screen) {
            Ok(device) => {
                log::info!("Touchscreen: {} ({})", device.path(), device.name());
                Some(Arc::new(device))
            }
            Err(error) if touch_selector == "auto" => {
                log::warn!("Touchscreen unavailable: {error}; continuing without touch");
                None
            }
            Err(error) => return Err(error.into()),
        }
    };
    let touchscreen = touch.as_ref().map(|_| TOUCH_SIZE);
    let (touch_enabled, _) = watch::channel(false);
    let (media_enabled, _) = watch::channel(false);
    let (fatal, mut faults) = mpsc::channel(1);
    let unit = Unit(Arc::new(Inner {
        transport,
        #[cfg(feature = "ab5301b")]
        wireless,
        config,
        sink: Mutex::new(None),
        audio_config,
        audio: std::array::from_fn(|_| Mutex::new(None)),
        fatal,
        touch,
        touch_task: Mutex::new(None),
        media_task: Mutex::new(None),
        touch_enabled,
        media_enabled,
        input_open: AtomicBool::new(false),
        video_focused: AtomicBool::new(false),
        // Stock BZ500 profile: 720p with 256x120 margins, so the phone renders
        // into the panel's 1024x600 shown 1:1; DPI 160.
        video: VideoConfiguration {
            resolution: Wifi::video_resolution::Enum::_720p,
            fps: Wifi::video_fps::Enum::_30,
            dpi: 160,
            margins: VIDEO_MARGINS,
        },
        input: InputConfiguration {
            keycodes: media_keys::KEYCODES.to_vec(),
            touchscreen,
        },
        sensors: SensorInformation {
            // Night mode follows the headlights via /var/run/vehicle/night.
            sensors: HashSet::from([Wifi::sensor_type::Enum::NIGHT_DATA]),
        },
    }));
    let configuration = AndroidAutoConfiguration {
        unit: HeadUnitInfo {
            name: "HZ-B500".into(),
            car_model: "HZ-B500".into(),
            car_year: "2026".into(),
            car_serial: "development".into(),
            left_hand: true,
            head_manufacturer: "Development".into(),
            head_model: "F133".into(),
            sw_build: "cedar".into(),
            sw_version: "1".into(),
            native_media: false,
            hide_clock: None,
        },
        custom_certificate: None,
    };
    let setup = android_auto::setup();
    // --once: the HMI waits for this process; a held top-left corner returns
    // to it at any time. Own descriptor, so session touch input is unaffected.
    let corner = match (once, touch_selector.as_str()) {
        (false, _) | (_, "none") => None,
        _ => TouchDevice::open(&touch_selector, unit.0.config.screen)
            .map_err(|error| log::warn!("Corner exit unavailable: {error}"))
            .ok(),
    };
    let mut corner_exit = Box::pin(async {
        match &corner {
            Some(device) => device.hold_corner().await,
            None => std::future::pending().await,
        }
    });
    loop {
        let mut tasks = tokio::task::JoinSet::new();
        let (result, stopped) = {
            let run = Box::new(unit.clone()).run(configuration.clone(), &mut tasks, &setup);
            tokio::pin!(run);
            let (result, stopped, quit) = tokio::select! {
                r = &mut run => (r.map_err(io::Error::other), false, false),
                Some(error) = faults.recv() => (Err(io::Error::other(error)), true, false),
                _ = tokio::signal::ctrl_c() => (Ok(()), true, true),
                r = &mut corner_exit => {
                    log::info!("Corner held; returning to the HMI");
                    (r, true, true)
                }
            };
            if quit {
                // A user quit: let the phone end Android Auto (ShutdownRequest/
                // Response) instead of retrying a dropped session forever.
                android_auto::request_quit();
                let _ = tokio::time::timeout(std::time::Duration::from_millis(1500), &mut run).await;
            }
            (result, stopped)
        };
        tasks.abort_all();
        while tasks.join_next().await.is_some() {}
        let closed = unit.close().await;
        result?;
        closed?;
        if let Ok(error) = faults.try_recv() {
            return Err(io::Error::other(error).into());
        }
        if stopped || once || transport == Transport::Usb {
            break;
        }
        log::info!("Session ended; waiting for the next phone connection");
        tokio::select! {
            _ = tokio::time::sleep(std::time::Duration::from_secs(1)) => {},
            _ = tokio::signal::ctrl_c() => break,
        }
    }
    Ok(())
}
