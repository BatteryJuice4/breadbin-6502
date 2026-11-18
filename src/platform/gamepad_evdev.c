// Linux gamepads through evdev. Pads appear as /dev/input/event* devices that
// the logged-in user can read (udev's uaccess rule for joysticks). We rescan the
// folder about once a second for new devices (hot-plug) and drop a device when
// reading it fails. See gamepad.h.
#ifdef __linux__
#define _POSIX_C_SOURCE 200809L     // O_CLOEXEC under strict C11
#include "platform/gamepad_backend.h"
#include "platform/gamepad_evdev.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

// ---- model ------------------------------------------------------------------

typedef struct { uint16_t code; gamepad_button_t button; } button_map_t;

// Linux gamepad spec (Documentation/input/gamepad.rst): BTN_NORTH is the top face
// button (Xbox Y), BTN_WEST the left one (Xbox X).
static const button_map_t gamepad_buttons[] = {
    { BTN_SOUTH, GAMEPAD_A }, { BTN_EAST, GAMEPAD_B }, { BTN_WEST, GAMEPAD_X }, { BTN_NORTH, GAMEPAD_Y },
    { BTN_SELECT, GAMEPAD_BACK }, { BTN_MODE, GAMEPAD_GUIDE }, { BTN_START, GAMEPAD_START },
    { BTN_THUMBL, GAMEPAD_LSTICK }, { BTN_THUMBR, GAMEPAD_RSTICK },
    { BTN_TL, GAMEPAD_LB }, { BTN_TR, GAMEPAD_RB }, { BTN_TL2, GAMEPAD_LT }, { BTN_TR2, GAMEPAD_RT },
    { BTN_DPAD_UP, GAMEPAD_UP }, { BTN_DPAD_DOWN, GAMEPAD_DOWN },
    { BTN_DPAD_LEFT, GAMEPAD_LEFT }, { BTN_DPAD_RIGHT, GAMEPAD_RIGHT },
    // xpad's wireless pads report the d-pad as these
    { BTN_TRIGGER_HAPPY1, GAMEPAD_LEFT }, { BTN_TRIGGER_HAPPY2, GAMEPAD_RIGHT },
    { BTN_TRIGGER_HAPPY3, GAMEPAD_UP }, { BTN_TRIGGER_HAPPY4, GAMEPAD_DOWN },
};

// Generic HID joysticks (including C64-style USB joysticks): first buttons as A, B, X, Y...
static const button_map_t joystick_buttons[] = {
    { BTN_TRIGGER, GAMEPAD_A }, { BTN_THUMB, GAMEPAD_B }, { BTN_THUMB2, GAMEPAD_X }, { BTN_TOP, GAMEPAD_Y },
    { BTN_TOP2, GAMEPAD_LB }, { BTN_PINKIE, GAMEPAD_RB }, { BTN_BASE, GAMEPAD_LT }, { BTN_BASE2, GAMEPAD_RT },
    { BTN_BASE3, GAMEPAD_BACK }, { BTN_BASE4, GAMEPAD_START },
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

bool evpad_setup(evpad_t* p, const bool key_caps[KEY_CNT], const bool abs_caps[ABS_CNT],
                 const struct input_absinfo absinfo[ABS_CNT]) {
    memset(p, 0, sizeof(*p));
    if (key_caps[BTN_TOUCH] || key_caps[BTN_TOOL_FINGER] || key_caps[BTN_TOOL_PEN]) return false;
    const bool gamepad = key_caps[BTN_SOUTH];
    bool joystick = false;
    for (int c = BTN_JOYSTICK; c < BTN_JOYSTICK + 16; c++) joystick |= key_caps[c];
    joystick = joystick && abs_caps[ABS_X] && abs_caps[ABS_Y];
    if (!gamepad && !joystick) return false;
    p->joystick_style = !gamepad;
    for (int a = 0; a < ABS_CNT; a++) {
        if (!abs_caps[a]) continue;
        p->has_abs[a] = true;
        p->abs_min[a] = absinfo[a].minimum;
        p->abs_max[a] = absinfo[a].maximum;
        p->abs[a] = absinfo[a].value;
    }
    // Pads with a right stick on RX/RY use Z/RZ for the triggers (0..max).
    p->triggers_on_z = gamepad && p->has_abs[ABS_RX] && p->has_abs[ABS_Z] && p->abs_min[ABS_Z] >= 0;
    return true;
}

void evpad_event(evpad_t* p, uint16_t type, uint16_t code, int32_t value) {
    if (type == EV_KEY && code < KEY_CNT) p->key[code] = value != 0;      // 2 = autorepeat
    else if (type == EV_ABS && code < ABS_CNT) p->abs[code] = value;
}

// -1..1 (centered axis) or 0..1 (one-sided)
static float axis(const evpad_t* p, int a, bool one_sided) {
    if (!p->has_abs[a] || p->abs_max[a] <= p->abs_min[a]) return 0.0f;
    const float t = (float)(p->abs[a] - p->abs_min[a]) / (float)(p->abs_max[a] - p->abs_min[a]);
    const float v = one_sided ? t : t * 2.0f - 1.0f;
    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
}

void evpad_read(const evpad_t* p, gamepad_t* out) {
    uint32_t b = 0;
    const button_map_t* map = p->joystick_style ? joystick_buttons : gamepad_buttons;
    const size_t n = p->joystick_style ? COUNT(joystick_buttons) : COUNT(gamepad_buttons);
    for (size_t i = 0; i < n; i++) {
        if (p->key[map[i].code]) b |= GAMEPAD_BIT(map[i].button);
    }
    if (p->has_abs[ABS_HAT0X]) {
        if (p->abs[ABS_HAT0X] < 0) b |= GAMEPAD_BIT(GAMEPAD_LEFT);
        if (p->abs[ABS_HAT0X] > 0) b |= GAMEPAD_BIT(GAMEPAD_RIGHT);
    }
    if (p->has_abs[ABS_HAT0Y]) {
        if (p->abs[ABS_HAT0Y] < 0) b |= GAMEPAD_BIT(GAMEPAD_UP);
        if (p->abs[ABS_HAT0Y] > 0) b |= GAMEPAD_BIT(GAMEPAD_DOWN);
    }
    out->lx = axis(p, ABS_X, false);
    out->ly = axis(p, ABS_Y, false);
    out->lt = p->triggers_on_z ? axis(p, ABS_Z, true) : axis(p, ABS_BRAKE, true);
    out->rt = p->triggers_on_z ? axis(p, ABS_RZ, true) : axis(p, ABS_GAS, true);
    out->buttons = b | gamepad_axis_buttons(out->lx, out->ly, out->lt, out->rt);
}

// ---- devices ------------------------------------------------------------------

#define DEV_DIR "/dev/input"
#define RESCAN_POLLS 60         // ~1 s at 60 Hz
#define MAX_SKIPPED 64

typedef struct {
    int fd;                     // -1 = free
    char path[64];
    bool dropped;               // SYN_DROPPED: ignore events until SYN_REPORT, then resync
    evpad_t pad;
} evdev_dev_t;

// Devices that aren't pads (or can't be opened), remembered so they're not
// probed every second. Retried when their inode changes (e.g. udev grants access).
typedef struct {
    char path[64];
    dev_t rdev;
    time_t ctime;
} skipped_t;

static struct {
    evdev_dev_t devs[GAMEPAD_MAX];
    skipped_t skipped[MAX_SKIPPED];
    int nskipped;
    int polls_until_scan;
} ev;

static bool bit(const unsigned long* bits, int n) {
    return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

// Read current key/axis state (after opening or after events were dropped).
static void resync(evdev_dev_t* d) {
    unsigned long keys[KEY_CNT / (8 * sizeof(long)) + 1];
    memset(keys, 0, sizeof(keys));
    if (ioctl(d->fd, EVIOCGKEY(sizeof(keys)), keys) >= 0) {
        for (int c = 0; c < KEY_CNT; c++) d->pad.key[c] = bit(keys, c);
    }
    for (int a = 0; a < ABS_CNT; a++) {
        struct input_absinfo ai;
        if (d->pad.has_abs[a] && ioctl(d->fd, EVIOCGABS(a), &ai) >= 0) d->pad.abs[a] = ai.value;
    }
}

static bool is_skipped(const char* path, const struct stat* st) {
    for (int i = 0; i < ev.nskipped; i++) {
        const skipped_t* s = &ev.skipped[i];
        if (!strcmp(s->path, path)) return s->rdev == st->st_rdev && s->ctime == st->st_ctime;
    }
    return false;
}

static void skip(const char* path, const struct stat* st) {
    int i = 0;
    while (i < ev.nskipped && strcmp(ev.skipped[i].path, path) != 0) i++;
    if (i == ev.nskipped) {
        if (ev.nskipped == MAX_SKIPPED) return;
        ev.nskipped++;
    }
    snprintf(ev.skipped[i].path, sizeof(ev.skipped[i].path), "%s", path);
    ev.skipped[i].rdev = st->st_rdev;
    ev.skipped[i].ctime = st->st_ctime;
}

static void try_open(gamepad_t* pads, const char* path, const struct stat* st) {
    int slot = -1;
    for (int i = 0; i < GAMEPAD_MAX; i++) {
        if (ev.devs[i].fd >= 0 && !strcmp(ev.devs[i].path, path)) return;     // already open
        if (slot < 0 && ev.devs[i].fd < 0) slot = i;
    }
    if (slot < 0 || is_skipped(path, st)) return;
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        skip(path, st);
        return;
    }
    static unsigned long keybits[KEY_CNT / (8 * sizeof(long)) + 1], absbits[ABS_CNT / (8 * sizeof(long)) + 1];
    static bool key_caps[KEY_CNT], abs_caps[ABS_CNT];
    static struct input_absinfo absinfo[ABS_CNT];
    memset(keybits, 0, sizeof(keybits));
    memset(absbits, 0, sizeof(absbits));
    memset(absinfo, 0, sizeof(absinfo));
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
    for (int c = 0; c < KEY_CNT; c++) key_caps[c] = bit(keybits, c);
    for (int a = 0; a < ABS_CNT; a++) {
        abs_caps[a] = bit(absbits, a);
        if (abs_caps[a] && ioctl(fd, EVIOCGABS(a), &absinfo[a]) < 0) abs_caps[a] = false;
    }
    evdev_dev_t* d = &ev.devs[slot];
    if (!evpad_setup(&d->pad, key_caps, abs_caps, absinfo)) {
        close(fd);
        skip(path, st);
        return;
    }
    d->fd = fd;
    d->dropped = false;
    snprintf(d->path, sizeof(d->path), "%s", path);
    gamepad_t* g = &pads[slot];
    memset(g, 0, sizeof(*g));
    if (ioctl(fd, EVIOCGNAME(sizeof(g->name) - 1), g->name) < 0) g->name[0] = 0;
    resync(d);
    g->connected = true;
}

static void scan(gamepad_t* pads) {
    DIR* dir = opendir(DEV_DIR);
    if (!dir) return;
    struct dirent* e;
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        if (snprintf(path, sizeof(path), DEV_DIR "/%s", e->d_name) >= (int)sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) try_open(pads, path, &st);
    }
    closedir(dir);
}

static void drop(gamepad_t* pads, int i) {
    close(ev.devs[i].fd);
    ev.devs[i].fd = -1;
    ev.devs[i].path[0] = 0;
    pads[i].connected = false;
    pads[i].buttons = 0;
    pads[i].lx = pads[i].ly = pads[i].lt = pads[i].rt = 0.0f;
}

void gamepad_backend_init(gamepad_t* pads) {
    memset(&ev, 0, sizeof(ev));
    for (int i = 0; i < GAMEPAD_MAX; i++) ev.devs[i].fd = -1;
    scan(pads);
    ev.polls_until_scan = RESCAN_POLLS;
}

void gamepad_backend_shutdown(void) {
    for (int i = 0; i < GAMEPAD_MAX; i++) {
        if (ev.devs[i].fd >= 0) close(ev.devs[i].fd);
        ev.devs[i].fd = -1;
    }
}

void gamepad_backend_poll(gamepad_t* pads) {
    if (--ev.polls_until_scan <= 0) {
        scan(pads);
        ev.polls_until_scan = RESCAN_POLLS;
    }
    for (int i = 0; i < GAMEPAD_MAX; i++) {
        evdev_dev_t* d = &ev.devs[i];
        if (d->fd < 0) continue;
        struct input_event buf[64];
        for (;;) {
            const ssize_t n = read(d->fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) break;
                drop(pads, i);      // unplugged (ENODEV) or broken
                break;
            }
            if (n == 0) break;
            for (size_t k = 0; k < (size_t)n / sizeof(buf[0]); k++) {
                const struct input_event* e = &buf[k];
                if (e->type == EV_SYN && e->code == SYN_DROPPED) {
                    d->dropped = true;
                } else if (e->type == EV_SYN && e->code == SYN_REPORT && d->dropped) {
                    d->dropped = false;
                    resync(d);
                } else if (!d->dropped) {
                    evpad_event(&d->pad, e->type, e->code, e->value);
                }
            }
            if ((size_t)n < sizeof(buf)) break;
        }
        if (d->fd >= 0) evpad_read(&d->pad, &pads[i]);
    }
}

const char* gamepad_backend_name(void) { return "evdev"; }
#endif
