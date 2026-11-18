// Example trusted plugin (docs/plugins.md): auto-fire. While the player holds
// fire, the button is pressed and released every `rate` frames.
//
// A game repo builds it in with  -DBB_PLUGIN_SOURCES=path/to/autofire.c
// and a mod switches it on:
//
//     type    = "plugin"
//     plugins = ["autofire"]
//     [plugin.autofire]
//     port = 2        # joystick port (1 or 2)
//     rate = 4        # frames pressed, then frames released
#include "mods/plugin.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int port;           // 0 or 1
    int rate;
    uint32_t held;      // frames fire has been held
} autofire_t;

static void* autofire_start(core_t* core, const bb_plugin_config_t* config, char* err, size_t err_size) {
    (void)core;
    const int64_t port = bb_plugin_config_int(config, "port", 2);
    const int64_t rate = bb_plugin_config_int(config, "rate", 4);
    if (port != 1 && port != 2) {
        snprintf(err, err_size, "port must be 1 or 2");
        return NULL;
    }
    if (rate < 1 || rate > 60) {
        snprintf(err, err_size, "rate must be 1..60 frames");
        return NULL;
    }
    autofire_t* a = calloc(1, sizeof(*a));
    if (!a) {
        snprintf(err, err_size, "out of memory");
        return NULL;
    }
    a->port = (int)port - 1;
    a->rate = (int)rate;
    return a;
}

static void autofire_stop(void* self, core_t* core) {
    (void)core;
    free(self);
}

static void autofire_before_frame(void* self, const bb_plugin_frame_t* f) {
    autofire_t* a = self;
    if (!(f->joy[a->port] & CORE_JOY_FIRE)) {
        a->held = 0;
        return;
    }
    // pressed for `rate` frames, released for `rate` frames, ...
    if ((a->held / (uint32_t)a->rate) % 2 == 1) f->joy[a->port] &= (uint8_t)~CORE_JOY_FIRE;
    a->held++;
}

const bb_plugin_t bb_plugin_autofire = {
    .api = BB_PLUGIN_API,
    .name = "autofire",
    .description = "Auto-fire while the fire button is held",
    .start = autofire_start,
    .stop = autofire_stop,
    .before_frame = autofire_before_frame,
};
