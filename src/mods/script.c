// Script mods: the Lua host (see script.h, and docs/mods.md for the script API).
#include "mods/script.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOOK_STEP 1000              // instructions between budget checks
#define LOG_RING 64
#define LOG_LINE 200
#define LOG_MAX_PER_SCRIPT 1000     // lines; more are dropped (with one note)

typedef struct script {
    lua_State* L;
    scriptset_t* set;
    char id[64];
    size_t mem_used;
    long budget;                    // instructions left in the current call
    uint8_t watch[0x10000 / 8];
    int watch_count;
    int text_count;
    script_text_t texts[SCRIPT_MAX_TEXT];
    int log_lines;
    bool overflow_noted;
    bool failed;
    char fail_msg[256];
} script_t;

struct scriptset {
    script_t* scripts[SCRIPT_MAX];
    int count;
    // context while hooks run
    core_t* core;
    uint8_t* joy;                   // the coming frame's input; only set during on_frame
    uint8_t last_joy[2];
    // results
    int text_count;
    script_text_t texts[SCRIPT_MAX * SCRIPT_MAX_TEXT];
    int fail_count;
    struct { char id[64]; char msg[256]; } fails[SCRIPT_MAX];
    char log[LOG_RING][LOG_LINE];
    int log_head, log_count;
};

// ---- Lua state plumbing --------------------------------------------------------

static script_t* script_of(lua_State* L) {
    return *(script_t**)lua_getextraspace(L);
}

// Allocator with a cap: past SCRIPT_MEMORY_LIMIT, Lua gets a memory error.
static void* script_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    script_t* sc = ud;
    const size_t old = ptr ? osize : 0;    // without ptr, osize is a type tag
    if (nsize == 0) {
        sc->mem_used -= old;
        free(ptr);
        return NULL;
    }
    if (nsize > old && sc->mem_used + (nsize - old) > SCRIPT_MEMORY_LIMIT) return NULL;
    void* p = realloc(ptr, nsize);
    if (p) sc->mem_used = sc->mem_used - old + nsize;
    return p;
}

static void count_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    script_t* sc = script_of(L);
    sc->budget -= HOOK_STEP;
    if (sc->budget <= 0) {
        luaL_error(L, "the script ran too long (over %d instructions in one call)", SCRIPT_INSTRUCTION_LIMIT);
    }
}

static void add_log(scriptset_t* s, const char* id, const char* text) {
    char* line = s->log[(s->log_head + s->log_count) % LOG_RING];
    snprintf(line, LOG_LINE, "[%s] %s", id, text);
    if (s->log_count < LOG_RING) s->log_count++;
    else s->log_head = (s->log_head + 1) % LOG_RING;    // drop the oldest
}

static void script_log(script_t* sc, const char* text) {
    if (sc->log_lines >= LOG_MAX_PER_SCRIPT) return;
    if (++sc->log_lines == LOG_MAX_PER_SCRIPT) {
        add_log(sc->set, sc->id, "(more output from this script is dropped)");
        return;
    }
    add_log(sc->set, sc->id, text);
}

// ---- the bb table ----------------------------------------------------------------

static core_t* need_core(lua_State* L) {
    script_t* sc = script_of(L);
    if (!sc->set->core) luaL_error(L, "no machine is running");
    return sc->set->core;
}

static uint16_t check_addr(lua_State* L, int arg) {
    const lua_Integer a = luaL_checkinteger(L, arg);
    luaL_argcheck(L, a >= 0 && a <= 0xFFFF, arg, "address must be 0..65535");
    return (uint16_t)a;
}

static int l_read(lua_State* L) {
    lua_pushinteger(L, core_ram_read(need_core(L), check_addr(L, 1)));
    return 1;
}

static int l_read16(lua_State* L) {
    core_t* core = need_core(L);
    const uint16_t a = check_addr(L, 1);
    lua_pushinteger(L, core_ram_read(core, a) | (core_ram_read(core, (uint16_t)(a + 1)) << 8));
    return 1;
}

static int l_write(lua_State* L) {
    core_t* core = need_core(L);
    const uint16_t a = check_addr(L, 1);
    const lua_Integer v = luaL_checkinteger(L, 2);
    luaL_argcheck(L, v >= 0 && v <= 255, 2, "value must be 0..255");
    core_ram_write(core, a, (uint8_t)v);
    return 0;
}

static int l_frame(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)core_frame_count(need_core(L)));
    return 1;
}

static int check_port(lua_State* L, int arg) {
    const lua_Integer p = luaL_checkinteger(L, arg);
    luaL_argcheck(L, p == 1 || p == 2, arg, "port must be 1 or 2");
    return (int)p - 1;
}

static int l_joystick(lua_State* L) {
    const scriptset_t* s = script_of(L)->set;
    const int p = check_port(L, 1);
    lua_pushinteger(L, s->joy ? s->joy[p] : s->last_joy[p]);
    return 1;
}

static int l_set_joystick(lua_State* L) {
    scriptset_t* s = script_of(L)->set;
    const int p = check_port(L, 1);
    const lua_Integer m = luaL_checkinteger(L, 2);
    luaL_argcheck(L, m >= 0 && m <= 0x1F, 2, "mask must be 0..31 (bb.UP|bb.DOWN|bb.LEFT|bb.RIGHT|bb.FIRE)");
    if (!s->joy) return luaL_error(L, "bb.set_joystick only works in on_frame");
    s->joy[p] = (uint8_t)m;
    return 0;
}

static bool watched_by_others(const scriptset_t* s, const script_t* me, uint16_t a) {
    for (int i = 0; i < s->count; i++) {
        const script_t* o = s->scripts[i];
        if (o != me && (o->watch[a >> 3] & (1u << (a & 7)))) return true;
    }
    return false;
}

static void set_watch(script_t* sc, uint16_t a, bool on) {
    uint8_t* b = &sc->watch[a >> 3];
    const uint8_t bit = (uint8_t)(1u << (a & 7));
    if (on == !!(*b & bit)) return;
    if (on) { *b |= bit; sc->watch_count++; }
    else { *b &= (uint8_t)~bit; sc->watch_count--; }
    if (sc->set->core && !watched_by_others(sc->set, sc, a)) core_watch_write(sc->set->core, a, on);
}

static int l_watch_write(lua_State* L) {
    script_t* sc = script_of(L);
    need_core(L);
    const uint16_t a = check_addr(L, 1);
    const bool on = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2);
    if (on && !(sc->watch[a >> 3] & (1u << (a & 7))) && sc->watch_count >= SCRIPT_MAX_WATCHES) {
        return luaL_error(L, "a script can watch at most %d addresses", SCRIPT_MAX_WATCHES);
    }
    set_watch(sc, a, on);
    return 0;
}

static int l_text(lua_State* L) {
    script_t* sc = script_of(L);
    const lua_Integer x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
    size_t len;
    const char* str = luaL_tolstring(L, 3, &len);
    const lua_Integer rgb = luaL_optinteger(L, 4, 0xFFFFFF);
    if (sc->text_count >= SCRIPT_MAX_TEXT) return 0;     // extra texts are ignored
    script_text_t* t = &sc->texts[sc->text_count++];
    t->x = (int)(x < -1000 ? -1000 : x > 1000 ? 1000 : x);
    t->y = (int)(y < -1000 ? -1000 : y > 1000 ? 1000 : y);
    t->rgb = (uint32_t)rgb & 0xFFFFFFu;
    snprintf(t->text, sizeof(t->text), "%.*s", (int)(len < SCRIPT_TEXT_LEN ? len : SCRIPT_TEXT_LEN - 1), str);
    return 0;
}

// bb.log(...) and print(...): arguments joined with tabs, like print.
static int l_log(lua_State* L) {
    char line[LOG_LINE];
    size_t used = 0;
    line[0] = 0;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        const int w = snprintf(line + used, sizeof(line) - used, "%s%s", i > 1 ? "\t" : "", s);
        lua_pop(L, 1);
        if (w < 0 || (size_t)w >= sizeof(line) - used) { used = sizeof(line) - 1; break; }
        used += (size_t)w;
    }
    script_log(script_of(L), line);
    return 0;
}

static const luaL_Reg bb_funcs[] = {
    { "read", l_read }, { "read16", l_read16 }, { "write", l_write },
    { "frame", l_frame },
    { "joystick", l_joystick }, { "set_joystick", l_set_joystick },
    { "watch_write", l_watch_write },
    { "text", l_text }, { "log", l_log },
    { NULL, NULL },
};

// Runs once per state, before the script: trims the standard library to the
// sandbox and makes math.random deterministic.
static const char* const prelude =
    "local _load, _rs, select, tointeger = load, math.randomseed, select, math.tointeger\n"
    "dofile, loadfile = nil, nil\n"
    "string.dump = nil\n"
    "load = function(chunk, name, mode, ...)\n"
    "  if select('#', ...) > 0 then return _load(chunk, name, 't', ...) end\n"
    "  return _load(chunk, name, 't')\n"
    "end\n"
    "math.randomseed = function(a, b) return _rs(tointeger(a) or 0, tointeger(b) or 0) end\n"
    "math.randomseed(0)\n"
    "print = bb.log\n";

// ---- running scripts ---------------------------------------------------------------

static void script_free(script_t* sc) {
    if (!sc) return;
    if (sc->L) lua_close(sc->L);
    free(sc);
}

// Call the function under its arguments on top of the stack. False = the script failed.
static bool script_call(script_t* sc, int nargs) {
    sc->budget = SCRIPT_INSTRUCTION_LIMIT;
    if (lua_pcall(sc->L, nargs, 0, 0) == LUA_OK) return true;
    const char* msg = lua_tostring(sc->L, -1);
    snprintf(sc->fail_msg, sizeof(sc->fail_msg), "%s", msg ? msg : "error (not a string)");
    lua_pop(sc->L, 1);
    sc->failed = true;
    return false;
}

// Push a global hook function; false (nothing pushed) if the script has none.
static bool push_hook(script_t* sc, const char* name) {
    if (lua_getglobal(sc->L, name) == LUA_TFUNCTION) return true;
    lua_pop(sc->L, 1);
    return false;
}

static void remove_script(scriptset_t* s, int index, core_t* core) {
    script_t* sc = s->scripts[index];
    for (int i = index; i + 1 < s->count; i++) s->scripts[i] = s->scripts[i + 1];
    s->count--;
    // Its watches go, unless another script still watches the same address.
    if (core) {
        for (int a = 0; a < 0x10000; a++) {
            if ((sc->watch[a >> 3] & (1u << (a & 7))) && !watched_by_others(s, sc, (uint16_t)a)) {
                core_watch_write(core, (uint16_t)a, false);
            }
        }
    }
    script_free(sc);
}

// Stop the failed scripts and note them for scriptset_take_failure.
static void reap_failures(scriptset_t* s, core_t* core) {
    for (int i = 0; i < s->count;) {
        script_t* sc = s->scripts[i];
        if (!sc->failed) { i++; continue; }
        if (s->fail_count < SCRIPT_MAX) {
            snprintf(s->fails[s->fail_count].id, sizeof(s->fails[0].id), "%s", sc->id);
            snprintf(s->fails[s->fail_count].msg, sizeof(s->fails[0].msg), "%s", sc->fail_msg);
            s->fail_count++;
        }
        remove_script(s, i, core);
    }
}

scriptset_t* scriptset_create(void) {
    return calloc(1, sizeof(scriptset_t));
}

void scriptset_destroy(scriptset_t* s, core_t* core) {
    if (!s) return;
    scriptset_clear(s, core);
    free(s);
}

void scriptset_clear(scriptset_t* s, core_t* core) {
    while (s->count > 0) remove_script(s, s->count - 1, core);
    s->text_count = 0;
    s->last_joy[0] = s->last_joy[1] = 0;
}

int scriptset_count(const scriptset_t* s) {
    return s->count;
}

bool scriptset_add(scriptset_t* s, core_t* core, const char* id, const char* name,
                   const char* source, size_t len, char* err, size_t err_size) {
    if (s->count >= SCRIPT_MAX) {
        snprintf(err, err_size, "too many script mods (at most %d run at once)", SCRIPT_MAX);
        return false;
    }
    script_t* sc = calloc(1, sizeof(*sc));
    if (!sc) { snprintf(err, err_size, "out of memory"); return false; }
    sc->set = s;
    snprintf(sc->id, sizeof(sc->id), "%s", id);
    sc->L = lua_newstate(script_alloc, sc);
    if (!sc->L) { free(sc); snprintf(err, err_size, "out of memory"); return false; }
    *(script_t**)lua_getextraspace(sc->L) = sc;
    lua_State* L = sc->L;

    // Everything below runs protected: the libraries, the bb table, the prelude
    // and the script's main chunk can all fail (memory, errors, budget).
    s->scripts[s->count++] = sc;    // listed first so its watches are tracked
    s->core = core;
    s->joy = NULL;
    static const luaL_Reg libs[] = {
        { LUA_GNAME, luaopen_base }, { LUA_COLIBNAME, luaopen_coroutine },
        { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof(libs) / sizeof(libs[0]) && ok; i++) {
        lua_pushcfunction(L, libs[i].func);
        lua_pushstring(L, libs[i].name);
        sc->budget = SCRIPT_INSTRUCTION_LIMIT;
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) { ok = false; break; }
        lua_setglobal(L, libs[i].name);     // the base library returns _G itself: harmless
    }
    if (ok) {
        luaL_newlib(L, bb_funcs);
        static const struct { const char* name; int value; } consts[] = {
            { "UP", CORE_JOY_UP }, { "DOWN", CORE_JOY_DOWN }, { "LEFT", CORE_JOY_LEFT },
            { "RIGHT", CORE_JOY_RIGHT }, { "FIRE", CORE_JOY_FIRE }, { "api", 1 },
        };
        for (size_t i = 0; i < sizeof(consts) / sizeof(consts[0]); i++) {
            lua_pushinteger(L, consts[i].value);
            lua_setfield(L, -2, consts[i].name);
        }
        lua_setglobal(L, "bb");
        lua_sethook(L, count_hook, LUA_MASKCOUNT, HOOK_STEP);
        ok = luaL_loadbufferx(L, prelude, strlen(prelude), "=prelude", "t") == LUA_OK && script_call(sc, 0);
    }
    if (ok) {
        char chunk[80];
        snprintf(chunk, sizeof(chunk), "@%s", name && name[0] ? name : "main.lua");
        if (luaL_loadbufferx(L, source, len, chunk, "t") != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            snprintf(sc->fail_msg, sizeof(sc->fail_msg), "%s", msg ? msg : "can't load the script");
            sc->failed = true;
            ok = false;
        } else {
            ok = script_call(sc, 0);
        }
    } else if (!sc->failed) {
        snprintf(sc->fail_msg, sizeof(sc->fail_msg), "can't set up the script (out of memory?)");
    }
    s->core = NULL;
    if (!ok) {
        snprintf(err, err_size, "%s", sc->fail_msg[0] ? sc->fail_msg : "the script failed to start");
        remove_script(s, s->count - 1, core);
        return false;
    }
    return true;
}

void scriptset_before_frame(scriptset_t* s, core_t* core, uint8_t joy[2]) {
    s->core = core;
    s->joy = joy;
    for (int i = 0; i < s->count; i++) {
        script_t* sc = s->scripts[i];
        sc->text_count = 0;
        if (push_hook(sc, "on_frame")) script_call(sc, 0);
    }
    s->joy = NULL;
    s->last_joy[0] = joy[0];
    s->last_joy[1] = joy[1];
    s->core = NULL;
    reap_failures(s, core);
}

void scriptset_after_frame(scriptset_t* s, core_t* core) {
    s->core = core;
    int n = 0;
    bool overflow = false;
    const core_write_event_t* ev = core_write_events(core, &n, &overflow);
    for (int i = 0; i < s->count; i++) {
        script_t* sc = s->scripts[i];
        if (sc->watch_count > 0 && n > 0 && push_hook(sc, "on_write")) {
            const int fn = lua_gettop(sc->L);
            for (int k = 0; k < n && !sc->failed; k++) {
                if (!(sc->watch[ev[k].addr >> 3] & (1u << (ev[k].addr & 7)))) continue;
                lua_pushvalue(sc->L, fn);
                lua_pushinteger(sc->L, ev[k].addr);
                lua_pushinteger(sc->L, ev[k].value);
                script_call(sc, 2);
            }
            lua_settop(sc->L, fn - 1);
            if (overflow && !sc->overflow_noted) {
                sc->overflow_noted = true;
                script_log(sc, "more watched writes in one frame than the launcher records; later ones were skipped");
            }
        }
        if (!sc->failed && push_hook(sc, "on_after_frame")) script_call(sc, 0);
    }
    s->core = NULL;
    reap_failures(s, core);
    s->text_count = 0;
    for (int i = 0; i < s->count; i++) {
        const script_t* sc = s->scripts[i];
        memcpy(&s->texts[s->text_count], sc->texts, (size_t)sc->text_count * sizeof(script_text_t));
        s->text_count += sc->text_count;
    }
}

bool scriptset_take_failure(scriptset_t* s, char* id, size_t id_size, char* msg, size_t msg_size) {
    if (s->fail_count == 0) return false;
    snprintf(id, id_size, "%s", s->fails[0].id);
    snprintf(msg, msg_size, "%s", s->fails[0].msg);
    for (int i = 1; i < s->fail_count; i++) s->fails[i - 1] = s->fails[i];
    s->fail_count--;
    return true;
}

int scriptset_texts(const scriptset_t* s, const script_text_t** out) {
    *out = s->texts;
    return s->text_count;
}

bool scriptset_take_log(scriptset_t* s, char* line, size_t size) {
    if (s->log_count == 0) return false;
    snprintf(line, size, "%s", s->log[s->log_head]);
    s->log_head = (s->log_head + 1) % LOG_RING;
    s->log_count--;
    return true;
}
