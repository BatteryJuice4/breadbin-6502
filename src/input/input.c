#include "input/input.h"

#include <stdbool.h>
#include <string.h>

#define INPUT_QUEUE_SIZE 256

typedef struct {
    bool down;
    int key;    // CORE_KEY_* / ASCII
} key_event_t;

static struct {
    int joy_port;
    uint8_t joy_mask;
    key_event_t queue[INPUT_QUEUE_SIZE];
    int queue_len;
    bool release_all;
    bool keys_down[256];    // C64 keys currently held via KEY_DOWN events
} is;

void input_init(int joystick_port) {
    memset(&is, 0, sizeof(is));
    is.joy_port = (joystick_port == 1) ? 1 : 2;
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

static uint8_t map_joy_key(sapp_keycode k) {
    switch (k) {
        case SAPP_KEYCODE_KP_8:          return CORE_JOY_UP;
        case SAPP_KEYCODE_KP_2:          return CORE_JOY_DOWN;
        case SAPP_KEYCODE_KP_4:          return CORE_JOY_LEFT;
        case SAPP_KEYCODE_KP_6:          return CORE_JOY_RIGHT;
        case SAPP_KEYCODE_KP_0:
        case SAPP_KEYCODE_RIGHT_CONTROL: return CORE_JOY_FIRE;
        default:                         return 0;
    }
}

void input_handle_event(const sapp_event* ev) {
    switch (ev->type) {
        case SAPP_EVENTTYPE_CHAR: {
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
            const uint8_t jm = map_joy_key(ev->key_code);
            if (jm) {
                is.joy_mask = down ? (is.joy_mask | jm) : (is.joy_mask & (uint8_t)~jm);
                break;
            }
            const int k = map_special_key(ev->key_code);
            if (k) {
                if (down && ev->key_repeat && is.keys_down[k]) {
                    // Auto-repeat: let the C64 KERNAL handle repeat itself.
                    break;
                }
                is.keys_down[k] = down;
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
    is.joy_mask = 0;
}

void input_apply(core_t* core) {
    if (is.release_all) {
        for (int k = 0; k < 256; k++) {
            if (is.keys_down[k]) {
                core_key_up(core, k);
                is.keys_down[k] = false;
            }
        }
        is.release_all = false;
    }
    for (int i = 0; i < is.queue_len; i++) {
        if (is.queue[i].down) core_key_down(core, is.queue[i].key);
        else                  core_key_up(core, is.queue[i].key);
    }
    is.queue_len = 0;
    core_set_joystick(core, is.joy_port, is.joy_mask);
    core_set_joystick(core, is.joy_port == 1 ? 2 : 1, 0);
}
