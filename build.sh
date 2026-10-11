#!/usr/bin/env bash
set -euo pipefail

PIN=9530b49e9e83b8a5dcd09a59e3b211bacc64f08d
REPO=https://github.com/KeyboardHoarders/vial-qmk.git
KB=sofle_choc
KM=sofle_choc_tp

cd "$(dirname "$0")"
ROOT="$PWD"

# Toolchain: keg-only Homebrew ARM GCC, plus a project virtualenv holding the
# qmk CLI and the vendor tree's Python requirements (the Homebrew qmk bottle
# is broken on this macOS, so it is not used). See README for setup.
export PATH="/opt/homebrew/opt/arm-none-eabi-gcc@8/bin:/opt/homebrew/opt/arm-none-eabi-binutils/bin:/opt/homebrew/bin:$PATH"
if [ ! -x .venv/bin/python ]; then
    /opt/homebrew/bin/python3.15 -m venv .venv
    .venv/bin/python -m pip install --quiet --upgrade pip
    .venv/bin/python -m pip install --quiet -r vendor/vial-qmk/requirements.txt qmk
fi
# shellcheck disable=SC1091
. .venv/bin/activate
Q=vendor/vial-qmk

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

make -C "$Q" "$KB:$KM" CONVERT_TO=rp2040_ce ${EXTRA[@]+"${EXTRA[@]}"} -j"$(sysctl -n hw.ncpu)"

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
