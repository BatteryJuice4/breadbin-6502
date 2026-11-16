#pragma once
/*
    Host keyboard -> C64 keyboard matrix / joystick.

    Events are queued as they arrive and applied to the core only at frame
    boundaries (input_apply), so the emulation sees input per frame: the
    basis for deterministic replays later.

    Phase 1 fixed mapping (remapping comes with the launcher menu):
      typing            -> C64 keys (letters, digits, punctuation)
      Enter / Backspace -> RETURN / DEL      Insert -> INST
      Home              -> CLR/HOME          End    -> RUN/STOP
      Page Up           -> RESTORE           F1..F8 -> F1..F8
      Left Ctrl         -> CTRL              Left Alt -> C=
      cursor keys       -> C64 cursor keys
      keypad 8/2/4/6    -> joystick (port 2 by default), keypad 0 / Right Ctrl -> fire
      Esc               -> reserved for the launcher menu
*/
#include "core/core.h"
#include "sokol_app.h"

void input_init(int joystick_port);
void input_handle_event(const sapp_event* ev);
void input_apply(core_t* core);     // call right before core_run_frame()
void input_release_all(void);       // e.g. on focus loss
