#pragma once
/*
    Host keyboard -> C64 joystick / keyboard matrix.

    Events are queued as they arrive and applied to the core only at frame
    boundaries (input_apply), so the emulation sees input per frame: the
    basis for deterministic replays.

    Keys bound to joystick actions (settings_t.input) drive the joystick and
    are NOT passed to the C64 keyboard. All other keys type on the C64:
      typing            -> C64 keys (letters, digits, punctuation)
      Enter / Backspace -> RETURN / DEL      Insert -> INST
      Home              -> CLR/HOME          End    -> RUN/STOP
      Page Up           -> RESTORE           F1..F8 -> F1..F8
      Left Ctrl         -> CTRL              Left Alt -> C=
      cursor keys       -> C64 cursor keys
    The menu key and F11 are handled by the app before input sees them.
*/
#include "core/core.h"
#include "replay/replay.h"
#include "settings/settings.h"
#include "sokol_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_init(const input_settings_t* cfg);
void input_set_config(const input_settings_t* cfg);   // releases held keys
void input_handle_event(const sapp_event* ev);
void input_apply(core_t* core);     // call right before core_run_frame()
// This frame's input without applying it (for replays): C64 key events (at most
// REPLAY_MAX_KEYS; the rest wait for the next frame) and both joystick ports.
void input_collect(replay_input_t* out);
void input_release_all(void);       // e.g. on focus loss or when the menu opens

#ifdef __cplusplus
}
#endif
