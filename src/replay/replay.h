#pragma once
/*
    Diagnostic replays: what reached the emulator every frame (joystick bits and
    C64 key events), plus a fingerprint of the machine after the frame
    (core_fingerprint). Playing a replay feeds the same input again; the first
    frame whose fingerprint differs shows where behaviour changed.

    A replay starts with the game starting (cartridge inserted / disk or tape
    booted), so it includes disk and tape loading.

    File (.bbreplay, little-endian):
      "BBREPLAY"  u32 version (1)  u32 header length  header (TOML text)
      u32 frame count, then per frame:
        u8 joy port 1, u8 joy port 2, u8 key count, (u8 key, u8 down) x count,
        u64 fingerprint
    The header names the launcher version, game, ROM SHA-1, video standard,
    system ROM SHA-1s and the enabled mods (id@package SHA-1, in load order).
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REPLAY_MAX_KEYS 16
#define REPLAY_MAX_MODS 64
#define REPLAY_MAX_FRAMES (60u * 60u * 60u * 4u)     // 4 hours at 60 fps

typedef struct {
    uint8_t joy[2];             // ports 1 and 2 (CORE_JOY_* bits)
    uint8_t nkeys;
    struct { uint8_t key, down; } keys[REPLAY_MAX_KEYS];
} replay_input_t;

typedef struct {
    char launcher_version[32];
    char game_id[64];
    char rom_name[96];
    char rom_sha1[41];
    char media[16];             // "cartridge", "d64", "prg", "t64", "tap"
    bool ntsc;
    char sysrom_sha1[3][41];    // kernal, basic, chargen
    int mod_count;
    char mods[REPLAY_MAX_MODS][112];    // "id@sha1"
} replay_header_t;

typedef struct {
    replay_header_t header;
    uint32_t count, cap;
    replay_input_t* inputs;
    uint64_t* fingerprints;
} replay_t;

// Feed one frame's input to the core (keys first, then both joysticks).
void replay_input_apply(core_t* core, const replay_input_t* in);

void replay_init(replay_t* r, const replay_header_t* h);
void replay_free(replay_t* r);
bool replay_add(replay_t* r, const replay_input_t* in, uint64_t fingerprint);   // false when full

// Serialize to a malloc'd buffer. parse/load overwrite *out without freeing it.
uint8_t* replay_serialize(const replay_t* r, size_t* size);
bool replay_parse(const uint8_t* data, size_t size, replay_t* out, char* err, size_t err_size);
bool replay_save(const replay_t* r, const char* path, char* err, size_t err_size);
bool replay_load(const char* path, replay_t* out, char* err, size_t err_size);

// Playback: feed inputs in order and check fingerprints.
typedef struct {
    const replay_t* r;
    uint32_t pos;               // next frame
    bool diverged;
    uint32_t diverged_at;       // first frame (0-based) whose fingerprint differed
} replay_player_t;

void replay_player_init(replay_player_t* p, const replay_t* r);
bool replay_player_done(const replay_player_t* p);
// Input for the next frame (false when the replay is over).
bool replay_player_input(const replay_player_t* p, replay_input_t* out);
// After running that frame: compare and advance. Returns false on the first mismatch.
bool replay_player_check(replay_player_t* p, uint64_t fingerprint);

#ifdef __cplusplus
}
#endif
