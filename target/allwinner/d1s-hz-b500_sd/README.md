# HZ-B500 SD profile

The only Linux profile (`lunch d1s_hz_b500_sd-tina`). Linux boots from the SD
card; stock Melis stays untouched in the internal NOR flash (dual boot).

## Build and write

```bash
source build/envsetup.sh
lunch d1s_hz_b500_sd-tina
m -j4
p
sudo dd if=out/d1s-hz-b500_sd/tina_d1s-hz-b500_sd_uart0.img of=/dev/sdX bs=4M conv=fsync
```

`p` produces exactly one raw image, `out/d1s-hz-b500_sd/tina_d1s-hz-b500_sd_uart0.img`
(about 551 MiB), replaced on every build. Packing never writes a card.
`bash tests/sd-rootfs/layout.sh` checks a built image.

## Card layout

| Region | Location | Content |
| --- | --- | --- |
| Boot0 | 8 KiB (copy at 128 KiB) | First-stage loader with DRAM/UART/SD settings |
| Boot package | 12 MiB (copy at 16 MiB + 16 KiB) | OpenSBI, device tree and Linux |
| Partition 1 | from 36 MiB, 512 MiB | ext4 root filesystem, mounted read-write |
| Partition 2 | after the root, 3 MiB | Raw boot logo (1024×600) |

The rest of the card stays unallocated. The kernel command line is
`root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw`; boot logs and the login
shell use UART0 at 115200 baud, the LCD shows only the boot logo and the HMI.

The image is sized for a 4 GB card (7,698,432 sectors, stored in
`device/config/chips/d1s/configs/hz-b500_sd/sd-card-sectors`). For another card,
pass its real sector count (`sudo blockdev --getsz /dev/sdX`):

```bash
SD_CARD_SECTORS=7698432 p
```

## Boot logo

A fresh clone shows a generated text logo; a local
`device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg` (not in Git)
replaces it. `SD_BOOT_LOGO=none p` disables the logo; see the
[boot logo notes](../../../device/config/chips/d1s/configs/hz-b500_sd/assets/README.md)
for custom artwork. On the device, `boot-logo install IMAGE.jpg` replaces it for
the next boot. Linux takes over the running display without a black flash.

## Board settings

- **SD card:** runs at 15 MHz, 4-bit, default speed. Card initialization falls
  back from 400 kHz down to 100 kHz, which some cards need.
- **Touch:** Goodix GT968 on TWI1 (`0x5d`), no axis reversal.
- **USB:** USB0 in host mode (keyboard, storage, phone for Android Auto);
  USB1 and gadget mode are disabled.
- **Audio:** F133 headphone output feeds the AB5301B module; see the
  [AB5301B guide](../../../package/utils/ab5301b/README.md).
  `/usr/share/sounds/sample-1mb.wav` is a test sample.
