//! Wireless AA bootstrap through the sole UART owner's private Unix socket.
//!
//! This backend uses the stock full-stack radio protocol, not Bluetooth HCI.
#![allow(clippy::missing_docs_in_private_items)]

use crate::{Bluetooth, StockBluetooth};
use protobuf::Message;
use std::{collections::HashMap, io, net::Ipv4Addr, path::PathBuf, time::Duration};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::{TcpListener, TcpStream, UnixDatagram, UnixStream},
};

const MAX_MESSAGE: usize = 4096;
const BOOTSTRAP_TIMEOUT: Duration = Duration::from_secs(45);

/// Explicit credentials for an already configured WPA2 access point.
/// Intentionally does not implement Debug: this object contains the PSK.
#[derive(Clone)]
pub struct Configuration {
    socket: PathBuf,
    interface: String,
    hostapd_control: PathBuf,
    ip: Ipv4Addr,
    port: u16,
    ssid: String,
    psk: String,
    mac: String,
    channel: u32,
}

fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}

impl Configuration {
    /// Load a private (mode 0600) key=value file; see the packaged example.
    pub fn load(path: impl AsRef<std::path::Path>) -> io::Result<Self> {
        use std::os::unix::fs::PermissionsExt;
        let file = std::fs::File::open(path)?;
        let metadata = file.metadata()?;
        if !metadata.is_file()
            || metadata.len() > 8192
            || metadata.permissions().mode() & 0o077 != 0
        {
            return Err(invalid(
                "wireless configuration must be a private file of at most 8192 bytes",
            ));
        }
        let mut text = String::new();
        use std::io::Read;
        file.take(8193).read_to_string(&mut text)?;
        if text.len() > 8192 {
            return Err(invalid("wireless configuration too large"));
        }
        Self::parse(&text)
    }

    fn parse(text: &str) -> io::Result<Self> {
        let mut entries = HashMap::new();
        for line in text
            .lines()
            .map(str::trim)
            .filter(|s| !s.is_empty() && !s.starts_with('#'))
        {
            let (key, value) = line
                .split_once('=')
                .ok_or_else(|| invalid("expected key=value"))?;
            let key = key.trim();
            if ![
                "bt_socket",
                "hostapd_control",
                "interface",
                "ip",
                "port",
                "ssid",
                "psk",
                "mac",
                "channel",
            ]
            .contains(&key)
                || entries.insert(key, value.trim()).is_some()
            {
                return Err(invalid("unknown or duplicate wireless configuration key"));
            }
        }
        let get = |key| {
            entries
                .get(key)
                .copied()
                .ok_or_else(|| invalid("missing wireless configuration key"))
        };
        let configuration = Self {
            socket: (*entries
                .get("bt_socket")
                .unwrap_or(&"/var/run/ab5301b.sock.bt"))
            .into(),
            interface: get("interface")?.into(),
            hostapd_control: entries
                .get("hostapd_control")
                .map(PathBuf::from)
                .unwrap_or_else(|| {
                    PathBuf::from("/var/run/hostapd")
                        .join(entries.get("interface").copied().unwrap_or(""))
                }),
            ip: get("ip")?
                .parse()
                .map_err(|_| invalid("expected AP IPv4 address"))?,
            port: entries
                .get("port")
                .unwrap_or(&"6730")
                .parse()
                .map_err(|_| invalid("invalid TCP port"))?,
            ssid: get("ssid")?.into(),
            psk: get("psk")?.into(),
            mac: get("mac")?.to_ascii_lowercase(),
            channel: get("channel")?
                .parse()
                .map_err(|_| invalid("invalid Wi-Fi channel"))?,
        };
        let c = &configuration;
        let mac: Option<Vec<u8>> = c
            .mac
            .split(':')
            .map(|b| {
                if b.len() != 2 {
                    None
                } else {
                    u8::from_str_radix(b, 16).ok()
                }
            })
            .collect();
        if !c.socket.is_absolute()
            || !c.hostapd_control.is_absolute()
            || c.interface.is_empty()
            || c.interface.len() > 15
            || c.interface == "lo"
            || !c
                .interface
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b"_-.".contains(&b))
            || c.ip.is_unspecified()
            || c.ip.is_multicast()
            || c.ip.is_loopback()
            || c.ip.is_broadcast()
            || c.port == 0
            || c.ssid.is_empty()
            || c.ssid.len() > 32
            || c.ssid.chars().any(char::is_control)
            || !((8..=63).contains(&c.psk.len())
                && c.psk.is_ascii()
                && !c.psk.chars().any(char::is_control)
                || c.psk.len() == 64 && c.psk.bytes().all(|b| b.is_ascii_hexdigit()))
            || !(1..=196).contains(&c.channel)
            || !mac.is_some_and(|m| m.len() == 6 && m[0] & 1 == 0 && m.iter().any(|b| *b != 0))
        {
            return Err(invalid("invalid wireless AP settings"));
        }
        Ok(configuration)
    }

    /// Check the interface and hostapd state, then bind the advertised IP.
    /// AP mode, SSID, channel, DHCP and firewall are configured by netifd/hostapd.
    pub async fn connect(&self) -> io::Result<TcpStream> {
        let directory = PathBuf::from("/sys/class/net").join(&self.interface);
        let mac = tokio::fs::read_to_string(directory.join("address")).await?;
        if !mac.trim().eq_ignore_ascii_case(&self.mac) {
            return Err(invalid("configured AP MAC differs from interface MAC"));
        }
        let state = tokio::fs::read_to_string(directory.join("operstate")).await?;
        if !matches!(state.trim(), "up" | "unknown") {
            return Err(io::Error::new(
                io::ErrorKind::NotConnected,
                "AP interface is down",
            ));
        }
        self.check_access_point().await?;
        let listener = TcpListener::bind((self.ip, self.port)).await?;
        let stream = UnixStream::connect(&self.socket).await?;
        log::info!(
            "Wireless AA listening on {}:{} via {}",
            self.ip,
            self.port,
            self.interface
        );
        self.bootstrap(listener, stream).await
    }

    async fn check_access_point(&self) -> io::Result<()> {
        use std::sync::atomic::{AtomicU64, Ordering};
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let path = self.socket.with_file_name(format!(
            "aa-hostapd-{}-{}",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::Relaxed)
        ));
        let control = ControlSocket {
            socket: UnixDatagram::bind(&path)?,
            path,
        };
        control.socket.connect(&self.hostapd_control)?;
        tokio::time::timeout(Duration::from_secs(2), async {
            control.socket.send(b"STATUS").await?;
            let mut response = [0; 8192];
            let length = control.socket.recv(&mut response).await?;
            let text = std::str::from_utf8(&response[..length])
                .map_err(|_| invalid("invalid hostapd response"))?;
            self.check_status(text)
        })
        .await
        .map_err(|_| io::Error::new(io::ErrorKind::TimedOut, "hostapd STATUS timed out"))?
    }

    fn check_status(&self, status: &str) -> io::Result<()> {
        let fields: HashMap<_, _> = status
            .lines()
            .filter_map(|line| line.split_once('='))
            .collect();
        let frequency: u32 = fields.get("freq").and_then(|f| f.parse().ok()).unwrap_or(0);
        let expected_frequency = match self.channel {
            1..=13 => 2407 + self.channel * 5,
            14 => 2484,
            channel => 5000 + channel * 5,
        };
        if fields.get("state") != Some(&"ENABLED")
            || fields.get("ssid[0]") != Some(&self.ssid.as_str())
            || !fields
                .get("bssid[0]")
                .is_some_and(|mac| mac.eq_ignore_ascii_case(&self.mac))
            || frequency != expected_frequency
        {
            return Err(invalid(
                "hostapd AP state/SSID/BSSID/channel differs from wireless configuration",
            ));
        }
        Ok(())
    }

    async fn bootstrap(&self, listener: TcpListener, stream: UnixStream) -> io::Result<TcpStream> {
        let mut input = Vec::new();
        let mut active = true; // handshake starts at once, see below
        let mut version = false;
        let mut credentials = false;
        let timer = tokio::time::sleep(BOOTSTRAP_TIMEOUT);
        tokio::pin!(timer);
        // A single reader future owns IPC parsing so select cancellation cannot lose bytes.
        let (reader, mut writer) = stream.into_split();
        let mut receive = Box::pin(receive_packet(reader));
        // Like stock Melis, start the handshake at once: a phone that connected over
        // Bluetooth before this receiver ran sends no new SW, and the companion's
        // cached readiness is cleared by later IA reports. Without a phone this goes
        // nowhere; SW (re)starts it and the timeout below repeats it.
        send_version(&mut writer, self.channel).await?;
        loop {
            tokio::select! {
                packet = &mut receive => {
                    let (reader, packet) = packet;
                    let (kind, payload) = packet?;
                    match kind {
                        1 if payload.as_slice() == [1] => {
                            if !active {
                                active = true;
                                timer.as_mut().reset(tokio::time::Instant::now() + BOOTSTRAP_TIMEOUT);
                                send_version(&mut writer, self.channel).await?;
                            }
                        }
                        1 if payload.as_slice() == [0] => {},
                        2 if payload.as_slice() == b"SW\r\n" => {
                            // A phone (re)opened its setup channel: (re)send the version
                            // request, but never after its reply. A second request on a
                            // running exchange makes the phone abort the session.
                            if !version {
                                active = true;
                                timer.as_mut().reset(tokio::time::Instant::now() + BOOTSTRAP_TIMEOUT);
                                send_version(&mut writer, self.channel).await?;
                            }
                        }
                        2 if payload.as_slice() == b"IS\r\n" => {
                            active = false; version = false; credentials = false; input.clear();
                        }
                        2 => {},
                        4 if payload.is_empty() => return Err(io::Error::new(io::ErrorKind::ConnectionReset, "companion reset")),
                        3 if active => {
                            input.extend_from_slice(&payload);
                            while input.len() >= 4 {
                                let length = u16::from_be_bytes([input[0], input[1]]) as usize;
                                if length > MAX_MESSAGE { return Err(invalid("oversized Bluetooth bootstrap message")); }
                                if input.len() < length + 4 { break; }
                                let kind = u16::from_be_bytes([input[2], input[3]]);
                                let body = &input[4..length+4];
                                match kind {
                                    5 => {
                                        let reply = StockBluetooth::VersionResponse::parse_from_bytes(body).map_err(|_| invalid("malformed version response"))?;
                                        if !reply.has_major() || reply.major() != 1 || reply.status() != 0 {
                                            return Err(invalid("phone rejected wireless bootstrap version"));
                                        }
                                        let mut socket = Bluetooth::SocketInfoRequest::new();
                                        socket.set_ip_address(self.ip.to_string());
                                        socket.set_port(self.port.into());
                                        send_message(&mut writer, 1, socket.write_to_bytes().map_err(io::Error::other)?).await?;
                                        version = true;
                                    }
                                    2 => {
                                        let mut network = StockBluetooth::NetworkInfo::new();
                                        network.set_ssid(self.ssid.clone());
                                        network.set_psk(self.psk.clone());
                                        network.set_mac_addr(self.mac.clone());
                                        network.set_security_mode(8);
                                        send_message(&mut writer, 3, network.write_to_bytes().map_err(io::Error::other)?).await?;
                                        credentials = true;
                                    }
                                    7 if version => {
                                        let reply = StockBluetooth::SocketInfoResponse::parse_from_bytes(body).map_err(|_| invalid("malformed socket response"))?;
                                        if reply.has_status() && reply.status() != 0 {
                                            return Err(invalid("phone rejected wireless network"));
                                        }
                                    }
                                    6 => {}, // Stock handler only logs this packet.
                                    _ => return Err(invalid("unexpected Bluetooth bootstrap message")),
                                }
                                input.drain(..length+4);
                            }
                        }
                        3 => {}, // No data belongs to a session before SW.
                        _ => return Err(invalid("invalid companion IPC packet")),
                    }
                    receive = Box::pin(receive_packet(reader));
                }
                accepted = listener.accept() => {
                    let (tcp, _) = accepted?;
                    if active && version && credentials {
                        tcp.set_nodelay(true)?;
                        log::info!("Wireless AA TCP connection established");
                        return Ok(tcp);
                    }
                    // Reject connections that precede Bluetooth credential exchange.
                    drop(tcp);
                }
                _ = &mut timer, if active => return Err(io::Error::new(io::ErrorKind::TimedOut, "wireless bootstrap timed out")),
            }
        }
    }
}

struct ControlSocket {
    socket: UnixDatagram,
    path: PathBuf,
}
impl Drop for ControlSocket {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
    }
}

async fn receive_packet(
    mut reader: tokio::net::unix::OwnedReadHalf,
) -> (tokio::net::unix::OwnedReadHalf, io::Result<(u8, Vec<u8>)>) {
    let result = read_ipc(&mut reader).await;
    (reader, result)
}

async fn read_ipc(reader: &mut (impl tokio::io::AsyncRead + Unpin)) -> io::Result<(u8, Vec<u8>)> {
    let mut header = [0; 4];
    reader.read_exact(&mut header).await?;
    let length = u16::from_be_bytes([header[2], header[3]]) as usize;
    if header[0] != 1 || length > 255 {
        return Err(invalid("invalid companion IPC header"));
    }
    let mut payload = vec![0; length];
    reader.read_exact(&mut payload).await?;
    Ok((header[1], payload))
}

async fn send_version(
    writer: &mut (impl tokio::io::AsyncWrite + Unpin),
    channel: u32,
) -> io::Result<()> {
    let mut version = StockBluetooth::VersionRequest::new();
    version.set_major(1);
    version.set_minor(0);
    version.set_flags(2);
    version.set_channel(channel);
    send_message(
        writer,
        4,
        version.write_to_bytes().map_err(io::Error::other)?,
    )
    .await
}

async fn send_message(
    writer: &mut (impl tokio::io::AsyncWrite + Unpin),
    kind: u16,
    body: Vec<u8>,
) -> io::Result<()> {
    if body.len() > MAX_MESSAGE {
        return Err(invalid("oversized outbound bootstrap message"));
    }
    let mut frame = Vec::with_capacity(body.len() + 4);
    frame.extend_from_slice(&(body.len() as u16).to_be_bytes());
    frame.extend_from_slice(&kind.to_be_bytes());
    frame.extend_from_slice(&body);
    for chunk in frame.chunks(240) {
        writer.write_all(&[1, 0x83, 0, chunk.len() as u8]).await?;
        writer.write_all(chunk).await?;
    }
    Ok(())
}

#[cfg(test)]
mod tests;
