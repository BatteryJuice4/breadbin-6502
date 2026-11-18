#pragma once
/*
    Coverage maps on disk (see core_coverage() in core/core.h and
    docs/RECOMP_PLAN.md): which addresses ran as code, under which memory
    configuration, for one game (identified by its ROM/media SHA-1). Sessions are
    merged into the same file, so playing (or replaying) more of the game adds to it.

    .bbcov file: "BBCOV1\n", the SHA-1 as 40 hex characters + "\n", then
    CORE_COVERAGE_SIZE masks as 32-bit little-endian values (address order).
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// <dir>/<sha1>.bbcov; false if the SHA-1 isn't 40 hex digits or it doesn't fit.
bool coverage_path(const char* dir, const char* sha1, char* out, size_t out_size);
// Loads a map; `map` holds CORE_COVERAGE_SIZE entries. False with err set on a
// damaged file; false with err "" if there's no file.
bool coverage_load(const char* path, char sha1[41], uint32_t* map, char* err, size_t err_size);
// Merges `map` into the file (created if missing; refused if it belongs to
// another SHA-1) and writes it atomically.
bool coverage_merge_save(const char* path, const char* sha1, const uint32_t* map, char* err, size_t err_size);
// Addresses that ran as code at least once.
uint32_t coverage_count(const uint32_t* map);

#ifdef __cplusplus
}
#endif
