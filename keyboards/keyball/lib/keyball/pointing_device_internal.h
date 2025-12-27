#pragma once

// Forward to the real QMK internal header
#include <quantum/pointing_device_internal.h>

// Shim for Drashna's Community Module Macro
#ifndef ASSERT_COMMUNITY_MODULES_MIN_API_VERSION
#define ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(...) 
#endif

// Prototypes for _kb hooks expected by the module
report_mouse_t pointing_device_task_pointing_device_accel_kb(report_mouse_t mouse_report);
bool process_record_pointing_device_accel_kb(uint16_t keycode, keyrecord_t *record);
void keyboard_post_init_pointing_device_accel_kb(void);

// Dummy definitions for custom keycodes used in the module
// We map them to a safe range so the switch statements compile.
// Since we don't bind these in the keymap, they won't trigger.
enum drashna_accel_keycodes_shim {
    CM_MOUSE_ACCEL_TOGGLE = QK_USER_16, // Use QK_USER range to be safe
    CM_MOUSE_ACCEL_TAKEOFF,
    CM_MOUSE_ACCEL_GROWTH_RATE,
    CM_MOUSE_ACCEL_OFFSET,
    CM_MOUSE_ACCEL_LIMIT
};
