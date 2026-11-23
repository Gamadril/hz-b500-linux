# HZ-B500 / F133 hardware video sink

The optional `cedar` feature feeds Android Auto's elementary H.264 stream to
the Tina SDK's CedarC decoder and presents linear NV21 DMA buffers through
the display engine. It does not decode video in Rust, convert frames to RGB
or require Qt/egui/OpenGL. The configuration is **1280x720 at 30 fps with 256x120 margins** (the centred 1024x600 is shown).

`cedar-headunit` supports wired USB video and ALSA media/speech/system audio,
plus a raw-file video mode. The integrated SDK guide in the package's
`BUILDING.md` describes its private plug/dmix PCM, audio options, board routing and Goodix
evdev touchscreen input. It has no microphone capture. The desktop example
remains available separately. Audio/video operation is confirmed on the device.

## Board firmware prerequisites

Use the HZ-B500 Linux 5.4 Tina SDK matching the installed firmware. Header and
binary ABI compatibility matters: do not combine arbitrary Cedar releases or
Melis modules with these Linux libraries.

1. The kernel needs `CONFIG_VIDEO_ENCODER_DECODER_SUNXI=y`, ION/dma-buf/IOMMU
   support and the working LCD/display driver. The current port already enables
   these and contains the Cedar DMA mapping repair.
2. **Use `CONFIG_DISP2_SUNXI_COMPOSER=y` and rebuild the kernel.** The integrated
   SD profile enables it; DDR remains unchanged. The retained
   `platform/hz-b500-composer.patch` records the original standalone proposal
   and should not be applied to the integrated tree. Rebuild/boot the SD
   firmware using its normal instructions; source changes do not update an
   existing kernel image.
3. Build/install the matching `libcedarx` SDK package, including CedarC's
   `libvdecoder.so`, `libMemAdapter.so`, `libawh264.so` and their dependencies
   (`libVE`, `libvideoengine`, buffer managers and base libraries). This package
   is selected by the integrated SD profile. The SDK's package configuration also
   selects the allocator: use its Linux 5.4 IOMMU configuration for the SD port.
   Install its matching Cedar configuration file too. Loading only the H.264
   plugin avoids explicitly registering every other codec.
4. The target needs the USB host controller, writable `/dev/bus/usb` access,
   `/dev/disp`, `/dev/cedar_dev` and `/dev/ion`. Stop other display/composer
   clients first. The sink uses video channel 0/layer 0 by default; the BSP
   framebuffer normally uses channel 1. An already enabled chosen layer is
   rejected rather than overwritten. The fence timeline is shared per display,
   so other compositors must not use it concurrently.

Cargo and the receiver do not change the kernel configuration. The integrated
SD recipe/profile supply the required configuration and runtime libraries.

## Cross-build

Build on the host using recent Rust with edition-2024 support, `protoc`, a C
compiler, modern `binutils-riscv64-linux-gnu` and the board's matching Linux
cross compiler/sysroot:

```bash
rustup target add riscv64gc-unknown-linux-gnu
export CEDAR_SDK_ROOT=/path/to/hz-b500-linux
export CROSS_COMPILE=/path/to/riscv64-glibc-gcc-thead_20200702/bin/riscv64-unknown-linux-gnu-
bash tools/build-hz-b500.sh
```

Cargo compiles a small C bridge against the SDK's `vdecoder.h` and
`video/sunxi_display2.h`. No vendor struct layouts are duplicated in Rust.
For installed SDK headers instead of the source tree, supply `CEDAR_INCLUDE_DIR`
and `SUNXI_INCLUDE_DIR` and build with Cargo directly. The latter directory must
contain `video/sunxi_display2.h`; kernel headers are searched after system
headers to avoid accidentally including internal kernel types in userspace.

The Rust binary loads Cedar libraries at runtime, using the target loader's
normal search path. They are not linked into the host build. **A binary linked
using a modern distribution's RISC-V glibc may not run on the older Tina rootfs.**
Use the board compiler as shown. Output stays in
`$CEDAR_SDK_ROOT/out/android-auto/cargo/`; the helper uses the modern linker with the SDK compiler's
start files and libc. `CEDAR_LD` can select another modern RISC-V GNU linker.
The executable is
`$CARGO_TARGET_DIR/riscv64gc-unknown-linux-gnu/release/cedar-headunit`; copy the binary and
required target libraries to the board using your normal deployment procedure.

The crate's existing top-level `rustls-webpki` patch is retained for its built-in
TLS certificate. Applications consuming this crate from a separate workspace
must also carry that patch in their workspace root, as required by Cargo.

## Check on the target

Run from UART after stopping other GUI/composer applications:

```sh
ls -l /dev/disp /dev/cedar_dev /dev/ion
cedar-headunit --file /path/to/1280x720-annex-b.h264
cedar-headunit
```

The file must be an elementary Annex-B H.264 stream including SPS/PPS, not an
MP4 container and not Melis's Robin packet format. File mode is an unpaced
decode/display smoke test; it is not a timed movie player and does not flush
delayed end-of-stream frames. A live phone session supplies the stream and
negotiates 720p/30. Connect the phone by USB and accept its pairing prompts.
`--screen N`, `--channel N` and `--layer N` select the display plane; use only
the board's valid video-capable plane. Ctrl-C closes the active sink.

Record the full UART output, `/proc/meminfo`, application RSS, Cedar errors and
whether frames are actually visible. Host tests prove no physical decoding,
scanout, colour correctness, phone compatibility or 64 MiB memory budget.

## Buffer lifetime and failure handling

The sink requests NV21 from Cedar, obtains the DMA-buf fd from the memory
adapter, preserves decoded crop/stride, and letterboxes into the current LCD
dimensions. Pictures remain owned by the sink while display uses them.

This BSP retires HWC fence N only after submission N+1 reaches `sync_finish`.
The previous picture is returned to Cedar only after that fence signals.
Shutdown/hiding submits a disabled layer and retires remaining pictures before
freeing the decoder. `FBIO_WAITFORVSYNC` is a no-op in this kernel and is never
used as a substitute. Unknown BSP ioctls can return success, so the sink checks
the HWC output fields as well as the return value and fails before claiming a
layer on a kernel without the composer API.

Presented pictures use z-order 17, above the BSP framebuffer at 16. This matches
the existing HMI video's TOP plane and prevents a retained fbdev image from
covering AA video. The HMI tiles pause the HMI themselves; stop `hz-ui` before
a manual CLI start.

On a timeout, pictures potentially used by scanout remain allocated. The
receiver ends the session, and shutdown attempts to disable/retire the layer.
If retirement still fails, the native context remains pinned until process
exit; it is not returned to the decoder for reuse. Capture this failure before
restarting. Decoder progress and compressed packet sizes are bounded, and the
worker queue holds at most two commands. Cedar/ioctl work runs on one native
worker with a 512 KiB stack, outside the async executor.

## Host checks

```bash
export CEDAR_SDK_ROOT=/path/to/hz-b500-linux
bash tests/cedar/check.sh
CARGO_TARGET_DIR="$CEDAR_SDK_ROOT/out/android-auto/cargo-host" \
  cargo check --locked --bin cedar-headunit --features cedar
```

The native tests use real SDK headers with mocked decoder/device operations.
They check split ring-buffer copies, timestamps, fd zero, crop/scale, focus
changes, exact fence retirement order, timeout retention, missing-composer
detection and failure cleanup under AddressSanitizer/UBSan. The script also
compiles the bridge for RISC-V if the distribution cross compiler is installed.
Tests and Cargo outputs stay under the SDK's ignored `out/` directory.
