// Windows gamepads through XInput (Xbox-compatible pads, which covers most PC
// pads). The DLL is loaded at run time so a missing one only means "no gamepads".
// Empty slots are checked about once a second: XInputGetState is slow for them.
// See gamepad.h.
#ifdef _WIN32
#include "platform/gamepad_backend.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#include <stdio.h>
#include <string.h>

#define RESCAN_POLLS 60

typedef DWORD(WINAPI* xinput_get_state_fn)(DWORD, XINPUT_STATE*);

static struct {
    HMODULE dll;
    xinput_get_state_fn get_state;
    int polls_until_scan;
} xi;

void gamepad_backend_init(gamepad_t* pads) {
    (void)pads;
    memset(&xi, 0, sizeof(xi));
    static const char* const dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !xi.dll; i++) xi.dll = LoadLibraryA(dlls[i]);
    if (xi.dll) xi.get_state = (xinput_get_state_fn)(void (*)(void))GetProcAddress(xi.dll, "XInputGetState");
}

void gamepad_backend_shutdown(void) {
    if (xi.dll) FreeLibrary(xi.dll);
    memset(&xi, 0, sizeof(xi));
}

static float stick(SHORT v) {
    const float f = v < 0 ? (float)v / 32768.0f : (float)v / 32767.0f;
    return f < -1.0f ? -1.0f : f > 1.0f ? 1.0f : f;
}

void gamepad_backend_poll(gamepad_t* pads) {
    if (!xi.get_state) return;
    const bool scan = --xi.polls_until_scan <= 0;
    if (scan) xi.polls_until_scan = RESCAN_POLLS;
    for (DWORD i = 0; i < XUSER_MAX_COUNT && i < GAMEPAD_MAX; i++) {
        gamepad_t* g = &pads[i];
        if (!g->connected && !scan) continue;
        XINPUT_STATE s;
        memset(&s, 0, sizeof(s));
        if (xi.get_state(i, &s) != ERROR_SUCCESS) {
            memset(g, 0, sizeof(*g));
            continue;
        }
        if (!g->connected) snprintf(g->name, sizeof(g->name), "XInput controller %lu", (unsigned long)i + 1);
        g->connected = true;
        const WORD w = s.Gamepad.wButtons;
        static const struct { WORD mask; gamepad_button_t b; } map[] = {
            { XINPUT_GAMEPAD_A, GAMEPAD_A }, { XINPUT_GAMEPAD_B, GAMEPAD_B },
            { XINPUT_GAMEPAD_X, GAMEPAD_X }, { XINPUT_GAMEPAD_Y, GAMEPAD_Y },
            { XINPUT_GAMEPAD_BACK, GAMEPAD_BACK }, { XINPUT_GAMEPAD_START, GAMEPAD_START },
            { XINPUT_GAMEPAD_LEFT_THUMB, GAMEPAD_LSTICK }, { XINPUT_GAMEPAD_RIGHT_THUMB, GAMEPAD_RSTICK },
            { XINPUT_GAMEPAD_LEFT_SHOULDER, GAMEPAD_LB }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, GAMEPAD_RB },
            { XINPUT_GAMEPAD_DPAD_UP, GAMEPAD_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, GAMEPAD_DOWN },
            { XINPUT_GAMEPAD_DPAD_LEFT, GAMEPAD_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, GAMEPAD_RIGHT },
        };
        uint32_t b = 0;
        for (size_t k = 0; k < sizeof(map) / sizeof(map[0]); k++) {
            if (w & map[k].mask) b |= GAMEPAD_BIT(map[k].b);
        }
        g->lx = stick(s.Gamepad.sThumbLX);
        g->ly = -stick(s.Gamepad.sThumbLY);     // XInput: +y is up
        g->lt = (float)s.Gamepad.bLeftTrigger / 255.0f;
        g->rt = (float)s.Gamepad.bRightTrigger / 255.0f;
        g->buttons = b | gamepad_axis_buttons(g->lx, g->ly, g->lt, g->rt);
    }
}

const char* gamepad_backend_name(void) { return "xinput"; }
#endif
