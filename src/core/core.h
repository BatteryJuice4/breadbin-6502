#pragma once
/*
    core.h: the ONLY interface the rest of Breadbin-6502 uses to talk to the
    emulator. No other module may include chips headers. That keeps the core
    swappable (a more accurate emulator, or a per-game static-recompiled core).

    Determinism contract: given the same ROMs, settings and per-frame input,
    core_run_frame() produces identical RAM and framebuffer contents. The core
    never reads wall-clock time or unseeded randomness.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/cart.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct core core_t;

typedef enum {
    CORE_VIDEO_PAL = 0,
    CORE_VIDEO_NTSC,        // 6567R8 timing (65 cycles x 263 lines, 1.0227 MHz)
} core_video_std_t;

typedef void (*core_audio_fn)(const float* samples, int num_samples, void* user_data);

typedef struct {
    core_video_std_t video_std;
    // System ROM images (copied by core_create; caller keeps ownership)
    const uint8_t* rom_kernal;  size_t rom_kernal_size;   // 8 KB
    const uint8_t* rom_basic;   size_t rom_basic_size;    // 8 KB
    const uint8_t* rom_chargen; size_t rom_chargen_size;  // 4 KB
    // 1541 drive ROM ($C000-$FFFF, 16 KB; optional). With it, a real 1541 is
    // emulated as device 8 on the serial bus; without it there is no drive and
    // disks are only served through the LOAD trap.
    const uint8_t* rom_1541;    size_t rom_1541_size;
    // Audio output (optional; NULL = no audio, e.g. headless tests)
    int audio_sample_rate;      // Hz, e.g. 44100
    core_audio_fn audio_fn;
    void* audio_user_data;
} core_desc_t;

// Joystick bits for core_set_joystick()
enum {
    CORE_JOY_UP    = 1 << 0,
    CORE_JOY_DOWN  = 1 << 1,
    CORE_JOY_LEFT  = 1 << 2,
    CORE_JOY_RIGHT = 1 << 3,
    CORE_JOY_FIRE  = 1 << 4,
};

// C64 key codes for core_key_down()/core_key_up().
// Printable keys use their ASCII code ('A'..'Z' unshifted letters,
// 'a'..'z' shifted letters, digits and punctuation as typed).
enum {
    CORE_KEY_SPACE    = 0x20,
    CORE_KEY_CSRLEFT  = 0x08,
    CORE_KEY_CSRRIGHT = 0x09,
    CORE_KEY_CSRDOWN  = 0x0A,
    CORE_KEY_CSRUP    = 0x0B,
    CORE_KEY_DEL      = 0x01,
    CORE_KEY_INST     = 0x10,
    CORE_KEY_HOME     = 0x0C,
    CORE_KEY_CLR      = 0x02,
    CORE_KEY_RETURN   = 0x0D,
    CORE_KEY_CTRL     = 0x0E,
    CORE_KEY_CBM      = 0x0F,
    CORE_KEY_RESTORE  = 0xFF,
    CORE_KEY_STOP     = 0x03,
    CORE_KEY_RUN      = 0x07,
    CORE_KEY_LEFTARROW= 0x04,
    CORE_KEY_F1 = 0xF1, CORE_KEY_F2 = 0xF2, CORE_KEY_F3 = 0xF3, CORE_KEY_F4 = 0xF4,
    CORE_KEY_F5 = 0xF5, CORE_KEY_F6 = 0xF6, CORE_KEY_F7 = 0xF7, CORE_KEY_F8 = 0xF8,
};

// Framebuffer: one byte per pixel, value = C64 color index 0..15.
// Palette conversion happens in the video module (display-only).
typedef struct {
    const uint8_t* pixels;
    int width, height;          // full framebuffer size (row stride == width)
    int screen_x, screen_y;     // visible area (border included) inside the framebuffer
    int screen_w, screen_h;
    int display_x, display_y;   // the 320x200 display window (inside the border), framebuffer coords
    int display_w, display_h;
    float pixel_aspect;         // displayed width/height of one pixel (PAL ~0.936)
} core_video_t;

// Returns NULL on invalid arguments (e.g. wrong ROM sizes, unsupported video std).
core_t* core_create(const core_desc_t* desc);
void core_destroy(core_t* core);
// Power cycle: the machine starts exactly as after core_create (RAM, chips,
// keyboard), keeping the inserted cartridge, tape (rewound), file loader and watches.
// So a restarted game behaves the same whatever ran before.
void core_reset(core_t* core);

// Run exactly one video frame worth of CPU cycles.
void core_run_frame(core_t* core);
uint64_t core_frame_count(const core_t* core);
double core_frame_rate(const core_t* core);         // native frames per second
uint32_t core_cycles_per_frame(const core_t* core);

core_video_t core_video(const core_t* core);

// Input. Apply between frames; takes effect on the next core_run_frame().
void core_set_joystick(core_t* core, int port /* 1 or 2 */, uint8_t mask);
void core_key_down(core_t* core, int key);
void core_key_up(core_t* core, int key);

// Cartridges. The image is copied; the caller may free it afterwards.
// Inserting or removing a cartridge resets the machine (as on real hardware,
// where carts are only swapped with the power off).
bool core_insert_cart(core_t* core, const cart_t* cart);
void core_remove_cart(core_t* core);
bool core_has_cart(const core_t* core);

// ---- Files: KERNAL LOAD trap ("virtual drive") ----------------------------
// When the CPU calls the KERNAL LOAD entry ($FFD5, with the KERNAL ROM mapped in),
// the core asks the file callback for the file. If the callback serves it, the
// load completes instantly (as LOAD would: secondary address 0 loads to X/Y, else
// to the file's own address; end address in X/Y and $AE/$AF; carry clear) and the
// real KERNAL code is skipped. Programs that talk to the drive directly
// (fastloaders) bypass LOAD and aren't served. With the 1541 drive attached and a disk in
// it, a LOAD from device 8 whose ILOAD vector ($0330) a program pointed into RAM (its
// own loader hooked LOAD) isn't trapped either: that loader runs and talks to the drive. Deterministic: the
// result depends only on emulated state and the callback's data.
typedef enum {
    CORE_FILE_NOT_HANDLED = 0,  // not our device: let the real KERNAL LOAD run
    CORE_FILE_OK,               // *len bytes in buf: 2-byte load address + data
    CORE_FILE_NOT_FOUND,        // LOAD fails with "FILE NOT FOUND"
} core_file_result_t;

#define CORE_FILE_MAX (2u + 0x10000u)

// name: the PETSCII file name as passed to LOAD (not terminated).
typedef core_file_result_t (*core_file_fn)(void* user_data, uint8_t device, const uint8_t* name,
                                           size_t name_len, uint8_t* buf, size_t buf_size, size_t* len);
void core_set_file_loader(core_t* core, core_file_fn fn, void* user_data);
uint32_t core_files_loaded(const core_t* core);     // loads served since core_create

// ---- Tape (C1530 datasette, .tap) ---------------------------------------------
// The image is copied, and must also stay valid until it is removed or replaced:
// a reset (power cycle) inserts it again, rewound. Accepts TAP version 1 (version 0
// must be converted first, see media/tap.h). Returns false for malformed or too
// large images.
bool core_insert_tape(core_t* core, const uint8_t* tap, size_t size);
void core_remove_tape(core_t* core);
void core_tape_play(core_t* core);          // press PLAY (the motor follows the C64's control line)
void core_tape_stop(core_t* core);          // release the buttons
bool core_tape_motor_on(const core_t* core);
bool core_tape_at_end(const core_t* core);  // inserted and all pulses played

// ---- 1541 disk drive (needs rom_1541) ---------------------------------------------
// The drive runs its own 6502, RAM, two VIAs and the DOS ROM on its 1 MHz clock,
// talks to the C64 over the emulated serial bus (CIA-2), and reads/writes the disk
// as GCR bits under a rotating head, so fastloaders and copy protections work.
// A disk is a set of GCR tracks (see media/gcr.h), indexed by half-track
// (0 = track 1, 2 = track 2, odd = between tracks). Track sizes are their recorded
// length in bytes: every track passes under the head once per revolution (300 rpm).
#define CORE_DISK_HALFTRACKS 84
#define CORE_DISK_TRACK_MAX  8192
typedef struct {
    uint8_t* track[CORE_DISK_HALFTRACKS];   // GCR bytes; NULL = nothing recorded
    uint16_t size[CORE_DISK_HALFTRACKS];    // bytes (1..CORE_DISK_TRACK_MAX)
    bool write_protected;
    bool dirty;                 // set by the core when the drive writes a track
} core_disk_t;

bool core_has_drive(const core_t* core);
// The disk is used in place (the drive writes into its tracks) and must stay valid
// until it is ejected or replaced; NULL ejects. A reset keeps it inserted.
void core_insert_disk(core_t* core, core_disk_t* disk);
// Drive status for the UI (LED on = the DOS is busy or reports an error).
bool core_drive_led(const core_t* core);
bool core_drive_motor(const core_t* core);
int core_drive_halftrack(const core_t* core);

// ---- PC watches ----------------------------------------------------------------
// Count instruction fetches at an address (e.g. a KERNAL entry or a game's main
// loop). Returns a watch id (0..CORE_MAX_WATCHES-1), or -1 if all are in use.
// Hits are counted since the last reset (core_reset clears them, keeps the watches).
#define CORE_MAX_WATCHES 4
int core_add_watch(core_t* core, uint16_t pc);
uint32_t core_watch_hits(const core_t* core, int id);
void core_clear_watches(core_t* core);

// ---- Code coverage (analysis for the recompiler, docs/RECOMP_PLAN.md) ----------
// Records every address the C64's CPU fetched an opcode from, and under which memory
// configuration: one 32-bit mask per address, bit n set = fetched while configuration
// n was mapped, n = CPU port bits 0-2 (LORAM, HIRAM, CHAREN) | EXROM asserted << 3 |
// GAME asserted << 4. (Bank-switching cartridges will add their bank once supported.)
// Only observes: never part of save states, replays or fingerprints. The map
// survives core_reset (it accumulates until cleared). The 1541's CPU isn't traced.
#define CORE_COVERAGE_SIZE 0x10000
bool core_coverage_enable(core_t* core, bool on);   // false: out of memory
const uint32_t* core_coverage(const core_t* core);  // CORE_COVERAGE_SIZE masks, NULL when off
void core_coverage_clear(core_t* core);
// The memory configuration number (as above) mapped right now.
int core_memory_config(const core_t* core);
// CPU cycles run since the last reset (or since the state loaded was reset).
uint64_t core_cycle_count(const core_t* core);

// ---- write watches (script mods) ---------------------------------------------
// Record CPU writes to chosen addresses. Events are collected during a frame and
// read after it (in order), so nothing runs in the middle of emulation. A frame
// with more than CORE_MAX_WRITE_EVENTS watched writes keeps the first ones and
// sets the overflow flag. Watches survive core_reset, like PC watches.
#define CORE_MAX_WRITE_EVENTS 256
typedef struct {
    uint16_t addr;
    uint8_t value;
} core_write_event_t;
void core_watch_write(core_t* core, uint16_t addr, bool on);
void core_clear_write_watches(core_t* core);
int core_write_watch_count(const core_t* core);
// The last core_run_frame's events; count and overflow may be NULL.
const core_write_event_t* core_write_events(const core_t* core, int* count, bool* overflow);

// Fast 64-bit hash of the machine state that matters for replays: 64 KB RAM,
// colour RAM and CPU registers. Identical runs give identical values.
uint64_t core_fingerprint(core_t* core);

// ---- Save states -------------------------------------------------------------
// The whole machine (CPU, RAM, chips, keyboard, datasette position, the frame
// shown) except ROM contents (system ROMs, cartridge) and the tape image: those
// are never copied into a state and come from the machine it is loaded into, so
// that machine must have the same ROMs, cartridge (incl. patches) and tape.
// The data is this build's memory layout: core_state_layout() identifies it, and
// states only load into a build with the same layout (checked by core_load_state).
size_t core_state_size(void);
uint64_t core_state_layout(void);
// buf must hold core_state_size() bytes.
bool core_save_state(core_t* core, uint8_t* buf, size_t size);
// Returns false (machine unchanged) for a different layout, size, cartridge
// presence or tape size. Keys held in the state are released (none are held
// on the host at that moment).
bool core_load_state(core_t* core, const uint8_t* buf, size_t size);

// Raw 64 KB RAM access (not the CPU's banked view). For tests, cheats, fingerprints.
uint8_t core_ram_read(const core_t* core, uint16_t addr);
void core_ram_write(core_t* core, uint16_t addr, uint8_t value);

#ifdef __cplusplus
}
#endif
