# Using the board on Linux with Niri + DankMaterialShell

The firmware is OS-agnostic: every gesture ends up as ordinary mouse buttons,
wheel ticks or key presses. On macOS those land on built-in shortcuts; on Linux
the compositor has to be told what to do with them. This is what the board
sends and what to put in `~/.config/niri/config.kdl` to get the same behaviour.
DankMaterialShell (DMS) needs nothing for the gestures themselves; its IPC calls
are listed at the end for anyone who wants a gesture to open a shell panel.

Verified against the firmware in this repo (build 6), the Niri wiki (binds,
input, overview, 25.05+) and DMS docs (1.7). Key names can be confirmed on the
Linux box with `wev`.

## What the firmware sends

| Gesture / control | What the host receives | macOS meaning |
|---|---|---|
| 1 / 2 / 3-finger tap | mouse button 1 / 2 / 3 | click / right click / middle click |
| press 1/4 s then move | button 1 held | drag |
| two-finger scroll | wheel up/down/left/right, already inverted for natural feel | natural scrolling |
| three-finger swipe, fingers left | `Ctrl+Right` | Space to the right |
| three-finger swipe, fingers right | `Ctrl+Left` | Space to the left |
| three-finger swipe up | consumer usage 0x29F "AC Desktop Show All Windows" (QMK `KC_MISSION_CONTROL`) | Mission Control |
| three-finger swipe down | `Ctrl+Down` | App Expose |
| Ctrl + right encoder | `Ctrl+Tab` / `Ctrl+Shift+Tab` | next / previous tab |
| Super, Alt or Ctrl held + sideways swipe | exactly one wheel left/right tick per swipe | LinearMouse mappings (macOS only) |
| Cmd + vertical scroll | plain wheel with Super held | LinearMouse pinch zoom (macOS only) |

Linux specifics for two of those:

- The Mission Control usage is mapped by the kernel (since 4.4, 2019) to
  `KEY_SCALE`, which xkeyboard-config exposes as the keysym **`XF86LaunchA`**.
  That is the name Niri needs.
- Middle click on Linux pastes the primary selection in terminals and many
  editors, so a three-finger tap over a text field pastes. That is standard
  Linux behaviour, not a firmware quirk.

## Recommended: a Linux-aware firmware (one small change, not done yet)

`Ctrl+Left` and `Ctrl+Right` are word-wise cursor movement in every Linux
editor, terminal and browser text field. A global Niri bind on them would steal
that everywhere. The clean fix is to let the firmware emit different swipe keys
when it detects a Linux host. QMK's `OS_DETECTION_ENABLE` fingerprints the host
from its USB enumeration pattern (macOS, Windows, Linux) and the keymap can pick
the key set in `pointing_device_task_user`. On macOS nothing changes; on Linux
the swipes would send unused Super+Alt chords instead:

| Swipe | macOS (as today) | Linux |
|---|---|---|
| fingers left | `Ctrl+Right` | `Super+Alt+Right` |
| fingers right | `Ctrl+Left` | `Super+Alt+Left` |
| up | `XF86LaunchA` | `XF86LaunchA` (unchanged, nothing else uses it) |
| down | `Ctrl+Down` | `Super+Alt+Down` |

Niri binds for that firmware:

```kdl
binds {
    // Three-finger swipes (Sofle trackpad firmware, Linux key set)
    Super+Alt+Right { focus-column-right; }   // fingers left  = next column (macOS: next Space)
    Super+Alt+Left  { focus-column-left; }    // fingers right = previous column
    XF86LaunchA     { toggle-overview; }      // swipe up = Mission Control -> niri Overview (25.05+)
    Super+Alt+Down  { toggle-overview; }      // swipe down = App Expose; see note below

    // Super + sideways swipe (one wheel tick per swipe, Cmd on the Mac)
    Super+WheelScrollRight cooldown-ms=200 { focus-column-right; }
    Super+WheelScrollLeft  cooldown-ms=200 { focus-column-left; }

    // Ctrl + sideways swipe: workspaces instead of tabs (optional, see below)
    // Ctrl+WheelScrollRight cooldown-ms=200 { focus-workspace-down; }
    // Ctrl+WheelScrollLeft  cooldown-ms=200 { focus-workspace-up; }
}
```

Choices behind those lines:

- **Spaces -> columns or workspaces?** Niri's workspaces stack vertically and
  its columns scroll horizontally. A sideways swipe maps most naturally onto
  `focus-column-left/right`. If you think of macOS Spaces as workspaces, use
  `focus-workspace-up` for fingers-right and `focus-workspace-down` for
  fingers-left instead.
- **App Expose has no Niri equivalent** (no "all windows of this app" view).
  The Overview is the nearest thing. Other reasonable targets:
  `spawn "dms" "ipc" "call" "processlist" "toggle"` for a running-apps list, or
  `focus-window-previous` (check `niri msg action` for availability) to hop
  back to the last window.
- **Ctrl + sideways swipe** is tab switching on the Mac via LinearMouse. Niri
  binds can only run compositor actions or spawn commands, not type keys into
  the focused app, so there is no helper-free way to turn it into Ctrl+Tab.
  Use the encoder for tabs (it sends real Ctrl+Tab, works in every browser) and
  either leave Ctrl + sideways unbound or give it workspaces as above. Niri
  consumes scroll per axis while the modifier is held, so binding only the
  horizontal wheel leaves Ctrl + vertical scroll (browser zoom) alone.
- `cooldown-ms` is belt and braces; the firmware already sends one tick per
  swipe while Super/Alt/Ctrl is held.

## Without reflashing (works today, with a trade-off)

Bind the keys the firmware sends now. The cost is losing Ctrl+Left/Right word
movement system-wide, which most people will not accept for long:

```kdl
binds {
    Ctrl+Right  { focus-column-right; }
    Ctrl+Left   { focus-column-left; }
    XF86LaunchA { toggle-overview; }
    Ctrl+Down   { toggle-overview; }
    Super+WheelScrollRight cooldown-ms=200 { focus-column-right; }
    Super+WheelScrollLeft  cooldown-ms=200 { focus-column-left; }
}
```

Only `XF86LaunchA` and the Super+wheel lines are conflict-free; they can stay
in the final config either way.

## Input settings

- **Leave `natural-scroll` off for mice.** The firmware already inverts the
  pad's scroll, so Niri must not invert it again. Niri cannot configure one
  device differently from another of the same type (planned, not available),
  so if a regular mouse on the same machine wants natural scrolling, one of the
  two devices will feel wrong. The pad enumerates as a mouse.

  ```kdl
  input {
      mouse {
          // natural-scroll      <- keep commented out for the Sofle pad
          accel-profile "flat"   // optional: the firmware has no acceleration; adaptive is fine too
      }
  }
  ```

- Scroll speed, if 8 units per line feels different under libinput than on the
  Mac, is `scroll-factor` under `mouse` (per-axis since 25.08), or
  `TP_SCROLL_UNITS_PER_LINE` in the firmware.
- Nothing is needed for taps, drag or the encoder.

## DankMaterialShell

DMS does not see gestures; it exposes actions over IPC that a Niri bind can
spawn. Useful targets for a gesture or a layer key (`dms ipc list` prints the
full set for the installed version):

```kdl
binds {
    Mod+Space { spawn "dms" "ipc" "call" "spotlight" "toggle"; }
    Mod+N     { spawn "dms" "ipc" "call" "notifications" "toggle"; }
    Mod+C     { spawn "dms" "ipc" "call" "control-center" "toggle"; }
    Mod+X     { spawn "dms" "ipc" "call" "powermenu" "toggle"; }
    Mod+Escape { spawn "dms" "ipc" "call" "processlist" "toggle"; }
}
```

`dms ipc call keybinds toggle` shows an on-screen cheat sheet of the configured
binds, which is a handy way to check the swipe bindings landed.

## What does not carry over

- **Pinch zoom and swipe-to-navigate** are LinearMouse features on the Mac.
  Nothing on Linux turns wheel ticks into those gestures without a helper. The
  wheel-with-Ctrl zoom that browsers implement natively works as usual.
- **Vial** runs on Linux too; the layout lives in the keyboard, so it follows
  the board. Install the udev rule from the Vial site so it can open the device.
- **OLED and RGB behaviour** is identical; the idle dimming and wake-on-resume
  do not depend on the host.
