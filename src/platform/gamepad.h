#pragma once
/*
    Gamepads (our own small backends, no extra library):
      Linux    evdev (/dev/input/event*), hot-plug by rescanning
      Windows  XInput (xinput1_4 / 1_3 / 9_1_0, loaded at run time)
      others   none yet (gamepad_backend() says "none")

    Buttons follow the Xbox layout. Stick directions and analog triggers are
    also reported as buttons (past half way), so everything can be bound like
    a key. Call gamepad_poll() once per display frame.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAMEPAD_A = 0, GAMEPAD_B, GAMEPAD_X, GAMEPAD_Y,
    GAMEPAD_BACK, GAMEPAD_GUIDE, GAMEPAD_START,
    GAMEPAD_LSTICK, GAMEPAD_RSTICK,         // stick clicks
    GAMEPAD_LB, GAMEPAD_RB, GAMEPAD_LT, GAMEPAD_RT,
    GAMEPAD_UP, GAMEPAD_DOWN, GAMEPAD_LEFT, GAMEPAD_RIGHT,                  // d-pad
    GAMEPAD_LS_UP, GAMEPAD_LS_DOWN, GAMEPAD_LS_LEFT, GAMEPAD_LS_RIGHT,      // left stick directions
    GAMEPAD_BUTTON_COUNT,
} gamepad_button_t;

#define GAMEPAD_MAX 4
#define GAMEPAD_BIT(b) (1u << (b))

typedef struct {
    bool connected;
    char name[96];
    uint32_t buttons;           // GAMEPAD_BIT(gamepad_button_t), incl. stick directions/triggers
    float lx, ly;               // left stick, -1..1 (y: +1 = down)
    float lt, rt;               // triggers, 0..1
} gamepad_t;

void gamepad_init(void);
void gamepad_shutdown(void);
// Read all pads, notice connects/disconnects. Cheap; call every display frame.
void gamepad_poll(void);
const gamepad_t* gamepad_get(int index);    // NULL if index is out of range
uint32_t gamepad_buttons_all(void);         // OR of all connected pads
int gamepad_connected_count(void);
const char* gamepad_backend(void);          // "evdev", "xinput", "gamecontroller" or "none"
// A connect/disconnect since the last call ("Gamepad connected: ..."), for a notice.
bool gamepad_take_event(char* text, size_t size);

// Button names as used in settings ("A", "LStick_Up", ...), without the "Gamepad_" prefix.
const char* gamepad_button_name(gamepad_button_t b);
int gamepad_button_from_name(const char* name);     // -1 if unknown (case-insensitive)

// Shared by the backends: stick/trigger values -> direction/trigger buttons.
uint32_t gamepad_axis_buttons(float lx, float ly, float lt, float rt);

#ifdef __cplusplus
}
#endif
