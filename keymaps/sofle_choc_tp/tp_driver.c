// Custom pointing-device driver for the Azoteq IQS5xx pad. Cursor motion
// matches the vendor azoteq_iqs5xx_get_report(); taps, drag and scroll come from
// a firmware gesture layer (tp_gesture) instead of the chip's gestures. It also:
//  - owns the init retry (the vendor driver initialises once and never again),
//  - polls the chip at most once per report period,
//  - stamps every fresh frame with a non-zero sequence number in report_id so
//    the master can drop split reports it has already consumed.
// The vendor driver .c is still linked, for its I2C helpers only.

#include QMK_KEYBOARD_H
#include "drivers/sensors/azoteq_iqs5xx.h"
#include "i2c_master.h"
#include "timer.h"
#include "pointing_device.h"

#if defined(TP_MEASURE) && defined(CONSOLE_ENABLE)
#    define TP_MEASURE_PRINT
#    include "print.h"
#endif

#ifndef MOUSE_SHARED_EP
#    error "tp_driver uses report_id as a split sequence number; MOUSE_SHARED_EP is required"
#endif

// Register map. These mirror the private #defines in
// vendor/vial-qmk/drivers/sensors/azoteq_iqs5xx.c (values checked against it).
#ifndef AZOTEQ_IQS5XX_ADDRESS
#    define AZOTEQ_IQS5XX_ADDRESS (0x74 << 1)
#endif
#ifndef AZOTEQ_IQS5XX_TIMEOUT_MS
#    define AZOTEQ_IQS5XX_TIMEOUT_MS 10
#endif
#define AZOTEQ_IQS5XX_REG_PREVIOUS_CYCLE_TIME 0x000C
#define AZOTEQ_IQS5XX_REG_X_RESOLUTION 0x066E
#define AZOTEQ_IQS5XX_REG_Y_RESOLUTION 0x0670
#define AZOTEQ_IQS5XX_REG_SINGLE_FINGER_GESTURES 0x06B7
#define AZOTEQ_IQS5XX_REG_END_COMMS 0xEEEE
// Not in the vendor .c (IQS5xx datasheet, XY config block): measurement only.
#define TP_REG_MAX_MULTI_TOUCHES 0x066A

// Defined (non-static) in the vendor .c but not declared in its header.
extern i2c_status_t azoteq_iqs5xx_end_session(void);

#define TP_RETRY_INTERVAL_MS 500
#define TP_RETRY_MAX 40 // give up after ~20 s
#define TP_POLL_INTERVAL_MS AZOTEQ_IQS5XX_REPORT_RATE

// The readback in tp_retry_init() only distinguishes "init completed" from
// "chip merely answering" because the chip's factory idle rates are not 10 ms.
STATIC_ASSERT(AZOTEQ_IQS5XX_REPORT_RATE == 10, "TP readback check assumes the init report rate differs from the chip defaults");

static bool           tp_ready;
static uint8_t        tp_tries;
static uint32_t       tp_retry_timer;
static uint32_t       tp_poll_timer;
static report_mouse_t tp_last;
static uint8_t        tp_seq;
static uint16_t       tp_cpi_cache;
static bool           tp_cpi_tried;

static void tp_cpi_refresh(void) {
    // The vendor getter does not end the I2C session.
    tp_cpi_cache = azoteq_iqs5xx_get_cpi();
    azoteq_iqs5xx_end_session();
}

// Base data block (0x000C-0x0015) plus finger 1 absolute X, Y (0x0016-0x0019).
typedef struct PACKED {
    azoteq_iqs5xx_base_data_t base;
    uint8_t                   ax_h, ax_l, ay_h, ay_l;
} tp_meas_data_t;
STATIC_ASSERT(sizeof(azoteq_iqs5xx_base_data_t) == 10, "base data block must be 10 bytes");
STATIC_ASSERT(sizeof(tp_meas_data_t) == 14, "data block must be 14 bytes");

#ifdef TP_MEASURE_PRINT
static void tp_measure_banner(void);
static void tp_measure_frame(const tp_meas_data_t *md, uint32_t elapsed, bool in_session);
static bool     tp_banner_due;     // first touch seen; print after the frame is stored
static bool     tp_banner_pending = true; // re-print until the first touch session
static uint32_t tp_banner_timer;
#    define TP_EV(name) uprintf("E " name "\n")
#else
#    define TP_EV(name) ((void)0)
#endif

void pointing_device_driver_init(void) {
    // The core calls this on the pad side only (POINTING_DEVICE_THIS_SIDE).
    azoteq_iqs5xx_init();
}

static bool tp_rate_is_set(azoteq_iqs5xx_charging_modes_t mode) {
    azoteq_iqs5xx_report_rate_t rate = {0};
    if (azoteq_iqs5xx_get_report_rate(&rate, mode, true) != I2C_STATUS_SUCCESS) {
        return false;
    }
    // set_report_rate() writes the high byte first; get_report_rate() returns the raw bytes.
    uint16_t ms = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(rate.h, rate.l);
    return ms == AZOTEQ_IQS5XX_REPORT_RATE;
}

// get_report runs from transport_slave in the slave's main-loop matrix_scan
// (not an ISR), so blocking here stalls only the right half's own scan; the
// serial link keeps answering with stale shared memory.
// The Azoteq driver initialises once in pointing_device_init(). On the
// pad-side half that runs only after the 2 s SPLIT_USB_DETECT wait, when the
// chip is already idle and clock-stretches its replies; if init fails, the
// vendor status stays failed (it is private) and nothing is ever reported
// until the next power cycle. A tap on the pad opens the chip's comms window,
// which is why touching it "fixes" it. Re-run the init until it verifiably
// completed. Upstream QMK (PR #25315) deliberately leaves retry to user code.
static void tp_retry_init(void) {
    if (tp_ready || tp_tries >= TP_RETRY_MAX) {
        return;
    }
    if (timer_elapsed32(tp_retry_timer) < TP_RETRY_INTERVAL_MS) {
        return;
    }
    tp_retry_timer = timer_read32();

    if (tp_tries++ == 0) {
        // The first init may already have failed and there is no public getter
        // for its status; re-running it recomputes the status from scratch and
        // costs about 110 ms once at boot.
        azoteq_iqs5xx_init();
        return;
    }
    // Init writes REPORT_RATE (10 ms) to the IDLE and IDLE_TOUCH registers; read them back.
    if (tp_rate_is_set(AZOTEQ_IQS5XX_IDLE) && tp_rate_is_set(AZOTEQ_IQS5XX_IDLE_TOUCH)) {
        tp_ready = true;
        if (!tp_cpi_tried) {
            tp_cpi_tried = true;
            tp_cpi_refresh();
        }
#ifdef TP_MEASURE_PRINT
        tp_measure_banner();
        tp_banner_timer = timer_read32();
#endif
        return;
    }
    azoteq_iqs5xx_init();
}

// Tunables (config.h may override). See README.
#ifndef TP_TAP_MAX_MS
#    define TP_TAP_MAX_MS 200
#endif
#ifndef TP_TAP_MAX_DRIFT
#    define TP_TAP_MAX_DRIFT 40
#endif
#ifndef TP_MULTI_TAP_MAX_MS
#    define TP_MULTI_TAP_MAX_MS 300
#endif
#ifndef TP_MULTI_TAP_MAX_MOTION
#    define TP_MULTI_TAP_MAX_MOTION 60
#endif
#ifndef TP_CLICK_FRAMES
#    define TP_CLICK_FRAMES 2
#endif
#ifndef TP_HOLD_MS
#    define TP_HOLD_MS 250
#endif
#ifndef TP_HOLD_STILL_DRIFT
#    define TP_HOLD_STILL_DRIFT 12
#endif
#ifndef TP_DEAD_ZONE_UNITS
#    define TP_DEAD_ZONE_UNITS 12
#endif
#ifndef TP_DRAG_START_UNITS
#    define TP_DRAG_START_UNITS 16
#endif
#ifndef TP_SCROLL_UNITS_PER_LINE
#    define TP_SCROLL_UNITS_PER_LINE 8
#endif
#ifndef TP_SCROLL_BATCH_FRAMES
#    define TP_SCROLL_BATCH_FRAMES 4 // one scroll report per this many fresh frames (10 ms each)
#endif
#ifndef TP_SCROLL_MAX_LINES_PER_REPORT
#    define TP_SCROLL_MAX_LINES_PER_REPORT 8
#endif
#ifndef TP_SWIPE3_MIN_UNITS_X
#    define TP_SWIPE3_MIN_UNITS_X 100 // ~4 mm of three-finger travel left/right
#endif
#ifndef TP_SWIPE3_MIN_UNITS_Y
#    define TP_SWIPE3_MIN_UNITS_Y 200 // ~8 mm of three-finger travel up/down
#endif
#ifndef TP_SWIPE3_MAX_MS
#    define TP_SWIPE3_MAX_MS 700
#endif

// TP_PENDING2/TP_ARMED2 are the two-finger counterparts of TP_PENDING/TP_ARMED
// (build 13); both drags share TP_DRAGGING and differ by sess_drag_btn (BUTTON1,
// or BUTTON8 for the two-finger drag, which the master translates).
typedef enum { TP_IDLE, TP_PENDING, TP_MOVING, TP_ARMED, TP_DRAGGING, TP_PENDING2, TP_ARMED2 } tp_mode_t;

static bool      sess_active;
static uint32_t  sess_t0;
static uint8_t   sess_maxf;
static int16_t   sess_ax0, sess_ay0, sess_drift1;
static int16_t   sess_arm_ax, sess_arm_ay;
static uint8_t   sess_arm_frames;  // frames since the rolling drag reference was set
static int16_t   sess_hold_dx, sess_hold_dy;     // rel motion held back while armed (build 12)
static int16_t   sess_replay_x, sess_replay_y;   // held-back motion still to be sent after drag start
static uint8_t   sess_drag_btn;                  // button held while TP_DRAGGING (bit index)
static bool      sess_drag2;                     // the current TP_DRAGGING is the two-finger kind
static uint32_t  sess_hold2_t0;                  // two-finger hold: when the count became 2
static bool      sess_hold2_ref;                 // ... and whether the position reference has been taken
static int16_t   sess_hold2_ax0, sess_hold2_ay0, sess_drift2;
static int16_t   sess_prev_ax, sess_prev_ay;     // finger 1 absolute on the previous frame (two-finger drag motion)
static uint16_t  sess_lines;       // non-zero wheel lines emitted this session
static uint8_t   prev_n, frames_since_change, zero_frames, fail_count;
static uint16_t  sess_motion;
static bool      sess_scroll_seen;
static tp_mode_t sess_mode;
static int16_t   acc_v, acc_h;
static uint8_t   click_btn; // button bit index
static uint8_t   click_frames_left;
static int32_t   sess_sw_x, sess_sw_y; // summed rel motion over settled three-finger frames
static bool      sess_swipe_fired;
static int16_t   pend_v, pend_h;     // scroll lines waiting for the next batch
static uint8_t   scroll_batch_ctr;

static void tp_gesture_reset(void) {
    sess_active       = false;
    sess_mode         = TP_IDLE;
    click_frames_left = 0;
    acc_v = acc_h = 0;
    zero_frames   = 0;
}

static inline int16_t tp_abs16(int16_t v) {
    return v < 0 ? -v : v;
}

static void tp_click(pointing_device_buttons_t b) {
    click_btn         = b;
    click_frames_left = TP_CLICK_FRAMES;
}

// Scroll accumulator: one wheel line per TP_SCROLL_UNITS_PER_LINE of travel.
static int8_t tp_scroll_axis(int16_t *acc, int16_t d) {
    if ((d ^ *acc) < 0) *acc = 0; // direction reversal drops the stale remainder
    *acc += d;
    int16_t out = *acc / TP_SCROLL_UNITS_PER_LINE; // truncation toward zero keeps it symmetric
    *acc -= out * TP_SCROLL_UNITS_PER_LINE;
    // Natural scrolling: content follows the fingers, so the sign flips here.
    return (int8_t)CONSTRAIN_HID(-out);
}

// Add new lines to a pending count; a direction reversal drops the remainder.
static void tp_pend_add(int16_t *pend, int8_t lines) {
    if (lines == 0) return;
    if ((lines < 0) != (*pend < 0) && *pend != 0) *pend = 0;
    int32_t v = (int32_t)*pend + lines;
    *pend     = (int16_t)(v > 1000 ? 1000 : (v < -1000 ? -1000 : v));
}

static int8_t tp_pend_take(int16_t *pend) {
    int16_t out = *pend;
    if (out > TP_SCROLL_MAX_LINES_PER_REPORT) out = TP_SCROLL_MAX_LINES_PER_REPORT;
    if (out < -TP_SCROLL_MAX_LINES_PER_REPORT) out = -TP_SCROLL_MAX_LINES_PER_REPORT;
    *pend -= out;
    return (int8_t)out;
}

static void tp_scroll_flush(report_mouse_t *report) {
    report->v = tp_pend_take(&pend_v);
    report->h = tp_pend_take(&pend_h);
    sess_lines += tp_abs16(report->v) + tp_abs16(report->h);
}

// Runs on every fresh frame and produces the report. The chip's own tap, hold,
// swipe and zoom gestures are disabled in config.h; only its scroll bit is used.
static void tp_gesture(const tp_meas_data_t *md, report_mouse_t *report) {
    const azoteq_iqs5xx_base_data_t *bd = &md->base;
    uint8_t n      = bd->number_of_fingers;
    int16_t rx     = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(bd->x.h, bd->x.l);
    int16_t ry     = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(bd->y.h, bd->y.l);
    int16_t ax     = (int16_t)((md->ax_h << 8) | md->ax_l);
    int16_t ay     = (int16_t)((md->ay_h << 8) | md->ay_l);
    bool    scroll = bd->gesture_events_1.scroll;

    if (!sess_active && n > 0) {
        sess_active      = true;
        sess_t0          = timer_read32();
        sess_maxf        = n;
        sess_ax0         = ax;
        sess_ay0         = ay;
        sess_drift1      = 0;
        sess_motion      = 0;
        sess_scroll_seen = false;
        pend_v = pend_h = 0;
        scroll_batch_ctr = 0;
        sess_sw_x = sess_sw_y = 0;
        sess_swipe_fired      = false;
        sess_mode        = TP_PENDING;
        acc_v = acc_h = 0;
        sess_hold_dx = sess_hold_dy = 0;
        sess_replay_x = sess_replay_y = 0;
        sess_drag_btn  = POINTING_DEVICE_BUTTON1;
        sess_drag2     = false;
        sess_hold2_ref = false;
        sess_drift2    = 0;
        sess_prev_ax   = ax;
        sess_prev_ay   = ay;
#ifdef TP_MEASURE_PRINT
        if (tp_banner_pending) {
            tp_banner_due = true; // printed at the end of get_report
        }
#endif
        sess_lines = 0;
        zero_frames = 0;
    }
    uint32_t elapsed = sess_active ? timer_elapsed32(sess_t0) : 0;
#ifdef TP_MEASURE_PRINT
    tp_measure_frame(md, elapsed, sess_active);
#endif

    if (n != prev_n) {
        frames_since_change = 0;
    } else if (frames_since_change < 255) {
        frames_since_change++;
    }
    prev_n = n;
    zero_frames = (n == 0) ? (zero_frames < 255 ? zero_frames + 1 : 255) : 0;

    bool swallow_motion = false;
    // A finger left on the pad after a multi-finger gesture twitches when the
    // count changes: ignore motion for the first 2 frames after the change.
    bool twitch = sess_active && sess_maxf >= 2 && n == 1 && frames_since_change < 2;
    if (sess_active) {
        if (n > sess_maxf) sess_maxf = n;
        if (!twitch) {
            uint16_t m = (uint16_t)(tp_abs16(rx) + tp_abs16(ry));
            sess_motion = (sess_motion + m > 30000) ? 30000 : sess_motion + m;
        }
        sess_scroll_seen |= scroll;
        if (n == 1 && sess_maxf == 1) {
            int16_t d = MAX(tp_abs16(ax - sess_ax0), tp_abs16(ay - sess_ay0));
            if (d > sess_drift1) sess_drift1 = d;
        }
        // Second finger during the single-finger states abandons them; a
        // third does the same to the two-finger states.
        if (sess_maxf >= 2 && (sess_mode == TP_PENDING || sess_mode == TP_ARMED)) {
            sess_mode = TP_MOVING;
        }
        if (sess_maxf >= 3 && (sess_mode == TP_PENDING2 || sess_mode == TP_ARMED2)) {
            sess_mode = TP_MOVING;
        }
        if (n == 1 && sess_maxf == 1) {
            if (sess_mode == TP_PENDING) {
                // Arming needs stillness, not just "within tap drift".
                if (sess_drift1 > TP_HOLD_STILL_DRIFT) {
                    sess_mode = TP_MOVING;
                } else if (elapsed >= TP_HOLD_MS) {
                    sess_mode       = TP_ARMED;
                    sess_arm_ax     = ax;
                    sess_arm_ay     = ay;
                    sess_arm_frames = 0;
                    sess_hold_dx = sess_hold_dy = 0;
                    TP_EV("arm");
                }
            } else if (sess_mode == TP_ARMED) {
                // Build 12: while armed the cursor does not move. The motion is
                // held back, so when the drag starts the button goes down where
                // the finger rested, and the held-back motion is replayed on the
                // following frames so the drag catches up with the finger. Before
                // this, the first TP_DRAG_START_UNITS of a drag moved the cursor
                // without the button and the selection began that far in.
                sess_hold_dx += rx;
                sess_hold_dy += ry;
                if (MAX(tp_abs16(ax - sess_arm_ax), tp_abs16(ay - sess_arm_ay)) > TP_DRAG_START_UNITS) {
                    sess_mode      = TP_DRAGGING; // button goes down with zero motion first
                    swallow_motion = true;
                    sess_replay_x  = sess_hold_dx;
                    sess_replay_y  = sess_hold_dy;
                    TP_EV("drag_start");
                } else if (++sess_arm_frames >= 20) {
                    // Rolling reference (every 200 ms): a resting finger creeps
                    // slowly (measured <= 7 units per 200 ms) and never reaches
                    // the threshold, while a real drag passes it easily. The
                    // creep is dropped with the reference.
                    sess_arm_ax     = ax;
                    sess_arm_ay     = ay;
                    sess_arm_frames = 0;
                    sess_hold_dx = sess_hold_dy = 0;
                }
            }
        }
#ifdef TP_DRAG2_BUTTON
        // Two fingers (build 13): both held still for TP_HOLD_MS, then moved,
        // is a drag holding TP_DRAG2_BUTTON (right by default). Mirrors the
        // one-finger arm/drag above with one difference: the chip's rel x/y is
        // zero for two fingers unless its scroll gesture is running (in
        // measurements/2026-10-10-right-half-measure.log every f=2 frame
        // without the scroll bit has x=y=0), so this path works from finger
        // 1's absolute position throughout: stillness, the start threshold
        // and the drag motion itself (see the motion block).
        if (sess_maxf == 2 && sess_mode != TP_DRAGGING) {
            if (n == 2 && frames_since_change == 0) {
                // The count just became 2 (also after a 2-1-2 flicker): the
                // hold starts now. The position reference is taken 2 frames
                // later, when the landing has settled (finger 1 can shift as
                // the second finger lands).
                sess_mode      = TP_PENDING2;
                sess_hold2_t0  = timer_read32();
                sess_hold2_ref = false;
                sess_drift2    = 0;
            } else if (n != 2 && (sess_mode == TP_PENDING2 || sess_mode == TP_ARMED2)) {
                sess_mode = TP_MOVING; // a finger lifted before the drag started: plain pointing
            }
            if ((sess_mode == TP_PENDING2 || sess_mode == TP_ARMED2) && frames_since_change >= 2) {
                if (!sess_hold2_ref) {
                    sess_hold2_ref = true;
                    sess_hold2_ax0 = ax;
                    sess_hold2_ay0 = ay;
                }
                int16_t d = MAX(tp_abs16(ax - sess_hold2_ax0), tp_abs16(ay - sess_hold2_ay0));
                if (d > sess_drift2) sess_drift2 = d;
                if (sess_mode == TP_PENDING2) {
                    if (sess_drift2 > TP_HOLD_STILL_DRIFT || sess_lines != 0 || pend_v != 0 || pend_h != 0) {
                        sess_mode = TP_MOVING; // moved or scrolled: no drag this session
                    } else if (timer_elapsed32(sess_hold2_t0) >= TP_HOLD_MS) {
                        sess_mode       = TP_ARMED2;
                        sess_arm_ax     = ax;
                        sess_arm_ay     = ay;
                        sess_arm_frames = 0;
                        TP_EV("arm2");
                    }
                } else if (MAX(tp_abs16(ax - sess_arm_ax), tp_abs16(ay - sess_arm_ay)) > TP_DRAG_START_UNITS) {
                    // Button down with zero motion first; the travel since the
                    // arm reference is replayed over the following frames. The
                    // drag is reported as a BUTTON8 level: this half knows no
                    // modifier state, so the master maps it to TP_DRAG2_BUTTON
                    // (right), or to TP_DRAG2_CTRL_BUTTON (middle) when Ctrl is
                    // held (keymap.c).
                    sess_mode      = TP_DRAGGING;
                    sess_drag_btn  = POINTING_DEVICE_BUTTON8;
                    sess_drag2     = true;
                    swallow_motion = true;
                    sess_replay_x  = ax - sess_arm_ax;
                    sess_replay_y  = ay - sess_arm_ay;
                    TP_EV("drag2_start");
                } else if (++sess_arm_frames >= 20) {
                    sess_arm_ax     = ax; // rolling reference, as for one finger
                    sess_arm_ay     = ay;
                    sess_arm_frames = 0;
                }
            }
        }
#endif
    }

    // Cursor motion: one finger only, not while scrolling. A two-finger drag
    // (drag2) is the exception: it moves with one or two fingers, from finger
    // 1's absolute deltas, and skips the 2 frames after a count change (the
    // chip renumbers the fingers when one lifts, which jumps the absolute).
    bool drag2     = sess_mode == TP_DRAGGING && sess_drag2;
    bool dead_zone = sess_active && ((sess_mode == TP_PENDING && sess_drift1 <= TP_DEAD_ZONE_UNITS) || sess_mode == TP_ARMED || sess_mode == TP_ARMED2);
    if (drag2) {
        if (n >= 1 && n <= 2 && !swallow_motion && frames_since_change >= 2) {
            int32_t ox = (int32_t)(ax - sess_prev_ax) + sess_replay_x;
            int32_t oy = (int32_t)(ay - sess_prev_ay) + sess_replay_y;
            report->x     = CONSTRAIN_HID_XY(ox);
            report->y     = CONSTRAIN_HID_XY(oy);
            sess_replay_x = (int16_t)(ox - report->x);
            sess_replay_y = (int16_t)(oy - report->y);
        }
    } else if (n == 1 && !scroll && !swallow_motion && !twitch && !dead_zone) {
        int16_t ox = rx, oy = ry;
        if (sess_mode == TP_DRAGGING) {
            // Replay the motion held back while armed; what does not fit the
            // HID range is carried to the next frame.
            ox += sess_replay_x;
            oy += sess_replay_y;
            report->x     = CONSTRAIN_HID_XY(ox);
            report->y     = CONSTRAIN_HID_XY(oy);
            sess_replay_x = ox - report->x;
            sess_replay_y = oy - report->y;
        } else {
            report->x = CONSTRAIN_HID_XY(ox);
            report->y = CONSTRAIN_HID_XY(oy);
        }
    }
    if (n > 0) {
        sess_prev_ax = ax;
        sess_prev_ay = ay;
    }
    // Scroll: exactly two fingers; three or more suppress it for the session,
    // and so do an armed two-finger hold and a two-finger drag.
    if (sess_active && scroll && n == 2 && sess_maxf < 3 && sess_mode != TP_ARMED2 && !drag2) {
        tp_pend_add(&pend_v, tp_scroll_axis(&acc_v, ry));
        tp_pend_add(&pend_h, tp_scroll_axis(&acc_h, rx));
    }
    // Batched emission at a steady cadence (macOS wheel acceleration keys off
    // event density and count). The counter ticks on every fresh frame of the
    // session, scrolling or not; empty batches send 0.
    if (sess_active && n > 0 && ++scroll_batch_ctr >= TP_SCROLL_BATCH_FRAMES) {
        scroll_batch_ctr = 0;
        tp_scroll_flush(report);
    }

    // Three-finger swipe. Sign convention: rel x/y use the same sign as the
    // cursor deltas sent for one finger (+x right, +y down on screen), so
    // +X = finger moving right on the pad, -Y = finger moving up. Evidence
    // (measurements/2026-10-10-right-half-measure.log, swipe sessions with
    // maxf=3, scr=1): the sessions with sumy=569/822 show y=-17..-51 while the
    // absolute ay falls 632 -> 43, and the sumx=524/484 sessions show negative
    // x while ax falls; rel is the signed delta of the absolute position.
    // Whether a falling ay is physically "up" depends on the pad mounting; it
    // is assumed to match the cursor (rel -y moves the cursor up).
    // Once three fingers have been seen, frames with two or three fingers
    // count (a finger sliding off the edge of the 43 mm pad mid-swipe must not
    // stop the sum; scroll is already suppressed for these sessions). The 2
    // frames after any count change are skipped (staggered landing / lift).
    // The chip reports non-zero rel during these frames because its scroll
    // bit is set.
    if (sess_active && sess_maxf >= 3 && n >= 2 && frames_since_change >= 2) {
        sess_sw_x += rx;
        sess_sw_y += ry;
    }
    if (sess_active && sess_maxf >= 3 && !sess_swipe_fired && elapsed <= TP_SWIPE3_MAX_MS) {
        int32_t axs = sess_sw_x < 0 ? -sess_sw_x : sess_sw_x;
        int32_t ays = sess_sw_y < 0 ? -sess_sw_y : sess_sw_y;
        // Horizontal travel is scarce on this pad, so left/right needs less
        // distance and only has to beat the vertical component.
        if (axs >= TP_SWIPE3_MIN_UNITS_X && axs > ays) {
            sess_swipe_fired = true;
            if (sess_sw_x > 0) {
                tp_click(POINTING_DEVICE_BUTTON5);
                TP_EV("swipe R");
            } else {
                tp_click(POINTING_DEVICE_BUTTON4);
                TP_EV("swipe L");
            }
        } else if (ays >= TP_SWIPE3_MIN_UNITS_Y && ays > 2 * axs) {
            sess_swipe_fired = true;
            if (sess_sw_y < 0) {
                tp_click(POINTING_DEVICE_BUTTON6);
                TP_EV("swipe U");
            } else {
                tp_click(POINTING_DEVICE_BUTTON7);
                TP_EV("swipe D");
            }
        }
    }

    // Lift: decide once.
    // A drag ends only after 2 consecutive empty frames, so one dropped frame
    // does not release the button.
    if (sess_active && n == 0 && (sess_mode != TP_DRAGGING || zero_frames >= 2)) {
        if (sess_mode == TP_DRAGGING) {
            TP_EV("drag_end");
        } else if (sess_maxf == 1 && (sess_mode == TP_PENDING || sess_mode == TP_MOVING) && elapsed <= TP_TAP_MAX_MS && sess_drift1 <= TP_TAP_MAX_DRIFT) {
            tp_click(POINTING_DEVICE_BUTTON1);
            TP_EV("click1");
        } else if (sess_maxf == 2 && sess_lines == 0 && elapsed <= TP_MULTI_TAP_MAX_MS && sess_motion <= TP_MULTI_TAP_MAX_MOTION) {
            tp_click(POINTING_DEVICE_BUTTON2);
            TP_EV("click2");
        } else if (sess_maxf == 3 && !sess_swipe_fired && sess_lines == 0 && elapsed <= TP_MULTI_TAP_MAX_MS && sess_motion <= TP_MULTI_TAP_MAX_MOTION) {
            tp_click(POINTING_DEVICE_BUTTON3);
            TP_EV("click3");
        } // maxf == 1 && ARMED: a resting finger, no click
#ifdef TP_MEASURE_PRINT
        static const char *const mode_names[] = {"idle", "pending", "moving", "armed", "drag", "pending2", "armed2"};
        uprintf("S dur=%u maxf=%u motion=%u drift=%d scr=%u mode=%s\n", (unsigned)elapsed, (unsigned)sess_maxf, (unsigned)sess_motion, (int)sess_drift1, (unsigned)sess_scroll_seen, mode_names[sess_mode]);
#endif
        tp_scroll_flush(report); // do not lose the tail
        pend_v = pend_h = 0;
        scroll_batch_ctr = 0;
        sess_active = false;
        sess_mode   = TP_IDLE;
        acc_v = acc_h = 0;
        zero_frames   = 0;
    }

    uint8_t buttons = 0;
    if (sess_mode == TP_DRAGGING) {
        buttons = pointing_device_handle_buttons(buttons, true, (pointing_device_buttons_t)sess_drag_btn);
    }
    if (click_frames_left > 0) {
        buttons = pointing_device_handle_buttons(buttons, true, (pointing_device_buttons_t)click_btn);
        click_frames_left--;
    }
    report->buttons = buttons;
}

report_mouse_t pointing_device_driver_get_report(report_mouse_t mouse_report) {
    tp_retry_init();
    if (!tp_ready) {
        return tp_last; // all zero
    }
    if (timer_elapsed32(tp_poll_timer) < TP_POLL_INTERVAL_MS) {
        return tp_last; // same report_id: the master treats it as a duplicate
    }
    // The poll period is counted from the start of the read. A build-12
    // experiment counted it from the read's return (so the next poll would land
    // on the chip's comms window and the clock-stretch stall would vanish); it
    // made drags start noticeably later, for reasons not understood, and was
    // reverted the same day. The RGB blink is handled on the keymap side instead
    // (matrix_slave_scan_user).
    tp_poll_timer = timer_read32();
#ifdef TP_MEASURE_PRINT
    if (tp_banner_pending && timer_elapsed32(tp_banner_timer) >= 10000) {
        tp_banner_timer = timer_read32();
        tp_measure_banner(); // console may not have been attached at boot
    }
#endif

    tp_meas_data_t md = {0};
    if (i2c_read_register16(AZOTEQ_IQS5XX_ADDRESS, AZOTEQ_IQS5XX_REG_PREVIOUS_CYCLE_TIME, (uint8_t *)&md, sizeof(md), AZOTEQ_IQS5XX_TIMEOUT_MS) != I2C_STATUS_SUCCESS) {
        // Motion is cleared so a stale delta is not replayed when dedup is off.
        // Buttons are kept: they are our own levels (drag latch, synthetic
        // clicks), and a single glitch must not release a drag or truncate a
        // click. report_id stays, so the master still sees the same frame, and
        // the gesture state is left alone.
        tp_last.x = 0;
        tp_last.y = 0;
        tp_last.h = 0;
        tp_last.v = 0;
        // After ~100 ms of consecutive failures give up on the levels too:
        // release everything and drop the session. Fires once per outage.
        if (fail_count < 255 && ++fail_count == 10) {
            tp_last.buttons = 0;
            tp_gesture_reset();
        }
        return tp_last;
    }
    fail_count = 0;
    azoteq_iqs5xx_end_session();

    report_mouse_t report = {0};
    tp_gesture(&md, &report);

    tp_seq++;
    if (tp_seq == 0) {
        tp_seq = 1; // 0 is reserved for "no sequence" (zeroed report)
    }
    report.report_id = tp_seq;
    tp_last          = report;
#ifdef TP_MEASURE_PRINT
    if (tp_banner_due) {
        // After the frame is stored, so the ~8 I2C reads do not distort the first session.
        tp_banner_due     = false;
        tp_banner_pending = false;
        tp_measure_banner();
    }
#endif
    return report;
}

uint16_t pointing_device_driver_get_cpi(void) {
    // Cached in tp_retry_init: the slave handler calls this on every request.
    return tp_cpi_cache;
}

void pointing_device_driver_set_cpi(uint16_t cpi) {
    azoteq_iqs5xx_set_cpi(cpi);
    tp_cpi_refresh();
}

#ifdef TP_MEASURE_PRINT
static uint16_t tp_read16(uint16_t reg) {
    uint8_t b[2] = {0, 0};
    i2c_read_register16(AZOTEQ_IQS5XX_ADDRESS, reg, b, 2, AZOTEQ_IQS5XX_TIMEOUT_MS);
    return (uint16_t)((b[0] << 8) | b[1]);
}

static void tp_measure_banner(void) {
    static const char *const names[] = {"active", "idle_touch", "idle", "lp1", "lp2"};
    uprintf("TP product=%u\n", (unsigned)azoteq_iqs5xx_get_product());
    uprintf("TP res x=%u y=%u\n", (unsigned)tp_read16(AZOTEQ_IQS5XX_REG_X_RESOLUTION), (unsigned)tp_read16(AZOTEQ_IQS5XX_REG_Y_RESOLUTION));
    uint8_t mt = 0;
    i2c_read_register16(AZOTEQ_IQS5XX_ADDRESS, TP_REG_MAX_MULTI_TOUCHES, &mt, 1, AZOTEQ_IQS5XX_TIMEOUT_MS);
    uprintf("TP max_touches=%u\n", (unsigned)mt);
    for (uint8_t m = AZOTEQ_IQS5XX_ACTIVE; m <= AZOTEQ_IQS5XX_LP2; m++) {
        azoteq_iqs5xx_report_rate_t r = {0};
        if (azoteq_iqs5xx_get_report_rate(&r, (azoteq_iqs5xx_charging_modes_t)m, false) == I2C_STATUS_SUCCESS) {
            uprintf("TP rate %s=%ums\n", names[m], (unsigned)(uint16_t)AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(r.h, r.l));
        } else {
            uprintf("TP rate %s=err\n", names[m]);
        }
    }
    azoteq_iqs5xx_gesture_config_t cfg = {0};
    if (i2c_read_register16(AZOTEQ_IQS5XX_ADDRESS, AZOTEQ_IQS5XX_REG_SINGLE_FINGER_GESTURES, (uint8_t *)&cfg, sizeof(cfg), AZOTEQ_IQS5XX_TIMEOUT_MS) == I2C_STATUS_SUCCESS) {
        uprintf("TP gest tap_t=%u tap_d=%u hold=%u\n", (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.tap_time), (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.tap_distance), (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.hold_time));
        uprintf("TP gest swipe_t=%u swipe_d=%u scr_d=%u\n", (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.swipe_initial_time), (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.swipe_initial_distance), (unsigned)AZOTEQ_IQS5XX_SWAP_H_L_BYTES(cfg.scroll_initial_distance));
    } else {
        uprintf("TP gest read failed\n");
    }
    azoteq_iqs5xx_end_session();
}

// One line per fresh frame with a finger or gesture bit set.
static void tp_measure_frame(const tp_meas_data_t *md, uint32_t elapsed, bool in_session) {
    const azoteq_iqs5xx_base_data_t *bd = &md->base;
    int16_t rx = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(bd->x.h, bd->x.l);
    int16_t ry = AZOTEQ_IQS5XX_COMBINE_H_L_BYTES(bd->y.h, bd->y.l);
    int16_t ax = (int16_t)((md->ax_h << 8) | md->ax_l);
    int16_t ay = (int16_t)((md->ay_h << 8) | md->ay_l);
    uint8_t g0 = *(const uint8_t *)&bd->gesture_events_0;
    uint8_t g1 = *(const uint8_t *)&bd->gesture_events_1;
    if (bd->number_of_fingers > 0 || g0 || g1) {
        uprintf("t=%u f=%u x=%d y=%d ax=%d ay=%d g0=%02X g1=%02X\n", (unsigned)(in_session ? elapsed : 0), (unsigned)bd->number_of_fingers, (int)rx, (int)ry, (int)ax, (int)ay, g0, g1);
    }
}
#endif // TP_MEASURE_PRINT
