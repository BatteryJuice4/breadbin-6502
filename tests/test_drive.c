// The emulated 1541 with the real DOS: loading over the serial bus, with no LOAD
// trap. Needs a 1541 ROM, which can't be committed: set BB_TEST_ROM_DIR to a
// folder with 1541.rom (or 1541-c000.rom + 1541-e000.rom), optionally with real
// kernal/basic/chargen ROMs too. Without it the test reports "skipped" (77).
#include "core/core.h"
#include "media/d64.h"
#include "media/gcr.h"
#include "rom/sysroms.h"
#include "test_images.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

#define KERNAL_CHRIN 0xFFCF

// Types text through the KERNAL keyboard buffer ($0277.., count in $C6; it holds
// 10 keys), a chunk at a time while running frames.
static void type(core_t* c, const char* text, uint32_t* frames) {
    size_t pos = 0;
    const size_t n = strlen(text);
    while (pos < n) {
        if (core_ram_read(c, 0xC6) == 0) {
            size_t k = 0;
            for (; k < 10 && pos < n; k++, pos++) core_ram_write(c, (uint16_t)(0x0277 + k), (uint8_t)text[pos]);
            core_ram_write(c, 0xC6, (uint8_t)k);
        }
        core_run_frame(c);
        (*frames)++;
    }
    for (int i = 0; i < 200 && core_ram_read(c, 0xC6) != 0; i++, (*frames)++) core_run_frame(c);
}

static void dump_screen(core_t* c) {
    for (int row = 0; row < 25; row++) {
        char line[41];
        for (int col = 0; col < 40; col++) {
            const uint8_t sc = core_ram_read(c, (uint16_t)(0x0400 + row * 40 + col)) & 0x7F;
            line[col] = (sc >= 1 && sc <= 26) ? (char)('A' + sc - 1) : (sc >= 0x20 && sc < 0x40) ? (char)sc : '.';
        }
        line[40] = 0;
        fprintf(stderr, "  |%s|\n", line);
    }
}

// Waits until BASIC asks for input again (after `after` earlier prompts).
static bool wait_prompt(core_t* c, int watch, uint32_t after, uint32_t max_frames, uint32_t* frames) {
    for (uint32_t i = 0; i < max_frames; i++, (*frames)++) {
        core_run_frame(c);
        if (core_watch_hits(c, watch) > after) return true;
    }
    return false;
}

static void test_load(const sysroms_t* roms, const char* label) {
    core_t* c = core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms->kernal, .rom_kernal_size = sizeof(roms->kernal),
        .rom_basic = roms->basic, .rom_basic_size = sizeof(roms->basic),
        .rom_chargen = roms->chargen, .rom_chargen_size = sizeof(roms->chargen),
        .rom_1541 = roms->drive1541, .rom_1541_size = sizeof(roms->drive1541),
    });
    CHECK(c && core_has_drive(c), "%s: core with a drive", label);
    if (!c) return;

    uint8_t p1[128];
    const size_t n1 = make_part1(p1, 8);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 } };
    uint8_t* img = make_d64(files, 2, D64_SIZE);
    d64_t d;
    char err[128];
    gcr_image_t g;
    CHECK(d64_open(img, D64_SIZE, &d, err, sizeof(err)) && gcr_from_d64(&d, &g, err, sizeof(err)), "%s: %s", label, err);
    core_insert_disk(c, &g.disk);
    core_reset(c);

    const int chrin = core_add_watch(c, KERNAL_CHRIN);
    uint32_t frames = 0;
    CHECK(wait_prompt(c, chrin, 0, 600, &frames), "%s: no BASIC prompt", label);
    // Let the drive finish its own reset (it takes about a second) before talking to it.
    for (int i = 0; i < 100; i++, frames++) core_run_frame(c);
    const uint32_t boot = frames;
    type(c, "LOAD\"PART1\",8,1\r", &frames);
    // Loading is over when the drive has run and BASIC reads input again.
    bool led_seen = false, motor_seen = false;
    uint32_t prompts = 0;
    for (uint32_t i = 0; i < 3000; i++, frames++) {
        core_run_frame(c);
        led_seen |= core_drive_led(c);
        if (core_drive_motor(c) && !motor_seen) {
            motor_seen = true;
            prompts = core_watch_hits(c, chrin);
        }
        if (motor_seen && core_watch_hits(c, chrin) > prompts) break;
    }
    const uint32_t loaded = frames;
    CHECK(motor_seen && led_seen, "%s: drive motor/LED never on", label);
    CHECK(core_ram_read(c, 0x0801) == 0x0B && core_ram_read(c, 0x0805) == 0x9E, "%s: PART1 not in memory", label);
    type(c, "RUN\r", &frames);
    for (uint32_t i = 0; i < 3000 && core_ram_read(c, 0xC001) == 0; i++, frames++) core_run_frame(c);
    CHECK(core_ram_read(c, 0xC000) == 0x42, "%s: PART1 didn't run", label);
    CHECK(core_ram_read(c, 0xC001) == 0x77, "%s: PART2 load from the drive failed ($C001 = $%02X)", label,
          core_ram_read(c, 0xC001));
    CHECK(core_files_loaded(c) == 0, "%s: the LOAD trap served a file", label);
    CHECK(!g.disk.dirty, "%s: loading wrote to the disk", label);
    if (failures) dump_screen(c);
    printf("test_drive: %s: prompt after %u frames, PART1 loaded in %u, PART2 in %u\n", label, boot,
           loaded - boot, frames - loaded);
    core_destroy(c);
    gcr_free(&g);
    free(img);
}

int main(void) {
    const char* dir = getenv("BB_TEST_ROM_DIR");
    static sysroms_t user;
    char err[256];
    if (!dir || !sysroms_load(&user, dir, err, sizeof(err)) || !user.has_1541) {
        printf("test_drive: skipped (set BB_TEST_ROM_DIR to a folder with a 1541 ROM)\n");
        return 77;
    }
    printf("test_drive: 1541 ROM %s (%s)\n", user.drive1541_sha1, user.drive1541_name ? user.drive1541_name : "unknown");
    static sysroms_t open;
    sysroms_load_default(&open);
    memcpy(open.drive1541, user.drive1541, sizeof(open.drive1541));
    open.has_1541 = true;
    test_load(&open, "Open ROMs");
    if (user.kernal_basic_source == SYSROMS_SOURCE_USER) test_load(&user, "real ROMs");
    if (failures == 0) printf("test_drive: all passed\n");
    return failures ? 1 : 0;
}
