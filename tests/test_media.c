// Disk and tape media: format parsers, identity hashes, and booting each format
// through autostart on the real core with the bundled Open ROMs. All test images
// are generated here (original code, no third-party files).
#include "core/core.h"
#include "media/autostart.h"
#include "media/d64.h"
#include "media/media.h"
#include "media/t64.h"
#include "media/tap.h"
#include "rom/sha1.h"
#include "rom/sysroms.h"
#include "test_images.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static sysroms_t roms;

// ---- .t64 builder ------------------------------------------------------------
static uint8_t* make_t64(const dfile_t* files, int n, size_t* size, bool bad_end) {
    size_t total = 0x40 + 32 * (size_t)n;
    for (int i = 0; i < n; i++) total += files[i].size - 2;
    uint8_t* t = calloc(1, total);
    memcpy(t, "C64S tape image file", 20);
    t[0x20] = 0x00; t[0x21] = 0x01;
    t[0x22] = (uint8_t)n; t[0x24] = (uint8_t)n;
    memset(t + 0x28, ' ', 24);
    size_t off = 0x40 + 32 * (size_t)n;
    for (int i = 0; i < n; i++) {
        uint8_t* e = t + 0x40 + 32 * i;
        const uint16_t start = (uint16_t)(files[i].data[0] | (files[i].data[1] << 8));
        const uint16_t end = (bad_end && i == 0) ? 0xC3C6 : (uint16_t)(start + files[i].size - 2);
        e[0] = 1; e[1] = 0x82;
        e[2] = (uint8_t)start; e[3] = (uint8_t)(start >> 8);
        e[4] = (uint8_t)end; e[5] = (uint8_t)(end >> 8);
        e[8] = (uint8_t)off; e[9] = (uint8_t)(off >> 8);
        memset(e + 16, ' ', 16);
        memcpy(e + 16, files[i].name, strlen(files[i].name));
        memcpy(t + off, files[i].data + 2, files[i].size - 2);
        off += files[i].size - 2;
    }
    *size = total;
    return t;
}

// ---- .tap encoder (standard CBM KERNAL tape format) ---------------------------
typedef struct { uint8_t* p; size_t n, cap; } tbuf_t;
enum { PS = 0x30, PM = 0x42, PL = 0x56 };   // short/medium/long pulse (cycles / 8)
static void pulse(tbuf_t* b, uint8_t v) { if (b->n < b->cap) b->p[b->n++] = v; }
static void pulses(tbuf_t* b, uint8_t v, int count) { for (int i = 0; i < count; i++) pulse(b, v); }
static void tbyte(tbuf_t* b, uint8_t v) {
    pulse(b, PL); pulse(b, PM);                         // byte marker
    uint8_t check = 1;
    for (int i = 0; i < 8; i++) {
        const int bit = (v >> i) & 1;
        check ^= (uint8_t)bit;
        if (bit) { pulse(b, PM); pulse(b, PS); } else { pulse(b, PS); pulse(b, PM); }
    }
    if (check) { pulse(b, PM); pulse(b, PS); } else { pulse(b, PS); pulse(b, PM); }
}
static void tblock(tbuf_t* b, const uint8_t* data, size_t n, int leader) {
    pulses(b, PS, leader);
    for (int copy = 0; copy < 2; copy++) {
        for (int i = 9; i >= 1; i--) tbyte(b, (uint8_t)(copy ? i : 0x80 | i));    // countdown
        uint8_t x = 0;
        for (size_t i = 0; i < n; i++) { tbyte(b, data[i]); x ^= data[i]; }
        tbyte(b, x);
        pulse(b, PL); pulse(b, PS);                     // end of data
        pulses(b, PS, copy ? 78 : 79);                  // gap / trailer
    }
}
static uint8_t* make_tap(const uint8_t* prg, size_t prg_size, const char* name, size_t* size) {
    tbuf_t b = { malloc(1u << 20), 20, 1u << 20 };
    memcpy(b.p, "C64-TAPE-RAW", 12);
    b.p[12] = 1; b.p[13] = b.p[14] = b.p[15] = 0;
    const uint16_t start = (uint16_t)(prg[0] | (prg[1] << 8));
    const uint16_t end = (uint16_t)(start + prg_size - 2);
    uint8_t hdr[192];
    memset(hdr, ' ', sizeof(hdr));
    hdr[0] = 3;     // non-relocatable program
    hdr[1] = (uint8_t)start; hdr[2] = (uint8_t)(start >> 8);
    hdr[3] = (uint8_t)end; hdr[4] = (uint8_t)(end >> 8);
    memcpy(hdr + 5, name, strlen(name));
    tblock(&b, hdr, sizeof(hdr), 0x6A00);
    tblock(&b, prg + 2, prg_size - 2, 0x1500);
    pulses(&b, PS, 200);
    const size_t data = b.n - 20;
    b.p[16] = (uint8_t)data; b.p[17] = (uint8_t)(data >> 8); b.p[18] = (uint8_t)(data >> 16); b.p[19] = 0;
    *size = b.n;
    return b.p;
}

static bool contains(const uint8_t* hay, size_t n, const char* needle) {
    const size_t k = strlen(needle);
    for (size_t i = 0; i + k <= n; i++) if (!memcmp(hay + i, needle, k)) return true;
    return false;
}

static uint8_t* dup(const uint8_t* p, size_t n) { uint8_t* q = malloc(n); memcpy(q, p, n); return q; }

// ---- parser tests --------------------------------------------------------------
static void test_d64(void) {
    uint8_t p1[128];
    const size_t n1 = make_part1(p1, 8);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 },
                              { "NOTES", part2, sizeof(part2), 0x81 } };   // a SEQ file
    uint8_t* img = make_d64(files, 3, D64_SIZE);
    d64_t d;
    char err[128];
    CHECK(d64_open(img, D64_SIZE, &d, err, sizeof(err)) && d.tracks == 35, "open: %s", err);
    CHECK(!d64_open(img, 1000, &d, err, sizeof(err)), "bad size accepted");
    d64_open(img, D64_SIZE, &d, err, sizeof(err));

    d64_entry_t e[8];
    CHECK(d64_dir(&d, e, 8) == 3 && e[0].name_len == 5 && !memcmp(e[1].name, "PART2", 5), "directory");
    d64_entry_t f;
    CHECK(d64_find(&d, (const uint8_t*)"*", 1, &f) && !memcmp(f.name, "PART1", 5), "* = first file");
    CHECK(d64_find(&d, (const uint8_t*)"PART2", 5, &f) && !memcmp(f.name, "PART2", 5), "exact name");
    CHECK(d64_find(&d, (const uint8_t*)"?ART2", 5, &f) && !memcmp(f.name, "PART2", 5), "? wildcard");
    CHECK(d64_find(&d, (const uint8_t*)"0:PART2", 7, &f) && !memcmp(f.name, "PART2", 5), "drive prefix");
    CHECK(d64_find(&d, (const uint8_t*)"PA*", 3, &f) && !memcmp(f.name, "PART1", 5), "prefix wildcard");
    CHECK(!d64_find(&d, (const uint8_t*)"PART", 4, &f), "partial name must not match");
    CHECK(!d64_find(&d, (const uint8_t*)"NOTES", 5, &f), "SEQ file must not load");

    static uint8_t buf[CORE_FILE_MAX];
    size_t len = 0;
    CHECK(d64_load(&d, (const uint8_t*)"PART1", 5, buf, sizeof(buf), &len) && len == n1 && !memcmp(buf, p1, n1), "read PART1");

    // A big file spanning tracks round-trips.
    static uint8_t big[30000];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7 + (i >> 8));
    const dfile_t bigf[] = { { "BIG", big, sizeof(big), 0 } };
    uint8_t* img2 = make_d64(bigf, 1, D64_SIZE);
    d64_t d2;
    d64_open(img2, D64_SIZE, &d2, NULL, 0);
    CHECK(d64_load(&d2, (const uint8_t*)"BIG", 3, buf, sizeof(buf), &len) && len == sizeof(big) && !memcmp(buf, big, len), "multi-track file");
    CHECK(!d64_load(&d2, (const uint8_t*)"BIG", 3, buf, 1000, &len), "file larger than the buffer must fail");
    // A sector chain that loops back on itself fails instead of hanging.
    img2[soff(1, 0)] = 1; img2[soff(1, 0) + 1] = 0;
    CHECK(!d64_load(&d2, (const uint8_t*)"BIG", 3, buf, sizeof(buf), &len), "looping chain accepted");
    free(img2);

    // Directory listing: a linked BASIC program at $0401.
    CHECK(d64_load(&d, (const uint8_t*)"$", 1, buf, sizeof(buf), &len), "directory listing");
    CHECK(buf[0] == 0x01 && buf[1] == 0x04, "listing load address");
    int lines = 0;
    size_t pos = 2;
    while (pos + 1 < len && (buf[pos] | buf[pos + 1])) {
        const unsigned next = (unsigned)(buf[pos] | (buf[pos + 1] << 8)) - 0x0401 + 2;
        CHECK(next > pos && next <= len, "bad line link");
        if (next <= pos || next > len) break;
        pos = next;
        lines++;
    }
    CHECK(lines == 5, "listing has %d lines, want 5 (header, 3 files, blocks free)", lines);
    CHECK(contains(buf, len, "\"PART2\"") && contains(buf, len, "BLOCKS FREE."), "listing text");

    // Identity ignores the error-byte block.
    uint8_t* with_errors = calloc(1, 175531);
    memcpy(with_errors, img, D64_SIZE);
    memset(with_errors + D64_SIZE, 1, 175531 - D64_SIZE);
    media_image_t a, b;
    CHECK(media_open(&a, MEDIA_FMT_D64, dup(img, D64_SIZE), D64_SIZE, err, sizeof(err)), "media_open d64: %s", err);
    CHECK(media_open(&b, MEDIA_FMT_D64, with_errors, 175531, err, sizeof(err)), "media_open d64+errors: %s", err);
    CHECK(!strcmp(a.sha1, b.sha1), "error bytes changed the identity");
    char direct[41];
    sha1_hex(img, D64_SIZE, direct);
    CHECK(!strcmp(a.sha1, direct), "d64 identity is the sector data");
    media_close(&a);
    media_close(&b);
    free(img);
}

static void test_t64_tap_detect(void) {
    uint8_t p1[128];
    const size_t n1 = make_part1(p1, 1);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 } };
    size_t tsize;
    uint8_t* t = make_t64(files, 2, &tsize, true);   // first entry has the classic wrong end address
    t64_t tt;
    char err[128];
    CHECK(t64_open(t, tsize, &tt, err, sizeof(err)) && tt.count == 2, "t64 open: %s", err);
    static uint8_t buf[CORE_FILE_MAX];
    size_t len = 0;
    CHECK(t64_load(&tt, (const uint8_t*)"", 0, buf, sizeof(buf), &len) && len == n1 && !memcmp(buf, p1, n1), "t64: empty name = first file, size corrected");
    CHECK(t64_load(&tt, (const uint8_t*)"PART2", 5, buf, sizeof(buf), &len) && len == sizeof(part2) && buf[2] == 0x77, "t64: by name");
    CHECK(t64_load(&tt, (const uint8_t*)"PA", 2, buf, sizeof(buf), &len) && len == n1, "t64: prefix like the KERNAL");
    CHECK(!t64_load(&tt, (const uint8_t*)"NOPE", 4, buf, sizeof(buf), &len), "t64: missing name");

    // .tap version 0 -> 1: a zero byte becomes an explicit 2048-cycle pulse.
    uint8_t v0[24] = "C64-TAPE-RAW";
    v0[12] = 0; v0[16] = 4;
    v0[20] = 0x30; v0[21] = 0x00; v0[22] = 0x42; v0[23] = 0x56;
    size_t n = 0;
    uint8_t* v1 = tap_to_v1(v0, sizeof(v0), &n, err, sizeof(err));
    static const uint8_t want[] = { 0x30, 0x00, 0x00, 0x08, 0x00, 0x42, 0x56 };
    CHECK(v1 && n == 20 + sizeof(want) && v1[12] == 1 && v1[16] == sizeof(want) && !memcmp(v1 + 20, want, sizeof(want)), "tap v0 conversion");
    free(v1);
    v0[12] = 2;
    CHECK(!tap_to_v1(v0, sizeof(v0), &n, err, sizeof(err)), "tap version 2 (C16) accepted");

    uint8_t d64[64] = { 0 };
    CHECK(media_detect("x.d64", t, tsize) == MEDIA_FMT_T64, "detect t64");
    CHECK(media_detect("x.tap", (const uint8_t*)"C64-TAPE-RAW\1\0\0\0\0\0\0\0", 20) == MEDIA_FMT_TAP, "detect tap");
    CHECK(media_detect("GAME.PRG", p1, n1) == MEDIA_FMT_PRG, "detect prg (case-insensitive)");
    CHECK(media_detect("x.foo", d64, sizeof(d64)) == MEDIA_FMT_UNKNOWN, "unknown");
    CHECK(media_format_kind(MEDIA_FMT_T64) == MEDIA_KIND_TAPE && media_format_kind(MEDIA_FMT_PRG) == MEDIA_KIND_DISK, "kinds");
    free(t);
}

// ---- booting through autostart -------------------------------------------------
typedef struct { uint32_t frames; uint8_t c000, c001; uint32_t loads; char screen[25][41]; } boot_result_t;

static void dump_screen(const boot_result_t* r) {
    for (int row = 0; row < 25; row++) fprintf(stderr, "  |%s|\n", r->screen[row]);
}

static boot_result_t boot(media_format_t fmt, uint8_t* data, size_t size, const autostart_opts_t* opts, uint32_t max_frames) {
    boot_result_t r = { 0 };
    core_t* c = core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
    media_image_t img;
    char err[128];
    if (!media_open(&img, fmt, data, size, err, sizeof(err))) {
        CHECK(false, "media_open %s: %s", media_format_name(fmt), err);
        core_destroy(c);
        return r;
    }
    core_set_file_loader(c, media_serve, &img);
    if (fmt == MEDIA_FMT_TAP) CHECK(core_insert_tape(c, img.tap_v1, img.tap_v1_size), "tape insert");
    core_reset(c);
    autostart_t a;
    autostart_begin(&a, c, fmt, opts);
    while (autostart_loading(&a) && r.frames < max_frames) {
        core_run_frame(c);
        autostart_frame(&a, c);
        r.frames++;
    }
    for (int i = 0; i < 25; i++) core_run_frame(c);  // let the program finish its work
    r.c000 = core_ram_read(c, 0xC000);
    r.c001 = core_ram_read(c, 0xC001);
    r.loads = core_files_loaded(c);
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < 40; col++) {
            const uint8_t sc = core_ram_read(c, (uint16_t)(0x0400 + row * 40 + col)) & 0x7F;
            r.screen[row][col] = (sc >= 1 && sc <= 26) ? (char)('A' + sc - 1) : (sc >= 0x20 && sc < 0x40) ? (char)sc : '.';
        }
        r.screen[row][40] = 0;
    }
    media_close(&img);
    core_destroy(c);
    return r;
}

static void test_boot_disk(void) {
    uint8_t p1[128];
    const size_t n1 = make_part1(p1, 8);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 } };
    boot_result_t r = boot(MEDIA_FMT_D64, make_d64(files, 2, D64_SIZE), D64_SIZE, NULL, 3000);
    CHECK(r.c000 == 0x42, "d64: program didn't run ($C000 = $%02X)", r.c000);
    CHECK(r.c001 == 0x77, "d64: in-game KERNAL load failed ($C001 = $%02X)", r.c001);
    CHECK(r.loads == 2, "d64: %u loads served, want 2", r.loads);
    CHECK(r.frames < 600, "d64: loading took %u frames", r.frames);
    printf("test_media: d64 booted in %u frames\n", r.frames);

    // Game pack names the file to load.
    autostart_opts_t o = { .file = "part2", .done_pc = -1 };
    const dfile_t first_is_other[] = { { "INTRO", part2, sizeof(part2), 0 }, { "PART2", p1, n1, 0 } };
    r = boot(MEDIA_FMT_D64, make_d64(first_is_other, 2, D64_SIZE), D64_SIZE, &o, 3000);
    CHECK(r.c000 == 0x42, "d64: named file not loaded");

    // done_pc: loading ends when the program reaches its main loop ($080D + 48).
    o = (autostart_opts_t){ .done_pc = 0x080D + 48 };
    r = boot(MEDIA_FMT_D64, make_d64(files, 2, D64_SIZE), D64_SIZE, &o, 3000);
    CHECK(r.c001 == 0x77 && r.frames < 600, "d64: done_pc (frames %u)", r.frames);

    // A missing file: LOAD fails with FILE NOT FOUND, nothing runs, and warp still ends
    // (here via done_frames; without it, at the time limit).
    const dfile_t none[] = { { "OTHER", part2, sizeof(part2), 0 } };
    o = (autostart_opts_t){ .file = "MISSING", .done_pc = -1, .done_frames = 300 };
    r = boot(MEDIA_FMT_D64, make_d64(none, 1, D64_SIZE), D64_SIZE, &o, 20000);
    CHECK(r.frames == 300, "d64: done_frames ended warp after %u frames", r.frames);
    CHECK(r.c000 != 0x42 && r.loads == 0, "d64: missing file");
}

static void test_boot_prg_t64(void) {
    uint8_t p1[128];
    size_t n1 = make_part1(p1, 0);
    boot_result_t r = boot(MEDIA_FMT_PRG, dup(p1, n1), n1, NULL, 3000);
    CHECK(r.c000 == 0x42 && r.loads == 1, "prg: didn't run ($C000 = $%02X, loads %u)", r.c000, r.loads);

    n1 = make_part1(p1, 1);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 } };
    size_t tsize;
    uint8_t* t = make_t64(files, 2, &tsize, false);
    r = boot(MEDIA_FMT_T64, t, tsize, NULL, 3000);
    CHECK(r.c000 == 0x42 && r.c001 == 0x77 && r.loads == 2, "t64: $C000 = $%02X, $C001 = $%02X, loads %u", r.c000, r.c001, r.loads);
}

static void test_boot_tap(void) {
    uint8_t p1[128];
    const size_t n1 = make_part1(p1, 0);
    size_t size;
    uint8_t* tap = make_tap(p1, n1, "PART1", &size);
    boot_result_t r = boot(MEDIA_FMT_TAP, tap, size, NULL, 20u * 60u * 50u);
    CHECK(r.c000 == 0x42, "tap: program didn't load/run ($C000 = $%02X after %u frames)", r.c000, r.frames);
    if (r.c000 != 0x42) dump_screen(&r);
    printf("test_media: tap booted in %u frames (%.1f s emulated)\n", r.frames, r.frames / 50.125);
}

int main(void) {
    sysroms_load_default(&roms);
    test_d64();
    test_t64_tap_detect();
    test_boot_disk();
    test_boot_prg_t64();
    test_boot_tap();
    if (failures == 0) printf("test_media: all passed\n");
    return failures ? 1 : 0;
}
