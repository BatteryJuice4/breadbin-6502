// Gamepads without hardware: the evdev model fed with made-up devices and
// events (Linux), the axis thresholds, and the path into the C64 joystick
// (bindings, short taps, buttons held over from the menu).
#include "input/input.h"
#include "input/keynames.h"
#include "platform/gamepad.h"
#include "settings/settings.h"
#if defined(__linux__) || defined(__FreeBSD__)
#include "platform/gamepad_evdev.h"
#endif

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

#define BIT GAMEPAD_BIT

static void test_axis_buttons(void) {
    CHECK(gamepad_axis_buttons(0.0f, 0.0f, 0.0f, 0.0f) == 0, "centered");
    CHECK(gamepad_axis_buttons(0.3f, -0.4f, 0.2f, 0.0f) == 0, "small deflection is no press");
    CHECK(gamepad_axis_buttons(-0.8f, 0.9f, 0.0f, 1.0f) ==
          (BIT(GAMEPAD_LS_LEFT) | BIT(GAMEPAD_LS_DOWN) | BIT(GAMEPAD_RT)), "left+down+RT");
    CHECK(gamepad_button_from_name("lstick_right") == GAMEPAD_LS_RIGHT && gamepad_button_from_name("Nope") == -1, "names");
}

#if defined(__linux__) || defined(__FreeBSD__)
static bool key_caps[KEY_CNT], abs_caps[ABS_CNT];
static struct input_absinfo absinfo[ABS_CNT];

static void caps_reset(void) {
    memset(key_caps, 0, sizeof(key_caps));
    memset(abs_caps, 0, sizeof(abs_caps));
    memset(absinfo, 0, sizeof(absinfo));
}

static void abs_axis(int a, int min, int max, int value) {
    abs_caps[a] = true;
    absinfo[a].minimum = min;
    absinfo[a].maximum = max;
    absinfo[a].value = value;
}

static uint32_t read_buttons(const evpad_t* p, gamepad_t* g) {
    memset(g, 0, sizeof(*g));
    evpad_read(p, g);
    return g->buttons;
}

static void test_evdev(void) {
    evpad_t p;
    gamepad_t g;

    // Xbox-style pad (xpad): face buttons, sticks -32768..32767, triggers on Z/RZ, hat d-pad
    caps_reset();
    const int xbox_keys[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_SELECT, BTN_START,
                              BTN_MODE, BTN_THUMBL, BTN_THUMBR };
    for (size_t i = 0; i < sizeof(xbox_keys) / sizeof(xbox_keys[0]); i++) key_caps[xbox_keys[i]] = true;
    abs_axis(ABS_X, -32768, 32767, 0);
    abs_axis(ABS_Y, -32768, 32767, 0);
    abs_axis(ABS_RX, -32768, 32767, 0);
    abs_axis(ABS_RY, -32768, 32767, 0);
    abs_axis(ABS_Z, 0, 255, 0);
    abs_axis(ABS_RZ, 0, 255, 0);
    abs_axis(ABS_HAT0X, -1, 1, 0);
    abs_axis(ABS_HAT0Y, -1, 1, 0);
    CHECK(evpad_setup(&p, key_caps, abs_caps, absinfo), "xbox pad not recognised");
    CHECK(p.triggers_on_z && !p.joystick_style, "xbox pad layout");
    CHECK(read_buttons(&p, &g) == 0, "idle pad reports buttons %08x", (unsigned)g.buttons);
    evpad_event(&p, EV_KEY, BTN_SOUTH, 1);
    evpad_event(&p, EV_KEY, BTN_NORTH, 1);
    evpad_event(&p, EV_ABS, ABS_HAT0X, -1);
    evpad_event(&p, EV_ABS, ABS_Y, -30000);
    evpad_event(&p, EV_ABS, ABS_RZ, 255);
    evpad_event(&p, EV_SYN, SYN_REPORT, 0);
    CHECK(read_buttons(&p, &g) == (BIT(GAMEPAD_A) | BIT(GAMEPAD_Y) | BIT(GAMEPAD_LEFT) | BIT(GAMEPAD_LS_UP) | BIT(GAMEPAD_RT)),
          "xbox pad buttons %08x", (unsigned)g.buttons);
    CHECK(g.ly < -0.8f && g.rt > 0.99f, "axes ly %.2f rt %.2f", (double)g.ly, (double)g.rt);
    evpad_event(&p, EV_KEY, BTN_SOUTH, 2);      // autorepeat keeps it held
    evpad_event(&p, EV_KEY, BTN_NORTH, 0);
    evpad_event(&p, EV_ABS, ABS_HAT0X, 0);
    evpad_event(&p, EV_ABS, ABS_HAT0Y, 1);
    evpad_event(&p, EV_ABS, ABS_Y, 0);
    evpad_event(&p, EV_ABS, ABS_RZ, 0);
    CHECK(read_buttons(&p, &g) == (BIT(GAMEPAD_A) | BIT(GAMEPAD_DOWN)), "after release %08x", (unsigned)g.buttons);

    // A pad with BTN_DPAD_* and 0..255 sticks centred at 128 (e.g. some HID pads)
    caps_reset();
    key_caps[BTN_SOUTH] = key_caps[BTN_EAST] = key_caps[BTN_DPAD_UP] = key_caps[BTN_DPAD_RIGHT] = true;
    abs_axis(ABS_X, 0, 255, 128);
    abs_axis(ABS_Y, 0, 255, 128);
    abs_axis(ABS_Z, 0, 255, 128);       // no RX: Z is a right stick axis here, not a trigger
    CHECK(evpad_setup(&p, key_caps, abs_caps, absinfo) && !p.triggers_on_z, "hid pad");
    CHECK(read_buttons(&p, &g) == 0, "centred 0..255 sticks press nothing (%08x)", (unsigned)g.buttons);
    evpad_event(&p, EV_KEY, BTN_DPAD_RIGHT, 1);
    evpad_event(&p, EV_ABS, ABS_X, 0);
    CHECK(read_buttons(&p, &g) == (BIT(GAMEPAD_RIGHT) | BIT(GAMEPAD_LS_LEFT)), "hid pad %08x", (unsigned)g.buttons);

    // Generic USB joystick (e.g. a C64-style one): BTN_TRIGGER/BTN_THUMB, 0..255 axes
    caps_reset();
    key_caps[BTN_TRIGGER] = key_caps[BTN_THUMB] = true;
    abs_axis(ABS_X, 0, 255, 127);
    abs_axis(ABS_Y, 0, 255, 127);
    CHECK(evpad_setup(&p, key_caps, abs_caps, absinfo) && p.joystick_style, "generic joystick");
    evpad_event(&p, EV_KEY, BTN_TRIGGER, 1);
    evpad_event(&p, EV_ABS, ABS_Y, 255);
    CHECK(read_buttons(&p, &g) == (BIT(GAMEPAD_A) | BIT(GAMEPAD_LS_DOWN)), "joystick %08x", (unsigned)g.buttons);
    CHECK(!evpad_identify(&p, 0x1234, 0x5678), "unknown pad identified");

    // Known pad: DragonRise 0079:0011 SNES-style, buttons 1-4 = X (top), A (right),
    // B (bottom), Y (left); by position the bottom one is GAMEPAD_A.
    caps_reset();
    for (int c = BTN_TRIGGER; c <= BTN_BASE4; c++) key_caps[c] = true;
    abs_axis(ABS_X, 0, 255, 127);
    abs_axis(ABS_Y, 0, 255, 127);
    CHECK(evpad_setup(&p, key_caps, abs_caps, absinfo) && evpad_identify(&p, 0x0079, 0x0011), "dragonrise pad");
    static const struct { int code; gamepad_button_t button; } snes[] = {
        { BTN_TRIGGER, GAMEPAD_Y }, { BTN_THUMB, GAMEPAD_B }, { BTN_THUMB2, GAMEPAD_A }, { BTN_TOP, GAMEPAD_X },
        { BTN_TOP2, GAMEPAD_LB }, { BTN_PINKIE, GAMEPAD_RB }, { BTN_BASE3, GAMEPAD_BACK }, { BTN_BASE4, GAMEPAD_START },
    };
    for (size_t i = 0; i < sizeof(snes) / sizeof(snes[0]); i++) {
        evpad_event(&p, EV_KEY, (uint16_t)snes[i].code, 1);
        CHECK(read_buttons(&p, &g) == BIT(snes[i].button), "dragonrise button %zu -> %08x", i, (unsigned)g.buttons);
        evpad_event(&p, EV_KEY, (uint16_t)snes[i].code, 0);
    }

    // Not pads
    caps_reset();
    key_caps[KEY_A] = key_caps[KEY_SPACE] = true;
    CHECK(!evpad_setup(&p, key_caps, abs_caps, absinfo), "keyboard accepted");
    caps_reset();
    key_caps[BTN_TOUCH] = key_caps[BTN_TOOL_FINGER] = key_caps[BTN_LEFT] = true;
    abs_axis(ABS_X, 0, 3000, 0);
    abs_axis(ABS_Y, 0, 2000, 0);
    CHECK(!evpad_setup(&p, key_caps, abs_caps, absinfo), "touchpad accepted");
    caps_reset();
    key_caps[BTN_TRIGGER] = true;      // a button but no axes
    CHECK(!evpad_setup(&p, key_caps, abs_caps, absinfo), "axis-less device accepted");
}
#endif

static uint8_t joy_after(uint32_t buttons, bool playing) {
    input_set_gamepad(buttons, playing);
    replay_input_t in;
    input_collect(&in);
    return in.joy[1];   // port 2 (default)
}

static void test_input_path(void) {
    settings_t s;
    settings_default(&s);
    input_init(&s.input);
    CHECK(joy_after(0, true) == 0, "idle");
    CHECK(joy_after(BIT(GAMEPAD_A), true) == CORE_JOY_FIRE, "A fires");
    CHECK(joy_after(BIT(GAMEPAD_A) | BIT(GAMEPAD_LS_LEFT), true) == (CORE_JOY_FIRE | CORE_JOY_LEFT), "stick left");
    CHECK(joy_after(BIT(GAMEPAD_UP) | BIT(GAMEPAD_B), true) == (CORE_JOY_UP | CORE_JOY_FIRE), "d-pad up + B");
    CHECK(joy_after(BIT(GAMEPAD_X) | BIT(GAMEPAD_START), true) == 0, "unbound buttons do nothing");

    // a tap between two frames still counts once
    input_set_gamepad(BIT(GAMEPAD_A), true);
    CHECK(joy_after(0, true) == CORE_JOY_FIRE, "short tap lost");
    CHECK(joy_after(0, true) == 0, "tap counted twice");

    // held when the menu closed (A pressed "Resume"): ignored until released
    joy_after(BIT(GAMEPAD_A), false);
    CHECK(joy_after(BIT(GAMEPAD_A), true) == 0, "button held from the menu fired");
    CHECK(joy_after(BIT(GAMEPAD_A) | BIT(GAMEPAD_RIGHT), true) == CORE_JOY_RIGHT, "other buttons still work");
    CHECK(joy_after(0, true) == 0, "release");
    CHECK(joy_after(BIT(GAMEPAD_A), true) == CORE_JOY_FIRE, "pressed again: fires");
    CHECK(joy_after(BIT(GAMEPAD_A), false) == 0, "nothing while the menu is open");

    // rebinding: fire only on Y
    joy_after(0, true);
    for (int b = 0; b < INPUT_MAX_BINDS; b++) s.input.keys[INPUT_FIRE][b] = 0;
    s.input.keys[INPUT_FIRE][0] = input_key_from_name("Gamepad_Y");
    s.input.joystick_port = 1;
    input_set_config(&s.input);
    input_set_gamepad(BIT(GAMEPAD_Y), true);
    replay_input_t in;
    input_collect(&in);
    CHECK(in.joy[0] == CORE_JOY_FIRE && in.joy[1] == 0, "Y on port 1: %02x %02x", in.joy[0], in.joy[1]);
    CHECK(joy_after(BIT(GAMEPAD_A), true) == 0, "A no longer bound");
}

// [input.c64_keys]: a bound button presses the C64 key through the key queue,
// releases it, and lets go of it when the menu opens.
static void test_c64_keys(void) {
    settings_t s;
    settings_default(&s);
    s.input.pad_c64_key[GAMEPAD_X] = CORE_KEY_F1;
    input_init(&s.input);
    replay_input_t in;
    input_collect(&in);     // the first frame sends input_init's release-all
    input_set_gamepad(BIT(GAMEPAD_X), true);
    input_collect(&in);
    CHECK(in.nkeys == 1 && in.keys[0].key == CORE_KEY_F1 && in.keys[0].down, "X presses F1");
    CHECK(in.joy[1] == 0, "X isn't a joystick button");
    input_set_gamepad(BIT(GAMEPAD_X), true);
    input_collect(&in);
    CHECK(in.nkeys == 0, "held: no repeat");
    input_set_gamepad(0, true);
    input_collect(&in);
    CHECK(in.nkeys == 1 && in.keys[0].key == CORE_KEY_F1 && !in.keys[0].down, "released");
    input_set_gamepad(BIT(GAMEPAD_X), true);
    input_collect(&in);
    input_set_gamepad(BIT(GAMEPAD_X), false);      // the menu opens with X held
    input_collect(&in);
    CHECK(in.nkeys == 1 && !in.keys[0].down, "released when the menu opens");
    input_set_gamepad(BIT(GAMEPAD_X), true);       // still held after resuming: ignored
    input_collect(&in);
    CHECK(in.nkeys == 0, "a button held from the menu doesn't press its key");
}

int main(void) {
    test_axis_buttons();
#if defined(__linux__) || defined(__FreeBSD__)
    test_evdev();
#endif
    test_input_path();
    test_c64_keys();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("gamepad: all tests passed\n");
    return 0;
}
