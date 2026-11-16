#pragma once
/*
    Cartridge images: .crt parsing and raw .bin wrapping.

    Pure data handling with no emulator dependency, so it's unit-testable and shared
    by any core. Supported so far: hardware type 0 ("normal cartridge") in its three
    configurations, 8K, 16K and Ultimax. Bank-switching types are added only when a
    game pack needs them; until then they're rejected with a clear message.

    Identity: sha1 covers the ROM chip data only (CHIP packet payloads in
    file order for .crt, the whole file for .bin), so the same ROM matches
    whether it's wrapped as .crt or dumped raw.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CART_MAX_FILE_SIZE (4u * 1024u * 1024u)

typedef enum {
    CART_MODE_8K = 1,       // EXROM asserted: ROML at $8000
    CART_MODE_16K,          // EXROM+GAME asserted: ROML at $8000, ROMH at $A000
    CART_MODE_ULTIMAX,      // GAME asserted: ROML at $8000 (optional), ROMH at $E000
} cart_mode_t;

typedef struct {
    char name[33];          // from the .crt header (empty for .bin)
    uint16_t hw_type;       // .crt hardware type (0 = normal cartridge)
    cart_mode_t mode;
    bool exrom, game;       // expansion port lines, true = asserted (low)
    bool has_roml, has_romh;
    uint8_t roml[0x2000];   // $FF where the cartridge has no ROM
    uint8_t romh[0x2000];
    char sha1[41];          // SHA-1 of the ROM chip data, lowercase hex
} cart_t;

// Parse a .crt image. Returns false with a human-readable message in err on failure.
bool cart_parse_crt(const uint8_t* data, size_t size, cart_t* out, char* err, size_t err_size);

// Wrap a raw dump loaded at load_addr: $8000 (4K/8K -> 8K mode, 16K -> 16K mode)
// or $E000/$F000 (Ultimax). A game pack supplies load_addr for .bin files.
bool cart_from_bin(const uint8_t* data, size_t size, uint16_t load_addr, cart_t* out, char* err, size_t err_size);

// True if data starts with the .crt signature.
bool cart_is_crt(const uint8_t* data, size_t size);

// Human-readable name of a .crt hardware type ("Ocean type 1", ...), or NULL if unknown.
const char* cart_hw_type_name(uint16_t hw_type);
const char* cart_mode_name(cart_mode_t mode);

#ifdef __cplusplus
}
#endif
