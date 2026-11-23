# Setting up a fresh clone

This repository contains the complete Tina SDK for the HZ-B500 in **one Git
repository**: kernel, bootloader, OpenSBI, package recipes, board profile and
the added applications. No `repo` tool or submodules are needed. Vendor
revisions at export time are listed in [vendor-revisions.tsv](vendor-revisions.tsv).

Not in Git: compilers, the package download cache and all build output.

## Steps

1. Install the host packages from the [main README](../README.md#build-and-install).
2. Install the two compilers as described in [prebuilt/README.md](../prebuilt/README.md).
3. Optionally put package source archives into [dl/](../dl/README.md); otherwise
   the build downloads them (some old vendor URLs may be gone).
4. Build and pack:

```bash
bash scripts/import-sdk-assets.sh --check
source build/envsetup.sh
lunch d1s_hz_b500_sd-tina
m -j4
p
```

The result is `out/d1s-hz-b500_sd/tina_d1s-hz-b500_sd_uart0.img`; see the
[SD profile](../target/allwinner/d1s-hz-b500_sd/README.md) for writing it.
Android Auto needs additional Rust tools, see
[Android Auto](../package/multimedia/android-auto/README.md).

When an older SDK checkout exists, its compilers and download cache can be
copied instead:

```bash
bash scripts/import-sdk-assets.sh --from-sdk /path/to/existing-sdk --downloads
```

## Directory overview

| Directory | Content |
| --- | --- |
| `build/`, `config/`, `scripts/`, `toolchain/`, `tools/` | SDK build system (`toolchain/` holds recipes, not a compiler) |
| `lichee/` | Linux 5.4 kernel, Boot0 and OpenSBI |
| `device/`, `target/` | Board configuration and the SD profile |
| `package/` | Package recipes and the added applications |
| `tests/` | Host checks; their output goes to `out/` |
| `prebuilt/`, `dl/` | External compilers and download cache (ignored) |
| `out/` | Build output (ignored) |

The kernel configuration lives in
`device/config/chips/d1s/configs/hz-b500_sd/linux/config-5.4`, not in
`lichee/linux-5.4/.config`. Some vendor files are hidden by ignore rules; check
new files with `git check-ignore -v PATH`.
