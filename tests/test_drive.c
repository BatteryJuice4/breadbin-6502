// The emulated 1541 with the real DOS: loading and saving over the serial bus,
// with no LOAD trap. Needs a 1541 ROM, which can't be committed: set
// BB_TEST_ROM_DIR to a folder with 1541.rom (or 1541-c000.rom + 1541-e000.rom),
// optionally with real kernal/basic/chargen ROMs too. Without it the test reports
// "skipped" (77).
#include "core/core.h"
#include "media/autostart.h"
#include "media/d64.h"
#include "media/gcr.h"
#include "media/media.h"
#include "rom/sysroms.h"
#include "test_images.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

#define KERNAL_CHRIN 0xFFCF

static core_t* make_core(const sysroms_t* roms) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms->kernal, .rom_kernal_size = sizeof(roms->kernal),
        .rom_basic = roms->basic, .rom_basic_size = sizeof(roms->basic),
        .rom_chargen = roms->chargen, .rom_chargen_size = sizeof(roms->chargen),
        .rom_1541 = roms->drive1541, .rom_1541_size = sizeof(roms->drive1541),
    });
}

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

// Boot to the BASIC prompt and give the drive time to finish its own reset.
static bool boot(core_t* c, int chrin, uint32_t* frames) {
    core_reset(c);
    for (uint32_t i = 0; i < 600; i++, (*frames)++) {
        core_run_frame(c);
        if (core_watch_hits(c, chrin) > 0 && i >= 100) return true;
    }
    return false;
}

// Run a typed drive command to its end: the drive's motor has run and BASIC
// reads input again (LOAD/SAVE never read input themselves).
static bool drive_command(core_t* c, int chrin, const char* text, uint32_t* frames, bool* led_seen) {
    type(c, text, frames);
    bool motor_seen = false;
    uint32_t prompts = 0;
    for (uint32_t i = 0; i < 6000; i++, (*frames)++) {
        core_run_frame(c);
        if (led_seen) *led_seen |= core_drive_led(c);
        if (core_drive_motor(c) && !motor_seen) {
            motor_seen = true;
            prompts = core_watch_hits(c, chrin);
        }
        if (motor_seen && core_watch_hits(c, chrin) > prompts) return true;
    }
    return false;
}

// Is `name` in the directory as the DOS wrote it on the GCR surface?
static bool on_disk(const core_disk_t* disk, const char* name) {
    uint8_t s[256];
    int t = 18, sec = 1;
    for (int guard = 0; t != 0 && guard < 18; guard++) {
        if (!gcr_read_sector(disk, t, sec, s)) return false;
        for (int e = 0; e < 8; e++) {
            const uint8_t* d = s + e * 32;
            if (d[2] == 0) continue;
            int len = 0;
            while (len < 16 && d[5 + len] != 0xA0) len++;
            if (len == (int)strlen(name) && !memcmp(d + 5, name, (size_t)len)) return true;
        }
        t = s[0];
        sec = s[1];
    }
    return false;
}

static uint8_t* test_disk(void) {
    static uint8_t p1[128];
    const size_t n1 = make_part1(p1, 8);
    const dfile_t files[] = { { "PART1", p1, n1, 0 }, { "PART2", part2, sizeof(part2), 0 } };
    return make_d64(files, 2, D64_SIZE);
}

// LOAD and RUN by hand; the program then loads PART2 itself; then SAVE a new
// file and load it back.
static void test_load_save(const sysroms_t* roms, const char* label) {
    core_t* c = make_core(roms);
    CHECK(c && core_has_drive(c), "%s: core with a drive", label);
    if (!c) return;
    uint8_t* img = test_disk();
    d64_t d;
    char err[128];
    gcr_image_t g;
    CHECK(d64_open(img, D64_SIZE, &d, err, sizeof(err)) && gcr_from_d64(&d, &g, err, sizeof(err)), "%s: %s", label, err);
    core_insert_disk(c, &g.disk);
    const int chrin = core_add_watch(c, KERNAL_CHRIN);

    uint32_t frames = 0;
    CHECK(boot(c, chrin, &frames), "%s: no BASIC prompt", label);
    const uint32_t booted = frames;
    bool led_seen = false;
    CHECK(drive_command(c, chrin, "LOAD\"PART1\",8,1\r", &frames, &led_seen), "%s: LOAD didn't finish", label);
    const uint32_t loaded = frames;
    CHECK(led_seen, "%s: drive LED never on", label);
    CHECK(core_ram_read(c, 0x0801) == 0x0B && core_ram_read(c, 0x0805) == 0x9E, "%s: PART1 not in memory", label);
    type(c, "RUN\r", &frames);
    for (uint32_t i = 0; i < 3000 && core_ram_read(c, 0xC001) == 0; i++, frames++) core_run_frame(c);
    CHECK(core_ram_read(c, 0xC000) == 0x42, "%s: PART1 didn't run", label);
    CHECK(core_ram_read(c, 0xC001) == 0x77, "%s: PART2 load from the drive failed ($C001 = $%02X)", label,
          core_ram_read(c, 0xC001));
    CHECK(core_files_loaded(c) == 0, "%s: the LOAD trap served a file", label);
    CHECK(!g.disk.dirty, "%s: loading wrote to the disk", label);
    printf("test_drive: %s: prompt after %u frames, PART1 loaded in %u, PART2 in %u\n", label, booted,
           loaded - booted, frames - loaded);
    if (failures) dump_screen(c);

    // Write: a BASIC line saved as a new file (the DOS allocates, writes the data
    // blocks, the directory entry and the BAM), then read back after NEW.
    const int before = failures;
    frames = 0;
    CHECK(boot(c, chrin, &frames), "%s: no prompt after reset", label);
    type(c, "10 PRINT\"HI\"\r", &frames);
    CHECK(drive_command(c, chrin, "SAVE\"NEWFILE\",8\r", &frames, NULL), "%s: SAVE didn't finish", label);
    const uint32_t saved = frames;
    CHECK(g.disk.dirty, "%s: SAVE didn't mark the disk written", label);
    CHECK(on_disk(&g.disk, "NEWFILE"), "%s: NEWFILE isn't in the directory on the disk", label);
    CHECK(on_disk(&g.disk, "PART1") && on_disk(&g.disk, "PART2"), "%s: SAVE damaged the directory", label);
    type(c, "NEW\r", &frames);
    CHECK(core_ram_read(c, 0x0801) == 0 && core_ram_read(c, 0x0802) == 0, "%s: NEW didn't clear the program", label);
    CHECK(drive_command(c, chrin, "LOAD\"NEWFILE\",8\r", &frames, NULL), "%s: LOAD NEWFILE didn't finish", label);
    // 10 PRINT"HI": link (2), line number 10 (2), PRINT token $99, '"'
    CHECK(core_ram_read(c, 0x0803) == 10 && core_ram_read(c, 0x0805) == 0x99 && core_ram_read(c, 0x0806) == '"',
          "%s: NEWFILE didn't load back", label);
    printf("test_drive: %s: SAVE took %u frames, loaded back in %u\n", label, saved, frames - saved);
    if (failures != before) dump_screen(c);
    core_destroy(c);
    gcr_free(&g);
    free(img);
}

// The launcher's way: media_open builds the surface, autostart types LOAD/RUN and
// decides when loading is over.
static void test_autostart(const sysroms_t* roms) {
    core_t* c = make_core(roms);
    if (!c) return;
    media_image_t m;
    char err[128];
    CHECK(media_open(&m, MEDIA_FMT_D64, test_disk(), D64_SIZE, err, sizeof(err)), "media_open: %s", err);
    m.use_drive = true;
    core_set_file_loader(c, media_serve, &m);
    core_insert_disk(c, media_disk(&m));
    core_reset(c);
    autostart_t a;
    autostart_begin(&a, c, m.fmt, NULL);
    uint32_t frames = 0;
    while (autostart_loading(&a) && frames < 20000) {
        core_run_frame(c);
        autostart_frame(&a, c);
        frames++;
    }
    for (int i = 0; i < 25; i++) core_run_frame(c);
    CHECK(core_ram_read(c, 0xC000) == 0x42 && core_ram_read(c, 0xC001) == 0x77,
          "autostart: $C000 = $%02X, $C001 = $%02X after %u frames", core_ram_read(c, 0xC000),
          core_ram_read(c, 0xC001), frames);
    CHECK(frames < 3000, "autostart took %u frames", frames);
    CHECK(core_files_loaded(c) == 0, "autostart: the LOAD trap served a file");
    if (failures) dump_screen(c);
    printf("test_drive: autostart finished after %u frames\n", frames);
    media_close(&m);
    core_destroy(c);
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
    test_load_save(&open, "Open ROMs");
    if (user.kernal_basic_source == SYSROMS_SOURCE_USER) test_load_save(&user, "real ROMs");
    test_autostart(&open);
    if (failures == 0) printf("test_drive: all passed\n");
    return failures ? 1 : 0;
}
