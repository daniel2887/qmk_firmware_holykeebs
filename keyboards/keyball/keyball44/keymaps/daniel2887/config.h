#pragma once

#define POINTING_DEVICE_AUTO_MOUSE_ENABLE
#define AUTO_MOUSE_DEFAULT_LAYER 11 // L_AUTO_MOUSE
#define AUTO_MOUSE_TIME 500

// Match ZMK tapping term approximately
#define TAPPING_TERM 200
#define TAPPING_TERM_PER_KEY
#define QUICK_TAP_TERM_PER_KEY
#define RETRO_TAPPING_PER_KEY

// Caps Word
#define CAPS_WORD_IDLE_TIMEOUT 3000
//#define DOUBLE_TAP_SHIFT_TURNS_ON_CAPS_WORD

// Mouse settings
#define MOUSEKEY_INTERVAL 16
#define MOUSEKEY_DELAY 0
#define MOUSEKEY_TIME_TO_MAX 60
#define MOUSEKEY_MAX_SPEED 7
#define MOUSEKEY_WHEEL_DELAY 0

// Unicode Settings
#define UNICODE_SELECTED_MODES UNICODE_MODE_LINUX, UNICODE_MODE_WINDOWS, UNICODE_MODE_WINCOMPOSE

// Keyball settings

// Scroll sensitivity divisor (Default is 4). Higher = slower/less sensitive.
#define KEYBALL_SCROLL_DIVISOR 42

// Keyball Pointer Acceleration
// Keyball Pointer Acceleration Configuration
// ----------------------------------------
// Master switch: Comment out to disable all acceleration logic (saves space, raw 1:1 input).
#define KEYBALL_POINTER_ACCEL_ENABLE

// Acceleration Algorithm Selection
// options:
//   KEYBALL_ACCEL_MODE_LUT    (Default) - Advanced 32-point curve, tunable via HTML tool.
//   KEYBALL_ACCEL_MODE_SIMPLE           - Linear formula: Base + (Speed * Factor).
// See keymap.c, `keyball_accel_t` struct for configuration.
#define KEYBALL_ACCEL_MODE KEYBALL_ACCEL_MODE_LUT
