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

// Raw 64 KB RAM access (not the CPU's banked view). For tests, cheats, fingerprints.
uint8_t core_ram_read(const core_t* core, uint16_t addr);
void core_ram_write(core_t* core, uint16_t addr, uint8_t value);

#ifdef __cplusplus
}
#endif
