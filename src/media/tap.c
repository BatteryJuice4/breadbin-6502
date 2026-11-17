// .tap raw tape images (see tap.h).
#include "media/tap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool tap_is_tap(const uint8_t* data, size_t size) {
    return data && size >= TAP_HEADER_SIZE && !memcmp(data, "C64-TAPE-RAW", 12);
}

bool tap_check(const uint8_t* data, size_t size, const uint8_t** pulses, size_t* pulses_size,
               char* err, size_t err_size) {
    if (!tap_is_tap(data, size)) {
        if (err) snprintf(err, err_size, "not a .tap tape image");
        return false;
    }
    if (data[12] > 1) {
        if (err) snprintf(err, err_size, ".tap version %u isn't a C64 tape (only versions 0 and 1 are)", data[12]);
        return false;
    }
    const uint32_t n = (uint32_t)data[16] | ((uint32_t)data[17] << 8) | ((uint32_t)data[18] << 16) | ((uint32_t)data[19] << 24);
    // Some tools write a wrong length; trust the file when the field overstates it.
    size_t len = size - TAP_HEADER_SIZE;
    if (n > 0 && n < len) len = n;
    if (len == 0) {
        if (err) snprintf(err, err_size, "the .tap image is empty");
        return false;
    }
    *pulses = data + TAP_HEADER_SIZE;
    *pulses_size = len;
    return true;
}

uint8_t* tap_to_v1(const uint8_t* data, size_t size, size_t* out_size, char* err, size_t err_size) {
    const uint8_t* p;
    size_t n;
    if (!tap_check(data, size, &p, &n, err, err_size)) return NULL;
    const bool v0 = data[12] == 0;
    size_t need = n;
    if (v0) {
        for (size_t i = 0; i < n; i++) if (p[i] == 0) need += 3;
    }
    if (need + TAP_HEADER_SIZE > TAP_MAX_SIZE) {
        if (err) snprintf(err, err_size, "the tape image is too large (%zu KB, at most %u KB)",
                          (need + TAP_HEADER_SIZE) / 1024, TAP_MAX_SIZE / 1024);
        return NULL;
    }
    uint8_t* out = malloc(need + TAP_HEADER_SIZE);
    if (!out) {
        if (err) snprintf(err, err_size, "out of memory");
        return NULL;
    }
    memcpy(out, data, 12);
    out[12] = 1;
    out[13] = out[14] = out[15] = 0;
    out[16] = (uint8_t)need;
    out[17] = (uint8_t)(need >> 8);
    out[18] = (uint8_t)(need >> 16);
    out[19] = (uint8_t)(need >> 24);
    uint8_t* o = out + TAP_HEADER_SIZE;
    for (size_t i = 0; i < n; i++) {
        if (v0 && p[i] == 0) {
            *o++ = 0;
            *o++ = 0x00;    // 2048 cycles = 0x000800, little endian
            *o++ = 0x08;
            *o++ = 0x00;
        } else {
            *o++ = p[i];
        }
    }
    *out_size = need + TAP_HEADER_SIZE;
    return out;
}
