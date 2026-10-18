/* Copyright 2023 Brian Low
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET
// Enabling this option changes the startup behavior to listen for an
// active USB communication to delegate which part is master and which
// is slave. With this option enabled and theres’s USB communication,
// then that half assumes it is the master, otherwise it assumes it
// is the slave.
//
// I've found this helps with some ProMicros where the slave does not boot
#define SPLIT_USB_DETECT

//#ifdef OLED_ENABLE
//#   define OLED_FONT_H "keyboards/splitkb/aurora/sofle_v2/glcdfont.c"
//#endif

#undef DYNAMIC_KEYMAP_LAYER_COUNT
#define DYNAMIC_KEYMAP_LAYER_COUNT 7

// Build 11: the Vial "QMK settings" as tuned on the board become the defaults
// (values re-read on 2026-10-18, build 17). TAPPING_TERM feeds Vial's own reset
// (qmk_settings_reset); the other three have no config.h equivalent in this tree,
// so keyboard_post_init_user (keymap.c) writes them whenever the EEPROM has just
// been reset (no layout marker). Vial's "reset QMK settings" button bypasses that
// and gives the vendor defaults (tapping term 200 via TAPPING_TERM, everything
// else off / equal to it).
#define TAPPING_TERM 200
#define SCTP_PERMISSIVE_HOLD 1     // Vial qsid 22
#define SCTP_QUICK_TAP_TERM  200   // Vial qsid 25 (Vial's reset sets it to TAPPING_TERM)
#define SCTP_FLOW_TAP_TERM   50    // Vial qsid 27 (Vial's reset sets it to 0)

// Idle behaviour (build 5): the OLED stays on as the "powered but idle" indicator
// and the RGB goes dark after a minute without input, coming back on the next
// key, encoder or trackpad event if RGB is enabled at all. Both still turn off
// when the host sleeps (USB suspend).
#define TP_IDLE_MS 60000                   // ms without input before the LEDs go dark and the OLED dims
#define OLED_TIMEOUT 0                     // never blank the OLED on inactivity
#define OLED_IDLE_BRIGHTNESS 1             // OLED contrast while idle (0-255; OLED_BRIGHTNESS, default 255, when active)
#define RGB_MATRIX_TIMEOUT TP_IDLE_MS
#define SPLIT_ACTIVITY_ENABLE              // right half learns about left-half/trackpad activity, so both halves time out together
#define RGB_MATRIX_SLEEP                   // LEDs off during USB suspend (the vendor's RGB_DISABLE_WHEN_USB_SUSPENDED is not read by this tree)
#define SPLIT_TRANSPORT_MIRROR             // If LED_MATRIX_KEYPRESSES or LED_MATRIX_KEYRELEASES is enabled, you also will want to enable SPLIT_TRANSPORT_MIRROR
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 100  // limits maximum brightness of LEDs (max 255). Higher may cause the controller to crash.

// Vial Support
#define VIAL_KEYBOARD_UID {0xA7, 0xA7, 0x9C, 0x4C, 0xE0, 0xB8, 0x00, 0x8D}

// The four corners
#define VIAL_UNLOCK_COMBO_ROWS { 0, 0 }
#define VIAL_UNLOCK_COMBO_COLS { 0, 5 }

//#define VIAL_UNLOCK_COMBO_ROWS { 0, 5, 3, 8 }
//#define VIAL_UNLOCK_COMBO_COLS { 0, 0, 0, 0 }


// Trackpad support
//#define AZOTEQ_IQS5XX_TPS43
#define AZOTEQ_IQS5XX_WIDTH_MM  43
#define AZOTEQ_IQS5XX_HEIGHT_MM 30

#define AZOTEQ_IQS5XX_RESOLUTION_X 1400
#define AZOTEQ_IQS5XX_RESOLUTION_Y 1400

#define AZOTEQ_IQS5XX_TAP_ENABLE false  // taps, two-finger tap and hold are done in tp_driver.c
#define AZOTEQ_IQS5XX_TWO_FINGER_TAP_ENABLE false
#define AZOTEQ_IQS5XX_PRESS_AND_HOLD_ENABLE false
#define AZOTEQ_IQS5XX_SWIPE_X_ENABLE false
#define AZOTEQ_IQS5XX_SWIPE_Y_ENABLE false
#define AZOTEQ_IQS5XX_ZOOM_ENABLE   false



// Sensitivity tuning
#define AZOTEQ_IQS5XX_TAP_TIME 110
#define AZOTEQ_IQS5XX_TAP_DISTANCE 12
#define AZOTEQ_IQS5XX_HOLD_TIME 320
// The pad-side half initialises the chip only after the 2 s SPLIT_USB_DETECT
// wait, when the IQS5xx is already in an idle mode and clock-stretches until its
// next cycle ends; the vendor's 3 ms cut those replies off. Once init succeeds the
// driver sets 10 ms report rates and disables the low-power timeouts, so the
// longer timeout only costs anything when the pad is actually not answering.
#define AZOTEQ_IQS5XX_TIMEOUT_MS 50
#define AZOTEQ_IQS5XX_SCROLL_INITIAL_DISTANCE 18

// Firmware gesture layer (tp_driver.c)
#define TP_TAP_MAX_MS        200   // max touch duration for a firmware tap (measured taps: 90-155 ms)
#define TP_TAP_MAX_DRIFT     40    // units (~1.6 mm); measured tap drift <= 26
#define TP_MULTI_TAP_MAX_MS  300   // 2- and 3-finger taps (fingers land staggered)
#define TP_MULTI_TAP_MAX_MOTION 60 // sum of |rel x|+|rel y| allowed during a multi-finger tap
#define TP_CLICK_FRAMES      2     // frames (10 ms each) a synthetic click is held
#define TP_HOLD_MS           180   // press-and-hold before drag arms (250 up to build 14; below TP_TAP_MAX_MS, so an armed finger that lifts within the tap window still clicks)
#define TP_HOLD_STILL_DRIFT  12    // units (~0.5 mm) a finger may move and still count as held still (resting: 5 in 250 ms, so well under in 180)
#define TP_DEAD_ZONE_UNITS   12    // no cursor motion until the touch has drifted this far
#define TP_DRAG_START_UNITS  16    // ~0.7 mm of travel within a 200 ms window after arming starts the drag; resting creep stays below this
// #define TP_DRAG2_BUTTON      POINTING_DEVICE_BUTTON2 // build 13's two-finger hold-then-move drag, off since build 14: two resting fingers trip the chip's scroll gesture before the hold arms, so it only ever scrolled. The modifier drags below replace it.
// #define TP_DRAG2_CTRL_BUTTON POINTING_DEVICE_BUTTON3
#define TP_DRAG_GUI_BUTTON   POINTING_DEVICE_BUTTON2 // build 14: Cmd/Super held when a one-finger drag starts = a drag with this button (right); the modifier is hidden from the host for the drag. Comment out to drop it.
#define TP_DRAG_CTRL_BUTTON  POINTING_DEVICE_BUTTON3 // ... Ctrl held = this button (middle: orbit/pan in CAD, like a mouse's middle button held while moving), Ctrl hidden likewise. Cmd/Super wins when both are held.
#define TP_SCROLL_UNITS_PER_LINE 8  // units of finger travel per wheel line (~0.33 mm); try 4, 8, 12

#define TP_SCROLL_BATCH_FRAMES 4   // one scroll report per 4 fresh frames (40 ms); see README
#define TP_SCROLL_MAX_LINES_PER_REPORT 8
#define TP_SWIPE3_MIN_UNITS_X 100  // three-finger swipe left/right: ~4 mm of travel; the 43 mm pad leaves little room beside three fingers
#define TP_SWIPE3_MIN_UNITS_Y 200  // three-finger swipe up/down: ~8 mm
#define TP_SWIPE3_MAX_MS     700   // ... and it must be reached within this long of the first touch

// Three-finger swipe actions, sent by the master (keymap.c) on the swipe codes.
// Named by the direction the fingers move. Defaults emulate the macOS
// four-finger swipes: left/right switch Spaces (fingers left = the Space to the
// right, as with natural scrolling), up = Mission Control, down = App Expose.
// Up/down need the default Mission Control shortcuts enabled in System Settings;
// left/right are Alt+arrow since build 9 (Ctrl+arrow before), so "Move left/right
// a space" must be rebound to Option+arrow there.
#define TP_SWIPE_LEFT_KEY     LALT(KC_RGHT)
#define TP_SWIPE_RIGHT_KEY    LALT(KC_LEFT)
#define TP_SWIPE_UP_KEY       KC_MISSION_CONTROL
#define TP_SWIPE_DOWN_KEY     LCTL(KC_DOWN)   // App Expose
// Build 7: OS detection (OS_DETECTION_ENABLE) picks a key set per host. The macOS
// set above is also used while the host is still unknown. Linux gets unused
// Super+Alt chords (Ctrl+Left/Right would steal word movement everywhere); see
// docs/linux-niri-dms.md for the Niri binds. Windows gets its virtual-desktop
// shortcuts (untested). The Mission Control key is XF86LaunchA on Linux.
// Fingers left = Super+Alt+Right = the column to the right, like the Mac's next
// Space (a build-11 attempt at the mirror was reverted the same day: the
// direction complaint turned out to be the Ctrl + two-finger swipe below).
#define TP_SWIPE_LEFT_KEY_LINUX   LGUI(LALT(KC_RGHT))
#define TP_SWIPE_RIGHT_KEY_LINUX  LGUI(LALT(KC_LEFT))
#define TP_SWIPE_UP_KEY_LINUX     KC_MISSION_CONTROL
#define TP_SWIPE_DOWN_KEY_LINUX   LGUI(LALT(KC_DOWN))
#define TP_SWIPE_LEFT_KEY_WIN     LCTL(LGUI(KC_RGHT))  // next virtual desktop
#define TP_SWIPE_RIGHT_KEY_WIN    LCTL(LGUI(KC_LEFT))
#define TP_SWIPE_UP_KEY_WIN       LGUI(KC_TAB)         // Task View
#define TP_SWIPE_DOWN_KEY_WIN     LGUI(KC_TAB)

// Build 8: scroll direction per host OS. The pad half (tp_driver.c) always
// applies natural scrolling, which is what the Mac wanted; the master flips an
// axis back (1) for the hosts below. On Linux the vertical direction came out
// backwards (user-reported, 2026-10-11), so vertical is flipped there.
// Horizontal was flipped there too from build 8 to build 9; build 10 leaves it as
// the pad sends it (user-reported on 2026-10-17: the two axes disagreed).
// tools/hid_watch.py prints the wheel sign per tick. Windows is a guess.
// Build 9: macOS (also iOS and a not-yet-detected host) gets its own pair;
// horizontal is flipped there too (user-requested 2026-10-16), vertical stays
// natural as the pad sends it.
#define TP_SCROLL_FLIP_V_MAC    0
#define TP_SCROLL_FLIP_H_MAC    1
#define TP_SCROLL_FLIP_V_LINUX  1
#define TP_SCROLL_FLIP_H_LINUX  0
#define TP_SCROLL_FLIP_V_WIN    0
#define TP_SCROLL_FLIP_H_WIN    1

// Discrete horizontal scroll while a modifier is held (build 6). LinearMouse maps
// Option/Control + horizontal wheel to back/forward and tab switching, one action
// per wheel tick, and a two-finger swipe produces several ticks. While one of
// these modifiers is down the master passes a single +/-1 horizontal tick and
// drops the rest until the pad has been quiet for TP_DISCRETE_H_GAP_MS, so one
// swipe is one action. Vertical scroll is never limited (Cmd+vertical is pinch
// zoom), and a horizontal tick is dropped while vertical ticks are still arriving
// so a slightly diagonal zoom cannot fire a back/forward navigation.
#define TP_DISCRETE_H_MODS (MOD_MASK_GUI | MOD_MASK_ALT | MOD_MASK_CTRL)
#define TP_DISCRETE_H_GAP_MS 250

// Build 8: on a Linux host, Ctrl + sideways swipe switches tabs from the firmware,
// the way Ctrl + encoder already does (Ctrl is held, so a bare Tab / Shift+Tab
// tap completes Ctrl+Tab / Ctrl+Shift+Tab), and the wheel tick is swallowed.
// macOS keeps the wheel tick for LinearMouse. TP_CTRL_SWIPE_NEXT_TAB_H is the
// sign of the (single, discrete) horizontal tick that means "next tab"; the
// other sign is "previous tab". It is the sign *after* the per-OS flip above:
// -1 was confirmed on 2026-10-11 with TP_SCROLL_FLIP_H_LINUX 1; build 10 set that
// flip to 0, which silently reversed this too, so build 11 uses +1 to keep the
// same finger motion meaning "next tab".
#define TP_CTRL_SWIPE_TABS_LINUX
#define TP_CTRL_SWIPE_NEXT_TAB_H 1

// Build 16: Alt + sideways swipe is browser history in Firefox on every OS
// (mousewheel.with_alt.action 2, no per-OS override): a positive wheel tick is
// Back, a negative one Forward. Fingers left should be Forward, like the next
// column (Super) and the next tab (Ctrl). The sign that reaches the browser is a
// by-product of the per-OS TP_SCROLL_FLIP_H_* above (plus LinearMouse's
// scrolling.reverse.horizontal on the Mac), so the correction is per OS too:
// with these set, fingers left reached Firefox as +1 (Back) on Linux and macOS
// and as -1 (Forward, already right) on Windows. 1 negates the single Alt tick;
// Alt alone, since Cmd/Super and Ctrl keep their own meanings. Windows untested.
#define TP_ALT_H_FLIP_MAC    1
#define TP_ALT_H_FLIP_LINUX  1
#define TP_ALT_H_FLIP_WIN    0

#define SPLIT_POINTING_ENABLE
#define POINTING_DEVICE_RIGHT

// Master drops split reports it has already seen, keyed on report_id (see tp_driver.c).
#define TP_SEQ_DEDUP
// The vendor header sets this to AZOTEQ_IQS5XX_REPORT_RATE + 1 only if it is
// undefined, and it is not visible here, so define it for every translation unit.
#ifdef TP_SEQ_DEDUP
#    define POINTING_DEVICE_TASK_THROTTLE_MS 1  // sample the split report every scan; dedup handles repeats
#else
#    define POINTING_DEVICE_TASK_THROTTLE_MS 11  // AZOTEQ_IQS5XX_REPORT_RATE (10) + 1
#endif

/* Select hand configuration */
#ifdef TP_MEASURE
#    define MASTER_RIGHT  // measurement build: USB plugged into the right (trackpad) half, used alone
#else
#    define MASTER_LEFT
#endif
// #define EE_HANDS


#define RGBLED_NUM 70
#define RGBLED_SPLIT {35,35}

// RGB Matrix
#define RGB_MATRIX_KEYPRESSES
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_CYCLE_LEFT_RIGHT
#define RGB_MATRIX_DEFAULT_SPD 60
#define RGB_MATRIX_DEFAULT_VAL RGB_MATRIX_MAXIMUM_BRIGHTNESS

// RGB Matrix effects
#define ENABLE_RGB_MATRIX_ALPHAS_MODS
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_BAND_SAT
#define ENABLE_RGB_MATRIX_BAND_VAL
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#define ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#define ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#define ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#define ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#define ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#define ENABLE_RGB_MATRIX_DUAL_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_BEACON
#define ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#define ENABLE_RGB_MATRIX_FLOWER_BLOOMING
#define ENABLE_RGB_MATRIX_RAINDROPS
#define ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#define ENABLE_RGB_MATRIX_HUE_BREATHING
#define ENABLE_RGB_MATRIX_HUE_PENDULUM
#define ENABLE_RGB_MATRIX_HUE_WAVE
#define ENABLE_RGB_MATRIX_PIXEL_FRACTAL
#define ENABLE_RGB_MATRIX_PIXEL_FLOW
#define ENABLE_RGB_MATRIX_PIXEL_RAIN
#define ENABLE_RGB_MATRIX_STARLIGHT
#define ENABLE_RGB_MATRIX_STARLIGHT_SMOOTH
#define ENABLE_RGB_MATRIX_STARLIGHT_DUAL_HUE
#define ENABLE_RGB_MATRIX_STARLIGHT_DUAL_SAT
#define ENABLE_RGB_MATRIX_RIVERFLOW
#define ENABLE_RGB_MATRIX_TYPING_HEATMAP
#define ENABLE_RGB_MATRIX_DIGITAL_RAIN
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#define ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#define ENABLE_RGB_MATRIX_SPLASH
#define ENABLE_RGB_MATRIX_MULTISPLASH
#define ENABLE_RGB_MATRIX_SOLID_SPLASH
#define ENABLE_RGB_MATRIX_SOLID_MULTISPLASH
