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
#include "pointing_device_accel.h"
#include "drivers/sensors/pmw33xx_common.h"

#include <string.h>
#include <math.h>
#include "raw_hid.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

    .accel_default = {
        .accel_lut = {
            .table = {
                0.1875f, 0.8047f, 4.7031f, 6.5625f, 6.9648f, 6.9648f, 6.9648f, 6.9648f,
                6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f,
                6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f,
                6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f, 6.9648f,
            },
            .max_speed_limit = 7.5f, // Safety Cap
            .global_gain = 1.0f,      // 1.00x Sensitivity
            .algo_version = 1,
            .num_points = 7,
            .points = {
                {0.0f, 0.1875f},
                {3.0391f, 0.2773f},
                {5.1484f, 1.9336f},
                {7.2578f, 4.1523f},
                {11.2656f, 6.3281f},
                {14.9219f, 6.9648f},
                {50.0000f, 6.9648f}
            }
        },
        .accel_simple = {
            // Base sensitivity (0.0 - 1.0 for dampening).
            // Lower this value (e.g. 0.3) to make slow movements much slower/more precise.
            .base   = 0.4f,

            // Acceleration rate per count of speed.
            // Increase this to make the cursor accelerate more aggressively as you move faster.
            .factor = 0.10f,

            // Maximum sensitivity multiplier.
            // Increase this if you want to cover more distance (e.g. multiple monitors) when flinging.
            .max    = 6.0f
        },
        .accel_drashna = {
            .takeoff     = 2.0f,
            .growth_rate = 0.25f,
            .offset      = 2.2f,
            .limit       = 0.2f,
            .limit_upper = 1.0f
        },
    },
    .anisotropy = {1.0f, 1.0f},
    .directional_sensitivity = {1.0f, 1.0f, 1.0f, 1.0f},
    .coord_rot_angle = 0.0f,
};

static uint16_t kb_last_speed = 0;

static void keyball_set_acceleration_data(const keyball_accel_t *data) {
    keyball.accel = *data;

    // Sync Drashna Config
    pointing_device_accel_set_takeoff(data->accel_drashna.takeoff);
    pointing_device_accel_set_growth_rate(data->accel_drashna.growth_rate);
    pointing_device_accel_set_offset(data->accel_drashna.offset);
    pointing_device_accel_set_limit(data->accel_drashna.limit);
    pointing_device_accel_set_limit_upper(data->accel_drashna.limit_upper);
}

void keyball_set_default_acceleration_data(const keyball_accel_t *data) {
    keyball.accel_default = *data;
    keyball_set_acceleration_data(data);
}

void keyball_set_default_anisotropy_data(const keyball_anisotropy_t *data) {
    keyball.anisotropy = *data;
}

float keyball_apply_anisotropy_x(float val) {
    return val * keyball.anisotropy.x;
}

float keyball_apply_anisotropy_y(float val) {
    return val * keyball.anisotropy.y;
}

void keyball_set_default_directional_sensitivity_data(const keyball_directional_sensitivity_t *data) {
    keyball.directional_sensitivity = *data;
}

float keyball_apply_directional_sensitivity_x(float val) {
    if (val > 0) {
        return val * keyball.directional_sensitivity.x_pos;
    } else {
        return val * keyball.directional_sensitivity.x_neg;
    }
}

float keyball_apply_directional_sensitivity_y(float val) {
    if (val > 0) {
        return val * keyball.directional_sensitivity.y_pos;
    } else {
        return val * keyball.directional_sensitivity.y_neg;
    }
}

void keyball_set_default_coord_rot(float angle) {
    keyball.coord_rot_angle = angle;
}

void keyball_apply_coord_rot(float *x_inout, float *y_inout) {
    if (keyball.coord_rot_angle == 0.0f) {
        return;
    }
    // Convert degrees to radians
    float rad = keyball.coord_rot_angle * (M_PI / 180.0f);
    float c = cosf(rad);
    float s = sinf(rad);

    // x' = x cos(theta) - y sin(theta)
    // y' = x sin(theta) + y cos(theta)
    float nx = *x_inout * c - *y_inout * s;
    float ny = *x_inout * s + *y_inout * c;

    *x_inout = nx;
    *y_inout = ny;
}

// Drashna Module Shims
report_mouse_t pointing_device_task_pointing_device_accel_kb(report_mouse_t mouse_report) {
    return mouse_report;
}

bool process_record_pointing_device_accel_kb(uint16_t keycode, keyrecord_t *record) {
    return true; // Continue processing
}

void keyboard_post_init_pointing_device_accel_kb(void) {
    // No-op
}

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

static void keyball_accel_apply_lut(keyball_motion_t *accum, float dx, float dy, int16_t *out_x, int16_t *out_y) {
    // Current Speed in counts
    float speed = sqrt(dx * dx + dy * dy);

    // Peak Hold for Tuner
    uint16_t speed_int = (uint16_t)speed;
    if (speed_int > kb_last_speed) {
        kb_last_speed = speed_int;
    }

    // Map speed to LUT index (0..31)
    // We Map 0..127 speed to 0..31 index -> speed / 4
    int index = (int)(speed / 4.0f);
    if (index >= ACCEL_LUT_SIZE - 1) {
        index = ACCEL_LUT_SIZE - 2;
    }

    // Linear Interpolation
    float y1 = keyball.accel.accel_lut.table[index];
    float y2 = keyball.accel.accel_lut.table[index+1];
    float t = (speed - (index * 4.0f)) / 4.0f;
    
    // Clamp t to 0..1 to be safe
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float scale = y1 + (y2 - y1) * t;

    // Apply Global Gain
    if (keyball.accel.accel_lut.global_gain > 0.0f) {
        scale *= keyball.accel.accel_lut.global_gain;
    }

    // Safety Clamp
    if ((keyball.accel.accel_lut.max_speed_limit > 0.0f) && (scale > keyball.accel.accel_lut.max_speed_limit)) {
        scale = keyball.accel.accel_lut.max_speed_limit;
    }

    // Apply Scaling with Remainder Accumulation
    float sx = dx * scale + accum->remainder_x;
    float sy = dy * scale + accum->remainder_y;

    *out_x = (int16_t)sx;
    *out_y = (int16_t)sy;

    accum->remainder_x = sx - *out_x;
    accum->remainder_y = sy - *out_y;
}

static void keyball_accel_apply_simple(keyball_motion_t *accum, float dx, float dy, int16_t *out_x, int16_t *out_y) {
    // 1. Calculate speed
    float speed = sqrt(dx * dx + dy * dy);

    // 2. Calculate Scale
    // scale = base + (speed * factor)
    float scale = keyball.accel.accel_simple.base + (speed * keyball.accel.accel_simple.factor);

    // 3. Clamp to Max
    if (keyball.accel.accel_simple.max > 0.0f && scale > keyball.accel.accel_simple.max) {
        scale = keyball.accel.accel_simple.max;
    }

    // 4. Apply Scaling with Remainder Accumulation
    float sx = dx * scale + accum->remainder_x;
    float sy = dy * scale + accum->remainder_y;

    *out_x = (int16_t)sx;
    *out_y = (int16_t)sy;

    accum->remainder_x = sx - *out_x;
    accum->remainder_y = sy - *out_y;
}

static void apply_acceleration(keyball_motion_t *accum, report_mouse_t *report, int16_t *out_x, int16_t *out_y) {
    float dx = (float)report->x;
    float dy = (float)report->y;

    keyball_apply_coord_rot(&dx, &dy);
    dx = keyball_apply_anisotropy_x(dx);
    dy = keyball_apply_anisotropy_y(dy);
    dx = keyball_apply_directional_sensitivity_x(dx);
    dy = keyball_apply_directional_sensitivity_y(dy);

    if (KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT) {
        keyball_accel_apply_lut(accum, dx, dy, out_x, out_y);
    } else if (KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_SIMPLE) {
        keyball_accel_apply_simple(accum, dx, dy, out_x, out_y);
    } else if (KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_DRASHNA) {
        // Track speed for tuner visualization
        // TODO: refactor this and make this computation generic across the 3 accel modes
        // Using int speed for Peak Hold
        // Note: For visualization we use anisotropy-applied values to match what the cursor does
        uint16_t speed = (uint16_t)sqrt(dx * dx + dy * dy);
        if (speed > kb_last_speed) {
            kb_last_speed = speed;
        }

        // Drashna's algorithm manages its own internal state/accumulation
        // We pass the raw report; anisotropy is applied INSIDE the driver now (via keyball_apply_anisotropy callbacks)
        report_mouse_t accel = pointing_device_task_pointing_device_accel(*report);
        *out_x = accel.x;
        *out_y = accel.y;
    } else {
        *out_x = (int16_t)dx;
        *out_y = (int16_t)dy;
    }
}

static void motion_to_mouse(report_mouse_t *report, report_mouse_t *output, bool is_left, bool as_scroll, keyball_motion_t *accum) {
#ifdef KEYBALL_POINTER_ACCEL_ENABLE
    int16_t ax, ay;
    apply_acceleration(accum, report, &ax, &ay);
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

    CMD_SET_CURVE_PT  = 0x10,
    CMD_SET_CURVE_ALL = 0x11,

    CMD_READ_ALL      = 0x12,

    CMD_SET_POINTS    = 0x13,

    CMD_GET_SPEED     = 0x20,
    CMD_RESET_CONFIG  = 0x30,

    CMD_SET_DRASHNA   = 0x40,
    CMD_GET_DRASHNA   = 0x41
};

// -------------------------------------------------------------------------
// RawHID Handlers
// -------------------------------------------------------------------------

static void handle_set_curve_pt(uint8_t *data, uint8_t length) {
    // [CMD, INDEX, VAL_B0, VAL_B1, VAL_B2, VAL_B3, MAX_B0..3, GAIN_B0..3]
    // 4-byte floats, Little Endian
    uint8_t idx = data[1];
    union { float f; uint8_t b[4]; } conv;

    if (idx < ACCEL_LUT_SIZE) {
        conv.b[0] = data[2]; conv.b[1] = data[3]; conv.b[2] = data[4]; conv.b[3] = data[5];
        keyball.accel.accel_lut.table[idx] = conv.f;
    }

    // Max Speed Limit
    conv.b[0] = data[6]; conv.b[1] = data[7]; conv.b[2] = data[8]; conv.b[3] = data[9];
    keyball.accel.accel_lut.max_speed_limit = conv.f;

    // Extended capability: Gain at index 10-13
    if (length > 13) {
        conv.b[0] = data[10]; conv.b[1] = data[11]; conv.b[2] = data[12]; conv.b[3] = data[13];
        if (conv.f > 0.0f) keyball.accel.accel_lut.global_gain = conv.f;
    }
}

static void handle_set_curve_all(uint8_t *data, uint8_t length) {
    // [CMD, START_IDX, COUNT, VAL0_B0..3, VAL1_B0..3, ...]
    uint8_t start_idx = data[1];
    uint8_t count = data[2];
    uint8_t offset = 3;
    union { float f; uint8_t b[4]; } conv;

    for (uint8_t i = 0; i < count; i++) {
        if (start_idx + i < ACCEL_LUT_SIZE && offset + 3 < length) {
            conv.b[0] = data[offset]; conv.b[1] = data[offset+1]; conv.b[2] = data[offset+2]; conv.b[3] = data[offset+3];
            keyball.accel.accel_lut.table[start_idx + i] = conv.f;
            offset += 4;
        }
    }
}

static void handle_set_points(uint8_t *data, uint8_t length) {
    // [CMD, START_IDX, COUNT, VER, P0_X_B0..3, P0_Y_B0..3, ...]
    uint8_t start_idx = data[1];
    uint8_t count = data[2];
    uint8_t offset = 4;
    keyball.accel.accel_lut.algo_version = data[3];    

    if (start_idx + count > keyball.accel.accel_lut.num_points) {
        keyball.accel.accel_lut.num_points = start_idx + count;
    }

    union { float f; uint8_t b[4]; } conv;

    for (uint8_t i = 0; i < count; i++) {
        if (start_idx + i < ACCEL_MAX_POINTS && offset + 7 < length) {
            // X
            conv.b[0] = data[offset]; conv.b[1] = data[offset+1]; conv.b[2] = data[offset+2]; conv.b[3] = data[offset+3];
            keyball.accel.accel_lut.points[start_idx + i].x = conv.f;
            offset += 4;
             // Y
            conv.b[0] = data[offset]; conv.b[1] = data[offset+1]; conv.b[2] = data[offset+2]; conv.b[3] = data[offset+3];
            keyball.accel.accel_lut.points[start_idx + i].y = conv.f;
            offset += 4;
        }
    }
}

static void handle_read_all(uint8_t *data) {
    // [CMD, OFFSET]
    // Offset 0: Metadata
    // Offset 1-5: LUT Chunks (7 floats per chunk -> 5 chunks for 32 items)
    // Offset 6+: Points Chunks (3 points per chunk)

    // Chunk Size for Float LUT: 7 items (28 bytes) fits in 32 bytes (minus 3 header)
    // 32 items / 7 = 4.57 -> 5 chunks. 
    // Indices: 0-6, 7-13, 14-20, 21-27, 28-31.
    // Offsets: 1, 2, 3, 4, 5. Points start at 6.

    uint8_t offset = data[1];
    uint8_t report[32];
    memset(report, 0, 32);
    report[0] = CMD_READ_ALL;
    report[1] = offset; // Echo offset

    union { float f; uint8_t b[4]; } conv;

    if (offset == 0) {
        // Metadata: [0=CMD, 1=OFF, 2..5=MAX, 6..9=GAIN, 10=ALGO, 11=NUM, 12=MODE]
        conv.f = keyball.accel.accel_lut.max_speed_limit;
        report[2] = conv.b[0]; report[3] = conv.b[1]; report[4] = conv.b[2]; report[5] = conv.b[3];

        conv.f = keyball.accel.accel_lut.global_gain;
        report[6] = conv.b[0]; report[7] = conv.b[1]; report[8] = conv.b[2]; report[9] = conv.b[3];

        report[10] = keyball.accel.accel_lut.algo_version;
        report[11] = keyball.accel.accel_lut.num_points;
        report[12] = KEYBALL_ACCEL_MODE;
    }

    else if (offset >= 6) {
        // Read Points Chunks
        // Chunk size 3 points (24 bytes)
        uint8_t start = (offset - 6) * 3;
        uint8_t count = 3;

        if (start >= keyball.accel.accel_lut.num_points) count = 0;
        else if (start + count > keyball.accel.accel_lut.num_points) count = keyball.accel.accel_lut.num_points - start;

        report[2] = count;
        uint8_t off = 3;
        for(uint8_t i=0; i<count; i++) {
            keyball_point_t *p = &keyball.accel.accel_lut.points[start + i];
            // X
            conv.f = p->x;
            report[off++] = conv.b[0]; report[off++] = conv.b[1]; report[off++] = conv.b[2]; report[off++] = conv.b[3];
            // Y
            conv.f = p->y;
            report[off++] = conv.b[0]; report[off++] = conv.b[1]; report[off++] = conv.b[2]; report[off++] = conv.b[3];
        }
    } else {
        // Read LUT Chunks (Offsets 1..5)
        uint8_t start = (offset - 1) * 7;
        uint8_t count = 7;

        if (start >= ACCEL_LUT_SIZE) count = 0;
        else if (start + count > ACCEL_LUT_SIZE) count = ACCEL_LUT_SIZE - start;

        report[2] = count;
        uint8_t off = 3;
        for(uint8_t i=0; i<count; i++) {
            conv.f = keyball.accel.accel_lut.table[start+i];
            report[off++] = conv.b[0]; report[off++] = conv.b[1]; report[off++] = conv.b[2]; report[off++] = conv.b[3];
        }
    }

    raw_hid_send(report, 32);
}

static void handle_get_speed(uint8_t *data, uint8_t length) {
    data[0] = CMD_GET_SPEED;
    data[1] = (kb_last_speed >> 8) & 0xFF;
    data[2] = kb_last_speed & 0xFF;
    raw_hid_send(data, length);
    kb_last_speed = 0; // Reset Peak Hold
}

static void handle_set_drashna(uint8_t *data) {
    // [CMD, TKO_0, TKO_1, TKO_2, TKO_3, GR_0, ..., OFS_0, ..., LIM_0, ...]
    // Little Endian float (4 bytes)
    union { float f; uint8_t b[4]; } conv;

     conv.b[0] = data[1]; conv.b[1] = data[2]; conv.b[2] = data[3]; conv.b[3] = data[4];
     keyball.accel.accel_drashna.takeoff = conv.f;

     conv.b[0] = data[5]; conv.b[1] = data[6]; conv.b[2] = data[7]; conv.b[3] = data[8];
     keyball.accel.accel_drashna.growth_rate = conv.f;

     conv.b[0] = data[9]; conv.b[1] = data[10]; conv.b[2] = data[11]; conv.b[3] = data[12];
     keyball.accel.accel_drashna.offset = conv.f;

     conv.b[0] = data[13]; conv.b[1] = data[14]; conv.b[2] = data[15]; conv.b[3] = data[16];
     keyball.accel.accel_drashna.limit = conv.f;

     conv.b[0] = data[17]; conv.b[1] = data[18]; conv.b[2] = data[19]; conv.b[3] = data[20];
     keyball.accel.accel_drashna.limit_upper = conv.f;

     keyball_set_acceleration_data(&keyball.accel);
}

static void handle_get_drashna(uint8_t *data) {
    uint8_t report[32];
    memset(report, 0, 32);
    report[0] = CMD_GET_DRASHNA;

    union { float f; uint8_t b[4]; } conv;

    conv.f = keyball.accel.accel_drashna.takeoff;
    report[1] = conv.b[0]; report[2] = conv.b[1]; report[3] = conv.b[2]; report[4] = conv.b[3];

    conv.f = keyball.accel.accel_drashna.growth_rate;
    report[5] = conv.b[0]; report[6] = conv.b[1]; report[7] = conv.b[2]; report[8] = conv.b[3];

    conv.f = keyball.accel.accel_drashna.offset;
    report[9] = conv.b[0]; report[10] = conv.b[1]; report[11] = conv.b[2]; report[12] = conv.b[3];

    conv.f = keyball.accel.accel_drashna.limit;
    report[13] = conv.b[0]; report[14] = conv.b[1]; report[15] = conv.b[2]; report[16] = conv.b[3];

    conv.f = keyball.accel.accel_drashna.limit_upper;
    report[17] = conv.b[0]; report[18] = conv.b[1]; report[19] = conv.b[2]; report[20] = conv.b[3];

    raw_hid_send(report, 32);
}

// TODO: refactor this and keyball_set_acceleration_data(), they do much the same thing
static void handle_reset_config(void) {
    keyball.accel = keyball.accel_default;

    // Sync Drashna Config
    pointing_device_accel_set_takeoff(keyball.accel.accel_drashna.takeoff);
    pointing_device_accel_set_growth_rate(keyball.accel.accel_drashna.growth_rate);
    pointing_device_accel_set_offset(keyball.accel.accel_drashna.offset);
    pointing_device_accel_set_limit(keyball.accel.accel_drashna.limit);
    pointing_device_accel_set_limit_upper(keyball.accel.accel_drashna.limit_upper);

    kb_last_speed = 0;
}

void raw_hid_receive(uint8_t *data, uint8_t length) {
    uint8_t cmd = data[0];

    switch (cmd) {
        case CMD_SET_CURVE_PT:
            handle_set_curve_pt(data, length);
            break;
        case CMD_SET_CURVE_ALL:
            handle_set_curve_all(data, length);
            break;
        case CMD_SET_POINTS:
            handle_set_points(data, length);
            break;
        case CMD_SET_DRASHNA:
            handle_set_drashna(data);
            break;
        case CMD_GET_DRASHNA:
            handle_get_drashna(data);
            break;

        case CMD_READ_ALL:
            handle_read_all(data);
            break;
        case CMD_GET_SPEED:
            handle_get_speed(data, length);
            break;
        case CMD_RESET_CONFIG:
            handle_reset_config();
            break;
        default:
            break;
    }
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

    pointing_device_accel_enabled(KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_DRASHNA);
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
