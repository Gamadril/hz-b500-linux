# HZ-B500 HMI (`hz-ui`)

The touch UI on the 1024×600 LCD. One process provides the Home launcher, the
instrument dashboard, media pages, settings and the automatic parking-sensor
view. It starts at boot on the SD image.

Based on two MIT-licensed LVGL projects, adapted and trimmed for this board:

| Source | Upstream | Base commit |
| --- | --- | --- |
| `src/dashboard` | [zhangwei43721/LVGL-CarDashboard](https://github.com/zhangwei43721/LVGL-CarDashboard) | `ae46e17cb7549f272b43e03e635c4a3bc45a49ff` |
| `src/hmi` | [moli721/RK3568-LVGL-CarDashboard](https://github.com/moli721/RK3568-LVGL-CarDashboard) | `92a2741d89664f576ca7cbea378084145480a5c5` |

## Pages

- **Home:** live BMW body view (doors, trunk, lights, turn signals from CAN) and
  the launcher: Dashboard, Music, Video, BT Music, Android Auto (wireless),
  Android Auto USB, Settings, and DOOM when [fbDOOM](../../games/fbdoom/README.md)
  is built.
- **Dashboard:** speed, RPM, coolant, fuel and manual gear from the
  [vehicle service](../../utils/vehicle/README.md). A hand hides while its
  value is unknown.
- **Parking sensors:** a full-screen view opens automatically while the car
  reports PDC active.
- **Music / Video:** play a local MP3 (libmpg123) or H.264 MOV (Cedar hardware
  decoding); see [media files](media/README.md). Repeated video playback can
  still run out of memory on the 64 MiB board.
- **BT Music:** phone audio through the AB5301B module, with play/pause/skip;
  see the [AB5301B guide](../../utils/ab5301b/README.md).
- **Android Auto:** wireless and USB tiles start a session; see
  [Android Auto](../../multimedia/android-auto/README.md). Holding the top-left
  screen corner for 2 s returns to Home (also in DOOM).
- **Settings:** Basic Setup (vehicle info, gear calibration), Sound, Backlight
  control, Picture, Connections (Bluetooth), System information.

Settings details:

- **Backlight control:** normal and dimmed level. The dimmed level is used
  while the car's headlights are on.
- **Picture:** brightness, contrast, hue and saturation of the display engine
  (50 = neutral), with a test pattern. Use it when black looks grey (seen on
  HW Rev. 1.6 panels).
- Sliders act while dragging and are saved on release.

## Service and configuration

`/etc/init.d/hz-ui` starts the UI. Settings are stored in `/etc/config/hz-ui`:

| Option | Default | Meaning |
| --- | --- | --- |
| `brightness` | `128` | Normal backlight, 16–255 |
| `dimmed_brightness` | `64` | Backlight with headlights on, 16–255 |
| `lcd_bright`, `lcd_contrast`, `lcd_hue`, `lcd_saturation` | `50` | Picture settings, 0–100 |
| `touch_device` | `auto` | Touchscreen event device |
| `framebuffer` | `/dev/fb0` | Display device |

Run it in the foreground for diagnosis:

```sh
/etc/init.d/hz-ui stop
hz-ui --framebuffer /dev/fb0 --touch-device auto
/etc/init.d/hz-ui start
```

Its log is in `logread`. Stop `hz-ui` before running other graphics programs.

## Desktop preview

Debian/Ubuntu prerequisites and build, from the SDK root:

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libfreetype-dev \
    libfontconfig-dev libpng-dev fonts-noto-core fonts-noto-cjk fonts-font-awesome
bash package/utils/vehicle/build-host.sh --fetch-deps
bash package/gui/car-dashboards/build-host.sh
bash package/gui/car-dashboards/run-host.sh hmi --demo
```

| Option | Effect |
| --- | --- |
| `--page NAME` | Start page: `main`, `dashboard`, `music`, `video`, `settings`, `gear-calibration` |
| `--demo` | Manual gauges; type `speed 80`, `rpm 3000` in the terminal |
| `--vehicle` | Read data from the vehicle simulator or service |

With simulated vehicle data:

```bash
bash package/utils/vehicle/simulate.sh            # terminal 1
export VEHICLE_UBUS_SOCKET="$PWD/out/vehicle/simulator/ubus.sock"
bash package/gui/car-dashboards/run-host.sh hmi --vehicle    # terminal 2
bash package/gui/car-dashboards/run-host.sh pdc              # parking view only
```

The preview has no audio, video or hardware access; Settings changes stay in
memory.

## Checks

```bash
bash tests/car-dashboards/check.sh     # desktop pages
bash tests/target-ui/check.sh          # target UI logic, rendering, settings
```
