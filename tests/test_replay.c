// Diagnostic replays: record typed input on a booting machine, play it back on a
// fresh one, and check that the file round-trips and divergence is detected.
#include "core/core.h"
#include "replay/replay.h"
#include "rom/sysroms.h"

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

#define FRAMES 400

// Scripted input: type "HELLO" after boot (press and release over several frames),
// and wiggle the joystick.
static void script(uint32_t f, replay_input_t* in) {
    memset(in, 0, sizeof(*in));
    static const char text[] = "HELLO";
    if (f >= 150 && f < 150 + 10 * (sizeof(text) - 1)) {
        const uint32_t i = (f - 150) / 10, phase = (f - 150) % 10;
        if (phase == 0 || phase == 4) {
            in->keys[0].key = (uint8_t)text[i];
            in->keys[0].down = phase == 0;
            in->nkeys = 1;
        }
    }
    in->joy[1] = (f / 30) % 2 ? CORE_JOY_FIRE : 0;
}

static void record(replay_t* r) {
    replay_header_t h = { 0 };
    snprintf(h.launcher_version, sizeof(h.launcher_version), "0.3.0");
    snprintf(h.game_id, sizeof(h.game_id), "test");
    snprintf(h.rom_name, sizeof(h.rom_name), "Typing \"test\"\tname");
    snprintf(h.rom_sha1, sizeof(h.rom_sha1), "%s", "0123456789abcdef0123456789abcdef01234567");
    snprintf(h.media, sizeof(h.media), "cartridge");
    h.ntsc = false;
    snprintf(h.sysrom_sha1[0], 41, "%040d", 1);
    h.mod_count = 2;
    snprintf(h.mods[0], sizeof(h.mods[0]), "base@%040d", 2);
    snprintf(h.mods[1], sizeof(h.mods[1]), "addon@%040d", 3);
    replay_init(r, &h);
    core_t* c = make_core();
    for (uint32_t f = 0; f < FRAMES; f++) {
        replay_input_t in;
        script(f, &in);
        replay_input_apply(c, &in);
        core_run_frame(c);
        CHECK(replay_add(r, &in, core_fingerprint(c)), "add frame %u", f);
    }
    // the typing reached the screen
    bool found = false;
    for (int a = 0x400; a < 0x7E8 - 5 && !found; a++) {
        found = core_ram_read(c, (uint16_t)a) == 8 && core_ram_read(c, (uint16_t)(a + 1)) == 5 &&
                core_ram_read(c, (uint16_t)(a + 4)) == 15;
    }
    CHECK(found, "HELLO not on screen: the script didn't type");
    core_destroy(c);
}

// Play back on a fresh core; returns the player state.
static replay_player_t play(const replay_t* r) {
    replay_player_t p;
    replay_player_init(&p, r);
    core_t* c = make_core();
    replay_input_t in;
    while (replay_player_input(&p, &in)) {
        replay_input_apply(c, &in);
        core_run_frame(c);
        replay_player_check(&p, core_fingerprint(c));
    }
    core_destroy(c);
    return p;
}

int main(void) {
    sysroms_load_default(&roms);
    static replay_t r;
    record(&r);
    CHECK(r.count == FRAMES, "frames %u", r.count);

    // fingerprints change while the machine runs (not a constant)
    CHECK(r.fingerprints[10] != r.fingerprints[300], "fingerprint doesn't change");

    replay_player_t p = play(&r);
    CHECK(replay_player_done(&p) && !p.diverged, "identical playback diverged at %u", p.diverged_at);

    // file round trip
    size_t n = 0;
    uint8_t* data = replay_serialize(&r, &n);
    CHECK(data && n > 0, "serialize");
    static replay_t back;
    char err[256];
    CHECK(replay_parse(data, n, &back, err, sizeof(err)), "parse: %s", err);
    CHECK(back.count == r.count && !memcmp(back.fingerprints, r.fingerprints, r.count * sizeof(uint64_t)), "frames round trip");
    bool inputs_same = true;
    for (uint32_t i = 0; i < r.count; i++) {
        inputs_same &= back.inputs[i].joy[0] == r.inputs[i].joy[0] && back.inputs[i].joy[1] == r.inputs[i].joy[1] &&
                       back.inputs[i].nkeys == r.inputs[i].nkeys &&
                       (r.inputs[i].nkeys == 0 || (back.inputs[i].keys[0].key == r.inputs[i].keys[0].key &&
                                                   back.inputs[i].keys[0].down == r.inputs[i].keys[0].down));
    }
    CHECK(inputs_same, "inputs round trip");
    CHECK(!strcmp(back.header.rom_name, r.header.rom_name) && !strcmp(back.header.rom_sha1, r.header.rom_sha1) &&
          back.header.mod_count == 2 && !strcmp(back.header.mods[1], r.header.mods[1]) && !back.header.ntsc &&
          !strcmp(back.header.sysrom_sha1[0], r.header.sysrom_sha1[0]), "header round trip");
    p = play(&back);
    CHECK(!p.diverged, "loaded replay diverged");
    replay_free(&back);

    // damaged files fail cleanly
    for (size_t cut = 0; cut < n; cut += (cut < 300 ? 1 : 97)) {
        static replay_t t;
        if (replay_parse(data, cut, &t, err, sizeof(err))) {
            CHECK(false, "truncated replay (%zu of %zu bytes) accepted", cut, n);
            replay_free(&t);
        }
    }
    data[0] = 'X';
    CHECK(!replay_parse(data, n, &back, err, sizeof(err)), "bad magic accepted");
    free(data);

    // a changed input is caught at (or right after) that frame
    static replay_t changed;
    data = replay_serialize(&r, &n);
    CHECK(replay_parse(data, n, &changed, err, sizeof(err)), "parse 2: %s", err);
    free(data);
    changed.inputs[170].nkeys = 0;    // drop the press of 'E'
    changed.inputs[174].nkeys = 0;    // and its release
    p = play(&changed);
    CHECK(p.diverged && p.diverged_at >= 170 && p.diverged_at < 180, "divergence at %u (expected 170-179)", p.diverged_at);
    replay_free(&changed);

    // file save/load
    char path[] = "test_replay.tmp.bbreplay";
    CHECK(replay_save(&r, path, err, sizeof(err)), "save: %s", err);
    static replay_t loaded;
    CHECK(replay_load(path, &loaded, err, sizeof(err)) && loaded.count == r.count, "load: %s", err);
    replay_free(&loaded);
    remove(path);

    replay_free(&r);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("replay: all tests passed\n");
    return 0;
}
