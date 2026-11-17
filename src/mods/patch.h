#pragma once
/*
    Applying patch mods to game data in memory. The data is the same byte view that
    the identity SHA-1 covers (see docs/mods.md): cartridge ROM (ROML then ROMH),
    .d64 sector data, or a whole .prg. Patches never change the size.

    Each function either applies completely or leaves buf untouched and explains why.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mods/mod.h"

#ifdef __cplusplus
extern "C" {
#endif

bool patch_apply_inline(uint8_t* buf, size_t size, const mod_patch_t* p, int count, char* err, size_t err_size);
bool patch_apply_ips(uint8_t* buf, size_t size, const uint8_t* ips, size_t ips_size, char* err, size_t err_size);
bool patch_apply_bps(uint8_t* buf, size_t size, const uint8_t* bps, size_t bps_size, char* err, size_t err_size);
// All of a patch mod: inline patches first, then its files in order.
bool patch_apply_mod(uint8_t* buf, size_t size, const mod_t* m, char* err, size_t err_size);

#ifdef __cplusplus
}
#endif
