#include QMK_KEYBOARD_H
#include "keymap_us_international.h"
#include "sendstring_us_international.h"
#ifdef OS_DETECTION_ENABLE
#    include "os_detection.h"
#endif

// Vial derives the VIA EEPROM magic from BUILD_ID, a random number generated
// on every build (vendor/vial-qmk/util/build_id.py, appended to version.h by
// builddefs/build_keyboard.mk). A new build therefore normally fails
// via_eeprom_is_valid() in via_init() and eeconfig_init_via() wipes the
// dynamic keymap, macros and Vial data. This keymap keeps the EEPROM layout
// identical to the vendor "vial" build (same layer count, matrix size,
// macro buffer and Vial features), so the stored data is still valid: re-stamp
// the magic for this build instead of letting via_init() reset it.
// The re-stamp is done when the layout marker stored in EEPROM (see
// SCTP_LAYOUT_MARKER below) matches this build's layout, i.e. the data on the
// board was written by a build with the identical EEPROM layout, and the VIA
// magic is not valid. The marker alone decides: do not test eeconfig_is_enabled()
// here, because in this tree it also requires via_eeprom_is_valid(), which is
// false on a new build until this very hook re-stamps it. A blank EEPROM has no
// marker, so it initialises normally. A deliberate reset (bootmagic,
// QK_CLEAR_EEPROM, Vial reset) goes through eeconfig_init(), whose
// eeconfig_init_user() zeroes the marker, so it is not undone by this hook.
// Order: keyboard_init() calls via_init() (-> via_init_kb()) before
// quantum_init(), whose "if (!eeconfig_is_enabled()) eeconfig_init();" would
// otherwise wipe everything, so the re-stamp happens first. Change the marker ('SCT2', ...) whenever the
// EEPROM layout changes (layer count, matrix size, macro buffer, Vial/QMK
// settings features, vendor pin). The first flash of a build with a new marker
// resets the EEPROM once; load the saved .vil in Vial afterwards.
_Static_assert(DYNAMIC_KEYMAP_LAYER_COUNT == 7, "EEPROM re-stamp assumes the vendor vial layout");
_Static_assert(MATRIX_ROWS == 10 && MATRIX_COLS == 6, "EEPROM re-stamp assumes the vendor vial layout");
_Static_assert(EECONFIG_USER_DATA_SIZE == 0, "layout marker uses the 4-byte EECONFIG_USER word");

// Layout marker in the otherwise unused 4-byte EECONFIG_USER word. Written on
// every boot (after via_init_kb has already read the old value).
#define SCTP_LAYOUT_MARKER 0x53435431u  // 'SCT1': vendor vial layout, 7 layers

void via_init_kb(void) {
    if (eeconfig_read_user() == SCTP_LAYOUT_MARKER && !via_eeprom_is_valid()) {
        via_eeprom_set_valid(true);
    }
}

#ifdef QMK_SETTINGS
#    include "qmk_settings.h"
// Vial setting ids (qsid), from the protos[] table in quantum/qmk_settings.c.
#    define SCTP_QSID_PERMISSIVE_HOLD 22
#    define SCTP_QSID_QUICK_TAP_TERM  25
#    define SCTP_QSID_FLOW_TAP_TERM   27

// Build 11: after an EEPROM reset Vial's QMK settings are the vendor defaults
// (qmk_settings_reset: tapping term = TAPPING_TERM, quick tap term = TAPPING_TERM,
// permissive hold off, flow tap off). Re-apply the values tuned in Vial (see
// SCTP_* in config.h). Runs after qmk_settings_init, so it updates both the live
// QS struct and the EEPROM copy.
static void sctp_apply_settings_defaults(void) {
    uint8_t  permissive = SCTP_PERMISSIVE_HOLD;
    uint16_t quick_tap  = SCTP_QUICK_TAP_TERM;
    uint16_t flow_tap   = SCTP_FLOW_TAP_TERM;
    qmk_settings_set(SCTP_QSID_PERMISSIVE_HOLD, &permissive, sizeof(permissive));
    qmk_settings_set(SCTP_QSID_QUICK_TAP_TERM, &quick_tap, sizeof(quick_tap));
    qmk_settings_set(SCTP_QSID_FLOW_TAP_TERM, &flow_tap, sizeof(flow_tap));
}
#endif

#if defined(SPLIT_ACTIVITY_ENABLE) && !defined(DISABLE_SYNC_TIMER)
// Build 12: right-half RGB blink while the pad moves. The master resends its
// clock every 100 ms (sync_timer, +2 ms) and its last-activity timestamps on
// every change (SPLIT_ACTIVITY_ENABLE). This half applies both here in its main
// loop (transport_slave, just before this hook), and that loop can be blocked
// in the pad's I2C read while the IQS572 clock-stretches to the end of its
// cycle. A clock sync applied that late leaves this half's clock behind the
// master's; the next pointing-activity stamp (every 10 ms while the pad moves)
// is then "in the future", last_input_activity_elapsed() wraps to ~4e9 ms,
// rgb_matrix_task reads that as past RGB_MATRIX_TIMEOUT and flushes a black
// frame (and oled_task_user dims the OLED) until the next sync, up to 100 ms.
// A stamp from the future can only mean a skewed clock, so pull it back to now.
// tp_driver.c also phase-locks its polls to the chip's window (shorter stalls).
void matrix_slave_scan_user(void) {
    if (last_input_activity_elapsed() > 0x80000000u) {
        uint32_t now = sync_timer_read32();
        set_activity_timestamps(now, now, now);
    }
}
#endif

void keyboard_post_init_user(void) {
    if (eeconfig_read_user() != SCTP_LAYOUT_MARKER) {
        // No marker: the EEPROM is blank or was just reset, so everything in it
        // is the vendor default right now.
#ifdef QMK_SETTINGS
        sctp_apply_settings_defaults();
#endif
        eeconfig_update_user(SCTP_LAYOUT_MARKER);
    }
}

#ifdef POINTING_DEVICE_ENABLE
// The pad driver (tp_driver.c) stamps each fresh frame with a non-zero
// sequence number in report_id; see README.
#ifndef MOUSE_SHARED_EP
#    error "tp_driver uses report_id as a split sequence number; MOUSE_SHARED_EP is required"
#endif

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
#ifdef TP_SEQ_DEDUP
    static uint8_t last_seq = 0;
    if (mouse_report.report_id == last_seq) {
        // Same slave frame sampled again: drop motion/scroll, keep buttons (they are levels).
        mouse_report.x = mouse_report.y = 0;
        mouse_report.h = mouse_report.v = 0;
    }
    last_seq = mouse_report.report_id;
#endif
#ifdef OS_DETECTION_ENABLE
    // Scroll direction per host (build 8): the pad half applies natural scrolling
    // for every host; flip an axis back where the host should see the other
    // direction (TP_SCROLL_FLIP_* in config.h). Runs before the discrete-h logic
    // below, which only looks at the sign.
    {
        bool flip_v = false, flip_h = false;
        switch (detected_host_os()) {
            case OS_LINUX:   flip_v = TP_SCROLL_FLIP_V_LINUX; flip_h = TP_SCROLL_FLIP_H_LINUX; break;
            case OS_WINDOWS: flip_v = TP_SCROLL_FLIP_V_WIN;   flip_h = TP_SCROLL_FLIP_H_WIN;   break;
            default:         flip_v = TP_SCROLL_FLIP_V_MAC;   flip_h = TP_SCROLL_FLIP_H_MAC;   break;  // macOS, iOS, or not yet known
        }
        if (flip_v) mouse_report.v = (int8_t)-mouse_report.v;  // never -128: the pad caps lines at +/-8
        if (flip_h) mouse_report.h = (int8_t)-mouse_report.h;
    }
#endif
    // Three-finger swipes arrive from the slave as BUTTON4..7 pulses (2 frames).
    // Only rising edges act, so one pulse gives exactly one key tap even though
    // this runs every 1 ms. The bits never reach the host.
    static uint8_t prev_swipe = 0;
    uint8_t        swipe      = mouse_report.buttons & 0x78; // bits 3..6
    uint8_t        edges      = swipe & (uint8_t)~prev_swipe;
    prev_swipe                = swipe;
    if (edges & 0x78) {
        uint16_t left = TP_SWIPE_LEFT_KEY, right = TP_SWIPE_RIGHT_KEY, up = TP_SWIPE_UP_KEY, down = TP_SWIPE_DOWN_KEY;
#ifdef OS_DETECTION_ENABLE
        switch (detected_host_os()) {
            case OS_LINUX:
                left = TP_SWIPE_LEFT_KEY_LINUX; right = TP_SWIPE_RIGHT_KEY_LINUX; up = TP_SWIPE_UP_KEY_LINUX; down = TP_SWIPE_DOWN_KEY_LINUX;
                break;
            case OS_WINDOWS:
                left = TP_SWIPE_LEFT_KEY_WIN; right = TP_SWIPE_RIGHT_KEY_WIN; up = TP_SWIPE_UP_KEY_WIN; down = TP_SWIPE_DOWN_KEY_WIN;
                break;
            default:  // macOS, iOS, or not yet known
                break;
        }
#endif
        if (edges & 0x08) tap_code16(left);   // BUTTON4: fingers moved left
        if (edges & 0x10) tap_code16(right);  // BUTTON5: fingers moved right
        if (edges & 0x20) tap_code16(up);     // BUTTON6: fingers up
        if (edges & 0x40) tap_code16(down);   // BUTTON7: fingers down
    }
    mouse_report.buttons &= (uint8_t)~0x78;
#ifdef TP_DISCRETE_H_MODS
    {
        static uint16_t last_h_time = 0, last_v_time = 0;
        if (mouse_report.v != 0) {
            last_v_time = timer_read();
        }
        if (mouse_report.h != 0) {
            if (get_mods() & (TP_DISCRETE_H_MODS)) {
                bool quiet     = timer_elapsed(last_h_time) > TP_DISCRETE_H_GAP_MS && timer_elapsed(last_v_time) > TP_DISCRETE_H_GAP_MS;
                mouse_report.h = quiet ? (mouse_report.h > 0 ? 1 : -1) : 0;
#if defined(OS_DETECTION_ENABLE) && defined(TP_CTRL_SWIPE_TABS_LINUX)
                // Linux host: Ctrl + sideways swipe = tab switching done here, like
                // the encoder (config.h). The tick never reaches the host.
                if (mouse_report.h != 0 && (get_mods() & MOD_MASK_CTRL) && detected_host_os() == OS_LINUX) {
                    if (mouse_report.h == (TP_CTRL_SWIPE_NEXT_TAB_H)) {
                        tap_code(KC_TAB);
                    } else {
                        register_code(KC_LSFT);
                        tap_code(KC_TAB);
                        unregister_code(KC_LSFT);
                    }
                    mouse_report.h = 0;
                }
#endif
            }
            last_h_time = timer_read();  // every fresh horizontal tick extends the lockout
        }
    }
#endif
    mouse_report.report_id = 0;
    // Natural scrolling is applied on the slave, in tp_driver.c.
    return mouse_report;
}
#endif // POINTING_DEVICE_ENABLE

// Default keymap, baked from the live Vial layout read on 2026-10-17 with
// tools/dump_layout.py (layout/current_layout.json). Vial's EEPROM copy still
// takes precedence day to day; this is what an EEPROM reset comes back to.
// TD(1) on the base layer is a Vial tap dance (tap MO(1), double-tap TG(1)); it
// lives in EEPROM only, so after a reset that key is dead until re-created in Vial.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
  KC_ESC, KC_1, KC_2, KC_3, KC_4, KC_5,   KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS,
  KC_GRV, KC_Q, KC_W, KC_E, KC_R, KC_T,   KC_Y, KC_U, KC_I, KC_O, KC_P, KC_EQL,
  KC_TAB, LSFT_T(KC_A), LGUI_T(KC_S), LALT_T(KC_D), LCTL_T(KC_F), KC_G,   KC_H, RCTL_T(KC_J), RALT_T(KC_K), RGUI_T(KC_L), RSFT_T(KC_SCLN), KC_QUOT,
  KC_DEL, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_MUTE,   RM_TOGG, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_BSLS,
  TD(1), MO(2), MO(3), KC_BSPC, KC_ENT,   MO(3), KC_SPC, KC_LBRC, KC_RBRC, HYPR(KC_TAB)
),
[1] = LAYOUT(
  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_HOME, KC_ESC, KC_PSLS, KC_PAST, KC_PMNS, KC_NO,
  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_END, KC_P7, KC_P8, KC_P9, KC_PPLS, KC_NO,
  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_PGUP, KC_P4, KC_P5, KC_P6, KC_PCMM, KC_NO,
  KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO,   KC_NO, KC_PGDN, KC_P1, KC_P2, KC_P3, KC_PEQL, KC_NO,
  TG(1), KC_NO, KC_NO, _______, _______,   KC_PENT, KC_P0, KC_PDOT, KC_NO, KC_NO
),
[2] = LAYOUT(
  _______, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5,   KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11,
  KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5,   KC_6, KC_7, KC_8, KC_9, KC_0, KC_F12,
  _______, LSFT(KC_1), LSFT(KC_2), LSFT(KC_3), LSFT(KC_4), LSFT(KC_5),   LSFT(KC_6), LSFT(KC_7), LSFT(KC_8), LSFT(KC_9), LSFT(KC_0), LSFT(KC_BSLS),
  _______, KC_EQL, KC_MINS, LSFT(KC_EQL), LSFT(KC_LBRC), LSFT(KC_RBRC), _______,   _______, KC_LBRC, KC_RBRC, KC_SCLN, LSFT(KC_SCLN), KC_BSLS, _______,
  _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______
),
[3] = LAYOUT(
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, KC_INS, KC_PSCR, KC_APP, KC_NO, KC_NO,   KC_PGUP, _______, KC_UP, _______, _______, KC_BSPC,
  _______, KC_LALT, KC_LCTL, KC_LSFT, KC_NO, KC_CAPS,   KC_PGDN, KC_LEFT, KC_DOWN, KC_RGHT, KC_DEL, KC_BSPC,
  _______, KC_UNDO, KC_CUT, KC_COPY, KC_PSTE, KC_NO, RM_NEXT,   RM_PREV, KC_NO, _______, KC_NO, _______, KC_NO, _______,
  _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______
),
[4] = LAYOUT(
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______
),
[5] = LAYOUT(
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______
),
[6] = LAYOUT(
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______, _______, _______,
  _______, _______, _______, _______, _______,   _______, _______, _______, _______, _______
),
};

const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [0] = { ENCODER_CCW_CW(KC_UP, KC_DOWN), ENCODER_CCW_CW(KC_LEFT, KC_RGHT) },
    [1] = { ENCODER_CCW_CW(KC_VOLD, KC_VOLU), ENCODER_CCW_CW(KC_VOLD, KC_VOLU) },
    [2] = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [3] = { ENCODER_CCW_CW(RM_HUEU, RM_HUED), ENCODER_CCW_CW(RM_VALU, RM_VALD) },
    [4] = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [5] = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
    [6] = { ENCODER_CCW_CW(_______, _______), ENCODER_CCW_CW(_______, _______) },
};

// Encoder: Ctrl + Left/Right from an encoder (right encoder on the base layer)
// switches tabs instead of Spaces, the same Ctrl + horizontal convention as the
// trackpad's Ctrl + sideways swipe. Only encoder events are intercepted, so the
// real arrow keys with Ctrl still move between Spaces. Ctrl is already held, so a
// bare Tab (or Shift+Tab) completes Ctrl+Tab / Ctrl+Shift+Tab.
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (IS_ENCODEREVENT(record->event) && record->event.pressed && (get_mods() & MOD_MASK_CTRL) && (keycode == KC_LEFT || keycode == KC_RGHT)) {
        if (keycode == KC_LEFT) {
            register_code(KC_LSFT);
            tap_code(KC_TAB);
            unregister_code(KC_LSFT);
        } else {
            tap_code(KC_TAB);
        }
        return false;
    }
    return true;
}

#ifdef OLED_ENABLE
#    include "oled_name.h"

// Left (USB) half, 32 px wide = 5 characters of the 6x8 font, 16 text rows:
// rows 0-8 the name bitmap (ink from the first pixel row), row 9 blank, rows
// 10-11 the host-OS label (white box, from oled_name.h), row 12 blank, row 13
// "Layer", row 14 the active layer's name. The right half keeps the vendor logo
// (oled_task_kb draws it when this returns true for the slave).
// Every line is positioned with oled_set_cursor and padded by hand: oled_write_ln
// on a line that exactly fills the 5 columns ("Layer", "linux", "10key") wraps
// to the next row and clears it with spaces, which is what used to wipe the top
// of the name on a Linux host (the OS line ended up on row 0).
#define TP_OLED_OS_ROW    10
#define TP_OLED_LAYER_ROW 13

static void tp_oled_line(uint8_t row, const char *s) {
    oled_set_cursor(0, row);
    for (uint8_t i = 0; i < oled_max_chars(); i++) {
        oled_write_char(*s ? *s++ : ' ', false);
    }
}

static const char *tp_layer_name(uint8_t layer) {
    switch (layer) {
        case 0: return "kbd";
        case 1: return "10key";
        case 2: return "mods";
        case 3: return "rgb";
        case 4: return "L4";
        case 5: return "L5";
        case 6: return "L6";
        default: return "?";
    }
}

// Host suspend turns the OLED off (suspend_power_down_quantum) and wake only
// restores the RGB. The vendor relied on OLED_TIMEOUT's activity path to turn it
// back on at the first keypress; with OLED_TIMEOUT 0 that path is compiled out,
// so turn it on explicitly at wake, with a safety net in oled_task_user (the task
// never runs while suspended, so an off panel there is always a leftover).
void suspend_wakeup_init_user(void) {
    oled_on();
}

bool oled_task_user(void) {
    if (!is_oled_on()) {
        oled_on();
    }
    // Dim both halves' displays after TP_IDLE_MS without input (the right half
    // sees the left half's and the trackpad's activity via SPLIT_ACTIVITY_ENABLE).
    // The driver only sends the contrast command when the level changes.
    oled_set_brightness(last_input_activity_elapsed() > TP_IDLE_MS ? OLED_IDLE_BRIGHTNESS : OLED_BRIGHTNESS);
    if (!is_keyboard_master()) {
        return true;
    }
    oled_set_cursor(0, 0);
    oled_write_raw_P(oled_name_bitmap, sizeof(oled_name_bitmap));
    uint8_t os_idx = OLED_OS_UNKNOWN;
#ifdef OS_DETECTION_ENABLE
    // Detected host OS in a white box, so a wrong guess is visible at a glance.
    switch (detected_host_os()) {
        case OS_MACOS:   os_idx = OLED_OS_MAC; break;
        case OS_IOS:     os_idx = OLED_OS_IOS; break;
        case OS_LINUX:   os_idx = OLED_OS_LINUX; break;
        case OS_WINDOWS: os_idx = OLED_OS_WIN; break;
        default:         break;
    }
#endif
    oled_set_cursor(0, TP_OLED_OS_ROW);
    oled_write_raw_P(oled_os_bitmap[os_idx], sizeof(oled_os_bitmap[0]));
    tp_oled_line(TP_OLED_LAYER_ROW, "Layer");
    tp_oled_line(TP_OLED_LAYER_ROW + 1, tp_layer_name(get_highest_layer(layer_state | default_layer_state)));
    return false;
}
#endif // OLED_ENABLE
