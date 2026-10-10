# sofle-choc-tp

A small QMK/Vial overlay for a Sofle Choc 2.1 (RP2040-CE controllers, built with `CONVERT_TO=rp2040_ce`) with an Azoteq IQS5xx trackpad on the right half and USB on the left half. It adds a custom keymap, `sofle_choc_tp`, on top of a pinned checkout of KeyboardHoarders/vial-qmk (commit 9530b49), which `build.sh` clones into `vendor/`. The keymap is a copy of the vendor `vial` keymap (7 layers) with three firmware changes.

How we know it is `vial`: a picotool full-flash image of the shipped firmware matched the `vial` build byte-for-byte, while `vialtrack` matched about 7%.

## What changed

Trackpad init retry. The Azoteq driver initialises once at boot and never retries. On the pad-side half that init only runs after the 2 s USB-detect wait, by which time the chip is idle and clock-stretches its replies until its next cycle ends; the vendor's 3 ms I2C timeout cut those off, leaving the pad dead until the next power cycle. The timeout is now 50 ms, and the keymap re-runs the init and verifies it by reading back the report rates the init sets, every 500 ms for up to 20 seconds. Tapping the pad within those 20 seconds also opens the chip's comms window and rescues it.

Natural scrolling. The scroll deltas from the pad are negated on the master in `pointing_device_task_user`, so macOS can stay on its default natural setting.

EEPROM preservation. Vial normally treats every new build as a different firmware (the EEPROM magic derives from a random BUILD_ID) and wipes the dynamic keymap, macros and Vial data. Because this keymap keeps the same EEPROM layout as `vial` (7 layers, same matrix, same features), `via_init_kb()` re-stamps the magic instead, so the layout stored via Vial survives the reflash. This build also writes a layout marker (`SCTP_LAYOUT_MARKER`) into the unused EECONFIG_USER word on every boot; the next build will make the re-stamp conditional on that marker matching, so a layout change cannot silently reuse incompatible data.

## Build

    ./build.sh

The first run clones the pinned vial-qmk into `vendor/` and creates a Python virtualenv in `.venv/` holding the `qmk` CLI and the vendor tree's requirements. Output goes to `out/sofle_choc_tp_rp2040_ce_<timestamp>.uf2` and `out/latest.uf2`. `./build.sh clean` cleans the build and removes `out/`.

Toolchain notes (macOS, Apple Silicon, October 2026): the Homebrew `qmk/qmk/qmk` formula pulls in the compilers but its own `qmk` bottle is broken (it references a Python that is not installed), so the script does not use it. It uses the keg-only `osx-cross/arm/arm-none-eabi-gcc@8` and `arm-none-eabi-binutils` directly, and `/opt/homebrew/bin/python3.15` for the virtualenv. If you set up a fresh machine:

    brew tap osx-cross/arm && brew trust osx-cross/arm
    brew install osx-cross/arm/arm-none-eabi-gcc@8 osx-cross/arm/arm-none-eabi-binutils python@3.15 picotool

## Before flashing

1. In Vial, use File > Save current layout and keep the .vil file. This is the guaranteed way to get the layout back (File > Load saved layout) if the EEPROM preservation does not work.
2. Optional but cheap: take a full flash image of each half with picotool while it is in the bootloader, before dragging the new .uf2 on. This captures the vendor firmware and the EEPROM region exactly as they are:

       picotool save -a out/backup_left.bin    # with the left half in RPI-RP2 mode
       picotool save -a out/backup_right.bin   # same for the right half

   Restore a half later with `picotool load out/backup_left.bin` while it is in the bootloader (`-a` is not needed for load; the image contains its own addresses).

## Flash

Both halves get the same .uf2. On each half, double-tap the reset button to enter the bootloader; an RPI-RP2 drive appears. Drag `out/latest.uf2` onto it and the half reboots by itself.

## Rollback

`out/rollback_vendor_vial_rp2040_ce.uf2` is the vendor `vial` keymap rebuilt from the same pinned tree. To rebuild it:

    make -C vendor/vial-qmk sofle_choc:vial CONVERT_TO=rp2040_ce

Note that the vendor keymap does not preserve EEPROM across builds, so flashing this rollback image wipes the stored layout; restore it from the .vil in Vial afterwards. The picotool backups above restore the exact original state instead.

## What to test after flashing

- The pad works without touching it at boot. Test 5 plug-ins, and again after the board has been unplugged for more than 30 seconds.
- Scroll direction feels natural (content follows the fingers).
- Taps and two-finger tap work.
- The keymap is intact (open Vial and compare with the saved .vil).
- RGB, OLED and encoders behave as before.
- If Vial shows a wrong or scrambled layout after flashing, the stored data did not match this build's layout; use File > Load saved layout with the .vil. If the board misbehaves badly, hold the top-left key of the left half while plugging it in (bootmagic) to clear EEPROM, then load the .vil.
