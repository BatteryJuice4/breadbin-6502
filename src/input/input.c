#include "input/input.h"
#include "input/keynames.h"

#include <stdbool.h>
#include <string.h>

#define INPUT_QUEUE_SIZE 256

typedef struct {
    bool down;
    int key;    // CORE_KEY_* / ASCII
} key_event_t;

static const uint8_t action_bits[INPUT_ACTION_COUNT] = {
    [INPUT_UP] = CORE_JOY_UP, [INPUT_DOWN] = CORE_JOY_DOWN, [INPUT_LEFT] = CORE_JOY_LEFT,
    [INPUT_RIGHT] = CORE_JOY_RIGHT, [INPUT_FIRE] = CORE_JOY_FIRE,
};

static struct {
    input_settings_t cfg;
    uint8_t bound_mask[INPUT_MAX_KEYCODE];   // joystick bits per host key (0 = not bound)
    bool host_held[INPUT_MAX_KEYCODE];       // bound host keys currently held
    uint8_t tapped;                          // joystick bits pressed since the last frame (short taps)
    key_event_t queue[INPUT_QUEUE_SIZE];
    int queue_len;
    bool release_all;
    bool c64_down[256];                      // C64 keys currently held via KEY_DOWN
    bool suppress_char;                      // drop the CHAR event of a bound key
} is;

void input_set_config(const input_settings_t* cfg) {
    input_release_all();
    is.cfg = *cfg;
    if (is.cfg.joystick_port != 1) is.cfg.joystick_port = 2;
    memset(is.bound_mask, 0, sizeof(is.bound_mask));
    for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
        for (int b = 0; b < INPUT_MAX_BINDS; b++) {
            const int k = cfg->keys[a][b];
            if (k > 0 && k < INPUT_MAX_KEYCODE) is.bound_mask[k] |= action_bits[a];
        }
    }
}

void input_init(const input_settings_t* cfg) {
    memset(&is, 0, sizeof(is));
    input_set_config(cfg);
}

static void enqueue(bool down, int key) {
    if (is.queue_len < INPUT_QUEUE_SIZE) {
        is.queue[is.queue_len++] = (key_event_t){ .down = down, .key = key };
    }
}

// Non-printable host keys -> C64 key code, 0 if unmapped.
static int map_special_key(sapp_keycode k) {
    switch (k) {
        case SAPP_KEYCODE_SPACE:        return CORE_KEY_SPACE;
        case SAPP_KEYCODE_ENTER:
        case SAPP_KEYCODE_KP_ENTER:     return CORE_KEY_RETURN;
        case SAPP_KEYCODE_BACKSPACE:    return CORE_KEY_DEL;
        case SAPP_KEYCODE_INSERT:       return CORE_KEY_INST;
        case SAPP_KEYCODE_HOME:         return CORE_KEY_HOME;
        case SAPP_KEYCODE_END:          return CORE_KEY_STOP;
        case SAPP_KEYCODE_PAGE_UP:      return CORE_KEY_RESTORE;
        case SAPP_KEYCODE_LEFT:         return CORE_KEY_CSRLEFT;
        case SAPP_KEYCODE_RIGHT:        return CORE_KEY_CSRRIGHT;
        case SAPP_KEYCODE_UP:           return CORE_KEY_CSRUP;
        case SAPP_KEYCODE_DOWN:         return CORE_KEY_CSRDOWN;
        case SAPP_KEYCODE_LEFT_CONTROL: return CORE_KEY_CTRL;
        case SAPP_KEYCODE_LEFT_ALT:     return CORE_KEY_CBM;
        case SAPP_KEYCODE_F1:           return CORE_KEY_F1;
        case SAPP_KEYCODE_F2:           return CORE_KEY_F2;
        case SAPP_KEYCODE_F3:           return CORE_KEY_F3;
        case SAPP_KEYCODE_F4:           return CORE_KEY_F4;
        case SAPP_KEYCODE_F5:           return CORE_KEY_F5;
        case SAPP_KEYCODE_F6:           return CORE_KEY_F6;
        case SAPP_KEYCODE_F7:           return CORE_KEY_F7;
        case SAPP_KEYCODE_F8:           return CORE_KEY_F8;
        default:                        return 0;
    }
}

void input_handle_event(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_CHAR: {
            if (is.suppress_char) {
                is.suppress_char = false;
                break;
            }
            uint32_t c = ev->char_code;
            if (c > 0x20 && c < 0x7F) {
                // Host lowercase = C64 unshifted (chips uses 'A'..'Z' for that), and vice versa.
                if (c >= 'a' && c <= 'z')      c -= 0x20;
                else if (c >= 'A' && c <= 'Z') c += 0x20;
                // Press + release; chips' keyboard helper keeps it down long enough to register.
                enqueue(true, (int)c);
                enqueue(false, (int)c);
            }
            break;
        }
        case SAPP_EVENTTYPE_KEY_DOWN:
        case SAPP_EVENTTYPE_KEY_UP: {
            const bool down = ev->type == SAPP_EVENTTYPE_KEY_DOWN;
            const int code = (int)ev->key_code;
            is.suppress_char = false;
            if (code > 0 && code < INPUT_MAX_KEYCODE && is.bound_mask[code]) {
                is.host_held[code] = down;
                if (down) is.tapped |= is.bound_mask[code];
                is.suppress_char = down;    // a bound letter key must not also type
                break;
            }
            const int k = map_special_key(ev->key_code);
            if (k) {
                if (down && ev->key_repeat && is.c64_down[k]) {
                    break;      // auto-repeat: the C64 KERNAL handles repeat itself
                }
                is.c64_down[k] = down;
                enqueue(down, k);
            }
            break;
        }
        case SAPP_EVENTTYPE_UNFOCUSED:
        case SAPP_EVENTTYPE_SUSPENDED:
            input_release_all();
            break;
        default:
            break;
    }
}

void input_release_all(void) {
    is.release_all = true;
    is.queue_len = 0;
    is.suppress_char = false;
    memset(is.host_held, 0, sizeof(is.host_held));
    is.tapped = 0;
}

void input_apply(core_t* core) {
    if (is.release_all) {
        for (int k = 0; k < 256; k++) {
            if (is.c64_down[k]) {
                core_key_up(core, k);
                is.c64_down[k] = false;
            }
        }
        is.release_all = false;
    }
    for (int i = 0; i < is.queue_len; i++) {
        if (is.queue[i].down) core_key_down(core, is.queue[i].key);
        else                  core_key_up(core, is.queue[i].key);
    }
    is.queue_len = 0;
    // a press that was released before this frame still counts for one frame
    uint8_t joy = is.tapped;
    is.tapped = 0;
    for (int k = 1; k < INPUT_MAX_KEYCODE; k++) {
        if (is.host_held[k]) joy |= is.bound_mask[k];
    }
    core_set_joystick(core, is.cfg.joystick_port, joy);
    core_set_joystick(core, is.cfg.joystick_port == 1 ? 2 : 1, 0);
}
