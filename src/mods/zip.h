#pragma once
/*
    Minimal read-only zip reader for .c64mod packages (stored and deflate entries).
    Packages are untrusted input, so it is strict:
      - at most ZIP_MAX_ENTRIES entries, ZIP_MAX_ENTRY_SIZE per file and
        ZIP_MAX_TOTAL_SIZE in all (uncompressed); inflating writes into a buffer of
        the declared size, so a "zip bomb" can't grow memory
      - no encrypted entries, no zip64, no multi-disk archives
      - names must be relative, use '/', and contain no ".." part
      - every extracted entry is CRC-32 checked
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIP_MAX_ENTRIES 256
#define ZIP_MAX_NAME 200
#define ZIP_MAX_ENTRY_SIZE (16u * 1024u * 1024u)
#define ZIP_MAX_TOTAL_SIZE (64u * 1024u * 1024u)

typedef struct {
    char name[ZIP_MAX_NAME + 1];
    uint16_t method;            // 0 stored, 8 deflate
    uint32_t crc;
    uint32_t comp_size;
    uint32_t size;              // uncompressed
    uint32_t local_offset;
    bool is_dir;
} zip_entry_t;

typedef struct {
    const uint8_t* data;        // not owned
    size_t size;
    int count;
    zip_entry_t entries[ZIP_MAX_ENTRIES];
} zip_t;

bool zip_open(zip_t* z, const uint8_t* data, size_t size, char* err, size_t err_size);
int zip_find(const zip_t* z, const char* name);     // -1 if absent
// Extract to a malloc'd buffer (NUL-terminated for convenience; *size excludes it).
uint8_t* zip_extract(const zip_t* z, int index, size_t* size, char* err, size_t err_size);

#ifdef __cplusplus
}
#endif
