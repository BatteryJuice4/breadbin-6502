#pragma once
// Host key names used in settings and game.toml ("Up", "Space", "LCtrl", "KP_8", "A", ...).
// Values are sokol_app keycodes (sapp_keycode); 0 means "unbound".
#include <stdbool.h>

#define INPUT_MAX_KEYCODE 512

// Returns the canonical name, or NULL for unknown/unbound codes.
const char* input_key_name(int keycode);
// Case-insensitive lookup; also accepts a few aliases ("Ctrl" = "LCtrl", "Return" = "Enter").
// Returns 0 if unknown.
int input_key_from_name(const char* name);
