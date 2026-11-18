// Code coverage (core_coverage*, debug/coverage.h): what ran as code under which
// memory configuration, the .bbcov files, and that tracing never changes emulation.
#include "core/core.h"
#include "debug/coverage.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static core_t* make_core(const sysroms_t* r) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = r->kernal, .rom_kernal_size = sizeof(r->kernal),
        .rom_basic = r->basic, .rom_basic_size = sizeof(r->basic),
        .rom_chargen = r->chargen, .rom_chargen_size = sizeof(r->chargen),
    });
}

static void test_core_tracing(const sysroms_t* r) {
    core_t* a = make_core(r);
    core_t* b = make_core(r);
    CHECK(core_coverage(a) == NULL, "tracing is off by default");
    CHECK(core_coverage_enable(a, true) && core_coverage(a), "enable");
    for (int i = 0; i < 120; i++) {
        core_run_frame(a);
        core_run_frame(b);
    }
    CHECK(core_fingerprint(a) == core_fingerprint(b), "tracing changed the emulation");
    CHECK(core_cycle_count(a) == 120ull * core_cycles_per_frame(a), "cycle count");
    const uint32_t* map = core_coverage(a);
    // At the BASIC prompt: the reset vector's target and KERNAL code ran with the
    // standard configuration (LORAM | HIRAM | CHAREN = 7, no cartridge).
    CHECK(core_memory_config(a) == 7, "memory config at the prompt: %d", core_memory_config(a));
    uint32_t kernal = 0, low = 0;
    for (uint32_t addr = 0; addr < CORE_COVERAGE_SIZE; addr++) {
        if (addr >= 0xE000 && (map[addr] & (1u << 7))) kernal++;
        if (addr >= 0x0200 && addr < 0x8000 && map[addr]) low++;
    }
    CHECK(kernal > 200, "KERNAL code traced under config 7: %u addresses", kernal);
    CHECK(low == 0, "%u addresses in RAM ran as code at the prompt", low);
    CHECK(coverage_count(map) >= kernal, "count");

    // A program in RAM shows up; reset keeps the map; clear empties it.
    const uint8_t prog[] = { 0xEE, 0x20, 0xD0, 0x4C, 0x00, 0xC0 };  // $C000: INC $D020 / JMP $C000
    for (int i = 0; i < 6; i++) core_ram_write(a, (uint16_t)(0xC000 + i), prog[i]);
    core_ram_write(a, 0x0318, 0x00);   // NMI vector -> $C000, then RESTORE
    core_ram_write(a, 0x0319, 0xC0);
    core_key_down(a, CORE_KEY_RESTORE);
    for (int i = 0; i < 5; i++) core_run_frame(a);
    core_key_up(a, CORE_KEY_RESTORE);
    CHECK(map[0xC000] && map[0xC003] && !map[0xC001] && !map[0xC002], "opcode fetches only: %x %x %x",
          map[0xC000], map[0xC001], map[0xC003]);
    core_reset(a);
    CHECK(core_coverage(a) == map && map[0xC000], "reset keeps the map");
    core_coverage_clear(a);
    CHECK(coverage_count(core_coverage(a)) == 0, "clear");
    core_coverage_enable(a, false);
    CHECK(core_coverage(a) == NULL, "disable");
    core_destroy(a);
    core_destroy(b);
}

static void test_files(void) {
    const char* sha = "a6f075fc73fe7cc4fd35ab2232800570fd33b80c";
    const char* other = "0000000000000000000000000000000000000001";
    char path[256], err[512];
    CHECK(!coverage_path("cov", "nothex", path, sizeof(path)), "bad SHA-1 accepted");
    CHECK(coverage_path("test_coverage_dir", sha, path, sizeof(path)), "path");
    remove(path);
    uint32_t* m1 = calloc(CORE_COVERAGE_SIZE, 4);
    uint32_t* m2 = calloc(CORE_COVERAGE_SIZE, 4);
    uint32_t* back = calloc(CORE_COVERAGE_SIZE, 4);
    m1[0x1000] = 1u << 7;
    m1[0xFFFF] = 0x80000001u;
    m2[0x1000] = 1u << 3;
    m2[0x2000] = 1u << 31;
    char got[41];
    CHECK(!coverage_load(path, got, back, err, sizeof(err)) && !err[0], "missing file: %s", err);
    CHECK(coverage_merge_save(path, sha, m1, err, sizeof(err)), "save: %s", err);
    CHECK(coverage_merge_save(path, sha, m2, err, sizeof(err)), "merge: %s", err);
    CHECK(coverage_load(path, got, back, err, sizeof(err)) && !strcmp(got, sha), "load: %s", err);
    CHECK(back[0x1000] == ((1u << 7) | (1u << 3)) && back[0x2000] == 1u << 31 && back[0xFFFF] == 0x80000001u,
          "merged values");
    CHECK(coverage_count(back) == 3, "count %u", coverage_count(back));
    CHECK(!coverage_merge_save(path, other, m1, err, sizeof(err)) && err[0], "another game's file overwritten");
    FILE* f = fopen(path, "wb");
    if (f) { fputs("BBCOV1\n", f); fclose(f); }
    CHECK(!coverage_load(path, got, back, err, sizeof(err)) && err[0], "damaged file accepted");
    CHECK(!coverage_merge_save(path, sha, m1, err, sizeof(err)), "damaged file overwritten");
    remove(path);
    free(m1);
    free(m2);
    free(back);
}

int main(void) {
    static sysroms_t r;
    sysroms_load_default(&r);
    test_core_tracing(&r);
    test_files();
    if (failures == 0) printf("test_coverage: all passed\n");
    return failures ? 1 : 0;
}
