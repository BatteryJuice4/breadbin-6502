#pragma once
/*
    GCR disk surfaces for the emulated 1541 (core_disk_t, see core/core.h).

    A real 1541 records each track as a ring of GCR-encoded bits: SYNC marks (runs
    of 1 bits), sector headers and data blocks, with every 4 bytes stored as 5
    bytes so that no more than two 0 bits ever follow each other. The drive's own
    code (the DOS, or a fastloader uploaded into it) reads and writes these bits,
    so the core needs the disk in this form, not as a list of sectors.

      gcr_from_d64   builds the surface a 1541 would have written when formatting
                     the disk and saving its sectors (35 or 40 tracks), including
                     the read errors listed in the optional error-byte block
                     (copy protections check for them).
      gcr_from_g64   loads a raw GCR image (.g64, "GCR-1541" version 0), which keeps
                     whatever was on the original disk, copy protection included.
      gcr_to_g64     writes a surface back as .g64 (used for save copies, so a disk
                     the game has written to never changes the user's file).
      gcr_read_sector  decodes one sector the way the DOS would (tests, export).

    Media identity for .g64: the SHA-1 over every present half-track in order, each
    as its 16-bit little-endian length followed by its bytes (header, offsets and
    padding left out).
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/core.h"
#include "media/d64.h"

#ifdef __cplusplus
extern "C" {
#endif

// A disk surface plus the memory it lives in (one allocation for all tracks).
typedef struct {
    core_disk_t disk;
    uint8_t* buf;
} gcr_image_t;

bool gcr_from_d64(const d64_t* d, gcr_image_t* out, char* err, size_t err_size);
bool gcr_from_g64(const uint8_t* data, size_t size, gcr_image_t* out, char* err, size_t err_size);
void gcr_free(gcr_image_t* img);

// Serialize as .g64. Returns the size written, or 0 if `cap` is too small.
// gcr_g64_size() gives the size needed.
size_t gcr_g64_size(const core_disk_t* disk);
size_t gcr_to_g64(const core_disk_t* disk, uint8_t* out, size_t cap);

// .g64 identity hash (see above), 40 hex chars + NUL.
void gcr_g64_sha1(const uint8_t* data, size_t size, char out[41]);
bool gcr_g64_signature(const uint8_t* data, size_t size);

// Decode sector `sector` of `track` (1-based, full tracks) from the surface:
// finds its header, checks the header and data checksums. False if it can't be
// read (as the DOS would report a read error).
bool gcr_read_sector(const core_disk_t* disk, int track, int sector, uint8_t out[256]);

// Standard 1541 layout.
int gcr_sectors_per_track(int track);       // 21/19/18/17 by speed zone
int gcr_speed_zone(int track);              // 3 (tracks 1-17) .. 0 (31+)
int gcr_track_bytes(int track);             // 7692/7142/6666/6250 at 300 rpm

// 4 bytes <-> 5 GCR bytes. gcr_decode4 returns false on an invalid code.
void gcr_encode4(const uint8_t in[4], uint8_t out[5]);
bool gcr_decode4(const uint8_t in[5], uint8_t out[4]);

#ifdef __cplusplus
}
#endif
