#!/usr/bin/env bash
set -euo pipefail

PIN=9530b49e9e83b8a5dcd09a59e3b211bacc64f08d
REPO=https://github.com/KeyboardHoarders/vial-qmk.git
KB=sofle_choc
KM=sofle_choc_tp

cd "$(dirname "$0")"
ROOT="$PWD"

# Toolchain. macOS: keg-only Homebrew ARM GCC (the Homebrew qmk bottle is broken
# on this macOS, so it is not used). Linux: the distro's arm-none-eabi-gcc,
# arm-none-eabi-binutils and arm-none-eabi-newlib on PATH (pacman on CachyOS/Arch).
# Both: a project virtualenv holding the qmk CLI and the vendor tree's Python
# requirements. See README for setup.
if [ "$(uname -s)" = Darwin ]; then
    export PATH="/opt/homebrew/opt/arm-none-eabi-gcc@8/bin:/opt/homebrew/opt/arm-none-eabi-binutils/bin:/opt/homebrew/bin:$PATH"
    PYTHON=/opt/homebrew/bin/python3.15
    NPROC="$(sysctl -n hw.ncpu)"
else
    PYTHON="$(command -v python3)"
    NPROC="$(nproc)"
fi
if ! command -v arm-none-eabi-gcc >/dev/null; then
    echo "ERROR: arm-none-eabi-gcc not on PATH (Linux: pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-newlib)" >&2
    exit 1
fi
Q=vendor/vial-qmk

# libc. The pinned tree links ARM targets with newlib-nano (--specs=nano.specs),
# which Debian's gcc-arm-none-eabi no longer ships: it defaults to picolibc.
# Reuse the tree's own picolibc recipe (platform.mk has it, but only for
# RISC-V): link against picolibc, alias its heap symbols to ChibiOS's, define
# USE_PICOLIBC for syscall-fallbacks.c and forward-declare struct _reent for
# ChibiOS's syscalls.c (tools/picolibc_reent.h). macOS/Arch with newlib are
# untouched.
LIBC=()
if ! arm-none-eabi-gcc -print-file-name=nano.specs | grep -q / \
   && arm-none-eabi-gcc --specs=picolibc.specs -E - </dev/null >/dev/null 2>&1; then
    echo "note: no nano.specs, building against picolibc"
    LIBC=(
        TOOLCHAIN_CFLAGS=--specs=picolibc.specs
        TOOLCHAIN_LDFLAGS=-Wl,--no-wchar-size-warning   # link goes through CC, which already carries the specs
        TOOLCHAIN_LDSYMBOLS=-Wl,--defsym=__heap_start=__heap_base__,--defsym=__heap_end=__heap_end__
        "EXTRAFLAGS=-DUSE_PICOLIBC -include $ROOT/tools/picolibc_reent.h"
    )
fi

if [ "${1:-}" = "clean" ]; then
    if [ -d "$Q" ]; then make -C "$Q" clean; fi
    rm -rf out
    exit 0
fi

if [ ! -d "$Q" ]; then
    mkdir -p vendor
    git clone --depth 1 --branch vial "$REPO" "$Q"
    (
        cd "$Q"
        git fetch --depth 1 origin "$PIN"
        git checkout "$PIN"
        git submodule update --init --depth 1 --recursive lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf
    )
else
    HEAD_NOW="$(git -C "$Q" rev-parse HEAD)"
    if [ "$HEAD_NOW" != "$PIN" ]; then
        echo "WARNING: $Q is at $HEAD_NOW, expected $PIN" >&2
    fi
fi

# The virtualenv needs the vendor tree's requirements.txt, so it comes after the clone.
if [ ! -x .venv/bin/python ]; then
    if "$PYTHON" -c 'import ensurepip' 2>/dev/null; then
        "$PYTHON" -m venv .venv
        .venv/bin/python -m pip install --quiet --upgrade pip
        .venv/bin/python -m pip install --quiet -r "$Q/requirements.txt" qmk
    elif command -v uv >/dev/null; then
        # Debian's python3 has no ensurepip unless python3-venv is installed;
        # uv builds the same virtualenv without it (no pip inside, use uv pip).
        uv venv --quiet --python "$PYTHON" .venv
        uv pip install --quiet --python .venv/bin/python -r "$Q/requirements.txt" qmk
    else
        echo "ERROR: cannot create .venv: python3 lacks ensurepip (apt install python3-venv) and uv is not installed" >&2
        exit 1
    fi
fi
# shellcheck disable=SC1091
. .venv/bin/activate

ln -sfn "$ROOT/keymaps/$KM" "$ROOT/$Q/keyboards/$KB/keymaps/$KM"

MEASURE=0
EXTRA=()
if [ "${1:-}" = "measure" ]; then
    MEASURE=1
    EXTRA=(TP_MEASURE=yes)
fi

# The two variants share one output name and QMK does not rebuild when a make
# variable changes, so always start from a clean object dir and no stale .uf2.
rm -rf "$Q"/.build/*"${KB}_${KM}"* "$Q"/"${KB}_${KM}"*.uf2

make -C "$Q" "$KB:$KM" CONVERT_TO=rp2040_ce ${LIBC[@]+"${LIBC[@]}"} ${EXTRA[@]+"${EXTRA[@]}"} -j"$NPROC"

UF2="$(find "$Q" -maxdepth 2 -name "${KB}_${KM}*.uf2" -not -path '*/lib/*' -print0 | xargs -0 ls -t 2>/dev/null | head -n1)"
if [ -z "$UF2" ]; then
    echo "ERROR: no .uf2 found after build" >&2
    exit 1
fi

mkdir -p out
if [ "$MEASURE" = 1 ]; then
    STAMPED="out/measure_right_half_$(date +%Y%m%d-%H%M).uf2"
    cp "$UF2" "$STAMPED"
    cp "$UF2" out/measure_right_half.uf2
    ls -l "$STAMPED" out/measure_right_half.uf2
else
    STAMPED="out/${KM}_rp2040_ce_$(date +%Y%m%d-%H%M).uf2"
    cp "$UF2" "$STAMPED"
    cp "$UF2" out/latest.uf2
    ls -l "$STAMPED" out/latest.uf2
fi
