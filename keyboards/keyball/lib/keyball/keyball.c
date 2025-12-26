/*
Copyright 2022 MURAOKA Taro (aka KoRoN, @kaoriya)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "quantum.h"
#ifdef SPLIT_KEYBOARD
#    include "transactions.h"
#endif

#include "keyball.h"
#include "drivers/sensors/pmw33xx_common.h"

#include <string.h>
#include <math.h>
#include "raw_hid.h"

const uint16_t CPI_DEFAULT    = KEYBALL_CPI_DEFAULT;
// Anything above this value makes the cursor fly across the screen.
const uint16_t CPI_MAX        = 3000 + 1;
const uint8_t SCROLL_DIV_MAX = 7;

const uint16_t AML_TIMEOUT_MIN = 100;
const uint16_t AML_TIMEOUT_MAX = 1000;
const uint16_t AML_TIMEOUT_QU  = 50;   // Quantization Unit

static const char BL = '\xB0'; // Blank indicator character
static const char LFSTR_ON[] PROGMEM = "\xB2\xB3";
static const char LFSTR_OFF[] PROGMEM = "\xB4\xB5";

keyball_t keyball = {
    .this_have_ball = false,
    .that_enable    = false,
    .that_have_ball = false,

    .this_motion = {0},
    .that_motion = {0},

    .cpi_value   = 0,

    .scroll_mode = false,
    .scroll_div  = 0,

    .pressing_keys = { BL, BL, BL, BL, BL, BL, 0 },
};

// Acceleration Tuning Globals
#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT
static keyball_accel_t kb_accel = ACCEL_LUT_DEFAULT;
static keyball_accel_t kb_accel_default = ACCEL_LUT_DEFAULT;
static uint16_t kb_last_speed = 0;

void keyball_set_acceleration_data(const keyball_accel_t *data) {
    kb_accel = *data;
    kb_accel_default = *data;
}

uint16_t keyball_get_last_speed(void) {
    return kb_last_speed;
}
#endif

//////////////////////////////////////////////////////////////////////////////
// Hook points

__attribute__((weak)) void keyball_on_adjust_layout(keyball_adjust_t v) {}

//////////////////////////////////////////////////////////////////////////////
// Static utilities

// divmod16 divides *v by div, returns the quotient, and assigns the remainder
// to *v.
static mouse_xy_report_t divmod16(mouse_xy_report_t *v, int16_t div) {
    mouse_xy_report_t r = *v / div;
    *v -= r * div;
    return r;
}

// clip2int8 clips an integer fit into int8_t.
static inline int8_t clip2int8(int16_t v) {
    return (v) < -127 ? -127 : (v) > 127 ? 127 : (int8_t)v;
}

#ifdef OLED_ENABLE
static const char *format_4d(int16_t d) {
    static char buf[5] = {0}; // max width (4) + NUL (1)
    char        lead   = ' ';

    if (d > 9999) {
        d    = 9999;
    } else if (d < -9999) {
        d    = -9999;
    }

    if (d < 0) {
        d    = -d;
        lead = '-';
    }
    buf[3] = (d % 10) + '0';
    d /= 10;
    if (d == 0) {
        buf[2] = lead;
        lead   = ' ';
    } else {
        buf[2] = (d % 10) + '0';
        d /= 10;
    }
    if (d == 0) {
        buf[1] = lead;
        lead   = ' ';
    } else {
        buf[1] = (d % 10) + '0';
        d /= 10;
    }
    buf[0] = lead;
    return buf;
}

static const char *format_cpi(int16_t d) {
    static char buf[10] = {0};

    sprintf(buf, "%5d", d);
    return buf;
}

static char to_1x(uint8_t x) {
    x &= 0x0f;
    return x < 10 ? x + '0' : x + 'a' - 10;
}
#endif

static void add_cpi(int16_t delta) {
    int16_t v = keyball_get_cpi() + delta;
    keyball_set_cpi(v < 1 ? 1 : v);
}

static void add_scroll_div(int8_t delta) {
    int8_t v = keyball_get_scroll_div() + delta;
    keyball_set_scroll_div(v < 1 ? 1 : v);
}

//////////////////////////////////////////////////////////////////////////////
// Pointing device driver

void pointing_device_init_kb(void) {

}

__attribute__((weak)) void keyball_on_apply_motion_to_mouse_move(report_mouse_t *report, report_mouse_t *output, bool is_left) {
// #if KEYBALL_MODEL == 61 || KEYBALL_MODEL == 39 || KEYBALL_MODEL == 147 || KEYBALL_MODEL == 44
//     output->x = clip2int8(report->y);
//     output->y = clip2int8(report->x);
//     if (is_left) {
//         output->x = -output->x;
//         output->y = -output->y;
//     }
// #else
// #    error("unknown Keyball model")
// #endif
    output->x = report->x;
    output->y = report->y;

}

__attribute__((weak)) void keyball_on_apply_motion_to_mouse_scroll(report_mouse_t *report, report_mouse_t *output, bool is_left) {
    // consume motion of trackball.
    #ifndef KEYBALL_SCROLL_DIVISOR
    #define KEYBALL_SCROLL_DIVISOR (1 << (keyball_get_scroll_div() - 1))
    #endif
    int16_t div = KEYBALL_SCROLL_DIVISOR;
    int16_t x = divmod16(&report->x, div);
    int16_t y = divmod16(&report->y, div);

    // apply to mouse report.
#if KEYBALL_MODEL == 61 || KEYBALL_MODEL == 39 || KEYBALL_MODEL == 147 || KEYBALL_MODEL == 44
    output->h = clip2int8(x);
    output->v = -clip2int8(y);
    if (is_left) {
        output->h = -output->h;
        output->v = -output->v;
    }

#else
#    error("unknown Keyball model")
#endif

    // Scroll snapping
#if KEYBALL_SCROLLSNAP_ENABLE == 1
    // Old behavior up to 1.3.2)
    uint32_t now = timer_read32();
    if (output->h != 0 || output->v != 0) {
        keyball.scroll_snap_last = now;
    } else if (TIMER_DIFF_32(now, keyball.scroll_snap_last) >= KEYBALL_SCROLLSNAP_RESET_TIMER) {
        keyball.scroll_snap_tension_h = 0;
    }
    if (abs(keyball.scroll_snap_tension_h) < KEYBALL_SCROLLSNAP_TENSION_THRESHOLD) {
        keyball.scroll_snap_tension_h += y;
        output->h = 0;
    }
#elif KEYBALL_SCROLLSNAP_ENABLE == 2
    // New behavior
    switch (keyball_get_scrollsnap_mode()) {
        case KEYBALL_SCROLLSNAP_MODE_VERTICAL:
            output->h = 0;
            break;
        case KEYBALL_SCROLLSNAP_MODE_HORIZONTAL:
            output->v = 0;
            break;
        default:
            // pass by without doing anything
            break;
    }
#endif
}

#ifdef KEYBALL_POINTER_ACCEL_ENABLE

#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT
static uint16_t isqrt16(uint16_t n) {
    uint16_t root = 0;
    uint16_t bit = 1 << 14;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static void keyball_accel_apply_lut(keyball_motion_t *accum, int8_t dx, int8_t dy, int16_t *out_x, int16_t *out_y) {
    uint16_t speed = isqrt16((int16_t)dx * dx + (int16_t)dy * dy);
    if (speed > kb_last_speed) {
        kb_last_speed = speed;
    }

    int32_t scale;

    // Map speed (0..127+) to LUT index (0..31)
    // We use 0..127 as the input range for the LUT.
    // Index = speed / 4.
    uint8_t index = speed >> 2;
    if (index >= ACCEL_LUT_SIZE - 1) {
        index = ACCEL_LUT_SIZE - 2;
    }

    // Linear Interpolation
    // remainder is the lower 2 bits of speed
    uint16_t y1 = kb_accel.lut[index];
    uint16_t y2 = kb_accel.lut[index+1];
    uint8_t rem = speed & 3; // 0..3

    // y = y1 + (y2 - y1) * rem / 4
    // Fixed point 8.8
    scale = y1 + (((int32_t)(y2 - y1) * rem) >> 2);

    // Apply Global Gain (Q8.8)
    // scale = scale * gain / 256
    if (kb_accel.global_gain > 0) {
        scale = (scale * kb_accel.global_gain) >> 8;
    } else {
        // Fallback if gain is 0 (shouldn't happen with default, but safety)
        // Treat 0 as 1.0x to avoid stuck cursor
        if (kb_accel.global_gain == 0) {
             // Do nothing (keep scale)
        }
    }

    // Safety clamp from tuner
    if (kb_accel.max_speed_limit > 0) {
        if (scale > kb_accel.max_speed_limit) scale = kb_accel.max_speed_limit;
    }

    int32_t sx = (int32_t)dx * scale + accum->remainder_x;
    int32_t sy = (int32_t)dy * scale + accum->remainder_y;

    *out_x = sx >> 8;
    *out_y = sy >> 8;

    accum->remainder_x = sx - (*out_x << 8);
    accum->remainder_y = sy - (*out_y << 8);
}
#endif

#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_SIMPLE
static void keyball_accel_apply_simple(keyball_motion_t *accum, int8_t dx, int8_t dy, int16_t *out_x, int16_t *out_y) {
    // Legacy Algorithm: output = input * (BASE + SPEED * FACTOR)
    float speed = sqrtf(dx * dx + dy * dy);
    float scale = KEYBALL_ACCEL_BASE + (speed * KEYBALL_ACCEL_FACTOR);

    if (scale > KEYBALL_ACCEL_MAX) scale = KEYBALL_ACCEL_MAX;

    // Simple float scaling without remainder accumulation (Legacy behavior)
    *out_x = (int16_t)(dx * scale);
    *out_y = (int16_t)(dy * scale);
}
#endif

static void apply_acceleration(keyball_motion_t *accum, int8_t dx, int8_t dy, int16_t *out_x, int16_t *out_y) {
#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT
    keyball_accel_apply_lut(accum, dx, dy, out_x, out_y);
#elif KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_SIMPLE
    keyball_accel_apply_simple(accum, dx, dy, out_x, out_y);
#else
    // NONE or CUSTOM fallbacks
    *out_x = dx;
    *out_y = dy;
#endif
}
#endif

static void motion_to_mouse(report_mouse_t *report, report_mouse_t *output, bool is_left, bool as_scroll, keyball_motion_t *accum) {
#ifdef KEYBALL_POINTER_ACCEL_ENABLE
    int16_t ax, ay;
    apply_acceleration(accum, report->x, report->y, &ax, &ay);
    accum->x += ax;
    accum->y += ay;
#else
    accum->x += report->x;
    accum->y += report->y;
#endif

    // Clip to int8_t range for the report, but keep full precision in accum
    int8_t rx = clip2int8(accum->x);
    int8_t ry = clip2int8(accum->y);

    report_mouse_t temp_report = {0};
    temp_report.x = rx;
    temp_report.y = ry;

    if (as_scroll) {
        keyball_on_apply_motion_to_mouse_scroll(&temp_report, output, is_left);
        // Update accumulator with the remainder (temp_report.x/y now holds the remainder)
        // plus any overflow that was clipped out
        accum->x = temp_report.x + (accum->x - rx);
        accum->y = temp_report.y + (accum->y - ry);
    } else {
        keyball_on_apply_motion_to_mouse_move(&temp_report, output, is_left);
        accum->x = 0;
        accum->y = 0;
    }

    // clear motion
    report->x = 0;
    report->y = 0;
}

report_mouse_t pointing_device_task_combined_kb(report_mouse_t left_report, report_mouse_t right_report) {
    report_mouse_t output = {0};
    report_mouse_t *this_report = is_keyboard_left() ? &left_report : &right_report;
    report_mouse_t *that_report = is_keyboard_left() ? &right_report : &left_report;
    motion_to_mouse(this_report, &output, is_keyboard_left(), keyball.scroll_mode, &keyball.this_motion);
    motion_to_mouse(that_report, &output, !is_keyboard_left(), keyball.scroll_mode ^ keyball.this_have_ball, &keyball.that_motion);
    // store mouse report for OLED.
    keyball.last_mouse = output;
    return output;
}

//////////////////////////////////////////////////////////////////////////////
// Split RPC

#ifdef SPLIT_KEYBOARD

static void rpc_get_info_handler(uint8_t in_buflen, const void *in_data, uint8_t out_buflen, void *out_data) {
    keyball_info_t info = {
        .ballcnt = keyball.this_have_ball ? 1 : 0,
    };
    *(keyball_info_t *)out_data = info;
    keyball_on_adjust_layout(KEYBALL_ADJUST_SECONDARY);
}

static void rpc_get_info_invoke(void) {
    static bool     negotiated = false;
    static uint32_t last_sync  = 0;
    static int      round      = 0;
    uint32_t        now        = timer_read32();
    if (negotiated || TIMER_DIFF_32(now, last_sync) < KEYBALL_TX_GETINFO_INTERVAL) {
        return;
    }
    last_sync = now;
    round++;
    keyball_info_t recv = {0};
    if (!transaction_rpc_exec(KEYBALL_GET_INFO, 0, NULL, sizeof(recv), &recv)) {
        if (round < KEYBALL_TX_GETINFO_MAXTRY) {
            dprintf("keyball:rpc_get_info_invoke: missed #%d\n", round);
            return;
        }
    }
    negotiated             = true;
    keyball.that_enable    = true;
    keyball.that_have_ball = recv.ballcnt > 0;
    dprintf("keyball:rpc_get_info_invoke: negotiated #%d %d\n", round, keyball.that_have_ball);

    // split keyboard negotiation completed.

#    ifdef VIA_ENABLE
    // adjust VIA layout options according to current combination.
    uint8_t  layouts = (keyball.this_have_ball ? (is_keyboard_left() ? 0x02 : 0x01) : 0x00) | (keyball.that_have_ball ? (is_keyboard_left() ? 0x01 : 0x02) : 0x00);
    uint32_t curr    = via_get_layout_options();
    uint32_t next    = (curr & ~0x3) | layouts;
    if (next != curr) {
        via_set_layout_options(next);
    }
#    endif

    keyball_on_adjust_layout(KEYBALL_ADJUST_PRIMARY);
}

#endif

//////////////////////////////////////////////////////////////////////////////
// OLED utility

#ifdef OLED_ENABLE
// clang-format off
const char PROGMEM code_to_name[] = {
    'a', 'b', 'c', 'd', 'e', 'f',  'g', 'h', 'i',  'j',
    'k', 'l', 'm', 'n', 'o', 'p',  'q', 'r', 's',  't',
    'u', 'v', 'w', 'x', 'y', 'z',  '1', '2', '3',  '4',
    '5', '6', '7', '8', '9', '0',  'R', 'E', 'B',  'T',
    '_', '-', '=', '[', ']', '\\', '#', ';', '\'', '`',
    ',', '.', '/',
};
// clang-format on
#endif

void keyball_oled_render_ballinfo(void) {
#ifdef OLED_ENABLE
    // Format: `Ball:{mouse x}{mouse y}{mouse h}{mouse v}`
    //
    // Output example:
    //
    //     Ball: -12  34   0   0

    // 1st line, "Ball" label, mouse x, y, h, and v.
    oled_write_P(PSTR("Ball\xB1"), false);
    oled_write(format_4d(keyball.last_mouse.x), false);
    oled_write(format_4d(keyball.last_mouse.y), false);
    oled_write(format_4d(keyball.last_mouse.h), false);
    oled_write(format_4d(keyball.last_mouse.v), false);

    // 2nd line, empty label and CPI
    oled_write_P(PSTR("    \xB1\xBC\xBD"), false);
    oled_write(format_cpi(keyball_get_cpi()), false);
    oled_write_char(' ', false);

    // indicate scroll snap mode: "VT" (vertical), "HN" (horiozntal), and "SCR" (free)
#if 1 && KEYBALL_SCROLLSNAP_ENABLE == 2
    switch (keyball_get_scrollsnap_mode()) {
        case KEYBALL_SCROLLSNAP_MODE_VERTICAL:
            oled_write_P(PSTR("VT"), false);
            break;
        case KEYBALL_SCROLLSNAP_MODE_HORIZONTAL:
            oled_write_P(PSTR("HO"), false);
            break;
        default:
            oled_write_P(PSTR("\xBE\xBF"), false);
            break;
    }
#else
    oled_write_P(PSTR("\xBE\xBF"), false);
#endif
    // indicate scroll mode: on/off
    if (keyball.scroll_mode) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }

    // indicate scroll divider:
    oled_write_P(PSTR(" \xC0\xC1"), false);
    oled_write_char('0' + keyball_get_scroll_div(), false);
#endif
}

void keyball_oled_render_ballsubinfo(void) {
#ifdef OLED_ENABLE
#endif
}

void keyball_oled_render_keyinfo(void) {
#ifdef OLED_ENABLE
    // Format: `Key :  R{row}  C{col} K{kc} {name}{name}{name}`
    //
    // Where `kc` is lower 8 bit of keycode.
    // Where `name`s are readable labels for pressing keys, valid between 4 and 56.
    //
    // `row`, `col`, and `kc` indicates the last processed key,
    // but `name`s indicate unreleased keys in best effort.
    //
    // It is aligned to fit with output of keyball_oled_render_ballinfo().
    // For example:
    //
    //     Key :  R2  C3 K06 abc
    //     Ball:   0   0   0   0

    // "Key" Label
    oled_write_P(PSTR("Key \xB1"), false);

    // Row and column
    oled_write_char('\xB8', false);
    oled_write_char(to_1x(keyball.last_pos.row), false);
    oled_write_char('\xB9', false);
    oled_write_char(to_1x(keyball.last_pos.col), false);

    // Keycode
    oled_write_P(PSTR("\xBA\xBB"), false);
    oled_write_char(to_1x(keyball.last_kc >> 4), false);
    oled_write_char(to_1x(keyball.last_kc), false);

    // Pressing keys
    oled_write_P(PSTR("  "), false);
    oled_write(keyball.pressing_keys, false);
#endif
}

void keyball_oled_render_layerinfo(void) {
#ifdef OLED_ENABLE
    // Format: `Layer:{layer state}`
    //
    // Output example:
    //
    //     Layer:-23------------
    //
    oled_write_P(PSTR("L\xB6\xB7r\xB1"), false);
    for (uint8_t i = 1; i < 8; i++) {
        oled_write_char((layer_state_is(i) ? to_1x(i) : BL), false);
    }
    oled_write_char(' ', false);

#    ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
    oled_write_P(PSTR("\xC2\xC3"), false);
    if (get_auto_mouse_enable()) {
        oled_write_P(LFSTR_ON, false);
    } else {
        oled_write_P(LFSTR_OFF, false);
    }

    oled_write(format_4d(get_auto_mouse_timeout() / 10) + 1, false);
    oled_write_char('0', false);
#    else
    oled_write_P(PSTR("\xC2\xC3\xB4\xB5 ---"), false);
#    endif
#endif
}

//////////////////////////////////////////////////////////////////////////////
// RawHID

// Command IDs
enum {
#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT
    CMD_SET_CURVE_PT = 0x10,
    CMD_SET_CURVE_ALL = 0x11,
    CMD_READ_ALL = 0x12,
    CMD_GET_SPEED = 0x20,
    CMD_RESET_CONFIG = 0x30
#endif
};

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t cmd = data[0];

#if KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT
    if (cmd == CMD_SET_CURVE_PT) {
        // [CMD, INDEX, VAL_H, VAL_L, MAX_H, MAX_L]
        uint8_t idx = data[1];
        if (idx < ACCEL_LUT_SIZE) {
            uint16_t val = (data[2] << 8) | data[3];
            kb_accel.lut[idx] = val;
        }
        uint16_t max_limit = (data[4] << 8) | data[5];
        kb_accel.max_speed_limit = max_limit;

        // Extended capability: Gain at index 6,7
        if (length > 7) {
             uint16_t gain = (data[6] << 8) | data[7];
             if (gain > 0) kb_accel.global_gain = gain;
        }
    }
    else if (cmd == CMD_SET_CURVE_ALL) {
        // [CMD, START_IDX, COUNT, VAL0_H, VAL0_L, VAL1_H, VAL1_L, ...]
        uint8_t start_idx = data[1];
        uint8_t count = data[2];
        uint8_t offset = 3;

        for (uint8_t i = 0; i < count; i++) {
            if (start_idx + i < ACCEL_LUT_SIZE && offset + 1 < length) {
                uint16_t val = (data[offset] << 8) | data[offset+1];
                kb_accel.lut[start_idx + i] = val;
                offset += 2;
            }
        }
    }
    else if (cmd == CMD_READ_ALL) {
        // [CMD, OFFSET]
        // Offset 0: Metadata [CMD, MAX_H, MAX_L, GAIN_H, GA_L]
        // Offset 1+: LUT Chunks
        uint8_t offset = data[1];
        uint8_t report[32];
        memset(report, 0, 32);
        report[0] = CMD_READ_ALL;

        if (offset == 0) {
            report[1] = 0; // echoed offset
            report[2] = (kb_accel.max_speed_limit >> 8) & 0xFF;
            report[3] = kb_accel.max_speed_limit & 0xFF;
            report[4] = (kb_accel.global_gain >> 8) & 0xFF;
            report[5] = kb_accel.global_gain & 0xFF;
            report[6] = kb_accel.algo_version;
            report[7] = kb_accel.num_points;
        } else if (offset >= 4) {
            // Points: 7 points per chunk (4 bytes each = 28 bytes)
            // Offset 4: 0-6, Offset 5: 7-13, Offset 6: 14-16
            uint8_t start = (offset - 4) * 7;
            uint8_t count = 7;
            if (start >= kb_accel.num_points) count = 0;
            else if (start + count > kb_accel.num_points) count = kb_accel.num_points - start;

            report[1] = offset;
            report[2] = count;
            uint8_t off = 3;
            for(uint8_t i=0; i<count; i++) {
                keyball_point_t *p = &kb_accel.points[start + i];
                report[off++] = (p->x >> 8) & 0xFF; // X High
                report[off++] = p->x & 0xFF;        // X Low
                report[off++] = (p->y >> 8) & 0xFF;
                report[off++] = p->y & 0xFF;
            }
        } else {
            // Page 1: 0..13 (Offset 1)
            // Page 2: 14..27 (Offset 2)
            // Page 3: 28..31 (Offset 3)
            uint8_t start = (offset - 1) * 14;
            uint8_t count = 14;
            if (start >= ACCEL_LUT_SIZE) count = 0;
            else if (start + count > ACCEL_LUT_SIZE) count = ACCEL_LUT_SIZE - start;

            report[1] = offset; // Echo offset
            report[2] = count;
            uint8_t off = 3;
            for(uint8_t i=0; i<count; i++) {
                uint16_t val = kb_accel.lut[start+i];
                report[off++] = (val >> 8) & 0xFF;
                report[off++] = val & 0xFF;
            }
        }
        raw_hid_send(report, 32);
    }
    else if (cmd == 0x13) { // CMD_SET_POINTS
        // [CMD, START_IDX, COUNT, VER, P0_X_H, P0_X_L, P0_Y_H, P0_Y_L, ...]
        uint8_t start_idx = data[1];
        uint8_t count = data[2];
        kb_accel.algo_version = data[3];

        uint8_t offset = 4;

        if (start_idx + count > kb_accel.num_points) kb_accel.num_points = start_idx + count;

        for (uint8_t i = 0; i < count; i++) {
            if (start_idx + i < ACCEL_MAX_POINTS && offset + 3 < length) {
                kb_accel.points[start_idx + i].x = (data[offset] << 8) | data[offset+1];
                offset += 2;
                kb_accel.points[start_idx + i].y = (data[offset] << 8) | data[offset+1];
                offset += 2;
            }
        }
    }
    else if (cmd == CMD_GET_SPEED) {
        // Respond with max speed since last read (Peak Hold)
        data[0] = CMD_GET_SPEED;
        data[1] = (kb_last_speed >> 8) & 0xFF;
        data[2] = kb_last_speed & 0xFF;
        raw_hid_send(data, length);

        // Reset for next interval
        kb_last_speed = 0;
    }
    else if (cmd == CMD_RESET_CONFIG) {
        // Restore default configuration from backup
        kb_accel = kb_accel_default;
        kb_last_speed = 0;
    }
#endif
}

//////////////////////////////////////////////////////////////////////////////
// Public API functions

bool keyball_get_scroll_mode(void) {
    return keyball.scroll_mode;
}

void keyball_set_scroll_mode(bool mode) {
    if (mode != keyball.scroll_mode) {
        keyball.scroll_mode_changed = timer_read32();
    }
    keyball.scroll_mode = mode;
}

keyball_scrollsnap_mode_t keyball_get_scrollsnap_mode(void) {
#if KEYBALL_SCROLLSNAP_ENABLE == 2
    return keyball.scrollsnap_mode;
#else
    return 0;
#endif
}

void keyball_set_scrollsnap_mode(keyball_scrollsnap_mode_t mode) {
#if KEYBALL_SCROLLSNAP_ENABLE == 2
    keyball.scrollsnap_mode = mode;
#endif
}

uint8_t keyball_get_scroll_div(void) {
    return keyball.scroll_div == 0 ? KEYBALL_SCROLL_DIV_DEFAULT : keyball.scroll_div;
}

void keyball_set_scroll_div(uint8_t div) {
    keyball.scroll_div = div > SCROLL_DIV_MAX ? SCROLL_DIV_MAX : div;
}

uint16_t keyball_get_cpi(void) {
    return keyball.cpi_value == 0 ? CPI_DEFAULT : keyball.cpi_value;
}

void keyball_set_cpi(uint16_t cpi) {
    if (cpi > CPI_MAX + 1) {
        cpi = CPI_MAX;
    }

    keyball.cpi_value   = cpi;
    dprintf("set cpi: %u\n", keyball.cpi_value);
    pointing_device_set_cpi_on_side(true, keyball.cpi_value);
    pointing_device_set_cpi_on_side(false, keyball.cpi_value);
    dprintf("actual after cpi: %u\n", pointing_device_get_cpi());
}

//////////////////////////////////////////////////////////////////////////////
// Keyboard hooks

void keyboard_post_init_kb(void) {
#ifdef SPLIT_KEYBOARD
    // register transaction handlers on secondary.
    if (!is_keyboard_master()) {
        transaction_register_rpc(KEYBALL_GET_INFO, rpc_get_info_handler);
    }
#endif

    keyball.this_have_ball = pmw33xx_init_ok;
    keyball_set_cpi(CPI_DEFAULT);

    // read keyball configuration from EEPROM
    if (eeconfig_is_enabled()) {
        keyball_config_t c = {.raw = eeconfig_read_kb()};
        printf("read cpi: %u, scroll_div: %u\n", c.cpi, c.sdiv);
        if (c.cpi < 1) {
            c.cpi = CPI_DEFAULT;
        }
        keyball_set_cpi(c.cpi);
        keyball_set_scroll_div(c.sdiv);
#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
        set_auto_mouse_enable(c.amle);
        set_auto_mouse_timeout(c.amlto == 0 ? AUTO_MOUSE_TIME : (c.amlto + 1) * AML_TIMEOUT_QU);
#endif
#if KEYBALL_SCROLLSNAP_ENABLE == 2
        keyball_set_scrollsnap_mode(c.ssnap);
#endif
    }

    keyball_on_adjust_layout(KEYBALL_ADJUST_PENDING);
    keyboard_post_init_user();
}

#ifdef SPLIT_KEYBOARD
void housekeeping_task_kb(void) {
    if (is_keyboard_master()) {
        rpc_get_info_invoke();
    }
}
#endif

static void pressing_keys_update(uint16_t keycode, keyrecord_t *record) {
    // Process only valid keycodes.
    if (keycode >= 4 && keycode < 57) {
        char value = pgm_read_byte(code_to_name + keycode - 4);
        char where = BL;
        if (!record->event.pressed) {
            // Swap `value` and `where` when releasing.
            where = value;
            value = BL;
        }
        // Rewrite the last `where` of pressing_keys to `value` .
        for (int i = 0; i < KEYBALL_OLED_MAX_PRESSING_KEYCODES; i++) {
            if (keyball.pressing_keys[i] == where) {
                keyball.pressing_keys[i] = value;
                break;
            }
        }
    }
}

#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
bool is_mouse_record_kb(uint16_t keycode, keyrecord_t* record) {
    switch (keycode) {
        case SCRL_MO:
            return true;
    }
    return is_mouse_record_user(keycode, record);
}
#endif

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // store last keycode, row, and col for OLED
    keyball.last_kc  = keycode;
    keyball.last_pos = record->event.key;

    pressing_keys_update(keycode, record);

    if (!process_record_user(keycode, record)) {
        return false;
    }

    // strip QK_MODS part.
    if (keycode >= QK_MODS && keycode <= QK_MODS_MAX) {
        keycode &= 0xff;
    }

    switch (keycode) {
#ifndef MOUSEKEY_ENABLE
        // process KC_MS_BTN1~8 by myself
        // See process_action() in quantum/action.c for details.
        case KC_MS_BTN1 ... KC_MS_BTN8: {
            extern void register_mouse(uint8_t mouse_keycode, bool pressed);
            register_mouse(keycode, record->event.pressed);
            // to apply QK_MODS actions, allow to process others.
            return true;
        }
#endif

        case SCRL_MO:
            keyball_set_scroll_mode(record->event.pressed);
            // process_auto_mouse may use this in future, if changed order of
            // processes.
            return true;
    }

    // process events which works on pressed only.
    if (record->event.pressed) {
        switch (keycode) {
            case KBC_RST:
                keyball_set_cpi(0);
                keyball_set_scroll_div(0);
#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
                set_auto_mouse_enable(false);
                set_auto_mouse_timeout(AUTO_MOUSE_TIME);
#endif
                break;
            case KBC_SAVE: {
                keyball_config_t c = {
                    .cpi   = keyball.cpi_value,
                    .sdiv  = keyball.scroll_div,
#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
                    .amle  = get_auto_mouse_enable(),
                    .amlto = (get_auto_mouse_timeout() / AML_TIMEOUT_QU) - 1,
#endif
#if KEYBALL_SCROLLSNAP_ENABLE == 2
                    .ssnap = keyball_get_scrollsnap_mode(),
#endif
                };
                eeconfig_update_kb(c.raw);
            } break;

            case CPI_I100:
                add_cpi(100);
                break;
            case CPI_D100:
                add_cpi(-100);
                break;
            case CPI_I1K:
                add_cpi(1000);
                break;
            case CPI_D1K:
                add_cpi(-1000);
                break;

            case SCRL_TO:
                keyball_set_scroll_mode(!keyball.scroll_mode);
                break;
            case SCRL_DVI:
                add_scroll_div(1);
                break;
            case SCRL_DVD:
                add_scroll_div(-1);
                break;

#if KEYBALL_SCROLLSNAP_ENABLE == 2
            case SSNP_HOR:
                keyball_set_scrollsnap_mode(KEYBALL_SCROLLSNAP_MODE_HORIZONTAL);
                break;
            case SSNP_VRT:
                keyball_set_scrollsnap_mode(KEYBALL_SCROLLSNAP_MODE_VERTICAL);
                break;
            case SSNP_FRE:
                keyball_set_scrollsnap_mode(KEYBALL_SCROLLSNAP_MODE_FREE);
                break;
#endif

#ifdef POINTING_DEVICE_AUTO_MOUSE_ENABLE
            case AML_TO:
                set_auto_mouse_enable(!get_auto_mouse_enable());
                break;
            case AML_I50:
                {
                    uint16_t v = get_auto_mouse_timeout() + 50;
                    set_auto_mouse_timeout(MIN(v, AML_TIMEOUT_MAX));
                }
                break;
            case AML_D50:
                {
                    uint16_t v = get_auto_mouse_timeout() - 50;
                    set_auto_mouse_timeout(MAX(v, AML_TIMEOUT_MIN));
                }
                break;
#endif

            default:
                return true;
        }
        return false;
    }

    return true;
}

// Disable functions keycode_config() and mod_config() in keycode_config.c to
// reduce size.  These functions are provided for customizing magic keycode.
// These two functions are mostly unnecessary if `MAGIC_KEYCODE_ENABLE = no` is
// set.
//
// If `MAGIC_KEYCODE_ENABLE = no` and you want to keep these two functions as
// they are, define the macro KEYBALL_KEEP_MAGIC_FUNCTIONS.
//
// See: https://docs.qmk.fm/#/squeezing_avr?id=magic-functions
//
#if !defined(MAGIC_KEYCODE_ENABLE) && !defined(KEYBALL_KEEP_MAGIC_FUNCTIONS)

uint16_t keycode_config(uint16_t keycode) {
    return keycode;
}

uint8_t mod_config(uint8_t mod) {
    return mod;
}

#endif
