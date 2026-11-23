# SD boot artwork

A local `linux_boot.jpg` can override the default HZ-B500/Linux text splash.
That supplied 1024×600 artwork is ignored by Git; no redistribution notice
accompanied it. A fresh clone uses the generated splash and needs no image file.

Packing converts a local override to the framed ARGB8888 format under `out/`.
For other JPEG/PNG artwork, run from the project root:

```bash
bash scripts/convert-boot-logo.sh /path/to/artwork.jpg out/boot-logo.bin
SD_BOOT_LOGO="$PWD/out/boot-logo.bin" p
```

`SD_BOOT_LOGO=none p` disables the splash partition payload. Keep rights and
attribution for any artwork you choose separately from the firmware source.
