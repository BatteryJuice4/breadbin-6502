#pragma once
/*
    Save copies of disks (owner decision): when a game writes to its disk in the
    emulated 1541 (high scores, saved games), the written disk is kept as a .g64 in
    the user's config folder, <config>/disks/<game id>/<media sha1>.g64, and used
    instead of the original the next time that disk is inserted. The user's own
    .d64/.g64 file is never changed.

    The name is the original disk's media SHA-1, so a save copy only ever replaces
    the disk it was made from. Deleting the file goes back to the original.
*/
#include <stdbool.h>
#include <stddef.h>
#include "core/core.h"
#include "media/gcr.h"

#ifdef __cplusplus
extern "C" {
#endif

// <dir>/<media_sha1>.g64. False if it doesn't fit.
bool disksave_path(const char* dir, const char* media_sha1, char* out, size_t out_size);

// Write the disk as .g64 (atomically: a temporary file, then a rename).
bool disksave_write(const char* path, const core_disk_t* disk, char* err, size_t err_size);

// Read a save copy. False (err set) if it's missing or unreadable; a missing
// file leaves err empty.
bool disksave_read(const char* path, gcr_image_t* out, char* err, size_t err_size);

#ifdef __cplusplus
}
#endif
