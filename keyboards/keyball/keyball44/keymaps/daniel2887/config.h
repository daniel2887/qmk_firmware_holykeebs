#pragma once

#define MASTER_LEFT
// #define MASTER_RIGHT

#ifdef RGBLIGHT_ENABLE
#    define RGBLIGHT_EFFECT_BREATHING
#    define RGBLIGHT_EFFECT_RAINBOW_MOOD
#    define RGBLIGHT_EFFECT_RAINBOW_SWIRL
#    define RGBLIGHT_EFFECT_SNAKE
#    define RGBLIGHT_EFFECT_KNIGHT
#    define RGBLIGHT_EFFECT_CHRISTMAS
#    define RGBLIGHT_EFFECT_STATIC_GRADIENT
#    define RGBLIGHT_EFFECT_RGB_TEST
#    define RGBLIGHT_EFFECT_ALTERNATING
#    define RGBLIGHT_EFFECT_TWINKLE
#endif

#define TAP_CODE_DELAY 5

#define POINTING_DEVICE_AUTO_MOUSE_ENABLE
#define AUTO_MOUSE_DEFAULT_LAYER 11

// Match ZMK tapping term approximately
#define TAPPING_TERM 100
//#define QUICK_TAP_TERM 200
#define PERMISSIVE_HOLD

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