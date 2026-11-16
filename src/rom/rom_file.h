#pragma once
// Loading user-supplied game media into memory. Files are only read, never
// copied, modified, cached or uploaded.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/cart.h"

// Read a whole file (up to max_size bytes). On success *out_data must be freed with free().
bool rom_read_file(const char* path, size_t max_size, uint8_t** out_data, size_t* out_size,
                   char* err, size_t err_size);

// Load a cartridge from a .crt, or from a raw .bin at bin_load_addr
// (0 = guess: $8000, 8K or 16K by size).
bool rom_load_cart(const char* path, uint16_t bin_load_addr, cart_t* out, char* err, size_t err_size);
