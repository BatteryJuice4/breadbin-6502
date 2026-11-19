// The recompiled-code bridge (core_set_recomp, recomp_abi.h) on the real machine.
// A tiny hand-written "program" for a test cartridge stands in for breadbin-recomp's
// output: its functions follow the interpreter's cycle steps (chips' m6502.h) for the
// five opcodes used. With the screen and all sprites on, VIC DMA stalls the CPU (RDY)
// all the time. A core running the program must match a plain core every frame
// (fingerprint and picture), and the program must refuse other cartridge data.
#include "core/cart.h"
#include "core/core.h"
#include "core/recomp_abi.h"
#include "rom/sha1.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

// ---- the "generated" code ------------------------------------------------------
// $8009 lda #$1b / sta $d011 / lda #$ff / sta $d015
// $8013 inc $d020 / lda $d012 / jmp $8013
static const uint8_t code[] = {
    0xA9, 0x1B, 0x8D, 0x11, 0xD0, 0xA9, 0xFF, 0x8D, 0x15, 0xD0,
    0xEE, 0x20, 0xD0, 0xAD, 0x12, 0xD0, 0x4C, 0x13, 0x80,
};

#define SA(a) pins = (pins & ~0xFFFFull) | ((a) & 0xFFFFull)
#define SD(d) pins = (pins & ~0xFF0000ull) | ((uint64_t)((d) & 0xFF) << 16)
#define GD() ((uint8_t)(pins >> 16))
#define RD() pins |= BB_RC_RW
#define WR() pins &= ~BB_RC_RW
#define CYCLE() pins = bb_rc_cycle(r, pins)
#define FETCH(a) do { SA(a); pins |= BB_RC_SYNC; r->pins = bb_rc_cycle(r, pins); return; } while (0)
#define NZ(v) r->cpu.P = (uint8_t)((r->cpu.P & ~(BB_RC_NF | BB_RC_ZF)) | (((v) & 0xFF) ? ((v) & BB_RC_NF) : BB_RC_ZF))
#define BEGIN uint64_t pins = r->pins & ~BB_RC_SYNC

static void lda_imm(bb_rc* r, uint16_t pc) {        // pc = opcode address
    BEGIN;
    RD(); SA(pc + 1); CYCLE();
    RD(); r->cpu.A = GD(); NZ(r->cpu.A); FETCH(pc + 2);
}
static void sta_abs(bb_rc* r, uint16_t pc) {
    BEGIN;
    RD(); SA(pc + 1); CYCLE();
    RD(); SA(pc + 2); uint8_t lo = GD(); CYCLE();
    RD(); SA((GD() << 8) | lo); SD(r->cpu.A); WR(); CYCLE();
    RD(); FETCH(pc + 3);
}
static void inc_abs(bb_rc* r, uint16_t pc) {
    BEGIN;
    RD(); SA(pc + 1); CYCLE();
    RD(); SA(pc + 2); uint8_t lo = GD(); CYCLE();
    RD(); SA((GD() << 8) | lo); CYCLE();
    RD(); uint8_t v = GD(); WR(); CYCLE();          // dummy write of the old value
    RD(); v++; NZ(v); SD(v); WR(); CYCLE();
    RD(); FETCH(pc + 3);
}
static void lda_abs(bb_rc* r, uint16_t pc) {
    BEGIN;
    RD(); SA(pc + 1); CYCLE();
    RD(); SA(pc + 2); uint8_t lo = GD(); CYCLE();
    RD(); SA((GD() << 8) | lo); CYCLE();
    RD(); r->cpu.A = GD(); NZ(r->cpu.A); FETCH(pc + 3);
}
static void jmp_abs(bb_rc* r, uint16_t pc) {
    BEGIN;
    RD(); SA(pc + 1); CYCLE();
    RD(); SA(pc + 2); uint8_t lo = GD(); CYCLE();
    RD(); FETCH((GD() << 8) | lo);
}

static void f8009(bb_rc* r) { lda_imm(r, 0x8009); }
static void f800b(bb_rc* r) { sta_abs(r, 0x800B); }
static void f800e(bb_rc* r) { lda_imm(r, 0x800E); }
static void f8010(bb_rc* r) { sta_abs(r, 0x8010); }
static void f8013(bb_rc* r) { inc_abs(r, 0x8013); }
static void f8016(bb_rc* r) { lda_abs(r, 0x8016); }
static void f8019(bb_rc* r) { jmp_abs(r, 0x8019); }

static const uint16_t addrs[] = { 0x8009, 0x800B, 0x800E, 0x8010, 0x8013, 0x8016, 0x8019 };
static const uint8_t ops[] = { 0xA9, 0x8D, 0xA9, 0x8D, 0xEE, 0xAD, 0x4C };
static const bb_rc_fn fns[] = { f8009, f800b, f800e, f8010, f8013, f8016, f8019 };

// --------------------------------------------------------------------------------
static sysroms_t roms;

static core_t* make_core(core_video_std_t std) {
    return core_create(&(core_desc_t){
        .video_std = std,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

static bool same_picture(const core_t* a, const core_t* b) {
    const core_video_t va = core_video(a), vb = core_video(b);
    return va.width == vb.width && va.height == vb.height &&
           !memcmp(va.pixels, vb.pixels, (size_t)va.width * (size_t)va.height);
}

static void lockstep(core_video_std_t std, const cart_t* cart, const bb_rc_program* prog) {
    core_t* a = make_core(std);
    core_t* b = make_core(std);
    core_insert_cart(a, cart);
    core_insert_cart(b, cart);
    char why[256] = "";
    CHECK(core_set_recomp(b, prog, why, sizeof(why)), "program refused: %s", why);
    CHECK(core_recomp_active(b), "not active");
    const int frames = 300;
    for (int f = 0; f < frames; f++) {
        core_run_frame(a);
        core_run_frame(b);
        if (core_fingerprint(a) != core_fingerprint(b) || !same_picture(a, b)) {
            CHECK(false, "%s: frame %d differs", std == CORE_VIDEO_NTSC ? "NTSC" : "PAL", f);
            break;
        }
    }
    const uint64_t total = (uint64_t)frames * core_cycles_per_frame(b);
    const uint64_t rc = core_recomp_cycles(b);
    printf("%s: generated code ran %llu of %llu cycles (%.1f%%)\n", std == CORE_VIDEO_NTSC ? "NTSC" : "PAL",
           (unsigned long long)rc, (unsigned long long)total, 100.0 * (double)rc / (double)total);
    CHECK(rc > total / 2, "generated code barely ran");
    // A reset keeps the program and starts the same way again.
    core_reset(a);
    core_reset(b);
    CHECK(core_recomp_cycles(b) == 0 && core_recomp_active(b), "reset");
    for (int f = 0; f < 20; f++) { core_run_frame(a); core_run_frame(b); }
    CHECK(core_fingerprint(a) == core_fingerprint(b), "after reset");
    core_destroy(a);
    core_destroy(b);
}

int main(void) {
    sysroms_load_default(&roms);
    static uint8_t img[0x2000];
    memset(img, 0xEA, sizeof(img));
    const uint8_t hdr[] = { 0x09, 0x80, 0x09, 0x80, 0xC3, 0xC2, 0xCD, '8', '0' };
    memcpy(img, hdr, sizeof(hdr));
    memcpy(img + 9, code, sizeof(code));
    static cart_t cart;
    char err[256];
    CHECK(cart_from_bin(img, sizeof(img), 0x8000, &cart, err, sizeof(err)), "cart: %s", err);
    char sha[41];
    sha1_hex(img, sizeof(img), sha);
    const bb_rc_program prog = { BB_RC_ABI_VERSION, sha, "test", 7, addrs, ops, fns };

    lockstep(CORE_VIDEO_PAL, &cart, &prog);
    lockstep(CORE_VIDEO_NTSC, &cart, &prog);

    // Refusals: other cartridge data (as with a patch mod), ABI version, no cartridge.
    char why[256];
    core_t* c = make_core(CORE_VIDEO_PAL);
    CHECK(!core_set_recomp(c, &prog, why, sizeof(why)) && strstr(why, "no cartridge"), "no cart: %s", why);
    static cart_t patched;
    patched = cart;
    patched.roml[0x14] ^= 1;    // operand of sta $d011
    core_insert_cart(c, &patched);
    CHECK(!core_recomp_active(c), "patched cart accepted");
    CHECK(!core_set_recomp(c, &prog, why, sizeof(why)) && strstr(why, "isn't the ROM"), "patched: %s", why);
    for (int f = 0; f < 10; f++) core_run_frame(c);
    CHECK(core_recomp_cycles(c) == 0, "ran on patched data");
    core_insert_cart(c, &cart);
    CHECK(core_recomp_active(c), "re-inserting the matching cart enables it");
    bb_rc_program bad = prog;
    bad.abi = BB_RC_ABI_VERSION + 1;
    CHECK(!core_set_recomp(c, &bad, why, sizeof(why)) && strstr(why, "ABI"), "abi: %s", why);
    CHECK(core_set_recomp(c, NULL, why, sizeof(why)) && !core_recomp_active(c), "remove");
    core_destroy(c);

    if (!failures) printf("test_recomp: all passed\n");
    return failures ? 1 : 0;
}
