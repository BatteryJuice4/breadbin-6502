#include "input/keynames.h"

#include "platform/gamepad.h"
#include "sokol_app.h"  // keycode enum only (no implementation needed)

#include <ctype.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    int code;
    const char* name;
} key_name_t;

static const key_name_t key_names[] = {
    { SAPP_KEYCODE_SPACE, "Space" }, { SAPP_KEYCODE_APOSTROPHE, "Apostrophe" },
    { SAPP_KEYCODE_COMMA, "Comma" }, { SAPP_KEYCODE_MINUS, "Minus" },
    { SAPP_KEYCODE_PERIOD, "Period" }, { SAPP_KEYCODE_SLASH, "Slash" },
    { SAPP_KEYCODE_0, "0" }, { SAPP_KEYCODE_1, "1" }, { SAPP_KEYCODE_2, "2" }, { SAPP_KEYCODE_3, "3" },
    { SAPP_KEYCODE_4, "4" }, { SAPP_KEYCODE_5, "5" }, { SAPP_KEYCODE_6, "6" }, { SAPP_KEYCODE_7, "7" },
    { SAPP_KEYCODE_8, "8" }, { SAPP_KEYCODE_9, "9" },
    { SAPP_KEYCODE_SEMICOLON, "Semicolon" }, { SAPP_KEYCODE_EQUAL, "Equal" },
    { SAPP_KEYCODE_A, "A" }, { SAPP_KEYCODE_B, "B" }, { SAPP_KEYCODE_C, "C" }, { SAPP_KEYCODE_D, "D" },
    { SAPP_KEYCODE_E, "E" }, { SAPP_KEYCODE_F, "F" }, { SAPP_KEYCODE_G, "G" }, { SAPP_KEYCODE_H, "H" },
    { SAPP_KEYCODE_I, "I" }, { SAPP_KEYCODE_J, "J" }, { SAPP_KEYCODE_K, "K" }, { SAPP_KEYCODE_L, "L" },
    { SAPP_KEYCODE_M, "M" }, { SAPP_KEYCODE_N, "N" }, { SAPP_KEYCODE_O, "O" }, { SAPP_KEYCODE_P, "P" },
    { SAPP_KEYCODE_Q, "Q" }, { SAPP_KEYCODE_R, "R" }, { SAPP_KEYCODE_S, "S" }, { SAPP_KEYCODE_T, "T" },
    { SAPP_KEYCODE_U, "U" }, { SAPP_KEYCODE_V, "V" }, { SAPP_KEYCODE_W, "W" }, { SAPP_KEYCODE_X, "X" },
    { SAPP_KEYCODE_Y, "Y" }, { SAPP_KEYCODE_Z, "Z" },
    { SAPP_KEYCODE_LEFT_BRACKET, "LeftBracket" }, { SAPP_KEYCODE_BACKSLASH, "Backslash" },
    { SAPP_KEYCODE_RIGHT_BRACKET, "RightBracket" }, { SAPP_KEYCODE_GRAVE_ACCENT, "Grave" },
    { SAPP_KEYCODE_WORLD_1, "World1" }, { SAPP_KEYCODE_WORLD_2, "World2" },
    { SAPP_KEYCODE_ESCAPE, "Escape" }, { SAPP_KEYCODE_ENTER, "Enter" }, { SAPP_KEYCODE_TAB, "Tab" },
    { SAPP_KEYCODE_BACKSPACE, "Backspace" }, { SAPP_KEYCODE_INSERT, "Insert" },
    { SAPP_KEYCODE_DELETE, "Delete" }, { SAPP_KEYCODE_RIGHT, "Right" }, { SAPP_KEYCODE_LEFT, "Left" },
    { SAPP_KEYCODE_DOWN, "Down" }, { SAPP_KEYCODE_UP, "Up" }, { SAPP_KEYCODE_PAGE_UP, "PageUp" },
    { SAPP_KEYCODE_PAGE_DOWN, "PageDown" }, { SAPP_KEYCODE_HOME, "Home" }, { SAPP_KEYCODE_END, "End" },
    { SAPP_KEYCODE_CAPS_LOCK, "CapsLock" }, { SAPP_KEYCODE_SCROLL_LOCK, "ScrollLock" },
    { SAPP_KEYCODE_NUM_LOCK, "NumLock" }, { SAPP_KEYCODE_PRINT_SCREEN, "PrintScreen" },
    { SAPP_KEYCODE_PAUSE, "Pause" },
    { SAPP_KEYCODE_F1, "F1" }, { SAPP_KEYCODE_F2, "F2" }, { SAPP_KEYCODE_F3, "F3" }, { SAPP_KEYCODE_F4, "F4" },
    { SAPP_KEYCODE_F5, "F5" }, { SAPP_KEYCODE_F6, "F6" }, { SAPP_KEYCODE_F7, "F7" }, { SAPP_KEYCODE_F8, "F8" },
    { SAPP_KEYCODE_F9, "F9" }, { SAPP_KEYCODE_F10, "F10" }, { SAPP_KEYCODE_F11, "F11" }, { SAPP_KEYCODE_F12, "F12" },
    { SAPP_KEYCODE_KP_0, "KP_0" }, { SAPP_KEYCODE_KP_1, "KP_1" }, { SAPP_KEYCODE_KP_2, "KP_2" },
    { SAPP_KEYCODE_KP_3, "KP_3" }, { SAPP_KEYCODE_KP_4, "KP_4" }, { SAPP_KEYCODE_KP_5, "KP_5" },
    { SAPP_KEYCODE_KP_6, "KP_6" }, { SAPP_KEYCODE_KP_7, "KP_7" }, { SAPP_KEYCODE_KP_8, "KP_8" },
    { SAPP_KEYCODE_KP_9, "KP_9" }, { SAPP_KEYCODE_KP_DECIMAL, "KP_Decimal" },
    { SAPP_KEYCODE_KP_DIVIDE, "KP_Divide" }, { SAPP_KEYCODE_KP_MULTIPLY, "KP_Multiply" },
    { SAPP_KEYCODE_KP_SUBTRACT, "KP_Subtract" }, { SAPP_KEYCODE_KP_ADD, "KP_Add" },
    { SAPP_KEYCODE_KP_ENTER, "KP_Enter" }, { SAPP_KEYCODE_KP_EQUAL, "KP_Equal" },
    { SAPP_KEYCODE_LEFT_SHIFT, "LShift" }, { SAPP_KEYCODE_LEFT_CONTROL, "LCtrl" },
    { SAPP_KEYCODE_LEFT_ALT, "LAlt" }, { SAPP_KEYCODE_LEFT_SUPER, "LSuper" },
    { SAPP_KEYCODE_RIGHT_SHIFT, "RShift" }, { SAPP_KEYCODE_RIGHT_CONTROL, "RCtrl" },
    { SAPP_KEYCODE_RIGHT_ALT, "RAlt" }, { SAPP_KEYCODE_RIGHT_SUPER, "RSuper" },
    { SAPP_KEYCODE_MENU, "Menu" },
};

static const key_name_t key_aliases[] = {
    { SAPP_KEYCODE_LEFT_CONTROL, "Ctrl" }, { SAPP_KEYCODE_LEFT_SHIFT, "Shift" },
    { SAPP_KEYCODE_LEFT_ALT, "Alt" }, { SAPP_KEYCODE_ENTER, "Return" },
    { SAPP_KEYCODE_ESCAPE, "Esc" }, { SAPP_KEYCODE_SPACE, " " },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

const char* input_key_name(int keycode) {
    if (INPUT_IS_GAMEPAD(keycode)) {
        static char buf[GAMEPAD_BUTTON_COUNT][32];
        const int b = keycode - INPUT_GAMEPAD_BASE;
        const char* n = gamepad_button_name((gamepad_button_t)b);
        if (!n) return NULL;
        if (!buf[b][0]) {
            const char prefix[] = "Gamepad_";
            memcpy(buf[b], prefix, sizeof(prefix) - 1);
            for (size_t i = 0; n[i] && i + sizeof(prefix) < sizeof(buf[b]); i++) buf[b][sizeof(prefix) - 1 + i] = n[i];
        }
        return buf[b];
    }
    for (size_t i = 0; i < COUNT(key_names); i++) {
        if (key_names[i].code == keycode) return key_names[i].name;
    }
    return NULL;
}

static bool name_eq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == *b;
}

int input_key_from_name(const char* name) {
    if (!name) return 0;
    if (!strncmp(name, "Gamepad_", 8) || !strncmp(name, "gamepad_", 8)) {
        const int b = gamepad_button_from_name(name + 8);
        return b >= 0 ? INPUT_GAMEPAD_BASE + b : 0;
    }
    for (size_t i = 0; i < COUNT(key_names); i++) {
        if (name_eq(key_names[i].name, name)) return key_names[i].code;
    }
    for (size_t i = 0; i < COUNT(key_aliases); i++) {
        if (name_eq(key_aliases[i].name, name)) return key_aliases[i].code;
    }
    return 0;
}
