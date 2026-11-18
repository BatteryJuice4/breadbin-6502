#pragma once
// Internal interface between gamepad.c and the per-OS backends.
#include "platform/gamepad.h"

// The backend owns pads[GAMEPAD_MAX] contents; gamepad.c reads them after poll.
void gamepad_backend_init(gamepad_t* pads);
void gamepad_backend_shutdown(void);
void gamepad_backend_poll(gamepad_t* pads);
const char* gamepad_backend_name(void);
