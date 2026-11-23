use super::*;

fn configuration() -> Configuration {
    Configuration::parse("interface=wlan0\nip=192.168.43.1\nssid=HZ-B500\npsk=private-passphrase\nmac=02:11:22:33:44:55\nchannel=36").unwrap()
}

async fn ipc(stream: &mut UnixStream, kind: u8, payload: &[u8]) {
    let header = [1, kind, (payload.len() >> 8) as u8, payload.len() as u8];
    // Exercise partial IPC headers and payloads as real Unix streams allow.
    for byte in header.iter().chain(payload) {
        stream.write_all(&[*byte]).await.unwrap();
        tokio::task::yield_now().await;
    }
}

async fn data(stream: &mut UnixStream, kind: u16, body: &[u8]) {
    let mut frame = (body.len() as u16).to_be_bytes().to_vec();
    frame.extend(kind.to_be_bytes());
    frame.extend(body);
    // Split the inner length/type as well as the protobuf across UART frames.
    for chunk in frame.chunks(3) {
        ipc(stream, 3, chunk).await;
    }
}

async fn message(stream: &mut UnixStream) -> (u16, Vec<u8>) {
    let mut frame = Vec::new();
    loop {
        let (kind, payload) = read_ipc(stream).await.unwrap();
        assert_eq!(kind, 0x83);
        assert!(payload.len() <= 240);
        frame.extend(payload);
        if frame.len() >= 4 {
            let length = u16::from_be_bytes([frame[0], frame[1]]) as usize;
            if frame.len() == length + 4 {
                return (
                    u16::from_be_bytes([frame[2], frame[3]]),
                    frame[4..].to_vec(),
                );
            }
        }
    }
}

#[test]
fn validates_ap_configuration_without_disclosing_credentials() {
    let valid = "interface=wlan0\nip=192.168.43.1\nssid=HZ-B500\npsk=private-passphrase\nmac=02:11:22:33:44:55\nchannel=36";
    assert!(Configuration::parse(valid).is_ok());
    for bad in [
        valid.replace("wlan0", "../net"),
        valid.replace("192.168.43.1", "0.0.0.0"),
        valid.replace("private-passphrase", "short"),
        valid.replace("02:11", "ff:11"),
        valid.replace("channel=36", "channel=0"),
        format!("{valid}\nchannel=6"),
        format!("{valid}\nother=1"),
    ] {
        let error = Configuration::parse(&bad)
            .err()
            .expect("invalid setting accepted");
        assert!(!error.to_string().contains("private-passphrase"));
    }
}

#[tokio::test]
async fn stock_bootstrap_fragmentation_and_tcp_handoff() {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
    let address = listener.local_addr().unwrap();
    let mut config = configuration();
    config.ip = Ipv4Addr::LOCALHOST;
    config.port = address.port();
    let (unit, mut radio) = UnixStream::pair().unwrap();
    let task = tokio::spawn(async move { config.bootstrap(listener, unit).await });
    // TCP before Bluetooth must be rejected, even while an IPC header is partial.
    radio.write_all(&[1]).await.unwrap();
    let mut early = TcpStream::connect(address).await.unwrap();
    let mut byte = [0];
    assert_eq!(
        tokio::time::timeout(Duration::from_secs(2), early.read(&mut byte))
            .await
            .unwrap()
            .unwrap(),
        0
    );
    radio.write_all(&[1, 0, 1, 0]).await.unwrap();
    ipc(&mut radio, 2, b"SW\r\n").await;
    let (kind, body) = message(&mut radio).await;
    assert_eq!(kind, 4);
    assert_eq!(body, [8, 1, 16, 0, 24, 2, 32, 36]);
    data(&mut radio, 5, &[8, 1, 16, 0, 32, 128]).await;
    // A truncated varint must close the stream rather than panic.
    assert!(
        tokio::time::timeout(Duration::from_secs(2), task)
            .await
            .unwrap()
            .unwrap()
            .is_err()
    );
}

#[tokio::test]
async fn full_stock_sequence_accepts_statusless_type7_and_reuses_tcp() {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
    let address = listener.local_addr().unwrap();
    let mut config = configuration();
    config.ip = Ipv4Addr::LOCALHOST;
    config.port = address.port();
    let (unit, mut radio) = UnixStream::pair().unwrap();
    let task = tokio::spawn(async move { config.bootstrap(listener, unit).await });
    ipc(&mut radio, 1, &[1]).await; // Reconnect to an already latched SW.
    assert_eq!(message(&mut radio).await.0, 4);
    data(&mut radio, 5, &[8, 1, 16, 0, 32, 0, 40, 36]).await;
    let (kind, body) = message(&mut radio).await;
    assert_eq!(kind, 1);
    let socket = Bluetooth::SocketInfoRequest::parse_from_bytes(&body).unwrap();
    assert_eq!(socket.ip_address(), "127.0.0.1");
    assert_eq!(socket.port(), address.port() as u32);
    data(&mut radio, 2, &[]).await;
    let (kind, body) = message(&mut radio).await;
    assert_eq!(kind, 3);
    let network = StockBluetooth::NetworkInfo::parse_from_bytes(&body).unwrap();
    assert_eq!(network.ssid(), "HZ-B500");
    assert_eq!(network.psk(), "private-passphrase");
    assert_eq!(network.mac_addr(), "02:11:22:33:44:55");
    assert_eq!(network.security_mode(), 8);
    // Stock sends no field 5 ap_type and may omit type7.status entirely.
    assert!(!body.windows(2).any(|x| x == [40, 0]));
    data(&mut radio, 7, &[]).await;
    let mut phone = TcpStream::connect(address).await.unwrap();
    phone.write_all(b"normal AA TCP bytes").await.unwrap();
    let mut connected = tokio::time::timeout(Duration::from_secs(2), task)
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    let mut payload = [0; 19];
    connected.read_exact(&mut payload).await.unwrap();
    assert_eq!(&payload, b"normal AA TCP bytes");
}

#[tokio::test]
async fn reset_and_rejected_version_end_bootstrap() {
    for reset in [true, false] {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
        let (unit, mut radio) = UnixStream::pair().unwrap();
        let task = tokio::spawn(async move { configuration().bootstrap(listener, unit).await });
        ipc(&mut radio, 1, &[1]).await;
        message(&mut radio).await;
        if reset {
            ipc(&mut radio, 4, &[]).await;
        } else {
            data(&mut radio, 5, &[8, 2]).await;
        }
        assert!(
            tokio::time::timeout(Duration::from_secs(2), task)
                .await
                .unwrap()
                .unwrap()
                .is_err()
        );
    }
}

#[tokio::test]
async fn output_is_fragmented_at_240_bytes() {
    let (mut unit, mut radio) = UnixStream::pair().unwrap();
    let task =
        tokio::spawn(async move { send_message(&mut unit, 3, vec![42; 500]).await.unwrap() });
    let (kind, body) = message(&mut radio).await;
    assert_eq!(kind, 3);
    assert_eq!(body, vec![42; 500]);
    task.await.unwrap();
}

#[test]
fn ap_status_must_match_ssid_mac_and_channel() {
    let c = configuration();
    let valid = "state=ENABLED\nfreq=5180\nssid[0]=HZ-B500\nbssid[0]=02:11:22:33:44:55\n";
    assert!(c.check_status(valid).is_ok());
    for bad in [
        valid.replace("ENABLED", "DISABLED"),
        valid.replace("5180", "5200"),
        valid.replace("HZ-B500", "other"),
        valid.replace("33:44:55", "33:44:56"),
        String::new(),
    ] {
        assert!(c.check_status(&bad).is_err());
    }
}

#[tokio::test]
async fn network_request_can_precede_version_and_hfp_disconnect() {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
    let address = listener.local_addr().unwrap();
    let (unit, mut radio) = UnixStream::pair().unwrap();
    let task = tokio::spawn(async move { configuration().bootstrap(listener, unit).await });
    ipc(&mut radio, 1, &[1]).await;
    assert_eq!(message(&mut radio).await.0, 4);
    data(&mut radio, 2, &[]).await;
    assert_eq!(message(&mut radio).await.0, 3);
    // HFP state is not evidence that the separate AA bootstrap link is gone.
    ipc(&mut radio, 2, b"IA\r\n").await;
    data(&mut radio, 5, &[8, 1, 32, 0]).await;
    assert_eq!(message(&mut radio).await.0, 1);
    let _phone = TcpStream::connect(address).await.unwrap();
    assert!(
        tokio::time::timeout(Duration::from_secs(2), task)
            .await
            .unwrap()
            .unwrap()
            .is_ok()
    );
}

#[tokio::test]
async fn hostapd_query_cleans_its_socket_on_success_and_cancellation() {
    let directory = std::env::temp_dir().join(format!("aa-hostapd-test-{}", std::process::id()));
    std::fs::create_dir(&directory).unwrap();
    let mut config = configuration();
    config.socket = directory.join("companion.sock.bt");
    config.hostapd_control = directory.join("ap");
    let server = UnixDatagram::bind(&config.hostapd_control).unwrap();
    let client_config = config.clone();
    let client = tokio::spawn(async move { client_config.check_access_point().await });
    let mut command = [0; 64];
    let (length, peer) =
        tokio::time::timeout(Duration::from_secs(2), server.recv_from(&mut command))
            .await
            .unwrap()
            .unwrap();
    assert_eq!(&command[..length], b"STATUS");
    server
        .send_to(
            b"state=ENABLED\nfreq=5180\nssid[0]=HZ-B500\nbssid[0]=02:11:22:33:44:55\n",
            peer.as_pathname().unwrap(),
        )
        .await
        .unwrap();
    client.await.unwrap().unwrap();
    assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
    let client = tokio::spawn(async move { config.check_access_point().await });
    tokio::time::timeout(Duration::from_secs(2), server.recv_from(&mut command))
        .await
        .unwrap()
        .unwrap();
    client.abort();
    assert!(client.await.unwrap_err().is_cancelled());
    assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
    drop(server);
    std::fs::remove_file(directory.join("ap")).unwrap();
    std::fs::remove_dir(directory).unwrap();
}

#[tokio::test]
async fn oversized_inner_frame_and_invalid_ipc_are_rejected() {
    for bad in [false, true] {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
        let (unit, mut radio) = UnixStream::pair().unwrap();
        let task = tokio::spawn(async move { configuration().bootstrap(listener, unit).await });
        ipc(&mut radio, 1, &[1]).await;
        message(&mut radio).await;
        if bad {
            radio.write_all(&[2, 3, 0, 0]).await.unwrap();
        } else {
            ipc(&mut radio, 3, &[0x10, 1, 0, 5]).await;
        }
        assert!(
            tokio::time::timeout(Duration::from_secs(2), task)
                .await
                .unwrap()
                .unwrap()
                .is_err()
        );
    }
}

#[tokio::test]
async fn version_is_sent_at_start_and_again_when_a_phone_connects() {
    let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).await.unwrap();
    let address = listener.local_addr().unwrap();
    let (unit, mut radio) = UnixStream::pair().unwrap();
    let task = tokio::spawn(async move { configuration().bootstrap(listener, unit).await });
    // Phone already connected before the receiver started: no state report, no SW.
    assert_eq!(message(&mut radio).await.0, 4);
    // A later SW (phone reconnected) restarts the handshake with a fresh request.
    ipc(&mut radio, 2, b"SW\r\n").await;
    assert_eq!(message(&mut radio).await.0, 4);
    data(&mut radio, 5, &[8, 1, 16, 0, 32, 0]).await;
    assert_eq!(message(&mut radio).await.0, 1);
    // After the phone's version reply, a further SW must not restart the exchange:
    // the next message is the type-3 answer, not another version request.
    ipc(&mut radio, 2, b"SW\r\n").await;
    data(&mut radio, 2, &[]).await;
    assert_eq!(message(&mut radio).await.0, 3);
    let _phone = TcpStream::connect(address).await.unwrap();
    assert!(
        tokio::time::timeout(Duration::from_secs(2), task)
            .await
            .unwrap()
            .unwrap()
            .is_ok()
    );
}
