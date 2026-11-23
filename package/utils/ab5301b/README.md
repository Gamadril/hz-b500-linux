# AB5301B audio and Bluetooth controller

The AB5301B module sits between the F133 audio output and the speaker
amplifier/line-out, and contains the Bluetooth radio. Linux controls it over
UART4 (`/dev/ttyS4`, 115200 8N1); audio itself travels as an analog signal.

```text
F133 headphone out (L/R) ──> AB5301B ──> speaker amplifier / line-out
F133 UART4 <── control ───> AB5301B (source, volume, mute, Bluetooth)
```

The `ab5301b` service owns the UART and starts at boot. The HMI and the
Android Auto receiver talk to it through its socket, never to the UART directly.

## Features

- **Audio:** source selection, volume 0–20, mute, internal speaker on/off.
- **Bluetooth music:** the module plays phone audio (A2DP) itself; Linux
  switches to it and sends play/pause/next/previous. Track title, artist and
  phone name are reported (HMI page "BT Music").
- **Connections:** allow or block phone connections, set the Bluetooth name and
  PIN (Settings → Connections in the HMI).
- **Wireless Android Auto:** forwards the phone's Bluetooth setup messages to
  the [Android Auto receiver](../../multimedia/android-auto/README.md).

There is no Linux Bluetooth (BlueZ/HCI) interface. FM and independent line-out
switching are not supported.

## Commands

```sh
ab5301b status                 # state, including the name the radio reports
ab5301b source sd              # local playback (default)
ab5301b volume 18
ab5301b speaker on             # internal speaker
ab5301b mute off
ab5301b bluetooth on           # allow phone connections
ab5301b name SWL-BT            # Bluetooth name, 1–16 characters, no spaces
ab5301b source bt-music        # play phone audio
ab5301b bt play|pause|next|prev|connect|disconnect
ab5301b bt                     # phone, track and play state
aplay -D hw:audiocodec,0 /usr/share/sounds/sample-1mb.wav
```

Runtime commands last until the service restarts; the HMI saves its changes.

## Configuration

`/etc/config/ab5301b`, applied with `/etc/init.d/ab5301b restart`:

| Option | Default | Meaning |
| --- | --- | --- |
| `audio.source` | `sd` | Audio source at startup |
| `audio.volume` | `18` | Volume 0–20 |
| `audio.speaker` | `0` | Internal speaker on (`1`) or off |
| `audio.mute` | `0` | Mute |
| `bluetooth.power` | `0` | Allow phone connections (needed for wireless Android Auto) |
| `bluetooth.local_name` | `SWL-BT` | Bluetooth name, sent at every start |
| `bluetooth.pin` | empty | PIN; empty keeps the radio's own |
| `bluetooth.enabled` | `1` | Bridge for wireless Android Auto |

With `power='0'` the radio stays visible to phone searches but accepts no
connections.

## Logs and checks

Service messages are in `logread`. For a full protocol trace, run it in the
foreground:

```sh
/etc/init.d/ab5301b stop
ab5301b --device /dev/ttyS4 --volume 18 --speaker 1 --mute 0 --source sd serve
/etc/init.d/ab5301b start
```

Host check: `bash tests/audio/ab5301b-check.sh`.
