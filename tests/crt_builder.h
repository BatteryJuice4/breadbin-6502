#pragma once
// Builds .crt images in memory for tests. No third-party ROMs are needed:
// test cartridges are tiny 6502 programs written in the tests themselves.
#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t buf[0x10000];
    size_t len;
} crt_builder_t;

static void crt_put16(crt_builder_t* b, size_t at, uint16_t v) {
    b->buf[at] = (uint8_t)(v >> 8);
    b->buf[at + 1] = (uint8_t)v;
}

static void crt_put32(crt_builder_t* b, size_t at, uint32_t v) {
    crt_put16(b, at, (uint16_t)(v >> 16));
    crt_put16(b, at + 2, (uint16_t)v);
}

// exrom_line / game_line are the electrical values stored in the header: 0 = asserted.
static void crt_begin(crt_builder_t* b, uint16_t hw_type, int exrom_line, int game_line, const char* name) {
    memset(b, 0, sizeof(*b));
    memcpy(b->buf, "C64 CARTRIDGE   ", 16);
    crt_put32(b, 0x10, 0x40);
    crt_put16(b, 0x14, 0x0100);
    crt_put16(b, 0x16, hw_type);
    b->buf[0x18] = (uint8_t)exrom_line;
    b->buf[0x19] = (uint8_t)game_line;
    strncpy((char*)b->buf + 0x20, name, 32);
    b->len = 0x40;
}

static void crt_chip(crt_builder_t* b, uint16_t chip_type, uint16_t bank, uint16_t load_addr,
                     const uint8_t* data, uint16_t size) {
    const size_t at = b->len;
    memcpy(b->buf + at, "CHIP", 4);
    crt_put32(b, at + 4, 0x10u + size);
    crt_put16(b, at + 8, chip_type);
    crt_put16(b, at + 10, bank);
    crt_put16(b, at + 12, load_addr);
    crt_put16(b, at + 14, size);
    memcpy(b->buf + at + 16, data, size);
    b->len = at + 16 + size;
}
