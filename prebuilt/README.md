# External toolchains (not included in Git)

Install the following SDK compiler versions under **prebuilt/** before
building. Compiler binaries and archives remain outside Git.

```text
prebuilt/
  gcc/linux-x86/riscv/toolchain-thead-glibc/
    riscv64-glibc-gcc-thead_20200702/
      bin/riscv64-unknown-linux-gnu-gcc
      sysroot/ ...
  bootloader/
    riscv64-linux-x86_64-20200528.tar.xz
```

Linux and userspace need the extracted T-Head GCC 8.1.0 distribution, including
its sysroot and libraries. Copying only its `bin/` directory is insufficient.
The matching archive is `riscv64-glibc-gcc-thead_20200702.tar.xz`; extract it
into `prebuilt/gcc/linux-x86/riscv/toolchain-thead-glibc/` so its top-level
`riscv64-glibc-gcc-thead_20200702/` directory matches the tree above.

Boot0/FES use the older RISC-V compiler. Place its original archive in
`bootloader/`, or place its fully extracted directory there with the same
basename. The SPL Makefile extracts a missing directory on first build.
`lichee/brandy-2.0/tools/toolchain` is a relative symlink to this directory.
The ARM compiler is not required. U-Boot has been removed.

OpenSBI separately uses the host-installed `riscv64-linux-gnu-gcc` compiler
(Debian/Ubuntu package `gcc-riscv64-linux-gnu`).

To install from archives, run these commands from the repository root. Replace
the archive directory with the location of the two matching files:

```bash
TOOLCHAIN_ARCHIVES=/path/to/toolchain-archives
mkdir -p prebuilt/gcc/linux-x86/riscv/toolchain-thead-glibc prebuilt/bootloader
tar -xJf "$TOOLCHAIN_ARCHIVES/riscv64-glibc-gcc-thead_20200702.tar.xz" \
    -C prebuilt/gcc/linux-x86/riscv/toolchain-thead-glibc
cp "$TOOLCHAIN_ARCHIVES/riscv64-linux-x86_64-20200528.tar.xz" prebuilt/bootloader/
bash scripts/import-sdk-assets.sh --check
```

The check also requires the host-installed OpenSBI cross compiler described
above. No previous SDK checkout is needed for this installation method.

If a compatible SDK is already available, the optional import helper can copy
its installed toolchains instead:

```bash
bash scripts/import-sdk-assets.sh --from-sdk /path/to/existing-sdk
# Also restore the existing source download cache, useful with old vendor URLs:
bash scripts/import-sdk-assets.sh --from-sdk /path/to/existing-sdk --downloads
bash scripts/import-sdk-assets.sh --check
```

The helper copies locally; it does not download or delete the source SDK.
Imported files remain ignored. See [source setup](../build/SOURCE_SETUP.md).
