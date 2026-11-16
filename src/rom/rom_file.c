#include "rom/rom_file.h"

#include <stdio.h>
#include <stdlib.h>

bool rom_read_file(const char* path, size_t max_size, uint8_t** out_data, size_t* out_size,
                   char* err, size_t err_size) {
    *out_data = NULL;
    *out_size = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_size, "can't open %s", path);
        return false;
    }
    // Read in chunks rather than trusting fseek/ftell (works for pipes and odd filesystems).
    size_t cap = 64 * 1024, len = 0;
    uint8_t* buf = malloc(cap);
    while (buf) {
        if (len == cap) {
            if (cap >= max_size + 1) break;
            cap *= 2;
            uint8_t* nb = realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0 || len > max_size) break;
    }
    const bool read_error = ferror(f) != 0;
    fclose(f);
    if (!buf) {
        snprintf(err, err_size, "out of memory reading %s", path);
        return false;
    }
    if (read_error) {
        free(buf);
        snprintf(err, err_size, "read error on %s", path);
        return false;
    }
    if (len > max_size) {
        free(buf);
        snprintf(err, err_size, "%s is too large (limit %zu bytes)", path, max_size);
        return false;
    }
    *out_data = buf;
    *out_size = len;
    return true;
}

bool rom_load_cart(const char* path, uint16_t bin_load_addr, cart_t* out, char* err, size_t err_size) {
    uint8_t* data;
    size_t size;
    if (!rom_read_file(path, CART_MAX_FILE_SIZE, &data, &size, err, err_size)) {
        return false;
    }
    bool ok;
    if (cart_is_crt(data, size)) {
        ok = cart_parse_crt(data, size, out, err, err_size);
    } else {
        ok = cart_from_bin(data, size, bin_load_addr ? bin_load_addr : 0x8000, out, err, err_size);
    }
    free(data);
    return ok;
}
