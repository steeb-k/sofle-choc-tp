#include QMK_KEYBOARD_H
#include "keymap_us_international.h"
#include "sendstring_us_international.h"

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

void keyboard_post_init_user(void) {
    if (eeconfig_read_user() != SCTP_LAYOUT_MARKER) {
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
    // Three-finger swipes arrive from the slave as BUTTON4..7 pulses (2 frames).
    // Only rising edges act, so one pulse gives exactly one key tap even though
    // this runs every 1 ms. The bits never reach the host.
    static uint8_t prev_swipe = 0;
    uint8_t        swipe      = mouse_report.buttons & 0x78; // bits 3..6
    uint8_t        edges      = swipe & (uint8_t)~prev_swipe;
    prev_swipe                = swipe;
    if (edges & 0x08) tap_code16(TP_SWIPE_LEFT_KEY);  // BUTTON4: fingers moved left
    if (edges & 0x10) tap_code16(TP_SWIPE_RIGHT_KEY); // BUTTON5: fingers moved right
    if (edges & 0x20) tap_code16(TP_SWIPE_UP_KEY);      // BUTTON6: finger up
    if (edges & 0x40) tap_code16(TP_SWIPE_DOWN_KEY);    // BUTTON7: finger down
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

// Default keymap, baked from the live Vial layout read on 2026-10-11 with
// tools/dump_layout.py (layout/current_layout.json). Vial's EEPROM copy still
// takes precedence day to day; this is what an EEPROM reset comes back to.
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
[0] = LAYOUT(
  KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5,   KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS,
  KC_ESC, KC_Q, KC_W, KC_E, KC_R, KC_T,   KC_Y, KC_U, KC_I, KC_O, KC_P, KC_BSPC,
  KC_TAB, KC_A, KC_S, KC_D, KC_F, KC_G,   KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT,
  KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_MUTE,   RM_TOGG, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_BSLS,
  MO(1), KC_LALT, KC_LCTL, KC_ENT, KC_LGUI,   MO(3), KC_SPC, KC_RCTL, KC_RALT, HYPR(KC_TAB)
),
[1] = LAYOUT(
  KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5,   KC_PSLS, KC_PAST, KC_PSLS, KC_LBRC, KC_RBRC, KC_EQL,
  KC_ESC, KC_Q, KC_W, KC_F, KC_P, KC_G,   KC_P7, KC_P8, KC_P9, KC_PAST, KC_SCLN, KC_DEL,
  KC_TAB, KC_A, KC_R, KC_S, KC_T, KC_D,   KC_P4, KC_P5, KC_P6, KC_PMNS, KC_O, KC_QUOT,
  KC_LSFT, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_MUTE,   KC_NO, KC_P1, KC_P2, KC_P3, KC_PPLS, KC_SLSH, KC_RSFT,
  KC_LGUI, KC_LALT, KC_LCTL, _______, KC_ENT,   KC_PENT, KC_P0, KC_PDOT, KC_PENT, KC_PENT
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

// Left (USB) half: the name bitmap, a rule, then "Layer" and the active layer's
// name. The right half keeps the vendor logo (oled_task_kb draws it when this
// returns true for the slave).
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
    static const char PROGMEM rule[32] = {[0 ... 31] = 0x18};  // 2 px line in the middle of the text row
    oled_set_cursor(0, 0);
    oled_write_raw_P(oled_name_bitmap, sizeof(oled_name_bitmap));
    oled_set_cursor(0, OLED_NAME_LINES);
    oled_write_raw_P(rule, sizeof(rule));
    oled_set_cursor(0, OLED_NAME_LINES + 1);
    oled_write_ln_P(PSTR("Layer"), false);
    oled_write_ln(tp_layer_name(get_highest_layer(layer_state | default_layer_state)), false);
    return false;
}
#endif // OLED_ENABLE
