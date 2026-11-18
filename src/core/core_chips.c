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

#include <stddef.h>
#include <stdint.h>
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
    // write watches
    uint8_t wwatch[0x10000 / 8];
    int wwatch_count;
    int wevent_count;
    bool wevent_overflow;
    core_write_event_t wevents[CORE_MAX_WRITE_EVENTS];
    uint8_t file_buf[CORE_FILE_MAX];
    // What core_power_on() needs to rebuild the machine
    core_audio_fn audio_fn;
    void* audio_user;
    int audio_sample_rate;
    uint8_t rom_kernal[0x2000], rom_basic[0x2000], rom_chargen[0x1000];
    const uint8_t* tape;        // caller's image (must stay valid while inserted)
    size_t tape_size;
};

#define CORE_KERNAL_LOAD 0xFFD5

// Build the machine from scratch, as after switching it on, keeping the inserted
// cartridge and tape (rewound). chips' c64_reset() only pulls the CPU's reset line
// and resets some chip registers: RAM, colour RAM, the keyboard matrix, the CPU's
// place in its current instruction and the datasette would carry over from the
// previous run, so a restarted game wouldn't start from the same state every time
// (and replays recorded after a restart wouldn't play back).
static void core_power_on(core_t* core) {
    c64_t* sys = &core->sys;
    const bool had_sys = sys->valid;
    const bool exrom = had_sys && sys->exp_exrom, game = had_sys && sys->exp_game;
    uint8_t roml[0x2000], romh[0x2000];
    if (core->has_cart) {
        memcpy(roml, sys->exp_roml, sizeof(roml));
        memcpy(romh, sys->exp_romh, sizeof(romh));
    }
    if (had_sys) c64_discard(sys);
    c64_init(sys, &(c64_desc_t){
        // NONE: keys always go to the keyboard matrix. Joysticks are driven only
        // through c64_joystick(), whose masks chips applies regardless of type.
        .joystick_type = C64_JOYSTICKTYPE_NONE,
        .ntsc = core->ntsc,
        .c1530_enabled = true,
        .audio = {
            .callback = { .func = core->audio_fn, .user_data = core->audio_user },
            .sample_rate = core->audio_sample_rate,
        },
        .roms = {
            .chars  = { .ptr = core->rom_chargen, .size = sizeof(core->rom_chargen) },
            .basic  = { .ptr = core->rom_basic,   .size = sizeof(core->rom_basic) },
            .kernal = { .ptr = core->rom_kernal,  .size = sizeof(core->rom_kernal) },
        },
    });
    // Workaround for a chips keymap bug (@7ef8776): the shifted layer also maps
    // '0' to the 0 key, overwriting the unshifted entry, so typing '0' sends
    // SHIFT+0 (which produces nothing). Re-register it unshifted: column 3, line 4.
    kbd_register_key(&sys->kbd, '0', 3, 4, 0);
    if (core->has_cart) {
        c64_set_expansion(sys, exrom, game, roml, romh);
        c64_reset(sys);
    }
    if (core->tape) {
        c1530_insert_tape(&sys->c1530, (chips_range_t){ .ptr = (void*)core->tape, .size = core->tape_size });
    }
    core->joy[0] = core->joy[1] = 0;
    core->frame_count = 0;
    core->load_rts = false;
    core->prev_sync = -1;
    memset(core->watch_hits, 0, sizeof(core->watch_hits));
    core->wevent_count = 0;
    core->wevent_overflow = false;
}

// core_t holds a c64_t, whose framebuffer is alignas(64), so core_t needs 64-byte
// alignment; malloc/calloc only promise 16 (alignof(max_align_t)). With wider SIMD
// (e.g. AVX2 on an x86-64-v3 build) the compiler uses aligned loads on core_t
// fields, which fault on a 16-byte-aligned block. aligned_alloc isn't in MSVC, so
// over-allocate and keep the original pointer just before the aligned block.
static core_t* core_alloc(void) {
    const size_t align = _Alignof(core_t) > sizeof(void*) ? _Alignof(core_t) : sizeof(void*);
    unsigned char* raw = calloc(1, sizeof(core_t) + align + sizeof(void*));
    if (!raw) return NULL;
    uintptr_t p = (uintptr_t)(raw + sizeof(void*));
    p = (p + align - 1) & ~(uintptr_t)(align - 1);
    ((void**)p)[-1] = raw;
    return (core_t*)p;
}

static void core_free(core_t* core) {
    if (core) free(((void**)core)[-1]);
}

core_t* core_create(const core_desc_t* desc) {
    if (!desc || (desc->video_std != CORE_VIDEO_PAL && desc->video_std != CORE_VIDEO_NTSC)) {
        return NULL;
    }
    if (!desc->rom_kernal || desc->rom_kernal_size != 0x2000 ||
        !desc->rom_basic || desc->rom_basic_size != 0x2000 ||
        !desc->rom_chargen || desc->rom_chargen_size != 0x1000) {
        return NULL;
    }
    core_t* core = core_alloc();
    if (!core) {
        return NULL;
    }
    core->ntsc = desc->video_std == CORE_VIDEO_NTSC;
    core->cycles_per_frame = core->ntsc ? CORE_NTSC_CYCLES_PER_FRAME : CORE_PAL_CYCLES_PER_FRAME;
    const uint32_t freq = core->ntsc ? C64_NTSC_FREQUENCY : C64_FREQUENCY;
    core->frame_us = (uint32_t)(((uint64_t)core->cycles_per_frame * 1000000u) / freq);

    core->audio_fn = desc->audio_fn;
    core->audio_user = desc->audio_user_data;
    core->audio_sample_rate = desc->audio_sample_rate > 0 ? desc->audio_sample_rate : 44100;
    memcpy(core->rom_kernal, desc->rom_kernal, sizeof(core->rom_kernal));
    memcpy(core->rom_basic, desc->rom_basic, sizeof(core->rom_basic));
    memcpy(core->rom_chargen, desc->rom_chargen, sizeof(core->rom_chargen));
    core_power_on(core);
    return core;
}

void core_destroy(core_t* core) {
    if (core) {
        c64_discard(&core->sys);
        core_free(core);
    }
}

void core_reset(core_t* core) {
    core_power_on(core);
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
    core->wevent_count = 0;
    core->wevent_overflow = false;
    const bool watch_writes = core->wwatch_count > 0;
    for (uint32_t i = 0; i < core->cycles_per_frame; i++) {
        pins = _c64_tick(sys, pins);
        if (pins & M6502_SYNC) {
            pins = core_on_fetch(core, pins);
        } else {
            core->prev_sync = -1;
        }
        // CPU write cycles are never stretched by RDY, so each write shows up once.
        if (watch_writes && !(pins & M6502_RW)) {
            const uint16_t addr = M6502_GET_ADDR(pins);
            if (core->wwatch[addr >> 3] & (1u << (addr & 7))) {
                if (core->wevent_count < CORE_MAX_WRITE_EVENTS) {
                    core->wevents[core->wevent_count].addr = addr;
                    core->wevents[core->wevent_count].value = M6502_GET_DATA(pins);
                    core->wevent_count++;
                } else {
                    core->wevent_overflow = true;
                }
            }
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

static uint64_t fnv1a(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

// ---- save states ----
// Layout: header, then c64_t from chips' snapshot with the ROM arrays zeroed and
// the tape buffer (8 MB, at the end of c1530_t) left out.
typedef struct {
    uint64_t layout;
    uint64_t frame_count;
    uint8_t joy[2];
    uint8_t has_cart, load_rts;
    int32_t prev_sync;
    uint32_t tape_size;
    uint32_t watch_hits[CORE_MAX_WATCHES];
} core_state_hdr_t;

#define STATE_TAPE_OFF (offsetof(c64_t, c1530) + offsetof(c1530_t, buf))
#define STATE_TAPE_END (STATE_TAPE_OFF + sizeof(((c1530_t*)0)->buf))
#define STATE_SYS_BYTES (sizeof(c64_t) - (STATE_TAPE_END - STATE_TAPE_OFF))

static c64_t state_tmp;     // snapshots are big (the tape buffer): not on the stack

size_t core_state_size(void) {
    return sizeof(core_state_hdr_t) + STATE_SYS_BYTES;
}

uint64_t core_state_layout(void) {
    // Changes whenever the snapshot format, struct sizes or field positions change.
    const uint64_t parts[] = {
        C64_SNAPSHOT_VERSION, sizeof(c64_t), sizeof(void*), sizeof(core_state_hdr_t),
        offsetof(c64_t, cpu), offsetof(c64_t, cia_1), offsetof(c64_t, cia_2), offsetof(c64_t, vic),
        offsetof(c64_t, sid), offsetof(c64_t, kbd), offsetof(c64_t, mem_cpu), offsetof(c64_t, mem_vic),
        offsetof(c64_t, c1530), offsetof(c64_t, ram), offsetof(c64_t, fb), offsetof(c64_t, exp_roml),
        sizeof(m6502_t), sizeof(m6526_t), sizeof(m6569_t), sizeof(m6581_t), sizeof(kbd_t), sizeof(mem_t),
        2,  // bump when this file's state code changes meaning
    };
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        for (int b = 0; b < 8; b++) h = (h ^ ((parts[i] >> (8 * b)) & 0xFF)) * 0x100000001B3ull;
    }
    return h;
}

bool core_save_state(core_t* core, uint8_t* buf, size_t size) {
    if (!buf || size != core_state_size()) return false;
    core_state_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.layout = core_state_layout();
    h.frame_count = core->frame_count;
    h.joy[0] = core->joy[0];
    h.joy[1] = core->joy[1];
    h.has_cart = core->has_cart;
    h.load_rts = core->load_rts;
    h.prev_sync = core->prev_sync;
    h.tape_size = core->sys.c1530.size;
    memcpy(h.watch_hits, core->watch_hits, sizeof(h.watch_hits));
    c64_save_snapshot(&core->sys, &state_tmp);
    // never store ROM contents
    memset(state_tmp.rom_char, 0, sizeof(state_tmp.rom_char));
    memset(state_tmp.rom_basic, 0, sizeof(state_tmp.rom_basic));
    memset(state_tmp.rom_kernal, 0, sizeof(state_tmp.rom_kernal));
    memset(state_tmp.exp_roml, 0, sizeof(state_tmp.exp_roml));
    memset(state_tmp.exp_romh, 0, sizeof(state_tmp.exp_romh));
    memcpy(buf, &h, sizeof(h));
    const uint8_t* src = (const uint8_t*)&state_tmp;
    memcpy(buf + sizeof(h), src, STATE_TAPE_OFF);
    memcpy(buf + sizeof(h) + STATE_TAPE_OFF, src + STATE_TAPE_END, sizeof(c64_t) - STATE_TAPE_END);
    return true;
}

bool core_load_state(core_t* core, const uint8_t* buf, size_t size) {
    if (!buf || size != core_state_size()) return false;
    core_state_hdr_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.layout != core_state_layout() || (bool)h.has_cart != core->has_cart ||
        h.tape_size != core->sys.c1530.size) {
        return false;
    }
    uint8_t* dst = (uint8_t*)&state_tmp;
    memcpy(dst, buf + sizeof(h), STATE_TAPE_OFF);
    memcpy(dst + STATE_TAPE_END, buf + sizeof(h) + STATE_TAPE_OFF, sizeof(c64_t) - STATE_TAPE_END);
    // ROMs and the tape image come from this machine
    const c64_t* sys = &core->sys;
    memcpy(state_tmp.rom_char, sys->rom_char, sizeof(sys->rom_char));
    memcpy(state_tmp.rom_basic, sys->rom_basic, sizeof(sys->rom_basic));
    memcpy(state_tmp.rom_kernal, sys->rom_kernal, sizeof(sys->rom_kernal));
    memcpy(state_tmp.exp_roml, sys->exp_roml, sizeof(sys->exp_roml));
    memcpy(state_tmp.exp_romh, sys->exp_romh, sizeof(sys->exp_romh));
    memcpy(state_tmp.c1530.buf, sys->c1530.buf, sys->c1530.size);
    if (!c64_load_snapshot(&core->sys, C64_SNAPSHOT_VERSION, &state_tmp)) return false;
    memset(core->sys.kbd.key_buffer, 0, sizeof(core->sys.kbd.key_buffer));
    core->frame_count = h.frame_count;
    core->joy[0] = h.joy[0];
    core->joy[1] = h.joy[1];
    core->load_rts = h.load_rts;
    core->prev_sync = h.prev_sync;
    memcpy(core->watch_hits, h.watch_hits, sizeof(core->watch_hits));
    return true;
}

uint64_t core_fingerprint(core_t* core) {
    m6502_t* cpu = &core->sys.cpu;
    const uint16_t pc = m6502_pc(cpu);
    const uint8_t regs[] = { (uint8_t)pc, (uint8_t)(pc >> 8), m6502_a(cpu), m6502_x(cpu), m6502_y(cpu),
                             m6502_s(cpu), m6502_p(cpu) };
    uint64_t h = 0xCBF29CE484222325ull;
    h = fnv1a(h, core->sys.ram, sizeof(core->sys.ram));
    h = fnv1a(h, core->sys.color_ram, sizeof(core->sys.color_ram));
    return fnv1a(h, regs, sizeof(regs));
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
    if (!c1530_insert_tape(&core->sys.c1530, (chips_range_t){ .ptr = (void*)tap, .size = size })) {
        core_remove_tape(core);
        return false;
    }
    core->tape = tap;
    core->tape_size = size;
    return true;
}

void core_remove_tape(core_t* core) {
    c1530_remove_tape(&core->sys.c1530);
    core->tape = NULL;
    core->tape_size = 0;
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

void core_watch_write(core_t* core, uint16_t addr, bool on) {
    uint8_t* b = &core->wwatch[addr >> 3];
    const uint8_t bit = (uint8_t)(1u << (addr & 7));
    if (on && !(*b & bit)) { *b |= bit; core->wwatch_count++; }
    if (!on && (*b & bit)) { *b &= (uint8_t)~bit; core->wwatch_count--; }
}

void core_clear_write_watches(core_t* core) {
    memset(core->wwatch, 0, sizeof(core->wwatch));
    core->wwatch_count = 0;
    core->wevent_count = 0;
    core->wevent_overflow = false;
}

int core_write_watch_count(const core_t* core) {
    return core->wwatch_count;
}

const core_write_event_t* core_write_events(const core_t* core, int* count, bool* overflow) {
    if (count) *count = core->wevent_count;
    if (overflow) *overflow = core->wevent_overflow;
    return core->wevents;
}

void core_clear_watches(core_t* core) {
    core->num_watches = 0;
}
