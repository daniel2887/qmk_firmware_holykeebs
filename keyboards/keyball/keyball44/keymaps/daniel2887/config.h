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
#define KEYBALL_POINTER_ACCEL_ENABLE
// Base sensitivity (0.0 - 1.0 for dampening).
// Lower this value (e.g. 0.3) to make slow movements much slower/more precise.
#define KEYBALL_ACCEL_BASE 0.4
// Acceleration rate per count of speed.
// Increase this to make the cursor accelerate more aggressively as you move faster.
#define KEYBALL_ACCEL_FACTOR 0.10
// Maximum sensitivity multiplier.
// Increase this if you want to cover more distance (e.g. multiple monitors) when flinging.
#define KEYBALL_ACCEL_MAX 6.0