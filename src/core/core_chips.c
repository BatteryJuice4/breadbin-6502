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
#include "core/patched/m6569.h" // chips m6569.h + NTSC (6567R8) timing (see header)
#include "chips/m6581.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "core/patched/c1530.h"  // chips c1530.h, larger tape buffer (see header)
#include "chips/m6522.h"
#include "systems/c1541.h"
#include "core/patched/c64.h"   // chips c64.h + expansion port (see header)

#include "core/core.h"

#include <stdlib.h>
#include <string.h>

// PAL: 63 cycles per line * 312 lines
#define CORE_PAL_CYCLES_PER_FRAME (M6569_HTOTAL * M6569_VTOTAL)
// 320x200 display window relative to the visible screen rect (measured; see test_core)
#define CORE_PAL_DISPLAY_X 32
#define CORE_PAL_DISPLAY_Y 36
// PAL C64 pixel aspect ratio (7.882 MHz dot clock vs. PAL square-pixel 7.375 MHz)
#define CORE_PAL_PIXEL_ASPECT 0.9365f
// NTSC: 65 cycles per line * 263 lines (6567R8)
#define CORE_NTSC_CYCLES_PER_FRAME (M6569_NTSC_HTOTAL * M6569_NTSC_VTOTAL)
// display window top is raster line 51, the NTSC beam starts at line 13 and the
// visible area 10 lines below that (see the patched c64.h)
#define CORE_NTSC_DISPLAY_X 32
#define CORE_NTSC_DISPLAY_Y 28
// NTSC C64 pixel aspect ratio (8.18 MHz dot clock vs. NTSC square-pixel 6.136 MHz)
#define CORE_NTSC_PIXEL_ASPECT 0.75f

struct core {
    c64_t sys;
    uint64_t frame_count;
    uint32_t cycles_per_frame;
    uint32_t frame_us;          // fixed per-frame time fed to the keyboard helper
    bool ntsc;
    uint8_t joy[2];
    bool has_cart;
    // KERNAL LOAD trap
    core_file_fn file_fn;
    void* file_user;
    uint32_t files_loaded;
    bool load_rts;              // the fetch at $FFD5 is being answered with RTS
    int32_t prev_sync;          // address of the previous tick's opcode fetch, -1 if none
    // PC watches
    int num_watches;
    uint16_t watch_pc[CORE_MAX_WATCHES];
    uint32_t watch_hits[CORE_MAX_WATCHES];
    uint8_t file_buf[CORE_FILE_MAX];
};

#define CORE_KERNAL_LOAD 0xFFD5

core_t* core_create(const core_desc_t* desc) {
    if (!desc || (desc->video_std != CORE_VIDEO_PAL && desc->video_std != CORE_VIDEO_NTSC)) {
        return NULL;
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
    core->ntsc = desc->video_std == CORE_VIDEO_NTSC;
    core->cycles_per_frame = core->ntsc ? CORE_NTSC_CYCLES_PER_FRAME : CORE_PAL_CYCLES_PER_FRAME;
    const uint32_t freq = core->ntsc ? C64_NTSC_FREQUENCY : C64_FREQUENCY;
    core->frame_us = (uint32_t)(((uint64_t)core->cycles_per_frame * 1000000u) / freq);

    c64_init(&core->sys, &(c64_desc_t){
        // NONE: keys always go to the keyboard matrix. Joysticks are driven only
        // through c64_joystick(), whose masks chips applies regardless of type.
        .joystick_type = C64_JOYSTICKTYPE_NONE,
        .ntsc = core->ntsc,
        .c1530_enabled = true,
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
    core->prev_sync = -1;
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
    core->load_rts = false;
    core->prev_sync = -1;
    memset(core->watch_hits, 0, sizeof(core->watch_hits));
}

// KERNAL LOAD at $FFD5: serve the file from the callback. Returns true when the
// load was handled (registers and RAM updated; the caller answers the fetch with RTS).
static bool core_trap_load(core_t* core) {
    c64_t* sys = &core->sys;
    // Only when the KERNAL ROM itself is mapped in (not a game's code in RAM).
    if (mem_readptr(&sys->mem_cpu, CORE_KERNAL_LOAD) != &sys->rom_kernal[CORE_KERNAL_LOAD - 0xE000]) {
        return false;
    }
    const uint8_t device = sys->ram[0xBA];
    const uint8_t sa = sys->ram[0xB9];
    const uint8_t name_len = sys->ram[0xB7];
    const uint16_t name_ptr = (uint16_t)(sys->ram[0xBB] | (sys->ram[0xBC] << 8));
    uint8_t name[256];
    for (unsigned i = 0; i < name_len; i++) {
        name[i] = mem_rd(&sys->mem_cpu, (uint16_t)(name_ptr + i));
    }
    size_t len = 0;
    const core_file_result_t r = core->file_fn(core->file_user, device, name, name_len,
                                               core->file_buf, sizeof(core->file_buf), &len);
    if (r == CORE_FILE_NOT_HANDLED) {
        return false;
    }
    m6502_t* cpu = &sys->cpu;
    if (r != CORE_FILE_OK || len < 2 || len > sizeof(core->file_buf)) {
        m6502_set_a(cpu, 4);                            // FILE NOT FOUND
        m6502_set_p(cpu, m6502_p(cpu) | M6502_CF);
        return true;
    }
    const bool verify = m6502_a(cpu) != 0;
    uint32_t addr = (sa == 0) ? (uint32_t)(m6502_x(cpu) | (m6502_y(cpu) << 8))
                              : (uint32_t)(core->file_buf[0] | (core->file_buf[1] << 8));
    uint8_t status = 0x40;                              // EOI, as after a serial load
    for (size_t i = 2; i < len && addr < 0x10000; i++, addr++) {
        if (verify) {
            if (sys->ram[addr] != core->file_buf[i]) status |= 0x10;
        } else {
            sys->ram[addr] = core->file_buf[i];         // LOAD always writes RAM (under ROM too)
        }
    }
    const uint16_t end = (uint16_t)(addr > 0xFFFF ? 0xFFFF : addr);
    sys->ram[0x90] |= status;
    sys->ram[0xAE] = (uint8_t)end;
    sys->ram[0xAF] = (uint8_t)(end >> 8);
    m6502_set_x(cpu, (uint8_t)end);
    m6502_set_y(cpu, (uint8_t)(end >> 8));
    m6502_set_p(cpu, (uint8_t)(m6502_p(cpu) & ~M6502_CF));
    core->files_loaded++;
    return true;
}

// Called after every tick in which the CPU fetches an opcode.
static uint64_t core_on_fetch(core_t* core, uint64_t pins) {
    const uint16_t addr = M6502_GET_ADDR(pins);
    // A fetch held by RDY (VIC DMA) shows up on several ticks; act once.
    if ((int32_t)addr != core->prev_sync) {
        core->prev_sync = addr;
        for (int i = 0; i < core->num_watches; i++) {
            if (core->watch_pc[i] == addr) core->watch_hits[i]++;
        }
        core->load_rts = (addr == CORE_KERNAL_LOAD) && core->file_fn && core_trap_load(core);
    }
    if (core->load_rts && addr == CORE_KERNAL_LOAD) {
        // The memory read for this fetch already happened; replace the opcode
        // with RTS (again on every RDY-held repeat, which re-reads memory).
        M6502_SET_DATA(pins, 0x60);
    }
    return pins;
}

void core_run_frame(core_t* core) {
    c64_t* sys = &core->sys;
    // Joystick state is latched once per frame for determinism.
    c64_joystick(sys, core->joy[0], core->joy[1]);
    // Exact cycle count per frame (c64_exec() takes microseconds and would round).
    uint64_t pins = sys->pins;
    for (uint32_t i = 0; i < core->cycles_per_frame; i++) {
        pins = _c64_tick(sys, pins);
        if (pins & M6502_SYNC) {
            pins = core_on_fetch(core, pins);
        } else {
            core->prev_sync = -1;
        }
    }
    sys->pins = pins;
    kbd_update(&sys->kbd, core->frame_us);
    core->frame_count++;
}

uint64_t core_frame_count(const core_t* core) {
    return core->frame_count;
}

double core_frame_rate(const core_t* core) {
    return (double)core->sys.freq_hz / (double)core->cycles_per_frame;
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
        .display_x = info.screen.x + (core->ntsc ? CORE_NTSC_DISPLAY_X : CORE_PAL_DISPLAY_X),
        .display_y = info.screen.y + (core->ntsc ? CORE_NTSC_DISPLAY_Y : CORE_PAL_DISPLAY_Y),
        .display_w = 320,
        .display_h = 200,
        .pixel_aspect = core->ntsc ? CORE_NTSC_PIXEL_ASPECT : CORE_PAL_PIXEL_ASPECT,
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

bool core_insert_cart(core_t* core, const cart_t* cart) {
    if (!cart) {
        return false;
    }
    bool exrom, game;
    switch (cart->mode) {
        case CART_MODE_8K:      exrom = true;  game = false; break;
        case CART_MODE_16K:     exrom = true;  game = true;  break;
        case CART_MODE_ULTIMAX: exrom = false; game = true;  break;
        default: return false;
    }
    c64_set_expansion(&core->sys, exrom, game, cart->roml, cart->romh);
    core->has_cart = true;
    core_reset(core);
    return true;
}

void core_remove_cart(core_t* core) {
    c64_set_expansion(&core->sys, false, false, NULL, NULL);
    core->has_cart = false;
    core_reset(core);
}

bool core_has_cart(const core_t* core) {
    return core->has_cart;
}

uint8_t core_ram_read(const core_t* core, uint16_t addr) {
    return core->sys.ram[addr];
}

void core_ram_write(core_t* core, uint16_t addr, uint8_t value) {
    core->sys.ram[addr] = value;
}

void core_set_file_loader(core_t* core, core_file_fn fn, void* user_data) {
    core->file_fn = fn;
    core->file_user = user_data;
}

uint32_t core_files_loaded(const core_t* core) {
    return core->files_loaded;
}

bool core_insert_tape(core_t* core, const uint8_t* tap, size_t size) {
    if (!tap || size == 0) return false;
    return c1530_insert_tape(&core->sys.c1530, (chips_range_t){ .ptr = (void*)tap, .size = size });
}

void core_remove_tape(core_t* core) {
    c1530_remove_tape(&core->sys.c1530);
}

// Workaround: c1530_play() also forces the motor on, but on a real C64 the motor
// is switched by the CPU port (bit 5), so pressing PLAY only closes the sense switch.
void core_tape_play(core_t* core) {
    core->sys.cas_port &= (uint8_t)~C64_CASPORT_SENSE;
}

void core_tape_stop(core_t* core) {
    core->sys.cas_port |= C64_CASPORT_SENSE;
}

bool core_tape_motor_on(const core_t* core) {
    return 0 == (core->sys.cas_port & C64_CASPORT_MOTOR);
}

bool core_tape_at_end(const core_t* core) {
    const c1530_t* t = &core->sys.c1530;
    return t->size > 0 && t->pos >= t->size && t->pulse_count == 0;
}

int core_add_watch(core_t* core, uint16_t pc) {
    if (core->num_watches >= CORE_MAX_WATCHES) return -1;
    core->watch_pc[core->num_watches] = pc;
    core->watch_hits[core->num_watches] = 0;
    return core->num_watches++;
}

uint32_t core_watch_hits(const core_t* core, int id) {
    return (id >= 0 && id < core->num_watches) ? core->watch_hits[id] : 0;
}

void core_clear_watches(core_t* core) {
    core->num_watches = 0;
}
