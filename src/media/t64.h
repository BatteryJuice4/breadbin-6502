#pragma once
/*
    .t64 tape archives: a directory of program files (no tape signal). Served
    instantly through the KERNAL LOAD trap for the tape device (1).

    Identity (media SHA-1): every used entry's file as served (2-byte load address
    + data), in directory order.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define T64_MAX_ENTRIES 64

typedef struct {
    uint8_t name[16];       // PETSCII, padded with spaces in the file (trimmed here)
    int name_len;
    uint16_t start;         // load address
    uint32_t offset;        // data offset in the file
    uint32_t size;          // data bytes (corrected: many images have a wrong end address)
} t64_entry_t;

typedef struct {
    const uint8_t* data;    // not owned
    size_t size;
    int count;
    t64_entry_t entries[T64_MAX_ENTRIES];
} t64_t;

bool t64_is_t64(const uint8_t* data, size_t size);
bool t64_open(const uint8_t* data, size_t size, t64_t* out, char* err, size_t err_size);
// Serve a LOAD: an empty name or "*" loads the first file, else CBM pattern match.
bool t64_load(const t64_t* t, const uint8_t* name, size_t name_len, uint8_t* out, size_t cap, size_t* len);

#ifdef __cplusplus
}
#endif
