// Save state files: round trip through a real machine, damaged files, and the
// compatibility check that decides whether a state may be loaded.
#include "core/core.h"
#include "rom/sysroms.h"
#include "state/savestate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static sysroms_t roms;

static core_t* make_core(void) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

static savestate_header_t header(void) {
    savestate_header_t h;
    memset(&h, 0, sizeof(h));
    snprintf(h.launcher_version, sizeof(h.launcher_version), "0.4.0");
    snprintf(h.layout, sizeof(h.layout), "%016llx", (unsigned long long)core_state_layout());
    snprintf(h.game_id, sizeof(h.game_id), "test");
    snprintf(h.rom_name, sizeof(h.rom_name), "A \"quoted\"\tname");
    snprintf(h.rom_sha1, sizeof(h.rom_sha1), "0123456789abcdef0123456789abcdef01234567");
    snprintf(h.media, sizeof(h.media), "cartridge");
    for (int i = 0; i < 3; i++) snprintf(h.sysrom_sha1[i], 41, "%040d", i + 1);
    snprintf(h.patches, sizeof(h.patches), "fix@%040d", 9);
    snprintf(h.saved_at, sizeof(h.saved_at), "2026-11-17 19:40");
    h.frame = 0x1234567890ull;
    return h;
}

int main(void) {
    sysroms_load_default(&roms);
    const size_t n = core_state_size();
    uint8_t* st = malloc(n);
    core_t* a = make_core();
    for (int f = 0; f < 300; f++) core_run_frame(a);
    CHECK(core_save_state(a, st, n), "core save");

    // file round trip
    const savestate_header_t h = header();
    const char* path = "test_state.tmp.bbstate";
    char err[512];
    CHECK(savestate_save(path, &h, st, n, err, sizeof(err)), "save: %s", err);
    savestate_header_t back;
    uint8_t* st2 = NULL;
    size_t n2 = 0;
    CHECK(savestate_load(path, &back, &st2, &n2, err, sizeof(err)), "load: %s", err);
    CHECK(st2 && n2 == n && !memcmp(st, st2, n), "state bytes round trip");
    CHECK(!strcmp(back.rom_name, h.rom_name) && !strcmp(back.patches, h.patches) && back.frame == h.frame &&
          !strcmp(back.layout, h.layout) && !strcmp(back.sysrom_sha1[2], h.sysrom_sha1[2]) &&
          !strcmp(back.saved_at, h.saved_at) && back.ntsc == h.ntsc && !strcmp(back.media, h.media) &&
          !strcmp(back.launcher_version, h.launcher_version) && !strcmp(back.game_id, h.game_id), "header round trip");
    // header only
    savestate_header_t only;
    CHECK(savestate_load(path, &only, NULL, NULL, err, sizeof(err)) && only.frame == h.frame, "header only: %s", err);

    // the loaded bytes continue the machine exactly
    core_t* b = make_core();
    CHECK(st2 && core_load_state(b, st2, n2), "core load");
    for (int f = 0; f < 50; f++) { core_run_frame(a); core_run_frame(b); }
    CHECK(core_fingerprint(a) == core_fingerprint(b), "machines differ after loading the file");
    free(st2);
    remove(path);

    // damaged files fail cleanly
    size_t fsize = 0;
    uint8_t* file = savestate_serialize(&h, st, n, &fsize);
    CHECK(file != NULL, "serialize");
    for (size_t cut = 0; file && cut < fsize; cut += (cut < 2000 ? 1 : 4099)) {
        savestate_header_t t;
        uint8_t* ts = NULL;
        size_t tn = 0;
        if (savestate_parse(file, cut, &t, &ts, &tn, err, sizeof(err))) {
            CHECK(false, "truncated state (%zu of %zu bytes) accepted", cut, fsize);
            free(ts);
        }
    }
    if (file) {
        file[fsize / 2] ^= 0x40;   // a flipped bit in the machine data
        savestate_header_t t;
        uint8_t* ts = NULL;
        size_t tn = 0;
        CHECK(!savestate_parse(file, fsize, &t, &ts, &tn, err, sizeof(err)) && strstr(err, "checksum"),
              "corrupted state accepted (%s)", err);
        file[fsize / 2] ^= 0x40;
        file[0] = 'X';
        CHECK(!savestate_parse(file, fsize, &t, NULL, NULL, err, sizeof(err)), "bad magic accepted");
    }
    free(file);

    // compatibility
    char why[512];
    savestate_header_t now = h;
    CHECK(savestate_compatible(&h, &now, why, sizeof(why)), "identical machine refused: %s", why);
    now = h; now.layout[0] = now.layout[0] == '0' ? '1' : '0';
    CHECK(!savestate_compatible(&h, &now, why, sizeof(why)) && strstr(why, "0.4.0"), "other build: %s", why);
    now = h; now.rom_sha1[0] = 'f';
    CHECK(!savestate_compatible(&h, &now, why, sizeof(why)) && strstr(why, "different cartridge"), "other ROM: %s", why);
    now = h; now.ntsc = true;
    CHECK(!savestate_compatible(&h, &now, why, sizeof(why)) && strstr(why, "PAL"), "video standard: %s", why);
    now = h; now.sysrom_sha1[1][0] = 'x';
    CHECK(!savestate_compatible(&h, &now, why, sizeof(why)) && strstr(why, "system ROMs"), "system ROMs: %s", why);
    now = h; now.patches[0] = 0;
    CHECK(!savestate_compatible(&h, &now, why, sizeof(why)) && strstr(why, "more patch mods"), "patches: %s", why);

    core_destroy(a);
    core_destroy(b);
    free(st);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("state: all tests passed\n");
    return 0;
}
