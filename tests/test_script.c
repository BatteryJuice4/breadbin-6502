// Script mods: the Lua host (mods/script.c) on a real core with the Open ROMs.
#include "core/core.h"
#include "mods/script.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static sysroms_t roms;

static core_t* make_core(void) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

// One frame the way main.c's step_frame runs it.
static void frame(scriptset_t* s, core_t* core, uint8_t joy[2]) {
    scriptset_before_frame(s, core, joy);
    core_set_joystick(core, 1, joy[0]);
    core_set_joystick(core, 2, joy[1]);
    core_run_frame(core);
    scriptset_after_frame(s, core);
}

static bool add(scriptset_t* s, core_t* core, const char* id, const char* src, char* err, size_t es) {
    return scriptset_add(s, core, id, "main.lua", src, strlen(src), err, es);
}

static void drain_log(scriptset_t* s, char* last, size_t size) {
    char line[256];
    while (scriptset_take_log(s, line, sizeof(line))) snprintf(last, size, "%s", line);
}

static void test_basics(void) {
    core_t* core = make_core();
    scriptset_t* s = scriptset_create();
    char err[256];
    uint8_t joy[2] = { 0, 0 };
    const char* src =
        "local n = 0\n"
        "bb.write(0xC000, 0x42)\n"                      // main chunk runs at start
        "function on_frame()\n"
        "  n = n + 1\n"
        "  bb.write(0xC001, n % 256)\n"
        "  if n == 3 then bb.set_joystick(2, bb.FIRE | bb.LEFT) end\n"
        "  bb.text(8, 16, 'frame ' .. n, 0x00FF00)\n"
        "end\n"
        "function on_after_frame()\n"
        "  bb.write(0xC002, bb.read(0xC001))\n"
        "  bb.write(0xC003, bb.joystick(2))\n"
        "  if n == 2 then print('two', bb.read16(0xC000)) end\n"
        "end\n";
    CHECK(add(s, core, "basic", src, err, sizeof(err)), "start: %s", err);
    CHECK(core_ram_read(core, 0xC000) == 0x42, "main chunk ran");
    for (int i = 0; i < 3; i++) frame(s, core, joy);
    CHECK(core_ram_read(core, 0xC001) == 3 && core_ram_read(core, 0xC002) == 3, "hooks ran each frame");
    CHECK(joy[1] == (CORE_JOY_FIRE | CORE_JOY_LEFT) && core_ram_read(core, 0xC003) == joy[1], "joystick injection");
    const script_text_t* t;
    const int nt = scriptset_texts(s, &t);
    CHECK(nt == 1 && !strcmp(t[0].text, "frame 3") && t[0].rgb == 0x00FF00 && t[0].x == 8, "overlay text");
    char last[256] = "";
    drain_log(s, last, sizeof(last));
    CHECK(!strcmp(last, "[basic] two\t" "578"), "log line: %s", last);     // 0x0242 = $42 | 2 << 8
    char id[64], msg[256];
    CHECK(!scriptset_take_failure(s, id, sizeof(id), msg, sizeof(msg)), "no failures");
    scriptset_destroy(s, core);
    core_destroy(core);
}

// The KERNAL writes the screen while booting: watch the first screen cell row.
static void test_watches(void) {
    core_t* core = make_core();
    scriptset_t* s = scriptset_create();
    char err[256];
    uint8_t joy[2] = { 0, 0 };
    const char* src =
        "count, last = 0, -1\n"
        "for a = 0x0400, 0x0427 do bb.watch_write(a) end\n"
        "function on_write(addr, value)\n"
        "  count = count + 1\n"
        "  last = addr\n"
        "end\n"
        "function on_after_frame() bb.write(0xC010, count > 0 and 1 or 0) end\n";
    CHECK(add(s, core, "watch", src, err, sizeof(err)), "start: %s", err);
    CHECK(core_write_watch_count(core) == 40, "core watches %d", core_write_watch_count(core));
    for (int i = 0; i < 150; i++) frame(s, core, joy);
    CHECK(core_ram_read(core, 0xC010) == 1, "on_write never called");
    // A second script sharing an address keeps it watched when the first stops.
    CHECK(add(s, core, "second", "bb.watch_write(0x0400)", err, sizeof(err)), "second: %s", err);
    CHECK(core_write_watch_count(core) == 40, "shared address counted once");
    scriptset_clear(s, core);
    CHECK(core_write_watch_count(core) == 0, "watches left after clear: %d", core_write_watch_count(core));
    scriptset_destroy(s, core);
    core_destroy(core);
}

static void test_sandbox(void) {
    core_t* core = make_core();
    scriptset_t* s = scriptset_create();
    char err[256];
    const char* src =
        "assert(io == nil and os == nil and package == nil and debug == nil and require == nil)\n"
        "assert(dofile == nil and loadfile == nil and string.dump == nil)\n"
        "assert(load('return 1 + 1')() == 2)\n"
        "local f, e = load('\\27Lua')\n"
        "assert(f == nil and e:find('binary'), e)\n"
        "assert(load('return x', 'c', 'b', {x = 5})() == 5)\n"     // mode is forced to text
        "assert(coroutine and table and string and math and utf8)\n";
    CHECK(add(s, core, "sandbox", src, err, sizeof(err)), "sandbox: %s", err);
    CHECK(!add(s, core, "syntax", "function (", err, sizeof(err)) && strstr(err, "main.lua"), "syntax error: %s", err);
    CHECK(!add(s, core, "throws", "error('nope')", err, sizeof(err)) && strstr(err, "nope"), "error: %s", err);
    CHECK(!add(s, core, "range", "bb.write(0x10000, 1)", err, sizeof(err)) && strstr(err, "65535"), "range: %s", err);
    CHECK(!add(s, core, "joy", "bb.set_joystick(1, 1)", err, sizeof(err)) && strstr(err, "on_frame"), "set_joystick outside on_frame: %s", err);
    CHECK(scriptset_count(s) == 1, "failed scripts aren't kept (%d)", scriptset_count(s));
    scriptset_destroy(s, core);
    core_destroy(core);
}

// Runaway scripts are stopped and reported; the others keep running.
static void test_limits(void) {
    core_t* core = make_core();
    scriptset_t* s = scriptset_create();
    char err[256], id[64], msg[256];
    uint8_t joy[2] = { 0, 0 };
    CHECK(!add(s, core, "spin", "while true do end", err, sizeof(err)) && strstr(err, "too long"), "loop at start: %s", err);
    CHECK(add(s, core, "good", "function on_frame() bb.write(0xC020, (bb.read(0xC020) + 1) % 256) end", err, sizeof(err)), "%s", err);
    CHECK(add(s, core, "later", "function on_frame() if bb.frame() > 2 then while true do end end end", err, sizeof(err)), "%s", err);
    CHECK(add(s, core, "hog", "t = {} function on_after_frame() for i = 1, 1e6 do t[#t + 1] = ('x'):rep(100) .. i end end", err, sizeof(err)), "%s", err);
    for (int i = 0; i < 6; i++) frame(s, core, joy);
    int n = 0;
    bool spun = false, hogged = false;
    while (scriptset_take_failure(s, id, sizeof(id), msg, sizeof(msg))) {
        n++;
        if (!strcmp(id, "later") && strstr(msg, "too long")) spun = true;
        if (!strcmp(id, "hog") && strstr(msg, "memory")) hogged = true;
    }
    CHECK(n == 2 && spun && hogged, "failures reported: %d (spin %d, memory %d)", n, spun, hogged);
    CHECK(scriptset_count(s) == 1 && core_ram_read(core, 0xC020) == 6, "the good script kept running");
    scriptset_destroy(s, core);
    core_destroy(core);
}

// Same script, two separate states: identical results (pairs order, math.random).
static void run_determinism(char* out, size_t size) {
    core_t* core = make_core();
    scriptset_t* s = scriptset_create();
    char err[256];
    const char* src =
        "local t = {}\n"
        "for _, w in ipairs({'alpha','beta','gamma','delta','epsilon','zeta','eta','theta'}) do t[w] = #w end\n"
        "local keys = {}\n"
        "for k in pairs(t) do keys[#keys + 1] = k end\n"
        "math.randomseed()\n"           // no seed given: still the fixed one
        "print(table.concat(keys, ','), math.random(1000000), math.random(1000000))\n";
    CHECK(add(s, core, "det", src, err, sizeof(err)), "%s", err);
    out[0] = 0;
    drain_log(s, out, size);
    scriptset_destroy(s, core);
    core_destroy(core);
}

static void test_determinism(void) {
    char a[256], b[256];
    run_determinism(a, sizeof(a));
    run_determinism(b, sizeof(b));
    CHECK(a[0] && !strcmp(a, b), "runs differ:\n  %s\n  %s", a, b);
}

int main(void) {
    sysroms_load_default(&roms);
    test_basics();
    test_watches();
    test_sandbox();
    test_limits();
    test_determinism();
    if (failures == 0) printf("test_script: all passed\n");
    return failures ? 1 : 0;
}
