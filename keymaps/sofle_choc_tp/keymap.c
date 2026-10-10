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
// The re-stamp below is only safe while the EEPROM layout is byte-for-byte
// the one the board already holds. If the vendor pin, the layer count, the
// matrix size, or any Vial/QMK-settings feature changes, remove this hook
// (or clear EEPROM: hold the top-left key of the left half while plugging in,
// then load the saved .vil in Vial).
_Static_assert(DYNAMIC_KEYMAP_LAYER_COUNT == 7, "EEPROM re-stamp assumes the vendor vial layout");
_Static_assert(MATRIX_ROWS == 10 && MATRIX_COLS == 6, "EEPROM re-stamp assumes the vendor vial layout");
void via_init_kb(void) {
    if (!via_eeprom_is_valid()) {
        via_eeprom_set_valid(true);
    }
}

// Layout marker in the otherwise unused 4-byte EECONFIG_USER word. Written on
// every boot; a future build changes the value whenever the EEPROM layout
// changes and re-stamps the VIA magic only when the stored marker matches.
// This build still re-stamps unconditionally because the board holds vendor
// "vial"-layout data with no marker yet.
#define SCTP_LAYOUT_MARKER 0x53435431u  // 'SCT1': vendor vial layout, 7 layers
void keyboard_post_init_user(void) {
    if (eeconfig_read_user() != SCTP_LAYOUT_MARKER) {
        eeconfig_update_user(SCTP_LAYOUT_MARKER);
    }
}

#ifdef POINTING_DEVICE_ENABLE
#include "drivers/sensors/azoteq_iqs5xx.h"

// The Azoteq driver initialises once in pointing_device_init(). On the
// pad-side half that runs only after the 2 s SPLIT_USB_DETECT wait, when the
// chip is already idle and clock-stretches its replies; if init fails, the
// driver records a failed status and azoteq_iqs5xx_get_report() returns
// nothing until the next power cycle. A tap on the pad opens the chip's comms
// window, which is why touching it "fixes" it. Re-run the init on the pad-side
// half until it verifiably completed.
// Upstream QMK (PR #25315) deliberately leaves retry to user code.
#define TP_RETRY_INTERVAL_MS 500
#define TP_RETRY_MAX 40  // give up after ~20 s

// The readback below only distinguishes "init completed" from "chip merely
// answering" because the chip's factory idle rates are not 10 ms.
STATIC_ASSERT(AZOTEQ_IQS5XX_REPORT_RATE == 10, "TP readback check assumes the init report rate differs from the chip defaults");

static bool     tp_ready = false;
static uint8_t  tp_tries = 0;
static uint32_t tp_timer = 0;

static bool tp_rate_is_set(azoteq_iqs5xx_charging_modes_t mode) {
    azoteq_iqs5xx_report_rate_t rate = {0};
    if (azoteq_iqs5xx_get_report_rate(&rate, mode, true) != I2C_STATUS_SUCCESS) {
        return false;
    }
    // set_report_rate() writes the high byte first; get_report_rate() returns the raw bytes.
    uint16_t ms = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(rate.h, rate.l);
    return ms == AZOTEQ_IQS5XX_REPORT_RATE;
}

void housekeeping_task_user(void) {
    if (tp_ready || tp_tries >= TP_RETRY_MAX) {
        return;
    }
    // Same side selection as pointing_device_init() in quantum/pointing_device/pointing_device.c
    // (POINTING_DEVICE_THIS_SIDE with POINTING_DEVICE_RIGHT): the pad is on the right half.
    if (is_keyboard_left()) {
        return;
    }
    if (timer_elapsed32(tp_timer) < TP_RETRY_INTERVAL_MS) {
        return;
    }
    tp_timer = timer_read32();

    if (tp_tries++ == 0) {
        // The driver's init may already have failed and there is no public getter for
        // its status; re-running it recomputes the status from scratch and costs
        // about 110 ms once at boot.
        azoteq_iqs5xx_init();
        return;
    }
    // Init writes REPORT_RATE (10 ms) to the IDLE and IDLE_TOUCH registers; read them back.
    if (tp_rate_is_set(AZOTEQ_IQS5XX_IDLE) && tp_rate_is_set(AZOTEQ_IQS5XX_IDLE_TOUCH)) {
        tp_ready = true;
        return;
    }
    azoteq_iqs5xx_init();
}

// Natural (macOS-style) scrolling: content follows the fingers. The Azoteq
// driver copies the raw gesture delta into v/h; flip both here, on the master,
// so macOS can stay on its default "natural" mouse setting.
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    // Clamp first: -(-128) would wrap in int8.
    mouse_report.v = (mouse_report.v == INT8_MIN) ? INT8_MAX : -mouse_report.v;
    mouse_report.h = (mouse_report.h == INT8_MIN) ? INT8_MAX : -mouse_report.h;
    return mouse_report;
}
#endif // POINTING_DEVICE_ENABLE

// Default keymap. This can be changed in Vial. Use oled.c to change beavior that Vial cannot change.

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
/*
 * QWERTY
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  |  `   |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | ESC  |   Q  |   W  |   E  |   R  |   T  |                    |   Y  |   U  |   I  |   O  |   P  | Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | Tab  |   A  |   S  |   D  |   F  |   G  |-------.    ,-------|   H  |   J  |   K  |   L  |   ;  |  '   |
 * |------+------+------+------+------+------|       |    |       |------+------+------+------+------+------|
 * |LShift|   Z  |   X  |   C  |   V  |   B  |-------|    |-------|   N  |   M  |   ,  |   .  |   /  |RShift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Enter  /       \Space \  |RAISE | RCTR | RAlt | RGUI |
 *            |      |      |      |      |/       /         \      \ |      |      |      |      |
 *            `----------------------------------'           '------''---------------------------'
 */

[0] = LAYOUT(
  KC_GRV,   KC_1,   KC_2,    KC_3,    KC_4,    KC_5,                     KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_GRV,
  KC_ESC,   KC_Q,   KC_W,    KC_E,    KC_R,    KC_T,                     KC_Y,    KC_U,    KC_I,    KC_O,    KC_P,  KC_BSPC,
  KC_TAB,   KC_A,   KC_S,    KC_D,    KC_F,    KC_G,                     KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN,  KC_QUOT,
  KC_LSFT,  KC_Z,   KC_X,    KC_C,    KC_V,    KC_B, KC_MUTE,     XXXXXXX,KC_N,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,  KC_RSFT,
                 KC_LGUI,KC_LALT,KC_LCTL, MO(2), KC_ENT,      KC_SPC,  MO(3), KC_RCTL, KC_RALT, KC_RGUI
),
/*
 * COLEMAK
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  |  `   |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | ESC  |   Q  |   W  |   F  |   P  |   G  |                    |   J  |   L  |   U  |   Y  |   ;  | Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | TAB  |   A  |   R  |   S  |   T  |   D  |-------.    ,-------|   H  |   N  |   E  |   I  |   O  |  '   |
 * |------+------+------+------+------+------|  MUTE |    |       |------+------+------+------+------+------|
 * |LShift|   Z  |   X  |   C  |   V  |   B  |-------|    |-------|   K  |   M  |   ,  |   .  |   /  |RShift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Enter  /       \Space \  |RAISE | RCTR | RAlt | RGUI |
 *            |      |      |      |      |/       /         \      \ |      |      |      |      |
 *            `----------------------------------'           '------''---------------------------'
 */

[1] = LAYOUT(
  KC_GRV,   KC_1,   KC_2,    KC_3,    KC_4,    KC_5,                      KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_GRV,
  KC_ESC,   KC_Q,   KC_W,    KC_F,    KC_P,    KC_G,                      KC_J,    KC_L,    KC_U,    KC_Y, KC_SCLN,  KC_BSPC,
  KC_TAB,   KC_A,   KC_R,    KC_S,    KC_T,    KC_D,                      KC_H,    KC_N,    KC_E,    KC_I,    KC_O,  KC_QUOT,
  KC_LSFT,  KC_Z,   KC_X,    KC_C,    KC_V,    KC_B, KC_MUTE,      XXXXXXX,KC_K,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,  KC_RSFT,
                 KC_LGUI,KC_LALT,KC_LCTL,KC_TRNS, KC_ENT,      KC_SPC,  KC_TRNS, KC_RCTL, KC_RALT, KC_RGUI
),
/* LOWER
 * ,-----------------------------------------.                    ,-----------------------------------------.
 * |      |  F1  |  F2  |  F3  |  F4  |  F5  |                    |  F6  |  F7  |  F8  |  F9  | F10  | F11  |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * |  `   |   1  |   2  |   3  |   4  |   5  |                    |   6  |   7  |   8  |   9  |   0  | F12  |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | Tab  |   !  |   @  |   #  |   $  |   %  |-------.    ,-------|   ^  |   &  |   *  |   (  |   )  |   |  |
 * |------+------+------+------+------+------|  MUTE |    |       |------+------+------+------+------+------|
 * | Shift|  =   |  -   |  +   |   {  |   }  |-------|    |-------|   [  |   ]  |   ;  |   :  |   \  | Shift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Enter  /       \Space \  |RAISE | RCTR | RAlt | RGUI |
 *            |      |      |      |      |/       /         \      \ |      |      |      |      |
 *            `----------------------------------'           '------''---------------------------'
 */
[2] = LAYOUT(
  _______,   KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,                       KC_F6,   KC_F7,   KC_F8,   KC_F9,  KC_F10,  KC_F11,
  KC_GRV,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                       KC_6,    KC_7,    KC_8,    KC_9,    KC_0,  KC_F12,
  _______, KC_EXLM,   KC_AT, KC_HASH,  KC_DLR, KC_PERC,                       KC_CIRC, KC_AMPR, KC_ASTR, KC_LPRN, KC_RPRN, KC_PIPE,
  _______,  KC_EQL, KC_MINS, KC_PLUS, KC_LCBR, KC_RCBR, _______,       _______, KC_LBRC, KC_RBRC, KC_SCLN, KC_COLN, KC_BSLS, _______,
                       _______, _______, _______, _______, _______,       _______, _______, _______, _______, _______
),
/* RAISE
 * ,----------------------------------------.                    ,-----------------------------------------.
 * |      |      |      |      |      |      |                    |      |      |      |      |      |      |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | Esc  | Ins  | Pscr | Menu |      |      |                    |      | PWrd |  Up  | NWrd | DLine| Bspc |
 * |------+------+------+------+------+------|                    |------+------+------+------+------+------|
 * | Tab  | LAt  | LCtl |LShift|      | Caps |-------.    ,-------|      | Left | Down | Rigth|  Del | Bspc |
 * |------+------+------+------+------+------|  MUTE  |    |       |------+------+------+------+------+------|
 * |Shift | Undo |  Cut | Copy | Paste|      |-------|    |-------|      | LStr |      | LEnd |      | Shift|
 * `-----------------------------------------/       /     \      \-----------------------------------------'
 *            | LGUI | LAlt | LCTR |LOWER | /Enter  /       \Space \  |RAISE | RCTR | RAlt | RGUI |
 *            |      |      |      |      |/       /         \      \ |      |      |      |      |
 *            `----------------------------------'           '------''---------------------------'
 */
[3] = LAYOUT(
  _______, _______ , _______ , _______ , _______ , _______,                           _______,  _______  , _______,  _______ ,  _______ ,_______,
  _______,  KC_INS,  KC_PSCR,   KC_APP,  XXXXXXX, XXXXXXX,                        KC_PGUP, _______,   KC_UP, _______,_______, KC_BSPC,
  _______, KC_LALT,  KC_LCTL,  KC_LSFT,  XXXXXXX, KC_CAPS,                       KC_PGDN,  KC_LEFT, KC_DOWN, KC_RGHT,  KC_DEL, KC_BSPC,
  _______,KC_UNDO, KC_CUT, KC_COPY, KC_PASTE, XXXXXXX,  _______,       _______,  XXXXXXX, _______, XXXXXXX, _______,   XXXXXXX, _______,
                         _______, _______, _______, _______, _______,       _______, _______, _______, _______, _______
)
};

const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [0] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS), ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
    [1] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS), ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
    [2] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS), ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
    [3] = { ENCODER_CCW_CW(KC_TRNS, KC_TRNS), ENCODER_CCW_CW(KC_TRNS, KC_TRNS) },
};
