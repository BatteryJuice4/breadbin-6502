#pragma once
/*
    System ROMs (KERNAL / BASIC / CHARGEN).

    Default: the MEGA65 Open ROMs embedded in the executable.
    Override: real Commodore ROMs supplied by the user, looked up in a
    directory as kernal.rom / basic.rom / chargen.rom. KERNAL and BASIC are
    replaced together or not at all (Open ROMs must not be mixed with other
    ROMs); CHARGEN may be replaced on its own.

    1541 drive ROM (optional, never bundled): 1541.rom (16 KB, $C000-$FFFF) or the
    two chips 1541-c000.rom + 1541-e000.rom (8 KB each), in the same directory. With
    it, the launcher emulates a real 1541 drive. Identified by the SHA-1 of all
    16 KB; unknown ROMs are still used (has_1541) but reported (drive1541_name NULL).
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SYSROMS_SOURCE_OPENROMS = 0,
    SYSROMS_SOURCE_USER,
} sysroms_source_t;

typedef struct {
    uint8_t kernal[0x2000];
    uint8_t basic[0x2000];
    uint8_t chargen[0x1000];
    sysroms_source_t kernal_basic_source;
    sysroms_source_t chargen_source;
    char kernal_sha1[41], basic_sha1[41], chargen_sha1[41];
    uint8_t drive1541[0x4000];
    bool has_1541;
    char drive1541_sha1[41];
    const char* drive1541_name;     // known ROM's name, or NULL
} sysroms_t;

// Fill with the embedded Open ROMs.
void sysroms_load_default(sysroms_t* roms);

// Start from the defaults, then apply overrides found in `dir` (may be NULL).
// Returns false and writes a message to err (if non-NULL) when the directory
// holds an invalid or incomplete override; the defaults remain in place then.
bool sysroms_load(sysroms_t* roms, const char* dir, char* err, size_t err_size);

const char* sysroms_source_name(sysroms_source_t src);

// Name of a known 1541 ROM by its 16 KB SHA-1, or NULL.
const char* sysroms_1541_name(const char* sha1);
