# Using the board on Linux with Niri + DankMaterialShell

The firmware is OS-agnostic at the USB level: every gesture ends up as ordinary
mouse buttons, wheel ticks or key presses. On macOS those land on built-in
shortcuts (plus LinearMouse); on Linux the compositor has to be told what to do
with them. Since build 7 the firmware also detects the host OS and adapts what
it sends, so the Linux side only needs a handful of Niri binds.

State on 2026-10-11: applied on the Linux box (CachyOS, niri 26.04, DMS 1.6.1).
The binds live in `~/.config/niri/sofle.kdl`, included from `config.kdl`; a
copy is kept in this repo as `host/niri-sofle.kdl`. What the board actually
sends can be checked at any time with `tools/hid_watch.py` (see the end).

## What the firmware sends

| Gesture / control | macOS host | Linux host (build 7+) |
|---|---|---|
| 1 / 2 / 3-finger tap | button 1 / 2 / 3 | same |
| press 1/4 s then move | button 1 held (drag) | same |
| two-finger scroll | wheel, natural direction on both axes | wheel, both axes flipped back by the master (build 8, see below) |
| three-finger swipe, fingers left | `Ctrl+Right` (next Space) | `Super+Alt+Right` |
| three-finger swipe, fingers right | `Ctrl+Left` (previous Space) | `Super+Alt+Left` |
| three-finger swipe up | consumer 0x29F "AC Desktop Show All Windows" (QMK `KC_MISSION_CONTROL`) | same key; the kernel maps it to `KEY_SCALE`, xkb calls it **`XF86LaunchA`** |
| three-finger swipe down | `Ctrl+Down` (App Expose) | `Super+Alt+Down` |
| Ctrl + right encoder | `Ctrl+Tab` / `Ctrl+Shift+Tab` | same |
| Super or Alt held + sideways swipe | exactly one wheel left/right tick | same |
| Ctrl held + sideways swipe | one wheel tick (LinearMouse: previous / next tab) | `Tab` / `Shift+Tab` tapped by the firmware while Ctrl is held = previous / next tab; no tick |
| Super + vertical scroll | plain wheel with Super held (LinearMouse: pinch zoom) | plain wheel with Super held |

How the firmware knows: QMK's `OS_DETECTION_ENABLE` fingerprints the host from
its USB enumeration pattern and `pointing_device_task_user` in `keymap.c` picks
the swipe key set and the scroll flip. The white box on the left OLED shows
the result (`macOS`, `linux`, `win`, `ios`, `?`), which settles about a quarter
second after plug-in; until then the macOS set is used. Detection re-runs on
replug or host reboot, not on a KVM switch that keeps the device enumerated.

Why Linux gets Super+Alt chords: `Ctrl+Left/Right` is word-wise cursor movement
in every Linux editor, terminal and browser text field, so a global Niri bind
on them would steal that everywhere. Super+Alt+arrow is unused by Niri and DMS.

Scroll direction (build 8): the pad half applies natural scrolling to both
axes for every host, which is what the Mac wanted. On Linux the vertical
direction came out backwards, so the master flips vertical back on a Linux host
(`TP_SCROLL_FLIP_V_LINUX 1` in `config.h`), and horizontal as well
(`TP_SCROLL_FLIP_H_LINUX 1`); both confirmed on the box. Niri's own `natural-scroll` must stay off for mice either
way (below).

Middle click on Linux pastes the primary selection in terminals and many
editors, so a three-finger tap over a text field pastes. That is standard Linux
behaviour, not a firmware quirk.

## The Niri binds (`~/.config/niri/sofle.kdl`)

```kdl
binds {
    Super+Alt+Right { focus-column-right; }   // fingers left  = next column (macOS: next Space)
    Super+Alt+Left  { focus-column-left; }    // fingers right = previous column
    XF86LaunchA     { toggle-overview; }      // swipe up = Mission Control -> Overview
    Super+Alt+Down  { toggle-overview; }      // swipe down = App Expose -> Overview (closes it too)

}
```

The real file carries `hotkey-overlay-title`s so the swipes show up in
`Mod+Shift+/` (Niri's hotkey overlay) and DMS's `dms ipc call keybinds toggle`
cheat sheet. `config.kdl` ends with `include "sofle.kdl"` (added after the DMS
includes; DMS regenerates its own `dms/*.kdl` files but never touches
`config.kdl` or `sofle.kdl`). Niri reloads on save; `niri validate` checks it.

Choices behind those lines:

- **Spaces -> columns or workspaces?** Niri's workspaces stack vertically and
  its columns scroll horizontally, so a sideways swipe maps most naturally onto
  `focus-column-left/right`, which is also what DMS already binds
  `Mod+WheelScrollLeft/Right` to (so **Super + sideways swipe** needs no line of
  its own). To think of Spaces as workspaces instead, use `focus-workspace-up`
  for fingers-right and `focus-workspace-down` for fingers-left.
- **App Expose has no Niri equivalent** (no "all windows of this app" view).
  The Overview is the nearest thing, and binding it to the down swipe as well
  means a down swipe closes an open Overview like on macOS. Other reasonable
  targets: `spawn "dms" "ipc" "call" "processlist" "toggle"` for a running-apps
  list, or `focus-window-previous` to hop back to the last window.
- **Ctrl + sideways swipe** is tab switching on the Mac via LinearMouse. Niri
  binds can only run compositor actions or spawn commands, not type keys into
  the focused app, and a key injected by a helper such as `wtype` arrives on a
  separate virtual keyboard without the physically held Ctrl (the app got a
  bare Tab). So on a Linux host the firmware does it itself, exactly like the
  Ctrl + encoder path: it taps Tab (next tab) or Shift+Tab (previous) while Ctrl
  is held and swallows the wheel tick (`TP_CTRL_SWIPE_TABS_LINUX`,
  `TP_CTRL_SWIPE_NEXT_TAB_H` to swap the direction). Nothing to bind, and
  Ctrl + vertical scroll (browser zoom) is untouched.

## Input settings

- **Leave `natural-scroll` off for mice** (it is, in `config.kdl`'s `input {
  mouse { } }`). The firmware already applies the direction, so Niri must not
  invert it again. Niri 26.04 still cannot configure one device differently from
  another of the same type (`mouse "name" { }` is rejected by `niri validate`),
  so if a regular mouse on the same machine wants natural scrolling, one of the
  two devices will feel wrong. The pad enumerates as a mouse.
- Scroll speed, if 8 units per line feels different under libinput than on the
  Mac, is `scroll-factor` under `mouse` (per-axis since 25.08), or
  `TP_SCROLL_UNITS_PER_LINE` in the firmware.
- Nothing is needed for taps, drag or the encoder.
- DMS's own `dms/input.kdl` is not included from this `config.kdl`, so its
  `mouse { accel-speed 0.0 }` does not apply; the `touchpad` section there is
  for the laptop's pad, not the Sofle.

## DankMaterialShell

DMS does not see gestures; it exposes actions over IPC that a Niri bind can
spawn. The installed binds (`dms/binds.kdl`) already give `Mod+Space` the
launcher, `Mod+N` notifications, `Mod+M` the process list, `Super+X` the power
menu and `Mod+D` / `Mod+Tab` the Overview. `dms ipc list` prints the full set
for the installed version; `dms ipc call keybinds toggle` shows an on-screen
cheat sheet including the Sofle lines above.

## Checking what the board sends

```
python3 tools/hid_watch.py
```

reads the board's hidraw nodes (world-readable thanks to the Vial udev rule
`/etc/udev/rules.d/92-viia.rules`; nothing is grabbed) and prints each key
press, consumer usage, mouse button and wheel tick, decoded. A three-finger
swipe on a Linux host should print `kbd press LGui+LAlt+Right` and a release;
swipe up prints `consumer 0x029F`. Two-finger scroll prints `wheel v=.. h=..`
with the sign the host sees. If the swipes print `LCtl+Right` instead, the board
detected the host as macOS (check the OLED) or is still on a pre-build-7 image.
`wev` (not installed) would show the same from the compositor's side.

## What does not carry over

- **Pinch zoom and swipe-to-navigate** are LinearMouse features on the Mac.
  Nothing on Linux turns wheel ticks into those gestures without a helper. The
  wheel-with-Ctrl zoom that browsers implement natively works as usual.
- **Vial** runs on Linux too; the layout lives in the keyboard, so it follows
  the board. The udev rule is installed on this box.
- **OLED and RGB behaviour** is identical; the idle dimming and wake-on-resume
  do not depend on the host.
