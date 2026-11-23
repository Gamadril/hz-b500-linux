#!/usr/bin/env bash
set -Eeuo pipefail
mpg_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
mpg_archive="$mpg_root/dl/mpg123-1.33.7.tar.bz2"
mpg_out="$mpg_root/out/mpg123"
case "${1:-}" in
    --fetch-deps)
        if [[ ! -f "$mpg_archive" ]]; then
            curl --fail --location --max-time 60 https://www.mpg123.de/download/mpg123-1.33.7.tar.bz2 -o "$mpg_archive"
        fi;;
    '') ;;
    *) printf 'Usage: %s [--fetch-deps]\n' "$0" >&2;exit 2;;
esac
printf '%s  %s\n' 31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee "$mpg_archive" | sha256sum --check --status
pkg-config --exists alsa
mkdir -p "$mpg_out/source" "$mpg_out/host"
[[ -d "$mpg_out/source/mpg123-1.33.7" ]] || tar -xjf "$mpg_archive" -C "$mpg_out/source"
cd "$mpg_out/host"
if [[ ! -f Makefile ]]; then
    "$mpg_out/source/mpg123-1.33.7/configure" --prefix="$mpg_out/host/prefix" \
        --disable-modules --with-audio=alsa --disable-network --disable-static >configure.log 2>&1
fi
make -j4 >build.log 2>&1
make install >install.log 2>&1
printf 'Built host mpg123 libraries and CLI: %s\n' "$mpg_out/host/prefix"
