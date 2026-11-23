# Board testing over two UARTs

Test and update a running board from the host without reflashing the SD card.
Two USB serial adapters are used:

| Adapter | Board | Settings |
| --- | --- | --- |
| `/dev/ttyUSB1` | UART0 debug console (TX, RX, GND) | 115200 8N1 |
| `/dev/ttyUSB0` | CAN bridge input: adapter TX to PB7 (UART3 RX), GND | 750000 8N1 |

The tools never flash the card or power-cycle the board.

## Session

Start the capture before powering the board, so the full boot log is kept:

```bash
bash scripts/board-uart.sh run /dev/ttyUSB1 /dev/ttyUSB0
```

It prints `READY` and a session directory under `out/hardware-test/`
(`SESSION` below). In another terminal:

```bash
bash scripts/board-uart.sh wake SESSION                       # wait for the shell
bash scripts/board-uart.sh exec SESSION 'ubus call vehicle snapshot'
bash scripts/board-uart.sh interrupt SESSION                  # Ctrl-C a stuck command
bash scripts/board-uart.sh stop SESSION
```

Each command's output is checked before it is accepted; kernel messages and
shell echo cannot be mistaken for it. Logs stay in the session directory.

## Vehicle and dashboard test

```bash
bash scripts/board-uart.sh test SESSION
```

Sends speed/RPM values (0/0, 80/2500, 160/4800) and the current time through
the CAN input, checks the decoded values and the dashboard hands, then checks
that everything turns unknown after the frames stop. Results are in a
`test-*` directory of the session.

## Updating software

```bash
make package/vehicle/compile -j4 V=s
bash scripts/board-uart.sh install SESSION out/d1s-hz-b500_sd/packages/base/vehicle_*.ipk
bash scripts/board-uart.sh deploy SESSION \
    out/d1s-hz-b500_sd/compile_dir/target/rootfs/usr/bin/hz-ui /usr/bin/hz-ui hz-ui
bash scripts/board-uart.sh put SESSION ./file.bin
```

`install` copies a package and installs it with opkg, restarting its service.
`deploy` replaces a single executable (keeping a backup); use it for the large
HMI. `put` copies any file up to 8 MiB. Kernel and device-tree changes need a
new SD image.

`bash scripts/prepare-hardware-test.sh` builds a fresh SD image with the tools
these commands need (opkg, BusyBox base64/stty/sha256sum).

## Diagnostic scripts

| Script | Purpose |
| --- | --- |
| `board-uart-baud-test.sh` | Compare UART3 reception at different baud rates |
| `board-uart-line-test.sh` | UART3 checks with internal loopback |
| `board-uart-raw-test.sh` | Capture raw UART3 data before SLCAN parsing |
| `board-uart-video-overlay-test.sh` | Check that the video plane is cleared |

Host check of the tools: `bash tests/board-uart/check.sh`.
