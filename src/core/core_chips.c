// core_chips.c: core.h implemented on top of floooh/chips (c64.h).
// This is the only file in the project that includes chips headers.
#define _USE_MATH_DEFINES   // MSVC: M_PI
#include <math.h>
#ifndef M_PI                // strict ISO C doesn't define it; chips' SID filter needs it
#define M_PI 3.14159265358979323846
#endif

#define CHIPS_IMPL
#include "chips/chips_common.h"
#include "chips/m6502.h"
#include "chips/m6526.h"
#include "chips/m6569.h"
#include "chips/m6581.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "systems/c1530.h"
#include "chips/m6522.h"
#include "systems/c1541.h"
#include "systems/c64.h"

#include "core/core.h"

#include <stdlib.h>

// PAL: 63 cycles per line * 312 lines
#define CORE_PAL_CYCLES_PER_FRAME (M6569_HTOTAL * M6569_VTOTAL)

struct core {
    c64_t sys;
    uint64_t frame_count;
    uint32_t cycles_per_frame;
    uint32_t frame_us;          // fixed per-frame time fed to the keyboard helper
    uint8_t joy[2];
};

core_t* core_create(const core_desc_t* desc) {
    if (!desc || desc->video_std != CORE_VIDEO_PAL) {
        return NULL;    // NTSC needs a 6567 VIC-II model, not in chips yet
    }
    if (!desc->rom_kernal || desc->rom_kernal_size != 0x2000 ||
        !desc->rom_basic || desc->rom_basic_size != 0x2000 ||
        !desc->rom_chargen || desc->rom_chargen_size != 0x1000) {
        return NULL;
    }
    core_t* core = calloc(1, sizeof(core_t));
    if (!core) {
        return NULL;
    }
    core->cycles_per_frame = CORE_PAL_CYCLES_PER_FRAME;
    core->frame_us = (uint32_t)(((uint64_t)core->cycles_per_frame * 1000000u) / C64_FREQUENCY);

    c64_init(&core->sys, &(c64_desc_t){
        // NONE: keys always go to the keyboard matrix. Joysticks are driven only
        // through c64_joystick(), whose masks chips applies regardless of type.
        .joystick_type = C64_JOYSTICKTYPE_NONE,
        .audio = {
            .callback = { .func = desc->audio_fn, .user_data = desc->audio_user_data },
            .sample_rate = desc->audio_sample_rate > 0 ? desc->audio_sample_rate : 44100,
        },
        .roms = {
            .chars  = { .ptr = (void*)desc->rom_chargen, .size = desc->rom_chargen_size },
            .basic  = { .ptr = (void*)desc->rom_basic,   .size = desc->rom_basic_size },
            .kernal = { .ptr = (void*)desc->rom_kernal,  .size = desc->rom_kernal_size },
        },
    });
    // Workaround for a chips keymap bug (@7ef8776): the shifted layer also maps
    // '0' to the 0 key, overwriting the unshifted entry, so typing '0' sends
    // SHIFT+0 (which produces nothing). Re-register it unshifted: column 3, line 4.
    kbd_register_key(&core->sys.kbd, '0', 3, 4, 0);
    return core;
}

void core_destroy(core_t* core) {
    if (core) {
        c64_discard(&core->sys);
        free(core);
    }
}

void core_reset(core_t* core) {
    c64_reset(&core->sys);
    core->joy[0] = core->joy[1] = 0;
    core->frame_count = 0;
}

void core_run_frame(core_t* core) {
    c64_t* sys = &core->sys;
    // Joystick state is latched once per frame for determinism.
    c64_joystick(sys, core->joy[0], core->joy[1]);
    // Exact cycle count per frame (c64_exec() takes microseconds and would round).
    uint64_t pins = sys->pins;
    for (uint32_t i = 0; i < core->cycles_per_frame; i++) {
        pins = _c64_tick(sys, pins);
    }
    sys->pins = pins;
    kbd_update(&sys->kbd, core->frame_us);
    core->frame_count++;
}

uint64_t core_frame_count(const core_t* core) {
    return core->frame_count;
}

double core_frame_rate(const core_t* core) {
    return (double)C64_FREQUENCY / (double)core->cycles_per_frame;
}

uint32_t core_cycles_per_frame(const core_t* core) {
    return core->cycles_per_frame;
}

core_video_t core_video(const core_t* core) {
    chips_display_info_t info = c64_display_info((c64_t*)&core->sys);
    return (core_video_t){
        .pixels = (const uint8_t*)info.frame.buffer.ptr,
        .width = info.frame.dim.width,
        .height = info.frame.dim.height,
        .screen_x = info.screen.x,
        .screen_y = info.screen.y,
        .screen_w = info.screen.width,
        .screen_h = info.screen.height,
    };
}

void core_set_joystick(core_t* core, int port, uint8_t mask) {
    if (port == 1 || port == 2) {
        core->joy[port - 1] = mask & 0x1F;
    }
}

void core_key_down(core_t* core, int key) {
    c64_key_down(&core->sys, key);
}

void core_key_up(core_t* core, int key) {
    c64_key_up(&core->sys, key);
}

uint8_t core_ram_read(const core_t* core, uint16_t addr) {
    return core->sys.ram[addr];
}

void core_ram_write(core_t* core, uint16_t addr, uint8_t value) {
    core->sys.ram[addr] = value;
}
