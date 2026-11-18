#pragma once
/*
    Save states (.bbstate): the machine at one moment (core_save_state), plus a
    header saying what it depends on, so a state is only loaded into a matching
    machine: same game data, system ROMs, patch mods, video standard and build
    layout. ROM contents are never stored (see core.h).

    File (little-endian):
      "BBSTATE\0"  u32 version (1)  u32 header length  header (TOML text)
      u32 state length  state bytes  u32 CRC-32 of everything before it
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char launcher_version[32];
    char layout[17];            // core_state_layout() in hex
    char game_id[64];
    char rom_name[96];
    char rom_sha1[41];          // the media in the machine (cartridge, disk or tape)
    char media[16];
    bool ntsc;
    char sysrom_sha1[3][41];    // kernal, basic, chargen
    char patches[2048];         // modset_patch_signature(): patch mods applied to the game
    char saved_at[32];          // local time, for display only ("2026-11-17 19:40")
    uint64_t frame;             // core_frame_count() when saved
} savestate_header_t;

// Serialize to a malloc'd buffer.
uint8_t* savestate_serialize(const savestate_header_t* h, const uint8_t* state, size_t state_size, size_t* size);
// On success *state is malloc'd (free it). Pass state = NULL to read only the header.
bool savestate_parse(const uint8_t* data, size_t size, savestate_header_t* h, uint8_t** state, size_t* state_size,
                     char* err, size_t err_size);
bool savestate_save(const char* path, const savestate_header_t* h, const uint8_t* state, size_t state_size,
                    char* err, size_t err_size);
bool savestate_load(const char* path, savestate_header_t* h, uint8_t** state, size_t* state_size,
                    char* err, size_t err_size);

// Can a state with header `saved` be loaded into the machine described by `now`?
// If not, why_not says why in words for the player.
bool savestate_compatible(const savestate_header_t* saved, const savestate_header_t* now, char* why_not,
                          size_t why_size);

#ifdef __cplusplus
}
#endif
