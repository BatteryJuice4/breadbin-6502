// macOS gamepads through Apple's GameController framework (Xbox, PlayStation,
// MFi and other "extended gamepad" controllers paired with the Mac). The
// framework tracks connections itself, so each poll just reads the current list.
// See gamepad.h. Objective-C, but no ARC/MRC-specific code: nothing is retained.
#if defined(__APPLE__)
#import <GameController/GameController.h>

#include "platform/gamepad_backend.h"

#include <stdio.h>
#include <string.h>

void gamepad_backend_init(gamepad_t* pads) { (void)pads; }
void gamepad_backend_shutdown(void) {}

// Messaging nil returns NO, so missing buttons simply read as released.
#define BB_BUTTON(input, id) do { if ((input).pressed) b |= GAMEPAD_BIT(id); } while (0)

void gamepad_backend_poll(gamepad_t* pads) {
    @autoreleasepool {
        int n = 0;
        for (GCController* c in [GCController controllers]) {
            if (n >= GAMEPAD_MAX) break;
            GCExtendedGamepad* e = c.extendedGamepad;
            if (!e) continue;   // micro gamepads (Siri remote) and wheels aren't joysticks
            gamepad_t* g = &pads[n++];
            const char* vendor = c.vendorName ? c.vendorName.UTF8String : NULL;
            snprintf(g->name, sizeof(g->name), "%s", vendor && vendor[0] ? vendor : "Game controller");
            g->connected = true;

            uint32_t b = 0;
            BB_BUTTON(e.buttonA, GAMEPAD_A);
            BB_BUTTON(e.buttonB, GAMEPAD_B);
            BB_BUTTON(e.buttonX, GAMEPAD_X);
            BB_BUTTON(e.buttonY, GAMEPAD_Y);
            BB_BUTTON(e.leftShoulder, GAMEPAD_LB);
            BB_BUTTON(e.rightShoulder, GAMEPAD_RB);
            BB_BUTTON(e.dpad.up, GAMEPAD_UP);
            BB_BUTTON(e.dpad.down, GAMEPAD_DOWN);
            BB_BUTTON(e.dpad.left, GAMEPAD_LEFT);
            BB_BUTTON(e.dpad.right, GAMEPAD_RIGHT);
            if (@available(macOS 10.14.1, *)) {
                BB_BUTTON(e.leftThumbstickButton, GAMEPAD_LSTICK);
                BB_BUTTON(e.rightThumbstickButton, GAMEPAD_RSTICK);
            }
            if (@available(macOS 10.15, *)) {
                BB_BUTTON(e.buttonMenu, GAMEPAD_START);       // Xbox "Menu", PlayStation "Options"
                BB_BUTTON(e.buttonOptions, GAMEPAD_BACK);     // Xbox "View", PlayStation "Share"
            }
            if (@available(macOS 11.0, *)) {
                BB_BUTTON(e.buttonHome, GAMEPAD_GUIDE);
            }
            g->lx = e.leftThumbstick.xAxis.value;
            g->ly = -e.leftThumbstick.yAxis.value;          // GameController: +y is up
            g->lt = e.leftTrigger.value;
            g->rt = e.rightTrigger.value;
            g->buttons = b | gamepad_axis_buttons(g->lx, g->ly, g->lt, g->rt);
        }
        for (; n < GAMEPAD_MAX; n++) memset(&pads[n], 0, sizeof(pads[n]));
    }
}

const char* gamepad_backend_name(void) { return "gamecontroller"; }
#endif
