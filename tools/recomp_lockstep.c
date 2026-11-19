// Dev tool (R3, workspace docs/RECOMP_PLAN.md): runs the built-in recompiled code
// next to the interpreter on the same seeded random input and compares every frame
// (fingerprint and picture). Built only with BB_RECOMP_SOURCES, on request:
//   cmake --build <dir> --target recomp_lockstep
// usage: recomp_lockstep GAME.crt [--frames N] [--port 1|2] [--ntsc] [--seed S] [--key-every N]
//   --key-every N  also tap F1 every N frames (games that start with F1)
#include "core/cart.h"
#include "core/core.h"
#include "core/recomp_abi.h"
#include "rom/rom_file.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const bb_rc_program* bb_rc_get_program(void);

static bool same_picture(const core_t* a, const core_t* b) {
    const core_video_t va = core_video(a), vb = core_video(b);
    return !memcmp(va.pixels, vb.pixels, (size_t)va.width * (size_t)va.height);
}

int main(int argc, char** argv) {
    const char* path = NULL;
    long frames = 20000, key_every = 0;
    int port = 2;
    bool ntsc = false;
    unsigned long seed = 1;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--frames") && v) { frames = atol(v); i++; }
        else if (!strcmp(a, "--port") && v) { port = atoi(v); i++; }
        else if (!strcmp(a, "--seed") && v) { seed = strtoul(v, NULL, 0); i++; }
        else if (!strcmp(a, "--key-every") && v) { key_every = atol(v); i++; }
        else if (!strcmp(a, "--ntsc")) ntsc = true;
        else if (a[0] != '-' && !path) path = a;
        else { fprintf(stderr, "usage: recomp_lockstep GAME.crt [--frames N] [--port 1|2] [--ntsc] [--seed S] [--key-every N]\n"); return 2; }
    }
    if (!path) return 2;
    char err[256];
    static cart_t cart;
    if (!rom_load_cart(path, 0, &cart, err, sizeof(err))) { fprintf(stderr, "%s\n", err); return 1; }
    static sysroms_t roms;
    sysroms_load_default(&roms);
    core_t* c[2];
    for (int k = 0; k < 2; k++) {
        c[k] = core_create(&(core_desc_t){
            .video_std = ntsc ? CORE_VIDEO_NTSC : CORE_VIDEO_PAL,
            .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
            .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
            .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
        });
        if (!c[k]) return 1;
        core_insert_cart(c[k], &cart);
    }
    if (!core_set_recomp(c[1], bb_rc_get_program(), err, sizeof(err))) {
        fprintf(stderr, "recompiled code refused: %s\n", err);
        return 1;
    }
    uint32_t rng = (uint32_t)seed | 1u;
    uint8_t joy = 0;
    for (long f = 0; f < frames; f++) {
        if (f % 20 == 0) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; joy = (uint8_t)(rng & 0x1F); }
        for (int k = 0; k < 2; k++) {
            core_set_joystick(c[k], port, joy);
            if (key_every && f % key_every == 0) core_key_down(c[k], CORE_KEY_F1);
            if (key_every && f % key_every == 5) core_key_up(c[k], CORE_KEY_F1);
            core_run_frame(c[k]);
        }
        if (core_fingerprint(c[0]) != core_fingerprint(c[1]) || !same_picture(c[0], c[1])) {
            printf("DIFFERENT at frame %ld\n", f);
            return 1;
        }
    }
    const uint64_t total = core_cycle_count(c[1]);
    printf("all %ld frames identical; recompiled code ran %.1f%% of %llu cycles\n", frames,
           100.0 * (double)core_recomp_cycles(c[1]) / (double)total, (unsigned long long)total);
    core_destroy(c[0]);
    core_destroy(c[1]);
    return 0;
}
