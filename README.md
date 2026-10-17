# sofle-choc-tp

A small QMK/Vial overlay for a Sofle Choc 2.1 (RP2040-CE controllers, built with `CONVERT_TO=rp2040_ce`) with an Azoteq IQS5xx trackpad on the right half and USB on the left half. It adds a custom keymap, `sofle_choc_tp`, on top of a pinned checkout of KeyboardHoarders/vial-qmk (commit 9530b49), which `build.sh` clones into `vendor/`. The keymap is a copy of the vendor `vial` keymap (7 layers) with the firmware changes below.

How we know it is `vial`: a picotool full-flash image of the shipped firmware matched the `vial` build byte-for-byte, while `vialtrack` matched about 7%.

## What changed

Own pointing driver. `tp_driver.c` is a `POINTING_DEVICE_DRIVER = custom` driver that reproduces the vendor Azoteq report logic (taps, two-finger tap, scroll, movement) but wraps the vendor driver's I2C helpers (`drivers/sensors/azoteq_iqs5xx.c` is still linked, only its helper functions are used). It owns the init retry and polls the chip at most once per 10 ms report period.

Trackpad init retry. The Azoteq driver initialises once at boot and never retries. On the pad-side half that init only runs after the 2 s USB-detect wait, by which time the chip is idle and clock-stretches its replies until its next cycle ends; the vendor's 3 ms I2C timeout cut those off, leaving the pad dead until the next power cycle. The timeout is now 50 ms, and `tp_driver.c` re-runs the init and verifies it by reading back the report rates the init sets, every 500 ms for up to 20 seconds. Tapping the pad within those 20 seconds also opens the chip's comms window and rescues it.

Sequence number and dedup. The master polls the slave (which holds the pad) roughly every 1 ms, but the chip produces a frame only every 10 ms. Without help, the master either samples the same frame twice (doubled scroll and motion) or, when throttled to the chip's period, can skip a frame (jittery scroll, missed taps). The driver stamps every fresh frame with a non-zero sequence number in the otherwise unused `report_id` field of the split mouse report. `pointing_device_task_user` on the master samples every scan (`POINTING_DEVICE_TASK_THROTTLE_MS 1`), zeroes motion and scroll when it sees a sequence number it already consumed (buttons are kept, they are levels), and clears `report_id` before the report goes to the host. Requires `MOUSE_SHARED_EP` (the default); the build fails otherwise. `TP_SEQ_DEDUP` in `config.h` switches the dedup off.

Firmware gestures (build 2). The chip's tap, two-finger-tap and press-and-hold gestures are disabled in `config.h` (swipe and zoom stay off, the chip's scroll gesture stays on); `tp_gesture()` in `tp_driver.c` decides everything else from the chip's per-frame finger count and relative/absolute position. Measured on the board (chip IQS572, 1024x1024 units over 43x40 mm, about 24 units/mm): deliberate taps last 90-155 ms (median 130) with drift up to 26 units, so the chip's 110 ms tap window missed most of them; its press-and-hold never arms on press-and-move and does arm on a resting finger, clicking on lift (a false click). Behaviour:
- Tap: one finger, up within `TP_TAP_MAX_MS` and drifted less than `TP_TAP_MAX_DRIFT` units, is a left click (a tap that rolled 13-40 units still clicks). Two fingers (no wheel lines emitted, little motion, up within `TP_MULTI_TAP_MAX_MS`) is a right click; three fingers is a middle click. `TP_MULTI_TAP_MAX_MOTION` is the total relative motion allowed during a multi-finger tap. Clicks are held for `TP_CLICK_FRAMES` frames (10 ms each).
- Dead zone: `TP_DEAD_ZONE_UNITS` (12, ~0.5 mm). The chip used to suppress motion at touchdown as part of its tap/hold gestures; now the firmware does it. The cursor does not move until the touch has drifted this far, then follows from that frame on (the suppressed delta is not replayed). Set it to 0 for no dead zone. After a multi-finger gesture, a finger left on the pad has its first two frames ignored (the count change twitches), then points normally.
- Drag: arming needs stillness: the finger must stay within `TP_HOLD_STILL_DRIFT` (12 units, ~0.5 mm; a resting finger measured 5 in 250 ms) for `TP_HOLD_MS`. Once armed, the drag starts when the finger moves more than `TP_DRAG_START_UNITS` (16) from a reference position that is re-taken every 200 ms; resting creep measured at most 7 units per 200 ms, a real drag passes 16 easily. The left button goes down with zero motion on that frame so the host sees mouseDown before motion, and the drag lasts until lift. A finger that rests and lifts never clicks or drags.
- Drag robustness: a drag ends only after two consecutive empty frames (one dropped frame does not release the button). Single I2C read failures keep the current buttons; after 10 consecutive failures (about 100 ms) all buttons are released and the gesture session is dropped.
- Scroll: the chip's two-finger scroll deltas feed an accumulator, one wheel line per `TP_SCROLL_UNITS_PER_LINE` units (default 8, about 0.33 mm of travel; measured deltas were 1-2 units/frame slow and 10-36 fast; the old 1 unit = 1 line was fast but jumpy, 24 would be far too slow). Try 4, 8 or 12. Scroll is sent in batches: lines accumulate and go out as one report every `TP_SCROLL_BATCH_FRAMES` fresh frames (default 4 = 40 ms, 25 Hz) carrying at most `TP_SCROLL_MAX_LINES_PER_REPORT` (8) lines, with the remainder carried over; the tail is flushed on lift and a direction reversal drops the pending lines. Reason: macOS wheel acceleration keeps the last ~8 scroll events per axis and scales the gain by their inter-event gap and count, so dense single-line ticks every 10 ms ramp to roughly 3.3x within half a second; batching to 25 Hz flattens the ramp to roughly 1.6x. The cadence is steady (the counter ticks on every frame of a session; empty batches send 0). If the ramp is still too strong: `defaults write -g com.apple.scrollwheel.scaling -1` disables wheel acceleration for mouse-type devices only (needs a replug or logout; the built-in trackpad uses a different path and is unaffected; undo with `defaults delete -g com.apple.scrollwheel.scaling`). To test per device, live: `hidutil property --matching '{"VendorID":0x424C,"ProductID":0x5343}' --set '{"HIDSupportsScrollAcceleration":false}'` (immediate, lost on replug). Direction is natural scrolling, applied on the pad half in `tp_driver.c` (the master no longer inverts). Three or more fingers suppress scroll for the session (reserved for swipes).
- Cursor motion is unchanged: it follows one finger immediately on touch. If that ever shows a dead zone or jitter at touchdown, the fallback is to re-enable the chip tap gesture and ignore its bit.

EEPROM preservation. Vial normally treats every new build as a different firmware (the EEPROM magic derives from a random BUILD_ID) and wipes the dynamic keymap, macros and Vial data. Because this keymap keeps the same EEPROM layout as `vial` (7 layers, same matrix, same features), `via_init_kb()` re-stamps the magic instead, so the layout stored via Vial survives the reflash. The re-stamp is conditional: it only happens when the layout marker (`SCTP_LAYOUT_MARKER`, stored in the unused EECONFIG_USER word and written on every boot) matches this build's marker. Change the marker whenever the EEPROM layout changes; the next flash then resets the EEPROM as stock Vial would, and the saved .vil restores the layout. A board that has never run a marker-writing build (no marker stored) is also reset once.

## Build 3

Three-finger swipes. A session that reaches three fingers sums the chip's relative motion over the settled three-finger frames only (the 2 frames after any count change are skipped, so the staggered landing does not count). Once three fingers have been seen, frames with two or three fingers count, so a finger sliding off the edge mid-swipe does not stop the sum. If within `TP_SWIPE3_MAX_MS` (700) the horizontal travel reaches `TP_SWIPE3_MIN_UNITS_X` (100, about 4 mm) and exceeds the vertical travel, or the vertical travel reaches `TP_SWIPE3_MIN_UNITS_Y` (200, about 8 mm) and exceeds twice the horizontal, the slave fires one swipe code per session (BUTTON4-7 for `TP_CLICK_FRAMES` frames). The master (`pointing_device_task_user` in `keymap.c`) acts on the rising edge of those bits, taps the mapped key, and strips the bits so the host never sees them. A session that swiped never produces the three-finger middle click, and scroll stays suppressed for three fingers.

| Finger moves | Code | Default key (`config.h`) | macOS action |
|---|---|---|---|
| right | BUTTON5 | `TP_SWIPE_RIGHT_KEY` = `LALT(KC_LEFT)` | Space to the left (like the macOS four-finger swipe) |
| left | BUTTON4 | `TP_SWIPE_LEFT_KEY` = `LALT(KC_RGHT)` | Space to the right |
| up | BUTTON6 | `TP_SWIPE_UP_KEY` = `KC_MISSION_CONTROL` | Mission Control |
| down | BUTTON7 | `TP_SWIPE_DOWN_KEY` = `LCTL(KC_DOWN)` | App Expose |

The defaults emulate the macOS four-finger swipes. Up and down rely on the default Mission Control shortcuts (System Settings > Keyboard > Keyboard Shortcuts > Mission Control); left and right send Alt+Left/Right since build 9 (they were Ctrl+Left/Right up to build 8), so "Move left/right a space" must be rebound from Ctrl+arrow to Option+arrow in the same settings pane. To change an action, edit the four `TP_SWIPE_*_KEY` defines in `config.h` (any keycode `tap_code16()` accepts); for browser back/forward use `LGUI(KC_LBRC)` / `LGUI(KC_RBRC)`. If up and down feel inverted, swap the UP and DOWN keys. The tunables are `TP_SWIPE3_MIN_UNITS_X` and `TP_SWIPE3_MIN_UNITS_Y` (smaller = a shorter swipe fires; horizontal is lower because three fingers leave little room on a 43 mm wide pad) and `TP_SWIPE3_MAX_MS`. The measurement build prints `E swipe L/R/U/D` when one fires.

EEPROM hook fix. Build 2's `via_init_kb()` tested `eeconfig_is_enabled()`, which in this tree also requires `via_eeprom_is_valid()`. On a new build that is false until this very hook re-stamps it, so the condition could never be true and the stored layout was wiped on flashing. The condition is now only `eeconfig_read_user() == SCTP_LAYOUT_MARKER && !via_eeprom_is_valid()`. A blank EEPROM has no marker and initialises normally; a deliberate reset (bootmagic, `QK_CLEAR_EEPROM`, Vial reset) runs `eeconfig_init`, which zeroes the marker. `via_init()` runs before `quantum_init()` in `keyboard_init()`, so the re-stamp happens first. Note the build-2 flash that wiped the layout also wrote the marker, so the next flash of build 3 should preserve the layout (load the .vil once after the build-2 wipe first).

## Build 4

OLED and baked-in layout; the trackpad code is unchanged.

Left OLED. `oled_task_user()` in `keymap.c` takes over the master display: "kznjk" rotated down the top 96 px (`oled_name.h`, a 32x96 bitmap in the driver's vertical-byte format), a 2 px rule, then `Layer` and the active layer's name (`tp_layer_name()`: 0 kbd, 1 10key, 2 mods, 3 rgb, 4-6 L4-L6). The right half keeps the vendor logo. The vendor's Caps Lock line and Qwrt/Clmk/Lower/Raise labels are gone. The 60 s OLED sleep is unchanged. To change the word or font, run `.venv/bin/python tools/make_oled_name.py --text ... [--font path --size N --rotate 90|-90]`; it rewrites `oled_name.h` and `layout/oled_preview.png`, then rebuild.

Baked layout. `keymaps[]` and `encoder_map[]` are now the layout that was on the board on 2026-10-11, read over the Vial protocol with `tools/dump_layout.py` (read-only; it refuses to send any set/reset command). Vial's EEPROM copy still wins day to day and the `via_init_kb` re-stamp still preserves it across flashes; the baked copy is what a bootmagic/EEPROM reset or a Vial "reset" comes back to, so a reset no longer lands on the seller's layout. Re-dump after changing the layout in Vial:

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib .venv/bin/python tools/dump_layout.py

then paste `current_layout_keymap.c.txt` over the two arrays in `keymap.c`. The dump also covers macros, tap dances, combos, key overrides and QMK settings (`current_layout.json`); all were empty on 2026-10-11, so nothing else is baked in. Since build 10 the dumps live per board (see Build 10): pass `--out layout/<board>`.

## Build 5

Idle behaviour swapped. The OLED no longer blanks after 60 s (`OLED_TIMEOUT 0`); instead, after `TP_IDLE_MS` (60000) without any key, encoder or trackpad input the RGB goes dark (`RGB_MATRIX_TIMEOUT`) and both OLEDs dim to `OLED_IDLE_BRIGHTNESS` (1) in `oled_task_user()`. Any input restores both. `SPLIT_ACTIVITY_ENABLE` forwards the master's activity timestamps to the right half so both halves time out and wake together. `RGB_MATRIX_SLEEP` turns the LEDs off during USB suspend; the vendor's `RGB_DISABLE_WHEN_USB_SUSPENDED` is not read by this QMK version, so the lights previously stayed on while the host slept.

## Build 6

Host-side gestures via LinearMouse plus the firmware support they need.

Discrete horizontal scroll. LinearMouse maps Cmd/Ctrl + horizontal wheel on the pad to back/forward and tab switching (`~/.config/linearmouse/linearmouse.json`, `buttons.mappings` with `wheel` triggers), one action per wheel tick. While a modifier in `TP_DISCRETE_H_MODS` (Cmd, Option, Ctrl) is held, the master passes a single +/-1 horizontal tick and drops the rest until the pad has produced no horizontal or vertical tick for `TP_DISCRETE_H_GAP_MS` (250), so one swipe is one action and a slightly diagonal Cmd+scroll pinch zoom cannot navigate. Vertical scroll is never limited.

Encoder tabs. `process_record_user` intercepts Left/Right coming from an encoder while Ctrl is held and sends Ctrl+Tab / Ctrl+Shift+Tab instead, so Ctrl + the right encoder switches tabs like Ctrl + a sideways swipe. Real arrow keys with Ctrl still switch Spaces.

OLED after host sleep. Suspend turns the OLED off and wake only restores the RGB; the vendor relied on `OLED_TIMEOUT`'s activity path to turn it back on, which build 5 compiled out. `suspend_wakeup_init_user` now calls `oled_on()`, with a safety net in `oled_task_user`.

## Linux (Niri + DankMaterialShell)

See [docs/linux-niri-dms.md](docs/linux-niri-dms.md): what each gesture sends on a Linux host, the Niri binds that reproduce the macOS behaviour (installed on the Linux box as `~/.config/niri/sofle.kdl`, copy in `host/niri-sofle.kdl`), the scroll-direction fix of build 8, and `tools/hid_watch.py` for checking what the board sends.

## Host side (Niri, Linux)

`host/niri-sofle.kdl` is a copy of `~/.config/niri/sofle.kdl`, included from `config.kdl`: the three-finger swipe binds (Super+Alt chords and XF86LaunchA to columns and the Overview). Super + sideways swipe is covered by DMS's existing Mod+WheelScroll binds; Ctrl + sideways swipe is handled in the firmware on Linux (no bind). Details in `docs/linux-niri-dms.md`. Installed on two Linux boxes (CachyOS and Debian, both Niri 26.04 + DMS); setting up another one is `cp host/niri-sofle.kdl ~/.config/niri/sofle.kdl`, append `include "sofle.kdl"` after the DMS includes at the end of `config.kdl`, and `niri validate`.

## Host side (LinearMouse, home Mac only)

`host/linearmouse.json` is a copy of `~/.config/linearmouse/linearmouse.json` (LinearMouse 0.12.0). The pad's scheme (matched by VID/PID/serial) holds:

- `scrolling.modifiers.vertical.command = pinchZoom`: Cmd + two-finger vertical scroll is a real pinch zoom.
- `scrolling.smoothed.horizontal` (linear, no inertia) and `scrolling.reverse.horizontal = true`: horizontal wheel ticks become phased, continuous scroll events, which is enough for Safari/Chrome swipe-to-navigate to trigger. It is not 1:1: LinearMouse ends the synthetic gesture 40 ms after the last tick, so the page cannot be held half-slid ("peek"), and the firmware's 40 ms horizontal batching sits right at that edge.
- `buttons.mappings`: Cmd + wheel right/left = Cmd+[ / Cmd+] (back/forward), Ctrl + wheel right/left = previous/next tab. The firmware's `TP_DISCRETE_H_MODS` logic makes each swipe one tick so these fire once. The 0.12.0 GUI did not list these wheel-triggered mappings even though they work; edit the JSON.

Known limit and the way past it: a wheel can never say "fingers still down", so a true trackpad-like swipe with peek needs the firmware to stream touch state over the raw HID channel (it already carries Vial) to a host helper that posts phased scroll/gesture events itself. The user plans to write such a helper (possibly a LinearMouse fork); the firmware side would be a raw HID report per frame, enabled only after the helper says hello so Vial is never disturbed.

## Build 7

Host-OS-aware swipe keys. `OS_DETECTION_ENABLE` (QMK's USB enumeration fingerprint) lets `pointing_device_task_user` pick the three-finger swipe key set: macOS/iOS/unknown use Alt+Left/Right (Ctrl+Left/Right up to build 8), Mission Control, Ctrl+Down; Linux uses Super+Alt+Left/Right/Down with the Mission Control key unchanged (`TP_SWIPE_*_KEY_LINUX`, bindable in Niri without stealing Ctrl+arrow word movement; see `docs/linux-niri-dms.md`); Windows uses Ctrl+Win+Left/Right and Win+Tab (`TP_SWIPE_*_KEY_WIN`, untested). Detection settles about a quarter second after enumeration; before that the macOS set is used. The left OLED shows the detected OS (`mac`, `linux`, `win`, `ios`, `os ?`) under the layer name. Detection runs only on the USB half and re-runs on every replug or host reboot, not on a KVM switch that keeps the device enumerated.

## Build 9

Three-finger swipe left/right on macOS now send Alt+Right / Alt+Left (`TP_SWIPE_LEFT_KEY` / `TP_SWIPE_RIGHT_KEY`, were Ctrl+Right / Ctrl+Left). Rebind "Move left a space" / "Move right a space" in System Settings > Keyboard > Keyboard Shortcuts > Mission Control from Ctrl+arrow to Option+arrow, or the swipes do nothing. The Linux (Super+Alt) and Windows (Ctrl+Win) sets, the up/down keys and the encoder's Ctrl+Tab are unchanged.

Horizontal scroll flipped on macOS. The build 8 per-OS flip now has a macOS pair too (`TP_SCROLL_FLIP_V_MAC 0`, `TP_SCROLL_FLIP_H_MAC 1`), used for macOS, iOS and a not-yet-detected host: vertical stays natural as the pad sends it, horizontal is reversed. This flip happens before LinearMouse sees the wheel, so on the Mac check `scrolling.reverse.horizontal` in `~/.config/linearmouse/linearmouse.json` (set to true in build 6; with the firmware flip it would reverse horizontal back again) and the Cmd + wheel left/right back/forward mappings, whose sides swap with the tick sign.

Baked layout refreshed. `keymaps[]` and `encoder_map[]` are the layout read from the board on 2026-10-16 (`layout/current_layout.json`; the 2026-10-11 dump is kept in `layout/backup-20261016/`). The two tap dances and two macros in that layout live in Vial's EEPROM only and are not baked.

## Build 10

Baked layout is the Debian-box board's. `keymaps[]` and `encoder_map[]` are the layout read from that board on 2026-10-17 (`layout/debian/current_layout.json`): home-row mods on S/D/F and J/K/L/;, Delete bottom-left, thumbs TD(1) / MO(2) / MO(3) / Backspace / Enter and MO(3) / Space / `[` / `]` / Hyper+Tab, layer 1 a right-hand numpad with Home/End/PgUp/PgDn, layer 2 F-keys and symbols, layer 3 nav and editing. TD(1) (tap MO(1), double-tap TG(1)) is a Vial tap dance in EEPROM and is not baked, so after an EEPROM reset that thumb key is dead until it is re-created in Vial. The QMK settings in Vial are per board too and are not baked (on this board: permissive hold on, tapping term 200 ms, as of 2026-10-17).

Two boards, two layouts. The two keyboards are used for different things and keep different layouts on purpose. Each board's dump lives in its own directory: `layout/debian/` (the board on the Debian box) and `layout/cachyos/` (the board on the CachyOS box, dump of 2026-10-16, the layout build 9 baked). Re-dump with `.venv/bin/python tools/dump_layout.py --out layout/<board>` and diff the two `current_layout_keymap.c.txt` files to see how they differ. The firmware is the same for both; only the baked fallback follows the Debian board. `layout/backup-20261016/` keeps the 2026-10-11 dump.

Linux horizontal scroll no longer flipped. Build 8 flipped both axes back on a Linux host; on 2026-10-17 the two axes disagreed on the Debian box, so `TP_SCROLL_FLIP_H_LINUX` is now 0 and only vertical is flipped there. macOS and Windows are unchanged.

## Build 8

Scroll direction per host OS. The pad half applies natural scrolling for every host (unchanged); `pointing_device_task_user` now flips an axis back per detected OS, before the discrete-horizontal logic. Vertical came out backwards on Linux, so `TP_SCROLL_FLIP_V_LINUX 1`; horizontal likewise (`TP_SCROLL_FLIP_H_LINUX 1`), both confirmed on the Linux box; Windows mirrors Linux, untested. Set all four to 0 for build 7 behaviour.

Ctrl + sideways swipe on Linux. A Niri bind cannot type into the focused app, so on a Linux host the firmware taps Tab / Shift+Tab itself while Ctrl is held, as the encoder does, and swallows the wheel tick (`TP_CTRL_SWIPE_TABS_LINUX`; `TP_CTRL_SWIPE_NEXT_TAB_H` picks which tick sign is "next"). macOS keeps the LinearMouse wheel-tick path.

Left OLED relaid out. The name bitmap now starts on the first pixel row (`tools/make_oled_name.py`, 9 text rows, regenerated with `--reuse` so the pixels are the ones from build 4), then a blank row, a 16 px white rounded box with the detected host OS knocked out of it (`macOS`, `linux`, `win`, `ios`, `?`; bitmaps in `oled_name.h`, built from the OLED's own 6x8 font), a blank row, `Layer` and the layer name. The rule is gone. Every line is placed with `oled_set_cursor` and padded by hand (`tp_oled_line`): `oled_write_ln` on a line that exactly fills the 5 columns ("Layer", "linux", "10key") wraps to the next row and clears it with spaces, which is why on a Linux host the OS line landed on row 0 and wiped the top of the k. The generator needs Pillow (`.venv/bin/pip install pillow`) and the vendor tree (for `glcdfont.c`).

Also: `build.sh` runs on Linux (distro arm-none-eabi toolchain, `nproc`), `tools/hid_watch.py` decodes what the board sends from its hidraw nodes (Linux, no root needed with the Vial udev rule), and the Niri binds are installed and copied to `host/niri-sofle.kdl`. Not yet built: this box has no ARM toolchain installed (see Build).

## Build

    ./build.sh

The first run clones the pinned vial-qmk into `vendor/` and creates a Python virtualenv in `.venv/` holding the `qmk` CLI and the vendor tree's requirements. Output goes to `out/sofle_choc_tp_rp2040_ce_<timestamp>.uf2` and `out/latest.uf2`. `./build.sh clean` cleans the build and removes `out/`.

Toolchain notes (macOS, Apple Silicon, October 2026): the Homebrew `qmk/qmk/qmk` formula pulls in the compilers but its own `qmk` bottle is broken (it references a Python that is not installed), so the script does not use it. It uses the keg-only `osx-cross/arm/arm-none-eabi-gcc@8` and `arm-none-eabi-binutils` directly, and `/opt/homebrew/bin/python3.15` for the virtualenv. If you set up a fresh machine:

    brew tap osx-cross/arm && brew trust osx-cross/arm
    brew install osx-cross/arm/arm-none-eabi-gcc@8 osx-cross/arm/arm-none-eabi-binutils python@3.15 picotool

Toolchain notes (Linux, CachyOS/Arch, October 2026): the script uses whatever `arm-none-eabi-gcc` is on PATH and the system `python3` for the virtualenv. One-time setup (picotool is in the AUR; it is optional, see Flash):

    sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-newlib
    paru -S picotool

Toolchain notes (Linux, Debian forky, October 2026): `gcc-arm-none-eabi` 15.3 from apt builds the tree unchanged, with two differences the script handles on its own. Debian ships picolibc instead of newlib for `arm-none-eabi` (no `nano.specs` at all), so when `nano.specs` is missing and `--specs=picolibc.specs` works, `build.sh` passes the tree's own picolibc recipe (the one `platform.mk` only applies to RISC-V) as make overrides: `--specs=picolibc.specs` in `TOOLCHAIN_CFLAGS`, `TOOLCHAIN_LDFLAGS` without `nano.specs`, the `__heap_start`/`__heap_end` aliases, and `EXTRAFLAGS=-DUSE_PICOLIBC -include tools/picolibc_reent.h` (a forward declaration of `struct _reent` for ChibiOS's `syscalls.c`, which has no picolibc guard). Debian's `python3` has no `ensurepip` unless `python3-venv` is installed, so when it is missing the script creates `.venv` with `uv` instead (install `uv` or `python3-venv`). One-time setup:

    sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi picolibc-arm-none-eabi picotool
    # plus either: sudo apt install python3-venv    or    uv on PATH

The vendor tree is pinned to a 2024 QMK that was built with GCC 8 on the Mac; the Arch GCC is 16 and the Debian one 15.3 (which built it on 2026-10-11 without any `-Wno-...`). If the build fails on a new warning-as-error or a newer-GCC quirk, the fix is usually a `-Wno-...` in `EXTRAFLAGS` or the Arch `arm-none-eabi-gcc` of an older release from the Arch Linux Archive; note it here when it happens.

## Measurement build

    ./build.sh measure

builds a variant with `TP_MEASURE=yes`: the QMK console is enabled and the right half is the master (`MASTER_RIGHT`), so it works on its own over USB. It writes `out/measure_right_half.uf2` (and a timestamped copy); `out/latest.uf2` is not touched.

1. Flash `out/measure_right_half.uf2` to the RIGHT half only.
2. Disconnect the TRRS cable and plug USB into the right half.
3. From the project directory: `. .venv/bin/activate && qmk console`.
4. Touch the pad. After startup it prints the chip's product number, resolution, max touches, report rates and gesture config (`TP ...` lines), then:
   - `t=<ms since first touch> f=<fingers> x=<relx> y=<rely> g0=<hex> g1=<hex>` for every fresh frame with a finger down or a gesture bit set (g0 and g1 are the chip's gesture event bytes).
   - `S dur=<ms> maxf=<max fingers> sumx=<sum of abs x> sumy=<sum of abs y> tap=<0/1> hold=<0/1> scr=<0/1>` once per touch, when all fingers lift.
5. Afterwards reflash `out/latest.uf2` to the right half (and reconnect the TRRS cable).

## Before flashing

1. In Vial, use File > Save current layout and keep the .vil file. This is the guaranteed way to get the layout back (File > Load saved layout) if the EEPROM preservation does not work.
2. Optional but cheap: take a full flash image of each half with picotool while it is in the bootloader, before dragging the new .uf2 on. This captures the vendor firmware and the EEPROM region exactly as they are:

       picotool save -a out/backup_left.bin    # with the left half in RPI-RP2 mode
       picotool save -a out/backup_right.bin   # same for the right half

   Restore a half later with `picotool load out/backup_left.bin` while it is in the bootloader (`-a` is not needed for load; the image contains its own addresses).

## Flash

Flash each half the same way with the same `.uf2` (left first). Double-tap reset, wait for the `RPI-RP2` drive, then from the repo root:

    diskutil unmount /Volumes/RPI-RP2 && picotool load -x out/latest.uf2

`picotool` talks to the bootloader over USB directly and reboots the board (`-x`) when done. Unmounting first matters: the board drops off the bus the instant the last block lands, and if macOS still has the FAT volume open at that moment (a Finder copy, `cp` writing extended attributes, Spotlight) it can hang on the vanished disk and, on 2026-10-11, kernel-panicked 60 s later ("busy timeout ... IOMediaBSDClient"). Dragging the file onto the drive works most of the time, but the unmount-then-picotool route never leaves the volume open.

If `picotool` says no device is found, the board is not in bootloader mode or the drive was already gone; double-tap reset again.

On Linux, `picotool load -x out/latest.uf2` works the same (AUR on Arch, `apt install picotool` on Debian; it needs the Raspberry Pi udev rule or `sudo` to open the bootloader device; the Debian package installs `60-picotool.rules`, which grants the `plugdev` group and the active seat). `tools/flash_linux.sh` does both halves unattended: it waits for a bootloader to show up, unmounts the `RPI-RP2` volume if udisks grabbed it, runs `picotool load -x`, then waits for the next half (`tools/flash_linux.sh [uf2] [halves] [seconds per half]`, defaults `out/latest.uf2 2 900`). Without picotool, copy the file onto the `RPI-RP2` drive: DMS/udisks usually mounts it under `/run/media/$USER/RPI-RP2`; otherwise `udisksctl mount -b /dev/disk/by-label/RPI-RP2`. Then `cp out/latest.uf2 /run/media/$USER/RPI-RP2/ && sync`. The drive vanishes on its own when the board reboots; Linux handles that without the macOS hang.

Connect the TRRS cable before plugging in USB and never hot-plug it: a half-seated TRRS plug can short the power rails between the halves, and a brown-out on the USB half shows up as the QMK logo stuck on its OLED (it decided it was the slave because USB never enumerated). Unplug everything, reseat TRRS, then plug USB back in.

## Rollback

`out/rollback_vendor_vial_rp2040_ce.uf2` is the vendor `vial` keymap rebuilt from the same pinned tree. To rebuild it:

    make -C vendor/vial-qmk sofle_choc:vial CONVERT_TO=rp2040_ce

Note that the vendor keymap does not preserve EEPROM across builds, so flashing this rollback image wipes the stored layout; restore it from the .vil in Vial afterwards. The picotool backups above restore the exact original state instead.

## What to test after flashing

- The pad works without touching it at boot. Test 5 plug-ins, and again after the board has been unplugged for more than 30 seconds.
- 20 deliberate taps all click.
- Two-finger tap is a right click; three-finger tap is a middle click.
- Press for a quarter second, then move: drag; release ends it.
- Slow careful pointing never starts a drag.
- A finger resting for 10+ seconds never clicks or drags.
- A tap does not nudge the cursor.
- Resting a finger on the pad never clicks.
- Scroll direction feels natural and the speed is right at 8 units per line (`TP_SCROLL_UNITS_PER_LINE` in `config.h`: try 4, 8, 12).
- The cursor moves immediately on touch.
- Three-finger swipe left moves to the Space on the right, swipe right to the Space on the left (same direction as the built-in trackpad).
- Three-finger swipe up opens Mission Control; down opens App Expose.
- A three-finger tap still middle-clicks.
- A slow three-finger touch that moves less than ~8 mm does nothing.
- The keymap is intact (open Vial and compare with the saved .vil).
- Left OLED shows kznjk from the very top, a white box with the host OS, `Layer` and the layer name; the name changes while a layer key is held (kbd, 10key, mods, rgb). Nothing overwrites the top of the k on Linux (`linux` and `10key` are 5-character lines).
- Right OLED still shows the logo. RGB and encoders behave as before.
- After a minute idle both halves' RGB go dark and both OLEDs dim; any key, encoder or pad touch restores them on both halves.
- After the host sleeps and wakes, the OLED comes back on by itself.
- The OLED's white box shows the detected host OS; on Linux the three-finger swipes send Super+Alt chords (bound in Niri per docs/linux-niri-dms.md) and `python3 tools/hid_watch.py` shows them as `LGui+LAlt+Right` etc.
- On Linux, two-finger scroll goes the same way as on the Mac on both axes (build 8 flips horizontal back; `hid_watch.py` prints the wheel sign per tick).
- Ctrl + right encoder switches tabs; Cmd + sideways swipe goes back/forward once per swipe; Ctrl + sideways swipe switches one tab (LinearMouse mappings on the Mac, firmware taps on Linux).
- On Linux, Ctrl + sideways swipe switches one tab per swipe with no horizontal scroll leaking through; `hid_watch.py` shows `LCtl+Tab` / `LCtl+LSft+Tab` and no wheel tick.
- If Vial shows a wrong or scrambled layout after flashing, the stored data did not match this build's layout; use File > Load saved layout with the .vil. If the board misbehaves badly, hold the top-left key of the left half while plugging it in (bootmagic) to clear EEPROM, then load the .vil.
