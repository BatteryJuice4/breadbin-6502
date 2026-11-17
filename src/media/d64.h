#pragma once
/*
    .d64 disk images (1541): 35 or 40 tracks, optionally with the error-byte block.
    Read-only: the image is never modified.

    Identity (media SHA-1): the sector data only (683 or 768 sectors of 256 bytes),
    without the optional error bytes.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define D64_MAX_ENTRIES 144     // 18 directory sectors x 8

typedef struct {
    const uint8_t* data;        // not owned
    size_t size;
    int tracks;                 // 35 or 40
} d64_t;

typedef struct {
    uint8_t name[16];           // PETSCII, padded with $A0
    int name_len;               // without padding
    uint8_t type;               // raw type byte ($82 = closed PRG)
    uint8_t track, sector;      // first data sector
    uint16_t blocks;
} d64_entry_t;

bool d64_open(const uint8_t* data, size_t size, d64_t* out, char* err, size_t err_size);
bool d64_size_ok(size_t size);
size_t d64_data_size(const d64_t* d);   // bytes of sector data (the hashed part)

// Directory entries in order; returns the count (<= max).
int d64_dir(const d64_t* d, d64_entry_t* out, int max);
// Disk name and ID from the BAM (PETSCII, padded with $A0).
void d64_disk_name(const d64_t* d, uint8_t name[16], uint8_t id[5]);

// Find the first PRG/USR file matching a CBM pattern ('*' matches the rest, '?' any
// one character; a "0:" or ":" drive prefix is ignored). Returns false if none.
bool d64_find(const d64_t* d, const uint8_t* pattern, size_t pattern_len, d64_entry_t* out);
// Read a file's bytes (for PRGs: 2-byte load address + data). Returns false on a
// broken sector chain or if the file doesn't fit in cap.
bool d64_read(const d64_t* d, const d64_entry_t* e, uint8_t* out, size_t cap, size_t* len);
// Serve a LOAD: "$" builds the directory listing as a BASIC program, anything
// else is matched with d64_find(). Returns false if not found.
bool d64_load(const d64_t* d, const uint8_t* name, size_t name_len, uint8_t* out, size_t cap, size_t* len);

#ifdef __cplusplus
}
#endif
