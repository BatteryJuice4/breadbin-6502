#pragma once
// evdev gamepad model (Linux, FreeBSD) (internal; exposed for tests). Pure: no files or
// ioctls here, those live in gamepad_evdev.c's device layer.
#if defined(__linux__) || defined(__FreeBSD__)
#define BB_HAVE_EVDEV 1
#include "platform/gamepad.h"

#ifdef __linux__
#include <linux/input.h>
#else
#include <dev/evdev/input.h>
#endif
#include <stdbool.h>
#include <stdint.h>

typedef struct evpad_known evpad_known_t;   // a pad with its own button layout (gamepad_evdev.c)

typedef struct {
    bool joystick_style;        // BTN_TRIGGER... buttons (generic/retro joysticks) instead of BTN_SOUTH...
    bool has_abs[ABS_CNT];
    int32_t abs_min[ABS_CNT], abs_max[ABS_CNT];
    bool triggers_on_z;         // ABS_Z / ABS_RZ are analog triggers (not a right stick)
    bool key[KEY_CNT];
    int32_t abs[ABS_CNT];
    const evpad_known_t* known; // set by evpad_identify for pads in the known-pads table
} evpad_t;

// Decide from the device's capabilities whether it's a gamepad/joystick, and set
// up the model. key_caps/abs_caps: one bool per code. absinfo: per ABS code (only
// read where abs_caps is set). Returns false for other devices (keyboards, mice,
// touchpads, ...).
bool evpad_setup(evpad_t* p, const bool key_caps[KEY_CNT], const bool abs_caps[ABS_CNT],
                 const struct input_absinfo absinfo[ABS_CNT]);
// After evpad_setup: pads in the known-pads table (by USB vendor:product) get
// their own button layout instead of the generic one. Returns whether it's known.
bool evpad_identify(evpad_t* p, uint16_t vendor, uint16_t product);
// One input_event (EV_KEY / EV_ABS; others are ignored).
void evpad_event(evpad_t* p, uint16_t type, uint16_t code, int32_t value);
// Current state as buttons and axes (name and connected are left alone).
void evpad_read(const evpad_t* p, gamepad_t* out);
#endif
