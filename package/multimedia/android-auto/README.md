# Android Auto receiver

`cedar-headunit` shows Android Auto on the HZ-B500 LCD, wired over USB or
wireless through the stock AB5301B radio and the board's Wi-Fi. It uses the
Cedar hardware H.264 decoder, ALSA audio and the Goodix touchscreen.

Based on [uglyoldbob/android-auto](https://github.com/uglyoldbob/android-auto)
0.3.8 (base `06c68d5ea8ac4f0ef0ab3b97dc60b9665f7074cb`, LGPL-3.0-or-later),
with local Cedar video, audio, touch, AB5301B and night-mode support. The
patched `rustls-webpki` dependency is ISC-licensed (`notices/webpki-ISC.txt`).

## Use

Start a session from the HMI's **Android Auto** (wireless) or **Android Auto
USB** tile. Hold the top-left screen corner for 2 s to end it and return to
Home. Tested with one phone.

| Feature | Details |
| --- | --- |
| Video | 1280×720 at 30 fps; the phone draws into the visible 1024×600 centre |
| Audio | Media, navigation prompts and system sounds, mixed to the speaker/line-out |
| Touch | Taps and drags (multitouch untested) |
| Night mode | Dark theme while the car's headlights are on |
| Wheel keys | Next/previous/play/stop from the car, via the vehicle service (untested) |
| Microphone | Not supported (no calls or voice input) |

From the UART shell (stop the HMI first with `/etc/init.d/hz-ui stop`):

```sh
cedar-headunit --once                              # USB, one session
android-auto-wireless --once                       # wireless, one session
cedar-headunit --touch-device /dev/input/eventN    # select touchscreen
cedar-headunit --file video.h264                   # play a raw H.264 test stream
```

`cedar-headunit --help` lists all options.

## Wireless

The wireless tile needs no setup. It enables the AB5301B Bluetooth bridge,
starts a Wi-Fi access point (`192.168.43.1/24`, channel 36) with DHCP, runs the
receiver and stops everything again afterwards. The phone pairs with the
radio over Bluetooth, gets the Wi-Fi credentials from it and connects
automatically.

- **Credentials:** `/etc/android-auto-hostapd.conf` is created on first use with
  a random passphrase; edit it to change SSID, channel or passphrase.
- **Country:** 5 GHz needs the right regulatory domain. Set
  `CONFIG_WIRELESS_REGDB_COUNTRY` (default `DE`) in `make menuconfig` before
  building; `iw reg get` shows the active domain on the device.
- **Bluetooth name/PIN:** `ab5301b.bluetooth.local_name` and `.pin` in
  `/etc/config/ab5301b`, or Settings → Connections.
- After a session the phone's Bluetooth link is dropped so it stops retrying
  Android Auto; it may reconnect by itself for music and calls.

Wi-Fi is prepared at boot (link up only, nothing is transmitted), so the access
point is ready about half a second after tapping the tile.

## Build

The SD profile includes the package. Besides the normal
[SDK prerequisites](../../../build/SOURCE_SETUP.md), the host needs a recent
Rust toolchain (edition 2024), `protobuf-compiler`, `cmake`, `libclang-dev` and
`binutils-riscv64-linux-gnu`:

```bash
rustup target add riscv64gc-unknown-linux-gnu
```

Then build the image as usual (`m -j4`, `p`). Cargo dependencies are locked by
`src/Cargo.lock` and downloaded on the first build.

Host checks:

```bash
export CEDAR_SDK_ROOT="$PWD"
bash package/multimedia/android-auto/src/tests/cedar/check.sh
bash package/multimedia/android-auto/src/tests/audio/check.sh
bash package/multimedia/android-auto/src/tests/touch/check.sh
CARGO_TARGET_DIR="$PWD/out/android-auto/cargo-host" cargo test --locked \
  --manifest-path package/multimedia/android-auto/src/Cargo.toml --lib --features cedar,ab5301b
```
