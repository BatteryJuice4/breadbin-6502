// Trusted plugins: registry, [plugin.x] settings in mod.toml, running instances.
// Built with examples/plugins/autofire.c and a plugin defined here.
#include "core/core.h"
#include "mods/mod.h"
#include "mods/plugin.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <stdlib.h>
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

extern const bb_plugin_t bb_plugin_autofire;

// poke: after every frame writes `value` to `address`; refuses to start if fail = true.
typedef struct { uint16_t addr; uint8_t value; } poke_t;
static int poke_running;

static void* poke_start(core_t* core, const bb_plugin_config_t* c, char* err, size_t err_size) {
    (void)core;
    if (bb_plugin_config_bool(c, "fail", false)) {
        snprintf(err, err_size, "asked to fail (%s)", bb_plugin_config_string(c, "why", "no reason"));
        return NULL;
    }
    poke_t* p = malloc(sizeof(*p));
    p->addr = (uint16_t)bb_plugin_config_int(c, "address", 0xC000);
    p->value = (uint8_t)bb_plugin_config_int(c, "value", 1);
    poke_running++;
    return p;
}
static void poke_stop(void* self, core_t* core) {
    (void)core;
    free(self);
    poke_running--;
}
static void poke_after(void* self, const bb_plugin_frame_t* f) {
    const poke_t* p = self;
    core_ram_write(f->core, p->addr, p->value);
}
static const bb_plugin_t poke = {
    .api = BB_PLUGIN_API, .name = "poke", .description = "test",
    .start = poke_start, .stop = poke_stop, .after_frame = poke_after,
};

static sysroms_t roms;

static core_t* make_core(void) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

#define HEAD "id = \"p\"\nname = \"P\"\nversion = \"1.0.0\"\ngame = \"demo\"\n"

static bool parse(const char* toml, mod_t* m, char* err, size_t es, char* warns, size_t ws) {
    return mod_parse(toml, strlen(toml), NULL, NULL, m, err, es, warns, ws);
}

static void test_registry(void) {
    plugin_unregister_all();
    CHECK(plugin_register(&bb_plugin_autofire) && plugin_register(&poke), "register");
    CHECK(!plugin_register(&poke), "duplicate accepted");
    bb_plugin_t bad = poke;
    bad.name = "Bad-Name";
    CHECK(!plugin_register(&bad), "bad name accepted");
    bad.name = "newer";
    bad.api = BB_PLUGIN_API + 1;
    CHECK(!plugin_register(&bad), "other API accepted");
    CHECK(plugin_count() == 2 && plugin_find("poke") == &poke && !plugin_find("nope"), "lookup");
}

static void test_manifest(void) {
    char err[256], warns[1024];
    mod_t m;
    const char* ok = HEAD "type = \"plugin\"\nplugins = [\"autofire\", \"poke\"]\n"
                     "[plugin.autofire]\nport = 1\nrate = 3\n"
                     "[plugin.poke]\naddress = 0xC100\nvalue = 7\nfail = false\nwhy = \"text\"\nscale = 1.5\n"
                     "[plugin.unlisted]\nx = 1\n";
    CHECK(parse(ok, &m, err, sizeof(err), warns, sizeof(warns)), "plugin mod: %s", err);
    CHECK(m.type == MOD_TYPE_PLUGIN && m.plugin_count == 2 && m.plugin_cfg_count == 7, "parsed (%d settings)", m.plugin_cfg_count);
    CHECK(strstr(warns, "unlisted"), "no warning for an unlisted plugin's settings");
    const bb_plugin_config_t c = { m.plugin_cfg, m.plugin_cfg_count, "poke" };
    CHECK(bb_plugin_config_int(&c, "address", 0) == 0xC100 && bb_plugin_config_int(&c, "value", 0) == 7, "ints");
    CHECK(!bb_plugin_config_bool(&c, "fail", true) && !strcmp(bb_plugin_config_string(&c, "why", ""), "text"), "bool/string");
    CHECK(bb_plugin_config_float(&c, "scale", 0) == 1.5 && bb_plugin_config_int(&c, "why", 9) == 9, "float, bad int -> default");
    CHECK(bb_plugin_config_int(&c, "rate", 42) == 42, "another plugin's setting leaked");
    mod_free(&m);

    static const char* const bad[] = {
        HEAD "type = \"plugin\"\n",                                         // lists no plugins
        HEAD "type = \"plugin\"\nplugins = [\"Auto-Fire\"]\n",              // bad name
        HEAD "type = \"plugin\"\nplugins = [\"poke\"]\n[plugin.poke]\nlist = [1, 2]\n",
        HEAD "type = \"plugin\"\nplugins = [\"poke\"]\nplugin = 3\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        err[0] = 0;
        const bool accepted = parse(bad[i], &m, err, sizeof(err), warns, sizeof(warns));
        CHECK(!accepted && err[0], "bad manifest %zu accepted", i);
        if (accepted) mod_free(&m);
    }
    // any type may switch plugins on
    CHECK(parse(HEAD "type = \"cheat\"\nplugins = [\"poke\"]\n[[cheat]]\naddress = 2\nvalue = 1\n", &m, err, sizeof(err),
                warns, sizeof(warns)) && m.plugin_count == 1, "cheat with plugin: %s", err);
    mod_free(&m);
}

static void test_running(void) {
    core_t* core = make_core();
    pluginset_t* s = pluginset_create();
    char err[256], warns[1024];
    mod_t m;
    CHECK(parse(HEAD "type = \"plugin\"\nplugins = [\"autofire\", \"poke\"]\n[plugin.autofire]\nrate = 2\n"
                "[plugin.poke]\naddress = 0xC200\nvalue = 0x5A\n", &m, err, sizeof(err), warns, sizeof(warns)), "%s", err);
    CHECK(pluginset_add_mod(s, core, &m, err, sizeof(err)), "start: %s", err);
    CHECK(pluginset_count(s) == 2 && poke_running == 1, "instances");
    // fire held for 8 frames on port 2: pressed 2, released 2, ...
    char pattern[9] = "";
    for (int i = 0; i < 8; i++) {
        uint8_t joy[2] = { 0, CORE_JOY_FIRE | CORE_JOY_UP };
        pluginset_before_frame(s, core, joy);
        pattern[i] = (joy[1] & CORE_JOY_FIRE) ? 'F' : '-';
        CHECK(joy[1] & CORE_JOY_UP, "other bits kept");
        core_set_joystick(core, 2, joy[1]);
        core_run_frame(core);
        pluginset_after_frame(s, core);
    }
    CHECK(!strcmp(pattern, "FF--FF--"), "autofire pattern %s", pattern);
    CHECK(core_ram_read(core, 0xC200) == 0x5A, "poke after_frame");
    mod_free(&m);

    // A mod whose second plugin refuses to start leaves nothing of its own running.
    CHECK(parse(HEAD "type = \"plugin\"\nplugins = [\"poke\", \"poke\"]\n[plugin.poke]\nfail = true\nwhy = \"config\"\n",
                &m, err, sizeof(err), warns, sizeof(warns)), "%s", err);
    CHECK(!pluginset_add_mod(s, core, &m, err, sizeof(err)) && strstr(err, "asked to fail (config)"), "failure: %s", err);
    CHECK(pluginset_count(s) == 2 && poke_running == 1, "rolled back (%d running)", pluginset_count(s));
    mod_free(&m);
    CHECK(parse(HEAD "type = \"plugin\"\nplugins = [\"missing\"]\n", &m, err, sizeof(err), warns, sizeof(warns)), "%s", err);
    CHECK(!pluginset_add_mod(s, core, &m, err, sizeof(err)) && strstr(err, "isn't built"), "missing plugin: %s", err);
    mod_free(&m);

    pluginset_clear(s, core);
    CHECK(pluginset_count(s) == 0 && poke_running == 0, "clear stops everything");
    pluginset_destroy(s, core);
    core_destroy(core);
}

int main(void) {
    sysroms_load_default(&roms);
    test_registry();
    test_manifest();
    test_running();
    if (failures == 0) printf("test_plugins: all passed\n");
    return failures ? 1 : 0;
}
