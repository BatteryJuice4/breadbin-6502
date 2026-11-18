#pragma once
// Host key names used in settings and game.toml ("Up", "Space", "LCtrl", "KP_8", "A", ...).
// Values are sokol_app keycodes (sapp_keycode); 0 means "unbound".
// Gamepad buttons share the same code space: INPUT_GAMEPAD_BASE + gamepad_button_t,
// named "Gamepad_<button>" ("Gamepad_A", "Gamepad_LStick_Up", ...).
#include <stdbool.h>

#define INPUT_MAX_KEYCODE 512                   // keyboard codes are below this
#define INPUT_GAMEPAD_BASE 1024
#define INPUT_IS_GAMEPAD(code) ((code) >= INPUT_GAMEPAD_BASE && (code) < INPUT_GAMEPAD_BASE + 32)

// Returns the canonical name, or NULL for unknown/unbound codes.
const char* input_key_name(int keycode);
// Case-insensitive lookup; also accepts a few aliases ("Ctrl" = "LCtrl", "Return" = "Enter").
// Returns 0 if unknown.
int input_key_from_name(const char* name);

// C64 keys that gamepad buttons can press ([input.c64_keys]): "F1".."F8", "Space",
// "Return", "RunStop", "Restore", "Del", "Home", "A".."Z" and "0".."9". Values are
// core key codes (CORE_KEY_* / the unshifted character). 0 = unknown.
int input_c64_key_from_name(const char* name);
const char* input_c64_key_name(int c64_key);       // NULL if it has no name
