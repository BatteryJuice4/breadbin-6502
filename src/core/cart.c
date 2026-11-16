#include "core/cart.h"
#include "rom/sha1.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CRT_SIGNATURE "C64 CARTRIDGE   "
#define CRT_HEADER_MIN 0x40
#define CHIP_HEADER_SIZE 0x10

static bool fail(char* err, size_t err_size, const char* fmt, ...) {
    if (err && err_size) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(err, err_size, fmt, args);
        va_end(args);
    }
    return false;
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void cart_clear(cart_t* c) {
    memset(c, 0, sizeof(*c));
    memset(c->roml, 0xFF, sizeof(c->roml));
    memset(c->romh, 0xFF, sizeof(c->romh));
}

static void set_mode_lines(cart_t* c) {
    c->exrom = (c->mode == CART_MODE_8K || c->mode == CART_MODE_16K);
    c->game = (c->mode == CART_MODE_16K || c->mode == CART_MODE_ULTIMAX);
}

// Copy a chip of `size` bytes loaded at `addr` into the ROML/ROMH windows.
// 4K chips are mirrored across their 8K window (address line A12 isn't decoded).
static bool place_chip(cart_t* c, uint16_t addr, const uint8_t* src, uint32_t size, char* err, size_t err_size) {
    const bool ultimax = c->mode == CART_MODE_ULTIMAX;
    if (addr == 0x8000 && size == 0x4000) {
        // one 16K chip spanning ROML + ROMH ($A000 normally, $E000 in Ultimax)
        if (c->mode == CART_MODE_8K) {
            return fail(err, err_size, "16K ROM chip in an 8K cartridge");
        }
        if (c->has_roml || c->has_romh) {
            return fail(err, err_size, "overlapping ROM chips at $8000");
        }
        memcpy(c->roml, src, 0x2000);
        memcpy(c->romh, src + 0x2000, 0x2000);
        c->has_roml = c->has_romh = true;
        return true;
    }
    if (size != 0x1000 && size != 0x2000) {
        return fail(err, err_size, "unsupported ROM chip size $%X at $%04X", size, addr);
    }
    uint16_t base;
    uint8_t* dst;
    bool* has;
    const uint16_t window = addr & 0xE000;
    if (window == 0x8000) {
        base = 0x8000; dst = c->roml; has = &c->has_roml;
    } else if (window == 0xA000 && c->mode == CART_MODE_16K) {
        base = 0xA000; dst = c->romh; has = &c->has_romh;
    } else if (window == 0xE000 && ultimax) {
        base = 0xE000; dst = c->romh; has = &c->has_romh;
    } else {
        return fail(err, err_size, "ROM chip at $%04X doesn't fit a %s cartridge", addr, cart_mode_name(c->mode));
    }
    const uint16_t offset = (uint16_t)(addr - base);
    const bool aligned = (size == 0x2000) ? (offset == 0) : (offset == 0 || offset == 0x1000);
    if (!aligned) {
        return fail(err, err_size, "ROM chip at $%04X (size $%X) is misaligned", addr, size);
    }
    if (*has) {
        return fail(err, err_size, "more than one ROM chip for the $%04X window (bank-switching carts aren't supported yet)", base);
    }
    if (size == 0x1000) {
        memcpy(dst, src, 0x1000);
        memcpy(dst + 0x1000, src, 0x1000);
    } else {
        memcpy(dst, src, 0x2000);
    }
    *has = true;
    return true;
}

bool cart_is_crt(const uint8_t* data, size_t size) {
    return data && size >= 16 && memcmp(data, CRT_SIGNATURE, 16) == 0;
}

bool cart_parse_crt(const uint8_t* data, size_t size, cart_t* out, char* err, size_t err_size) {
    cart_clear(out);
    if (!cart_is_crt(data, size)) {
        return fail(err, err_size, "not a .crt file (missing \"C64 CARTRIDGE\" signature)");
    }
    if (size > CART_MAX_FILE_SIZE) {
        return fail(err, err_size, "file too large (%zu bytes)", size);
    }
    if (size < CRT_HEADER_MIN) {
        return fail(err, err_size, "truncated .crt header");
    }
    const uint32_t header_len = be32(data + 0x10);
    // Some tools write 0x20 here even though the header is 0x40 bytes; tolerate that.
    const uint32_t chips_start = header_len < CRT_HEADER_MIN ? CRT_HEADER_MIN : header_len;
    if (chips_start > size) {
        return fail(err, err_size, "invalid .crt header length %u", header_len);
    }
    const uint16_t version = be16(data + 0x14);
    if ((version >> 8) > 2) {
        return fail(err, err_size, "unsupported .crt version %u.%02u", version >> 8, version & 0xFF);
    }
    out->hw_type = be16(data + 0x16);
    memcpy(out->name, data + 0x20, 32);
    out->name[32] = 0;

    if (out->hw_type != 0) {
        const char* tn = cart_hw_type_name(out->hw_type);
        return fail(err, err_size, "cartridge type %u (%s) isn't supported yet", out->hw_type, tn ? tn : "unknown");
    }
    // Header line values: 0 = asserted (low), 1 = inactive.
    const bool exrom = data[0x18] == 0;
    const bool game = data[0x19] == 0;
    if (exrom && !game)      out->mode = CART_MODE_8K;
    else if (exrom && game)  out->mode = CART_MODE_16K;
    else if (!exrom && game) out->mode = CART_MODE_ULTIMAX;
    else return fail(err, err_size, "cartridge header asserts neither EXROM nor GAME");
    set_mode_lines(out);

    sha1_ctx_t sha;
    sha1_init(&sha);
    size_t pos = chips_start;
    int nchips = 0;
    while (pos < size) {
        if (size - pos < CHIP_HEADER_SIZE) {
            return fail(err, err_size, "truncated CHIP header at offset %zu", pos);
        }
        const uint8_t* ch = data + pos;
        if (memcmp(ch, "CHIP", 4) != 0) {
            return fail(err, err_size, "expected CHIP packet at offset %zu", pos);
        }
        const uint32_t packet_len = be32(ch + 4);
        const uint16_t chip_type = be16(ch + 8);
        const uint16_t bank = be16(ch + 10);
        const uint16_t load_addr = be16(ch + 12);
        const uint16_t rom_size = be16(ch + 14);
        if (rom_size == 0 || (size_t)rom_size > size - pos - CHIP_HEADER_SIZE) {
            return fail(err, err_size, "CHIP packet at offset %zu is truncated or empty", pos);
        }
        if (packet_len < CHIP_HEADER_SIZE + (uint32_t)rom_size || packet_len > size - pos) {
            return fail(err, err_size, "CHIP packet at offset %zu has an invalid length", pos);
        }
        if (chip_type == 1) {
            // RAM chip: no ROM data to place or hash
        } else {
            if (bank != 0) {
                return fail(err, err_size, "bank %u found: bank-switching carts aren't supported for type 0", bank);
            }
            const uint8_t* payload = ch + CHIP_HEADER_SIZE;
            if (!place_chip(out, load_addr, payload, rom_size, err, err_size)) {
                return false;
            }
            sha1_update(&sha, payload, rom_size);
            nchips++;
        }
        pos += packet_len;
    }
    if (nchips == 0) {
        return fail(err, err_size, "cartridge contains no ROM chips");
    }
    if (out->mode == CART_MODE_ULTIMAX && !out->has_romh) {
        return fail(err, err_size, "Ultimax cartridge has no ROM at $E000 (no reset vector)");
    }
    if (out->mode != CART_MODE_ULTIMAX && !out->has_roml) {
        return fail(err, err_size, "cartridge has no ROM at $8000");
    }
    if (out->mode == CART_MODE_16K && !out->has_romh) {
        return fail(err, err_size, "16K cartridge has no ROM at $A000 (truncated file?)");
    }
    uint8_t digest[20];
    sha1_final(&sha, digest);
    for (int i = 0; i < 20; i++) snprintf(out->sha1 + i * 2, 3, "%02x", digest[i]);
    return true;
}

bool cart_from_bin(const uint8_t* data, size_t size, uint16_t load_addr, cart_t* out, char* err, size_t err_size) {
    cart_clear(out);
    if (!data || size == 0) {
        return fail(err, err_size, "empty ROM file");
    }
    if (load_addr == 0x8000 && (size == 0x1000 || size == 0x2000)) {
        out->mode = CART_MODE_8K;
    } else if (load_addr == 0x8000 && size == 0x4000) {
        out->mode = CART_MODE_16K;
    } else if ((load_addr == 0xE000 && size == 0x2000) || (load_addr == 0xF000 && size == 0x1000)) {
        out->mode = CART_MODE_ULTIMAX;
    } else {
        return fail(err, err_size, "raw ROM of %zu bytes at $%04X isn't a supported layout", size, load_addr);
    }
    set_mode_lines(out);
    if (!place_chip(out, load_addr, data, (uint32_t)size, err, err_size)) {
        return false;
    }
    sha1_hex(data, size, out->sha1);
    return true;
}

const char* cart_mode_name(cart_mode_t mode) {
    switch (mode) {
        case CART_MODE_8K:      return "8K";
        case CART_MODE_16K:     return "16K";
        case CART_MODE_ULTIMAX: return "Ultimax";
        default:                return "unknown";
    }
}

const char* cart_hw_type_name(uint16_t t) {
    // Subset of the VICE .crt hardware type list, for error messages.
    switch (t) {
        case 0:  return "normal cartridge";
        case 1:  return "Action Replay";
        case 2:  return "KCS Power Cartridge";
        case 3:  return "Final Cartridge III";
        case 4:  return "Simons' BASIC";
        case 5:  return "Ocean type 1";
        case 6:  return "Expert Cartridge";
        case 7:  return "Fun Play / Power Play";
        case 8:  return "Super Games";
        case 9:  return "Atomic Power";
        case 10: return "Epyx Fastload";
        case 11: return "Westermann Learning";
        case 12: return "Rex Utility";
        case 13: return "Final Cartridge I";
        case 14: return "Magic Formel";
        case 15: return "C64 Game System / System 3";
        case 16: return "Warp Speed";
        case 17: return "Dinamic";
        case 18: return "Zaxxon / Super Zaxxon";
        case 19: return "Magic Desk / Domark / HES Australia";
        case 20: return "Super Snapshot V5";
        case 21: return "Comal-80";
        case 32: return "EasyFlash";
        case 36: return "Retro Replay";
        case 57: return "GMod2";
        case 60: return "GMod3";
        case 85: return "MegaByter";
        default: return NULL;
    }
}
