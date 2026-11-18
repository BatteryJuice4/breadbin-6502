// Generated test media shared by the media and drive tests (original code, no
// third-party files). Header-only: static helpers, include in one test file each.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---- test programs -----------------------------------------------------------
// PART1 at $0801: "10 SYS2061", then: store $42 at $C000; if `device` != 0, load
// "PART2" from that device with the KERNAL (SETLFS/SETNAM/LOAD) and copy $C100
// to $C001 ($EE on a load error); then loop forever.
static size_t make_part1(uint8_t* out, uint8_t device) {
    static const uint8_t stub[] = { 0x01, 0x08,                     // load address $0801
        0x0B, 0x08, 0x0A, 0x00, 0x9E, '2', '0', '6', '1', 0x00,     // 10 SYS2061
        0x00, 0x00 };                                               // end of BASIC
    memcpy(out, stub, sizeof(stub));
    const uint16_t base = 0x080D;
    const uint16_t name = (uint16_t)(base + 51);
    uint8_t code[] = {
        0xA9, 0x42, 0x8D, 0x00, 0xC0,               //  0 LDA #$42 / STA $C000
        0xA9, device, 0xF0, 39,                     //  5 LDA #dev / BEQ done
        0xA2, device, 0xA9, 0x01, 0xA0, 0x01,       //  9 LDX #dev / LDA #1 / LDY #1
        0x20, 0xBA, 0xFF,                           // 15 JSR SETLFS
        0xA9, 0x05, 0xA2, (uint8_t)name, 0xA0, (uint8_t)(name >> 8),   // 18 LDA #5 / LDX/LDY name
        0x20, 0xBD, 0xFF,                           // 24 JSR SETNAM
        0xA9, 0x00, 0x20, 0xD5, 0xFF,               // 27 LDA #0 / JSR LOAD
        0xB0, 9,                                    // 32 BCS fail
        0xAD, 0x00, 0xC1, 0x8D, 0x01, 0xC0,         // 34 LDA $C100 / STA $C001
        0x4C, (uint8_t)(base + 48), (uint8_t)((base + 48) >> 8),       // 40 JMP done
        0xA9, 0xEE, 0x8D, 0x01, 0xC0,               // 43 fail: LDA #$EE / STA $C001
        0x4C, (uint8_t)(base + 48), (uint8_t)((base + 48) >> 8),       // 48 done: JMP done
        'P', 'A', 'R', 'T', '2',                    // 51
    };
    memcpy(out + sizeof(stub), code, sizeof(code));
    return sizeof(stub) + sizeof(code);
}

static const uint8_t part2[] = { 0x00, 0xC1, 0x77, 0x01, 0x02 };   // $C100: 77 01 02

// ---- .d64 builder ------------------------------------------------------------
#define D64_SIZE 174848
static int spt(int t) { return t <= 17 ? 21 : t <= 24 ? 19 : t <= 30 ? 18 : 17; }
static long soff(int t, int s) { long n = 0; for (int i = 1; i < t; i++) n += spt(i); return (n + s) * 256; }

typedef struct { const char* name; const uint8_t* data; size_t size; uint8_t type; } dfile_t;

static uint8_t* make_d64(const dfile_t* files, int n, size_t size) {
    uint8_t* d = calloc(1, size);
    uint8_t* bam = d + soff(18, 0);
    bam[0] = 18; bam[1] = 1; bam[2] = 0x41;
    for (int t = 1; t <= 35; t++) bam[4 * t] = (uint8_t)(t == 18 ? 0 : spt(t));
    memset(bam + 0x90, 0xA0, 27);
    memcpy(bam + 0x90, "TEST DISK", 9);
    memcpy(bam + 0xA2, "BB 2A", 5);
    uint8_t* dir = d + soff(18, 1);
    dir[0] = 0; dir[1] = 0xFF;
    int t = 1, s = 0;
    for (int i = 0; i < n; i++) {
        uint8_t* e = dir + i * 32;
        e[2] = files[i].type ? files[i].type : 0x82;
        e[3] = (uint8_t)t; e[4] = (uint8_t)s;
        memset(e + 5, 0xA0, 16);
        memcpy(e + 5, files[i].name, strlen(files[i].name));
        size_t pos = 0;
        int blocks = 0;
        while (1) {
            uint8_t* b = d + soff(t, s);
            const size_t chunk = files[i].size - pos > 254 ? 254 : files[i].size - pos;
            memcpy(b + 2, files[i].data + pos, chunk);
            pos += chunk;
            blocks++;
            if (++s >= spt(t)) { s = 0; t++; }
            if (pos >= files[i].size) { b[0] = 0; b[1] = (uint8_t)(chunk + 1); break; }
            b[0] = (uint8_t)t; b[1] = (uint8_t)s;
        }
        e[30] = (uint8_t)blocks;
    }
    return d;
}
