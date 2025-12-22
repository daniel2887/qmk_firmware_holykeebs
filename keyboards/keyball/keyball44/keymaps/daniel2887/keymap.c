/*
Copyright 2025 Daniel
*/

#include QMK_KEYBOARD_H
#include "quantum.h"

enum layers {
    L_BASE = 0,
    L_GAME,
    L_NAV,
    L_SYMB,
    L_NUM,
    L_MEDIA,
    L_ARROWS,
    L_FN,
    L_UNI_NAV,
    L_UNI_MATH,
    L_GAME_NUM,
};

// Unicode Code Points
#define UC_LE     0x2264 // ≤
#define UC_GE     0x2265 // ≥
#define UC_QE     0x225F // ≟
#define UC_RE     0x2248 // ≈
#define UC_NE     0x2260 // ≠
#define UC_DEG    0x00B0 // °
#define UC_LEFT   0x2190 // ←
#define UC_UP     0x2191 // ↑
#define UC_RIGHT  0x2192 // →
#define UC_DOWN   0x2193 // ↓
#define UC_BIDIR  0x2194 // ⇄

enum tap_dances {
    TD_TAB_ESC,
    TD_TO_LAYER
};

enum custom_keycodes {
    NAV_CW = KEYBALL_SAFE_RANGE,
};

// Tap Dance Definitions
void td_tab_esc_finished(tap_dance_state_t *state, void *user_data) {
    if (state->count == 1) {
        tap_code(KC_TAB);
    } else {
        tap_code(KC_ESC);
    }
}

void td_to_layer_finished(tap_dance_state_t *state, void *user_data) {
    if (state->count == 1) {
        layer_move(L_BASE);
    } else if (state->count == 2) {
        layer_move(L_GAME);
    } else {
        layer_move(L_ARROWS);
    }
}

tap_dance_action_t tap_dance_actions[] = {
    [TD_TAB_ESC] = ACTION_TAP_DANCE_FN(td_tab_esc_finished),
    [TD_TO_LAYER] = ACTION_TAP_DANCE_FN(td_to_layer_finished)
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [L_BASE] = LAYOUT_universal(
        KC_TAB,      KC_Q,             KC_W,            KC_E,            KC_R,             KC_T,                                        KC_Y,              KC_U,  KC_I,    KC_O,         KC_P,    KC_BSPC,
        KC_NO,       KC_A,             LSFT_T(KC_S),    LT(L_SYMB,KC_D), LT(L_NAV,KC_F),   KC_G,                                        KC_H,              KC_J,  KC_K,    RSFT_T(KC_L), KC_SCLN, KC_QUOT,
        KC_LSFT,     LT(L_MEDIA,KC_Z), KC_X,            KC_C,            KC_V,             KC_B,                                        KC_N,              KC_M,  KC_COMM, KC_DOT,       KC_SLSH, KC_ENT,
                                       KC_NO,           KC_NO,           KC_LGUI,          LALT_T(KC_APP),   KC_LCTL,           KC_SPC, NAV_CW,            KC_NO, KC_NO,                 MO(L_FN)
    ),

    // Outdated -- needs cleanup
    [L_GAME] = LAYOUT_universal(
        TD(TD_TAB_ESC), KC_Q,           KC_W,           KC_E,           KC_R,           KC_T,                                           KC_Y,           KC_U,           KC_I,           KC_O,           KC_P,           KC_BSPC,
        LT(L_MEDIA,KC_LGUI),KC_A,       KC_S,           KC_D,           KC_F,           KC_G,                                           KC_H,           KC_J,           KC_K,           KC_L,           KC_SCLN,        KC_QUOT,
        KC_LSFT,        KC_Z,           KC_X,           KC_C,           KC_V,           KC_B,                                           KC_N,           KC_M,           KC_COMM,        KC_DOT,         KC_SLSH,        KC_ENT,
                                        KC_NO,          KC_NO,          KC_LCTL,        LT(L_GAME_NUM,KC_LALT), KC_SPC,      KC_SPC,     MO(L_NAV),      KC_NO,          KC_NO,                          MO(L_FN)
    ),

    [L_NAV] = LAYOUT_universal(
        KC_NO,          KC_ESC,        KC_NO,           KC_NO,           KC_NO,            LALT(LCTL(LSFT(KC_T))),                          KC_PGUP, KC_HOME,       KC_UP,     KC_END,         KC_DEL,   KC_BSPC,
        KC_NO,          KC_NO,         KC_LSFT,         KC_TRNS,         KC_NO,            KC_NO,                                           KC_PGDN, KC_LEFT,       KC_DOWN,   KC_RGHT,        KC_NO,    KC_NO,
        KC_LSFT,        KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_BTN1,       KC_BTN2,   KC_BTN3,        KC_NO,    KC_NO,
        KC_NO,          KC_NO,                                           KC_LGUI,          KC_LALT, KC_LCTL,                KC_BSPC,        KC_NO,                  KC_NO,     KC_NO,          MO(L_FN)
    ),

    [L_SYMB] = LAYOUT_universal(
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_GRV,  KC_LBRC,       KC_EXLM,   KC_RBRC,        KC_ASTR,  KC_NO,
        KC_NO,          KC_NO,         KC_LSFT,         KC_NO,           KC_TRNS,          KC_NO,                                           KC_AMPR, KC_LPRN,       KC_MINS,   KC_RPRN,        KC_DLR,   KC_CIRC,
        KC_LSFT,        KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_PERC, KC_AT,         KC_EQL,    KC_HASH,        KC_BSLS,  KC_NO,
                                       KC_NO,           KC_NO,           KC_LGUI,          KC_LALT,     KC_LCTL,            KC_NO,          KC_NO,                  KC_NO,     KC_NO,          MO(L_FN)
    ),

    [L_NUM] = LAYOUT_universal(
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_7,          KC_8,      KC_9,           KC_ASTR,  KC_NO,
        KC_NO,          KC_NO,         KC_LSFT,         KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_4,          KC_5,      KC_6,           KC_NO,    KC_NO,
        KC_LSFT,        KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_DOT,  KC_1,          KC_2,      KC_3,           KC_SLSH,  KC_NO,
                                       KC_NO,           KC_NO,           KC_LGUI,          KC_LALT,     KC_LCTL,            KC_0,           KC_NO,                  KC_NO,     KC_NO,          MO(L_FN)
    ),

    [L_MEDIA] = LAYOUT_universal(
        KC_ESC,         KC_NO,         KC_MPRV,         KC_MPLY,         KC_MNXT,          KC_VOLU,                                         KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_MUTE,                                         KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_VOLD,                                         KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
                                       KC_NO,           KC_NO,           KC_NO,            KC_NO,        KC_NO,             KC_NO,          KC_NO,                  KC_NO,     KC_NO,          KC_NO
    ),

    [L_ARROWS] = LAYOUT_universal(
        KC_ESC,         KC_NO,         KC_NO,           KC_UP,           KC_NO,            KC_VOLU,                                         KC_VOLU, KC_NO,         KC_UP,     KC_NO,          KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_LEFT,         KC_DOWN,         KC_RIGHT,         KC_MUTE,                                         KC_MUTE, KC_LEFT,       KC_DOWN,   KC_RIGHT,       KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_VOLD,                                         KC_VOLD, KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
                                       KC_NO,           KC_NO,           KC_NO,            KC_NO,        KC_SPC,            KC_SPC,         KC_NO,                  KC_NO,     KC_NO,          MO(L_FN)
    ),

    [L_FN] = LAYOUT_universal(
        KC_PSCR,        KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_F12,  KC_F7,         KC_F8,     KC_F9,          KC_NO,    KC_SLEP,
        KC_NO,          KC_NO,         KC_LSFT,         MO(L_SYMB),      MO(L_NAV),        KC_NO,                                           KC_F11,  KC_F4,         KC_F5,     KC_F6,          KC_NO,    KC_NO,
        KC_LSFT,        KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_F10,  KC_F1,         KC_F2,     KC_F3,          KC_NO,    TD(TD_TO_LAYER),
                                       KC_NO,           KC_NO,           KC_LGUI,          KC_LALT,      KC_LCTL,           KC_NO,          KC_NO,                  KC_NO,     KC_NO,          KC_NO
    ),

    [L_UNI_NAV] = LAYOUT_universal(
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_NO,         UC(UC_UP),    KC_NO,        KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           MO(L_SYMB),      KC_NO,            KC_NO,                                           KC_NO,   UC(UC_LEFT),   UC(UC_DOWN),  UC(UC_RIGHT), KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_NO,         UC(UC_BIDIR), KC_NO,        KC_NO,    KC_NO,
                                       KC_NO,           KC_NO,           KC_NO,            KC_NO,        KC_NO,             KC_NO,          KC_NO,                  KC_NO,        KC_NO,        KC_NO
    ),

    [L_UNI_MATH] = LAYOUT_universal(
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_NO,         UC(UC_NE), KC_NO,          UC(UC_DEG), KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_NO,         UC(UC_RE), KC_NO,          KC_NO,      KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_NO,            KC_NO,                                           KC_NO,   KC_NO,         UC(UC_LE), UC(UC_GE),      UC(UC_QE),  KC_NO,
                                       KC_NO,           KC_NO,           KC_NO,            KC_NO,        KC_NO,             UC(UC_DEG),     KC_NO,                  KC_NO,     KC_NO,          KC_NO
    ),

    [L_GAME_NUM] = LAYOUT_universal(
        KC_ESC,         KC_1,          KC_NO,           KC_2,            KC_3,             KC_6,                                            KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_4,             KC_7,                                            KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
        KC_NO,          KC_NO,         KC_NO,           KC_NO,           KC_5,             KC_8,                                            KC_NO,   KC_NO,         KC_NO,     KC_NO,          KC_NO,    KC_NO,
                                       KC_NO,           KC_NO,           KC_NO,            KC_NO,        KC_NO,             KC_NO,          KC_NO,                  KC_NO,     KC_NO,          KC_NO
    )
};

void matrix_init_user(void) {
    // Set default Unicode mode to Linux
    set_unicode_input_mode(UNICODE_MODE_LINUX);
}

void keyboard_post_init_user(void) {
    // Override EEPROM settings to ensure scroll snapping is disabled by default
    keyball_set_scrollsnap_mode(KEYBALL_SCROLLSNAP_MODE_FREE);
}

bool caps_word_press_user(uint16_t keycode) {
    switch (keycode) {
        // Keycodes that continue Caps Word, with shift applied.
        case KC_A ... KC_Z:
        case KC_RSFT:
        case KC_LSFT:
            add_weak_mods(MOD_BIT(KC_LSFT));  // Apply shift to next key.
            return true;

        // Keycodes that continue Caps Word, without shifting.
        case KC_1 ... KC_0:
        case KC_MINS:
        case KC_SLSH:
        case KC_BSLS:
        case KC_UNDS:
            return true;

        default:
            return false;  // Deactivate Caps Word.
    }
}

static uint16_t nav_cw_timer = 0;
static bool nav_cw_used = false;

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode == NAV_CW) {
        if (record->event.pressed) {
            // 1. Enter navigation layer and start a timer to check later if this ends up being a "tap"
            nav_cw_timer = timer_read();
            nav_cw_used = false;
            layer_on(L_NAV);
        } else {
            // 3. On release, check if this was a "tap" of the LT(L_NAV) button.
            // A tap means: (a) no other button was pressed while in L_NAV, and
            //              (b) the button was released faster than TAPPING_TERM
            layer_off(L_NAV);
            if (!nav_cw_used && timer_elapsed(nav_cw_timer) < TAPPING_TERM) {
                caps_word_toggle();
            }
        }
        return false;
    }

    // 2. Keep track whether any other key was pressed while in L_NAV
    if (layer_state_is(L_NAV) && record->event.pressed) {
        nav_cw_used = true;
    }

    return true;
}

// Layer State Logic
layer_state_t layer_state_set_user(layer_state_t state) {
    // Tri-layers
    state = update_tri_layer_state(state, L_NAV, L_SYMB, L_NUM);
    state = update_tri_layer_state(state, L_NAV, L_FN, L_UNI_NAV);
    
    if (layer_state_cmp(state, L_NAV) && layer_state_cmp(state, L_SYMB) && layer_state_cmp(state, L_FN)) {
        state |= (1UL << L_UNI_MATH);
    } else {
        state &= ~(1UL << L_UNI_MATH);
    }

    // Keyball Scroll Mode
    keyball_set_scroll_mode(layer_state_cmp(state, L_NUM));

    return state;
}

#ifdef OLED_ENABLE
#    include "lib/oledkit/oledkit.h"
void oledkit_render_info_user(void) {
    keyball_oled_render_keyinfo();
    keyball_oled_render_ballinfo();
    keyball_oled_render_layerinfo();
}
#endif