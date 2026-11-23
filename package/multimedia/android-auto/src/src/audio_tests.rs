//! Exercise real audio handlers, including backpressure acknowledgements.
use super::*;
use std::sync::{
    Mutex,
    atomic::{AtomicBool, Ordering},
};

struct Unit {
    fail: AtomicBool,
    night: Mutex<Option<bool>>,
    events: Mutex<Vec<String>>,
    sensors: SensorInformation,
    input: InputConfiguration,
    video: VideoConfiguration,
}
impl Unit {
    fn new() -> Self {
        Self {
            fail: AtomicBool::new(false),
            night: Mutex::new(None),
            events: Mutex::new(Vec::new()),
            sensors: SensorInformation {
                sensors: HashSet::new(),
            },
            input: InputConfiguration {
                keycodes: vec![],
                touchscreen: None,
            },
            video: VideoConfiguration {
                resolution: Wifi::video_resolution::Enum::_480p,
                fps: Wifi::video_fps::Enum::_30,
                dpi: 111,
                margins: (0, 0),
            },
        }
    }
}
#[async_trait::async_trait]
impl AndroidAutoAudioOutputTrait for Unit {
    async fn open_output_channel(&self, _: AudioChannelType) -> Result<(), ()> {
        Ok(())
    }
    async fn close_output_channel(&self, _: AudioChannelType) -> Result<(), ()> {
        Ok(())
    }
    async fn receive_output_audio(&self, _: AudioChannelType, _: Vec<u8>) {
        panic!("unchecked write");
    }
    async fn write_output_audio(&self, kind: AudioChannelType, data: Vec<u8>) -> Result<(), ()> {
        assert_eq!(data, [1, 2, 3, 4]);
        self.events.lock().unwrap().push(format!("write {kind:?}"));
        if self.fail.load(Ordering::Relaxed) {
            Err(())
        } else {
            Ok(())
        }
    }
    async fn start_output_audio(&self, kind: AudioChannelType) {
        self.events.lock().unwrap().push(format!("start {kind:?}"));
    }
    async fn stop_output_audio(&self, kind: AudioChannelType) {
        self.events.lock().unwrap().push(format!("stop {kind:?}"));
    }
}
#[async_trait::async_trait]
impl AndroidAutoSensorTrait for Unit {
    fn get_supported_sensors(&self) -> &SensorInformation {
        &self.sensors
    }
    async fn start_sensor(&self, _: Wifi::sensor_type::Enum) -> Result<(), ()> {
        Ok(())
    }
    fn night_mode(&self) -> Option<bool> {
        *self.night.lock().unwrap()
    }
}
#[async_trait::async_trait]
impl AndroidAutoInputChannelTrait for Unit {
    async fn input_channel_opened(&self) {
        self.events.lock().unwrap().push("input opened".into());
    }
    async fn binding_request(&self, _: u32) -> Result<(), ()> {
        Ok(())
    }
    fn retrieve_input_configuration(&self) -> &InputConfiguration {
        &self.input
    }
}

#[tokio::test]
async fn input_channel_advertises_touch_and_notifies_after_a_successful_open_reply() {
    let mut unit = Unit::new();
    unit.input.touchscreen = Some((800, 480));
    let config = configuration();
    let handler = InputChannelHandler {};
    let descriptor = handler.build_channel(&config, 1, &unit).unwrap();
    let touch = descriptor
        .input_channel
        .as_ref()
        .unwrap()
        .touch_screen_config
        .as_ref()
        .unwrap();
    assert_eq!((touch.width(), touch.height()), (800, 480));
    let mut request = Wifi::ChannelOpenRequest::new();
    request.set_priority(0);
    request.set_channel_id(1);
    let mut data = (Wifi::CommonMessage::CHANNEL_OPEN_REQUEST as u16)
        .to_be_bytes()
        .to_vec();
    data.extend(request.write_to_bytes().unwrap());
    let frame = AndroidAutoFrame {
        header: FrameHeader {
            channel_id: 1,
            frame: FrameHeaderContents::new(false, FrameHeaderType::Single, true),
        },
        data,
    };
    let (writer, mut replies) = ssl::test_frame_writer();
    handler
        .receive_data(frame, &writer, &config, &unit)
        .await
        .unwrap();
    assert_eq!(*unit.events.lock().unwrap(), ["input opened"]);
    let ssl::SslThreadData::Frame(frame) = replies.recv().await.unwrap() else {
        panic!("expected open reply");
    };
    let response = Wifi::ChannelOpenResponse::parse_from_bytes(&frame.data[2..]).unwrap();
    assert_eq!(response.status(), Wifi::status::Enum::OK);
    unit.input.touchscreen = None;
    let descriptor = handler.build_channel(&config, 1, &unit).unwrap();
    assert!(
        descriptor
            .input_channel
            .as_ref()
            .unwrap()
            .touch_screen_config
            .is_none()
    );
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
impl AndroidAutoVideoChannelTrait for Unit {
    async fn receive_video(&self, _: Vec<u8>, _: Option<u64>) {}
    async fn setup_video(&self) -> Result<(), ()> {
        Ok(())
    }
    async fn teardown_video(&self) {}
    async fn wait_for_focus(&self) {}
    async fn set_focus(&self, _: bool) {}
    fn retrieve_video_configuration(&self) -> &VideoConfiguration {
        &self.video
    }
}
#[async_trait::async_trait]
impl AndroidAutoMainTrait for Unit {
    async fn connect(&self) {}
    async fn disconnect(&self) {}
    async fn get_receiver(
        &self,
    ) -> Option<tokio::sync::mpsc::Receiver<SendableAndroidAutoMessage>> {
        None
    }
}

fn configuration() -> AndroidAutoConfiguration {
    AndroidAutoConfiguration {
        custom_certificate: None,
        unit: HeadUnitInfo {
            name: "test".into(),
            car_model: String::new(),
            car_year: String::new(),
            car_serial: String::new(),
            left_hand: true,
            head_manufacturer: String::new(),
            head_model: String::new(),
            sw_build: String::new(),
            sw_version: String::new(),
            native_media: false,
            hide_clock: None,
        },
    }
}
fn specific(kind: Wifi::avchannel_message::Enum, bytes: Vec<u8>) -> AndroidAutoFrame {
    let mut data = (kind as u16).to_be_bytes().to_vec();
    data.extend(bytes);
    AndroidAutoFrame {
        header: FrameHeader {
            channel_id: 5,
            frame: FrameHeaderContents::new(false, FrameHeaderType::Single, false),
        },
        data,
    }
}
fn start(session: i32, config: u32) -> AndroidAutoFrame {
    let mut m = Wifi::AVChannelStartIndication::new();
    m.set_session(session);
    m.set_config(config);
    specific(
        Wifi::avchannel_message::Enum::START_INDICATION,
        m.write_to_bytes().unwrap(),
    )
}
fn packet() -> AndroidAutoFrame {
    AvChannelMessage::MediaIndication(5, Some(123456), vec![1, 2, 3, 4]).into()
}

#[tokio::test]
async fn audio_sessions_ack_after_acceptance_and_reject_failed_writes() {
    for (handler, kind, rate, channels) in [
        (
            ChannelHandler::MediaAudio(MediaAudioChannelHandler::default()),
            "Media",
            48000,
            2,
        ),
        (
            ChannelHandler::SystemAudio(SystemAudioChannelHandler::default()),
            "System",
            16000,
            1,
        ),
        (
            ChannelHandler::SpeechAudio(SpeechAudioChannelHandler::default()),
            "Speech",
            16000,
            1,
        ),
    ] {
        let unit = Unit::new();
        let config = configuration();
        let descriptor = handler.build_channel(&config, 5, &unit).unwrap();
        let format = &descriptor.av_channel.as_ref().unwrap().audio_configs[0];
        assert_eq!(
            (
                format.sample_rate(),
                format.channel_count(),
                format.bit_depth()
            ),
            (rate, channels, 16)
        );
        let (writer, mut replies) = ssl::test_frame_writer();
        assert!(matches!(
            handler
                .receive_data(packet(), &writer, &config, &unit)
                .await,
            Err(FrameIoError::Sequence(
                FrameSequenceError::AudioChannelNotOpen
            ))
        ));
        assert!(replies.try_recv().is_err());
        assert!(matches!(
            handler
                .receive_data(start(7, 1), &writer, &config, &unit)
                .await,
            Err(FrameIoError::Sequence(
                FrameSequenceError::InvalidAudioConfiguration
            ))
        ));
        handler
            .receive_data(start(7, 0), &writer, &config, &unit)
            .await
            .unwrap();
        handler
            .receive_data(packet(), &writer, &config, &unit)
            .await
            .unwrap();
        assert_eq!(
            *unit.events.lock().unwrap(),
            [format!("start {kind}"), format!("write {kind}")]
        );
        let ssl::SslThreadData::Frame(frame) = replies.recv().await.unwrap() else {
            panic!("expected ACK");
        };
        let AvChannelMessage::MediaIndicationAck(channel, ack) = (&frame).try_into().unwrap()
        else {
            panic!("expected audio ACK");
        };
        assert_eq!((channel, ack.session(), ack.value()), (5, 7, 1));
        unit.fail.store(true, Ordering::Relaxed);
        assert!(matches!(
            handler
                .receive_data(packet(), &writer, &config, &unit)
                .await,
            Err(FrameIoError::AudioOutputError)
        ));
        assert!(replies.try_recv().is_err());
        handler
            .receive_data(
                specific(Wifi::avchannel_message::Enum::STOP_INDICATION, vec![]),
                &writer,
                &config,
                &unit,
            )
            .await
            .unwrap();
        assert!(matches!(
            handler
                .receive_data(packet(), &writer, &config, &unit)
                .await,
            Err(FrameIoError::Sequence(
                FrameSequenceError::AudioChannelNotOpen
            ))
        ));
        unit.fail.store(false, Ordering::Relaxed);
        handler
            .receive_data(start(19, 0), &writer, &config, &unit)
            .await
            .unwrap();
        handler
            .receive_data(packet(), &writer, &config, &unit)
            .await
            .unwrap();
        let ssl::SslThreadData::Frame(frame) = replies.recv().await.unwrap() else {
            panic!("expected ACK");
        };
        let AvChannelMessage::MediaIndicationAck(_, ack) = (&frame).try_into().unwrap() else {
            panic!("expected ACK");
        };
        assert_eq!(ack.session(), 19);
    }
}

/// TCP enters the same AA version negotiation and TLS client as wired USB.
#[cfg(feature = "ab5301b")]
#[tokio::test]
async fn wireless_tcp_runs_shared_version_and_tls_negotiation() {
    use tokio::{io::{AsyncReadExt, AsyncWriteExt}, net::{TcpListener, TcpStream}};
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let mut phone = TcpStream::connect(listener.local_addr().unwrap()).await.unwrap();
    let (unit_stream, _) = listener.accept().await.unwrap();
    let session = tokio::spawn(async move {
        // setup() installs the same rustls crypto provider used by the binary.
        let _setup = setup();
        ConnectionType::Wireless(unit_stream).run(configuration(), &Box::new(Unit::new())).await;
    });
    let exchange = async {
        let mut version = [0; 10];
        phone.read_exact(&mut version).await.unwrap();
        assert_eq!(version, [0, 3, 0, 6, 0, 1, 0, 1, 0, 1]);
        // VERSION_RESPONSE (major 1, minor 1, compatible).
        phone.write_all(&[0, 3, 0, 8, 0, 2, 0, 1, 0, 1, 0, 0]).await.unwrap();
        let mut header = [0; 4];
        phone.read_exact(&mut header).await.unwrap();
        assert_eq!(header[..2], [0, 3]);
        let mut body = vec![0; u16::from_be_bytes([header[2], header[3]]) as usize];
        phone.read_exact(&mut body).await.unwrap();
        assert_eq!(&body[..2], &[0, 3]); // SSL_HANDSHAKE
        assert_eq!(body[2], 22); // TLS handshake record (ClientHello).
    };
    tokio::time::timeout(std::time::Duration::from_secs(3), exchange).await.unwrap();
    drop(phone);
    tokio::time::timeout(std::time::Duration::from_secs(3), session).await.unwrap().unwrap();
}

/// Night mode follows the vehicle state: sent on sensor start, then only on change.
#[tokio::test]
async fn night_sensor_sends_initial_state_and_changes_only() {
    let unit = Unit::new();
    *unit.night.lock().unwrap() = Some(true);
    let config = configuration();
    let handler = ChannelHandler::Sensor(SensorChannelHandler {});
    let (writer, mut replies) = ssl::test_frame_writer();
    let night_of = |data: ssl::SslThreadData| {
        let ssl::SslThreadData::Frame(frame) = data else { panic!("expected frame") };
        assert_eq!(frame.data[..2], (Wifi::sensor_channel_message::Enum::SENSOR_EVENT_INDICATION as u16).to_be_bytes());
        let event = Wifi::SensorEventIndication::parse_from_bytes(&frame.data[2..]).unwrap();
        event.night_mode[0].is_night()
    };
    sensor::NIGHT_CHANNEL.store(-1, std::sync::atomic::Ordering::Relaxed);
    let mut request = Wifi::SensorStartRequestMessage::new();
    request.set_sensor_type(Wifi::sensor_type::Enum::NIGHT_DATA);
    request.set_refresh_interval(0);
    let mut data = (Wifi::sensor_channel_message::Enum::SENSOR_START_REQUEST as u16).to_be_bytes().to_vec();
    data.extend(request.write_to_bytes().unwrap());
    let start = AndroidAutoFrame {
        header: FrameHeader { channel_id: 4, frame: FrameHeaderContents::new(true, FrameHeaderType::Single, false) },
        data,
    };
    handler.receive_data(start, &writer, &config, &unit).await.unwrap();
    assert!(matches!(replies.recv().await.unwrap(), ssl::SslThreadData::Frame(_))); // start response
    assert!(night_of(replies.recv().await.unwrap()));
    sensor::update_night(&unit, &writer).await.unwrap();
    assert!(replies.try_recv().is_err(), "unchanged state must not be resent");
    *unit.night.lock().unwrap() = Some(false);
    sensor::update_night(&unit, &writer).await.unwrap();
    assert!(!night_of(replies.recv().await.unwrap()));
    // A new session forgets the sensor until the phone starts it again.
    sensor::NIGHT_CHANNEL.store(-1, std::sync::atomic::Ordering::Relaxed);
    *unit.night.lock().unwrap() = Some(true);
    sensor::update_night(&unit, &writer).await.unwrap();
    assert!(replies.try_recv().is_err());
}
