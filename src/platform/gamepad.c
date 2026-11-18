// Gamepads: shared code (names, axis thresholds, connect notices) and the
// "none" backend for platforms without one yet. See gamepad.h.
#include "platform/gamepad_backend.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define AXIS_PRESS 0.5f     // stick/trigger past half way counts as a button

static struct {
    gamepad_t pads[GAMEPAD_MAX];
    bool was_connected[GAMEPAD_MAX];
    char event[160];
    bool initialized;
} gp;

static const char* const names[GAMEPAD_BUTTON_COUNT] = {
    [GAMEPAD_A] = "A", [GAMEPAD_B] = "B", [GAMEPAD_X] = "X", [GAMEPAD_Y] = "Y",
    [GAMEPAD_BACK] = "Back", [GAMEPAD_GUIDE] = "Guide", [GAMEPAD_START] = "Start",
    [GAMEPAD_LSTICK] = "LStick", [GAMEPAD_RSTICK] = "RStick",
    [GAMEPAD_LB] = "LB", [GAMEPAD_RB] = "RB", [GAMEPAD_LT] = "LT", [GAMEPAD_RT] = "RT",
    [GAMEPAD_UP] = "Up", [GAMEPAD_DOWN] = "Down", [GAMEPAD_LEFT] = "Left", [GAMEPAD_RIGHT] = "Right",
    [GAMEPAD_LS_UP] = "LStick_Up", [GAMEPAD_LS_DOWN] = "LStick_Down",
    [GAMEPAD_LS_LEFT] = "LStick_Left", [GAMEPAD_LS_RIGHT] = "LStick_Right",
};

const char* gamepad_button_name(gamepad_button_t b) {
    return (int)b >= 0 && b < GAMEPAD_BUTTON_COUNT ? names[b] : NULL;
}

int gamepad_button_from_name(const char* name) {
    if (!name) return -1;
    for (int b = 0; b < GAMEPAD_BUTTON_COUNT; b++) {
        const char* a = names[b];
        const char* n = name;
        while (*a && *n && tolower((unsigned char)*a) == tolower((unsigned char)*n)) a++, n++;
        if (!*a && !*n) return b;
    }
    return -1;
}

uint32_t gamepad_axis_buttons(float lx, float ly, float lt, float rt) {
    uint32_t b = 0;
    if (lx <= -AXIS_PRESS) b |= GAMEPAD_BIT(GAMEPAD_LS_LEFT);
    if (lx >= AXIS_PRESS)  b |= GAMEPAD_BIT(GAMEPAD_LS_RIGHT);
    if (ly <= -AXIS_PRESS) b |= GAMEPAD_BIT(GAMEPAD_LS_UP);
    if (ly >= AXIS_PRESS)  b |= GAMEPAD_BIT(GAMEPAD_LS_DOWN);
    if (lt >= AXIS_PRESS)  b |= GAMEPAD_BIT(GAMEPAD_LT);
    if (rt >= AXIS_PRESS)  b |= GAMEPAD_BIT(GAMEPAD_RT);
    return b;
}

void gamepad_init(void) {
    memset(&gp, 0, sizeof(gp));
    gamepad_backend_init(gp.pads);
    gp.initialized = true;
}

void gamepad_shutdown(void) {
    if (gp.initialized) gamepad_backend_shutdown();
    gp.initialized = false;
}

void gamepad_poll(void) {
    if (!gp.initialized) return;
    gamepad_backend_poll(gp.pads);
    for (int i = 0; i < GAMEPAD_MAX; i++) {
        const gamepad_t* p = &gp.pads[i];
        if (p->connected != gp.was_connected[i]) {
            snprintf(gp.event, sizeof(gp.event), "Gamepad %s: %s", p->connected ? "connected" : "disconnected",
                     p->name[0] ? p->name : "unnamed");
            gp.was_connected[i] = p->connected;
        }
    }
}

const gamepad_t* gamepad_get(int index) {
    return index >= 0 && index < GAMEPAD_MAX ? &gp.pads[index] : NULL;
}

uint32_t gamepad_buttons_all(void) {
    uint32_t b = 0;
    for (int i = 0; i < GAMEPAD_MAX; i++) {
        if (gp.pads[i].connected) b |= gp.pads[i].buttons;
    }
    return b;
}

int gamepad_connected_count(void) {
    int n = 0;
    for (int i = 0; i < GAMEPAD_MAX; i++) n += gp.pads[i].connected;
    return n;
}

const char* gamepad_backend(void) {
    return gamepad_backend_name();
}

bool gamepad_take_event(char* text, size_t size) {
    if (!gp.event[0]) return false;
    snprintf(text, size, "%s", gp.event);
    gp.event[0] = 0;
    return true;
}

#if !defined(__linux__) && !defined(_WIN32)
// No backend yet (macOS: GameController framework, FreeBSD: evdev; both planned).
void gamepad_backend_init(gamepad_t* pads) { (void)pads; }
void gamepad_backend_shutdown(void) {}
void gamepad_backend_poll(gamepad_t* pads) { (void)pads; }
const char* gamepad_backend_name(void) { return "none"; }
#endif
