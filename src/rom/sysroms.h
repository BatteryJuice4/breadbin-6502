#pragma once
/*
    System ROMs (KERNAL / BASIC / CHARGEN).

    Default: the MEGA65 Open ROMs embedded in the executable.
    Override: real Commodore ROMs supplied by the user, looked up in a
    directory as kernal.rom / basic.rom / chargen.rom. KERNAL and BASIC are
    replaced together or not at all (Open ROMs must not be mixed with other
    ROMs); CHARGEN may be replaced on its own.
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
} sysroms_t;

// Fill with the embedded Open ROMs.
void sysroms_load_default(sysroms_t* roms);

// Start from the defaults, then apply overrides found in `dir` (may be NULL).
// Returns false and writes a message to err (if non-NULL) when the directory
// holds an invalid or incomplete override; the defaults remain in place then.
bool sysroms_load(sysroms_t* roms, const char* dir, char* err, size_t err_size);

const char* sysroms_source_name(sysroms_source_t src);
