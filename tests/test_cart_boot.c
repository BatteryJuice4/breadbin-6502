// End-to-end cartridge tests: synthetic carts are parsed, inserted into the core,
// and boot on the emulated C64. Verifies autostart and the full PLA memory map.
#include "core/core.h"
#include "core/cart.h"
#include "rom/sysroms.h"
#include "crt_builder.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static sysroms_t roms;

#define MARK_ROML 0x11
#define MARK_ROMH 0x22
#define MARK_RAM  0x33
#define DONE_FLAG 0x5A

/*
    Probe routine, runs from RAM at $C100 (always RAM outside Ultimax), so it can
    bank out the cartridge it was copied from. It writes MARK_RAM under every
    probed address with all-RAM banking, then for $01 = $30..$37 stores what the
    CPU reads at $9FF0, $BFF0, $D010, $FFF0 into $C000 + 4*n. Finally sets $C020.
*/
static const uint8_t probe_prog[] = {
    0x78,                   // C100 SEI
    0xA9, 0x2F, 0x85, 0x00, // C101 LDA #$2F / STA $00   (CPU port DDR)
    0xA9, 0x30, 0x85, 0x01, // C105 LDA #$30 / STA $01   (all RAM)
    0xA9, MARK_RAM,         // C109 LDA #MARK_RAM
    0x8D, 0xF0, 0x9F,       // C10B STA $9FF0
    0x8D, 0xF0, 0xBF,       // C10E STA $BFF0
    0x8D, 0x10, 0xD0,       // C111 STA $D010
    0x8D, 0xF0, 0xFF,       // C114 STA $FFF0
    0xA2, 0x00,             // C117 LDX #0
    0xA0, 0x00,             // C119 LDY #0
    0x8A,                   // C11B loop: TXA
    0x09, 0x30,             // C11C ORA #$30
    0x85, 0x01,             // C11E STA $01
    0xAD, 0xF0, 0x9F,       // C120 LDA $9FF0
    0x99, 0x00, 0xC0,       // C123 STA $C000,Y
    0xC8,                   // C126 INY
    0xAD, 0xF0, 0xBF,       // C127 LDA $BFF0
    0x99, 0x00, 0xC0,       // C12A STA $C000,Y
    0xC8,                   // C12D INY
    0xAD, 0x10, 0xD0,       // C12E LDA $D010
    0x99, 0x00, 0xC0,       // C131 STA $C000,Y
    0xC8,                   // C134 INY
    0xAD, 0xF0, 0xFF,       // C135 LDA $FFF0
    0x99, 0x00, 0xC0,       // C138 STA $C000,Y
    0xC8,                   // C13B INY
    0xE8,                   // C13C INX
    0xE0, 0x08,             // C13D CPX #8
    0xD0, 0xDA,             // C13F BNE loop
    0xA9, 0x37, 0x85, 0x01, // C141 LDA #$37 / STA $01
    0xA9, DONE_FLAG,        // C145 LDA #DONE_FLAG
    0x8D, 0x20, 0xC0,       // C147 STA $C020
    0x4C, 0x4A, 0xC1,       // C14A JMP $C14A
};

// ROML autostart stub: CBM80 signature, copies $8100-$81FF to $C100 and jumps there.
static void build_roml(uint8_t roml[0x2000]) {
    static const uint8_t stub[] = {
        0x09, 0x80, 0x09, 0x80,             // cold/warm start vectors -> $8009
        0xC3, 0xC2, 0xCD, 0x38, 0x30,       // "CBM80"
        0xA2, 0x00,                         // 8009 LDX #0
        0xBD, 0x00, 0x81,                   // 800B LDA $8100,X
        0x9D, 0x00, 0xC1,                   // 800E STA $C100,X
        0xE8,                               // 8011 INX
        0xD0, 0xF7,                         // 8012 BNE $800B
        0x4C, 0x00, 0xC1,                   // 8014 JMP $C100
    };
    memset(roml, 0xEA, 0x2000);
    memcpy(roml, stub, sizeof(stub));
    memcpy(roml + 0x100, probe_prog, sizeof(probe_prog));
    roml[0x1FF0] = MARK_ROML;
}

/*
    Canonical C64 banking table (C64 wiki "Bank Switching"), indexed by
    EXROM GAME CHAREN HIRAM LORAM as electrical levels (1 = high/inactive).
    Columns: $8000 $A000 $D000 $E000. R=RAM L=ROML H=ROMH B=BASIC C=CHAR I=I/O K=KERNAL
    Written out literally so it checks the PLA code independently.
*/
static const char* banking_table[32] = {
    [0x1F] = "RBIK", [0x1E] = "RRIK", [0x1D] = "RRIR", [0x1C] = "RRRR",
    [0x1B] = "RBCK", [0x1A] = "RRCK", [0x19] = "RRCR", [0x18] = "RRRR",
    [0x0F] = "LBIK", [0x0E] = "RRIK", [0x0D] = "RRIR", [0x0C] = "RRRR",
    [0x0B] = "LBCK", [0x0A] = "RRCK", [0x09] = "RRCR", [0x08] = "RRRR",
    [0x07] = "LHIK", [0x06] = "RHIK", [0x05] = "RRIR", [0x04] = "RRRR",
    [0x03] = "LHCK", [0x02] = "RHCK", [0x01] = "RRRR", [0x00] = "RRRR",
};

static uint8_t expected_byte(char kind, int column) {
    static const uint16_t rom_off[4] = { 0x1FF0, 0x1FF0, 0x0010, 0x1FF0 };
    switch (kind) {
        case 'R': return MARK_RAM;
        case 'L': return MARK_ROML;
        case 'H': return MARK_ROMH;
        case 'B': return roms.basic[rom_off[column]];
        case 'K': return roms.kernal[rom_off[column]];
        case 'C': return roms.chargen[rom_off[column]];
        case 'I': return 0x00;      // VIC-II $D010 (sprite X MSBs), zero after reset
        default:  return 0xEE;
    }
}

static core_t* make_core(void) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal,   .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic,     .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

static void run(core_t* c, int frames) {
    for (int i = 0; i < frames; i++) core_run_frame(c);
}

static void check_banking(const core_t* c, int exrom_line, int game_line, const char* label) {
    CHECK(core_ram_read(c, 0xC020) == DONE_FLAG, "%s: probe routine didn't finish (cart didn't start?)", label);
    for (int port = 0; port < 8; port++) {
        const int idx = (exrom_line << 4) | (game_line << 3) | port;   // port bits = CHAREN HIRAM LORAM
        const char* row = banking_table[idx];
        for (int col = 0; col < 4; col++) {
            const uint8_t got = core_ram_read(c, (uint16_t)(0xC000 + port * 4 + col));
            const uint8_t want = expected_byte(row[col], col);
            static const char* names[4] = { "$9FF0", "$BFF0", "$D010", "$FFF0" };
            CHECK(got == want, "%s: $01=$%02X %s read $%02X, want '%c' ($%02X)",
                  label, 0x30 | port, names[col], got, row[col], want);
        }
    }
}

// The probe distinguishes regions by value, so the ROM bytes it reads must differ.
static void check_probe_bytes_distinct(void) {
    const uint8_t b = roms.basic[0x1FF0], k = roms.kernal[0x1FF0], ch = roms.chargen[0x10];
    const uint8_t marks[] = { MARK_ROML, MARK_ROMH, MARK_RAM, 0x00 };
    for (unsigned i = 0; i < sizeof(marks); i++) {
        CHECK(b != marks[i] && k != marks[i] && ch != marks[i], "probe ROM byte collides with marker $%02X", marks[i]);
    }
}

static void test_no_cart_banking(void) {
    core_t* c = make_core();
    run(c, 250);
    for (unsigned i = 0; i < sizeof(probe_prog); i++) core_ram_write(c, (uint16_t)(0xC100 + i), probe_prog[i]);
    // SYS 49408 ($C100) by typing it
    const char* cmd = "SYS 49408\r";
    for (const char* p = cmd; *p; p++) {
        int k = *p == '\r' ? CORE_KEY_RETURN : *p;
        core_key_down(c, k); run(c, 3); core_key_up(c, k); run(c, 3);
    }
    run(c, 10);
    check_banking(c, 1, 1, "no cart");
    core_destroy(c);
}

static void test_8k_16k(bool is16k) {
    static uint8_t roml[0x2000], romh[0x2000];
    build_roml(roml);
    memset(romh, 0xEA, sizeof(romh));
    romh[0x1FF0] = MARK_ROMH;

    crt_builder_t* b = &(crt_builder_t){ 0 };
    crt_begin(b, 0, 0, is16k ? 0 : 1, is16k ? "PROBE 16K" : "PROBE 8K");
    crt_chip(b, 0, 0, 0x8000, roml, 0x2000);
    if (is16k) crt_chip(b, 0, 0, 0xA000, romh, 0x2000);

    static cart_t cart;
    char err[256];
    CHECK(cart_parse_crt(b->buf, b->len, &cart, err, sizeof(err)), "parse: %s", err);

    core_t* c = make_core();
    CHECK(core_insert_cart(c, &cart) && core_has_cart(c), "insert failed");
    run(c, 50);     // KERNAL reset finds CBM80 within a few ms
    check_banking(c, 0, is16k ? 0 : 1, is16k ? "16K" : "8K");

    // Reset keeps the cart; removing it boots BASIC again
    core_reset(c);
    core_ram_write(c, 0xC020, 0);
    run(c, 50);
    CHECK(core_ram_read(c, 0xC020) == DONE_FLAG, "cart didn't restart after reset");
    core_remove_cart(c);
    CHECK(!core_has_cart(c), "cart still reported after removal");
    core_ram_write(c, 0xC020, 0);
    run(c, 250);
    CHECK(core_ram_read(c, 0xC020) == 0, "cart code ran after removal");
    const uint8_t ready[] = { 18, 5, 1, 4, 25, 46 };   // "READY." screen codes
    bool found = false;
    for (int a = 0x0400; a < 0x07E8 - 6 && !found; a++) {
        found = true;
        for (int i = 0; i < 6; i++) if (core_ram_read(c, (uint16_t)(a + i)) != ready[i]) { found = false; break; }
    }
    CHECK(found, "no READY. after removing the cart");
    core_destroy(c);
}

static void test_ultimax(void) {
    static uint8_t roml[0x2000], romh[0x2000];
    memset(roml, 0xEA, sizeof(roml));
    roml[0x1FF0] = MARK_ROML;
    static const uint8_t prog[] = {
        0x78,                   // E000 SEI
        0xA2, 0xFF, 0x9A,       // E001 LDX #$FF / TXS
        0xA9, 0x2F, 0x85, 0x00, // E004 DDR
        0xA9, 0x30, 0x85, 0x01, // E008 $01 = $30: would be all-RAM, but Ultimax ignores it
        0xA9, 0x77,             // E00C LDA #$77
        0x8D, 0x00, 0x10,       // E00E STA $1000   (open bus: dropped)
        0xAD, 0x00, 0x10,       // E011 LDA $1000   (open bus: $FF)
        0x8D, 0x00, 0x04,       // E014 STA $0400
        0xAD, 0xF0, 0x9F,       // E017 LDA $9FF0   (ROML)
        0x8D, 0x01, 0x04,       // E01A STA $0401
        0xAD, 0xF0, 0xBF,       // E01D LDA $BFF0   (open bus: $FF)
        0x8D, 0x02, 0x04,       // E020 STA $0402
        0xAD, 0xF0, 0xFF,       // E023 LDA $FFF0   (ROMH)
        0x8D, 0x03, 0x04,       // E026 STA $0403
        0xA9, DONE_FLAG,        // E029 LDA #DONE_FLAG
        0x8D, 0x04, 0x04,       // E02B STA $0404
        0x4C, 0x2E, 0xE0,       // E02E JMP $E02E
    };
    memset(romh, 0xEA, sizeof(romh));
    memcpy(romh, prog, sizeof(prog));
    romh[0x1FF0] = MARK_ROMH;
    const uint8_t vectors[6] = { 0x2E, 0xE0, 0x00, 0xE0, 0x2E, 0xE0 };  // NMI, RESET, IRQ
    memcpy(romh + 0x1FFA, vectors, 6);

    crt_builder_t* b = &(crt_builder_t){ 0 };
    crt_begin(b, 0, 1, 0, "PROBE ULTIMAX");
    crt_chip(b, 0, 0, 0x8000, roml, 0x2000);
    crt_chip(b, 0, 0, 0xE000, romh, 0x2000);
    static cart_t cart;
    char err[256];
    CHECK(cart_parse_crt(b->buf, b->len, &cart, err, sizeof(err)), "parse: %s", err);

    core_t* c = make_core();
    core_ram_write(c, 0x1000, 0x00);
    CHECK(core_insert_cart(c, &cart), "insert failed");
    run(c, 5);
    CHECK(core_ram_read(c, 0x0404) == DONE_FLAG, "Ultimax: reset vector code didn't run");
    CHECK(core_ram_read(c, 0x0400) == 0xFF, "Ultimax: $1000 read $%02X, want open bus $FF", core_ram_read(c, 0x0400));
    CHECK(core_ram_read(c, 0x0401) == MARK_ROML, "Ultimax: ROML at $9FF0 read $%02X", core_ram_read(c, 0x0401));
    CHECK(core_ram_read(c, 0x0402) == 0xFF, "Ultimax: $BFF0 read $%02X, want open bus $FF", core_ram_read(c, 0x0402));
    CHECK(core_ram_read(c, 0x0403) == MARK_ROMH, "Ultimax: ROMH at $FFF0 read $%02X", core_ram_read(c, 0x0403));
    CHECK(core_ram_read(c, 0x1000) != 0x77, "Ultimax: write to open $1000 reached RAM");
    core_destroy(c);
}

int main(void) {
    sysroms_load_default(&roms);
    check_probe_bytes_distinct();
    {
        core_t* c = make_core();
        CHECK(!core_insert_cart(c, NULL), "NULL cart accepted");
        core_destroy(c);
    }
    test_no_cart_banking();
    test_8k_16k(false);
    test_8k_16k(true);
    test_ultimax();
    if (failures == 0) printf("test_cart_boot: all passed\n");
    return failures ? 1 : 0;
}
