#pragma once
/*
    .tap raw tape images ("C64-TAPE-RAW", versions 0 and 1), played through the
    emulated datasette. Version 0 images are converted to version 1 (a zero byte
    becomes an explicit 2048-cycle pulse) because the datasette reads version 1.

    Identity (media SHA-1): the pulse data after the 20-byte header, as stored
    in the file (before any conversion).
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAP_HEADER_SIZE 20
#define TAP_MAX_SIZE (8u * 1024u * 1024u)   // the core's tape buffer

bool tap_is_tap(const uint8_t* data, size_t size);
// Validate and return the pulse data range (for hashing).
bool tap_check(const uint8_t* data, size_t size, const uint8_t** pulses, size_t* pulses_size,
               char* err, size_t err_size);
// Version 1 copy for core_insert_tape(); malloc'd, free() it. NULL on error.
uint8_t* tap_to_v1(const uint8_t* data, size_t size, size_t* out_size, char* err, size_t err_size);

#ifdef __cplusplus
}
#endif
