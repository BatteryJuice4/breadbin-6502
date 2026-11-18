// Mod manifests and packages (see mod.h, docs/mods.md).
#include "mods/mod.h"

#include "mods/zip.h"
#include "rom/rom_file.h"
#include "settings/toml_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD_MAX_MANIFEST (64u * 1024u)
#define MOD_MAX_DATA_FILE (16u * 1024u * 1024u)

const char* mod_type_name(mod_type_t t) {
    switch (t) {
        case MOD_TYPE_PATCH: return "patch";
        case MOD_TYPE_CHEAT: return "cheat";
        case MOD_TYPE_PALETTE: return "palette";
        case MOD_TYPE_SCRIPT: return "script";
    }
    return "?";
}

bool mod_id_valid(const char* id) {
    const size_t n = strlen(id);
    if (n == 0 || n >= MOD_ID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return id[0] != '.';
}

void mod_free(mod_t* m) {
    free(m->patches);
    for (int i = 0; i < m->file_count; i++) free(m->files[i].data);
    free(m->script);
    memset(m, 0, sizeof(*m));
}

static bool is_sha1(const char* s) {
    if (strlen(s) != 40) return false;
    for (int i = 0; i < 40; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static bool is_version(const char* s) {
    if (!isdigit((unsigned char)s[0])) return false;
    for (; *s; s++) if (!isdigit((unsigned char)*s) && *s != '.' && *s != '-' && !isalnum((unsigned char)*s)) return false;
    return true;
}

// "A9 05 EA" / "a905ea" -> bytes. Returns the count, or -1 on bad input / overflow.
static int parse_hex(const char* s, uint8_t* out, int max) {
    int n = 0, nib = -1;
    for (; *s; s++) {
        const char c = *s;
        if (c == ' ' || c == ',' || c == '\t' || c == '_') continue;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return -1;
        if (nib < 0) nib = v;
        else {
            if (n >= max) return -1;
            out[n++] = (uint8_t)(nib << 4 | v);
            nib = -1;
        }
    }
    return nib < 0 ? n : -1;
}

// String list key: copies up to max entries of at most width-1 chars.
static bool get_list(toml_datum_t tab, const char* key, char* out, int width, int max, int* count,
                     char* err, size_t err_size) {
    toml_datum_t a = toml_get(tab, key);
    if (a.type == TOML_UNKNOWN) return true;
    if (a.type != TOML_ARRAY) { snprintf(err, err_size, "%s: expected a list of strings", key); return false; }
    if (a.u.arr.size > max) { snprintf(err, err_size, "%s: more than %d entries", key, max); return false; }
    for (int i = 0; i < a.u.arr.size; i++) {
        toml_datum_t e = a.u.arr.elem[i];
        if (e.type != TOML_STRING || (int)strlen(e.u.s) >= width) {
            snprintf(err, err_size, "%s: entry %d must be a string of at most %d characters", key, i + 1, width - 1);
            return false;
        }
        snprintf(out + (size_t)i * (size_t)width, (size_t)width, "%s", e.u.s);
    }
    *count = a.u.arr.size;
    return true;
}

static void check_keys(toml_datum_t tab, const char* const* known, tu_warn_t* w, const char* sect) {
    for (int i = 0; i < tab.u.tab.size; i++) {
        const char* k = tab.u.tab.key[i];
        bool found = false;
        for (int j = 0; known[j] && !found; j++) found = !strcmp(k, known[j]);
        if (!found) tu_warn(w, "[%s] unknown key \"%s\" ignored", sect, k);
    }
}

static bool get_str(toml_datum_t tab, const char* key, char* out, size_t size, bool required, char* err, size_t err_size) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) {
        if (required) snprintf(err, err_size, "missing \"%s\"", key);
        return !required;
    }
    if (d.type != TOML_STRING || strlen(d.u.s) >= size) {
        snprintf(err, err_size, "\"%s\" must be a string of at most %zu characters", key, size - 1);
        return false;
    }
    snprintf(out, size, "%s", d.u.s);
    return true;
}

static bool get_uint(toml_datum_t tab, const char* key, int64_t lo, int64_t hi, int64_t* out, char* err, size_t err_size, const char* where) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type != TOML_INT64 || d.u.int64 < lo || d.u.int64 > hi) {
        snprintf(err, err_size, "%s: \"%s\" must be a number %lld..%lld", where, key, (long long)lo, (long long)hi);
        return false;
    }
    *out = d.u.int64;
    return true;
}

static bool parse_patches(toml_datum_t top, mod_t* m, mod_read_fn read, void* user, tu_warn_t* w, char* err, size_t err_size) {
    toml_datum_t arr = toml_get(top, "patch");
    if (arr.type != TOML_UNKNOWN) {
        if (arr.type != TOML_ARRAY) { snprintf(err, err_size, "[[patch]] must be a list of tables"); return false; }
        if (arr.u.arr.size > MOD_MAX_PATCHES) { snprintf(err, err_size, "more than %d [[patch]] entries", MOD_MAX_PATCHES); return false; }
        m->patches = calloc((size_t)arr.u.arr.size + 1, sizeof(mod_patch_t));
        if (!m->patches) { snprintf(err, err_size, "out of memory"); return false; }
        for (int i = 0; i < arr.u.arr.size; i++) {
            toml_datum_t p = arr.u.arr.elem[i];
            char where[32];
            snprintf(where, sizeof(where), "patch %d", i + 1);
            if (p.type != TOML_TABLE) { snprintf(err, err_size, "%s: expected a table", where); return false; }
            static const char* const keys[] = { "offset", "bytes", "expect", "label", NULL };
            check_keys(p, keys, w, "patch");
            mod_patch_t* o = &m->patches[m->patch_count];
            int64_t off;
            if (!get_uint(p, "offset", 0, 0xFFFFFF, &off, err, err_size, where)) return false;
            o->offset = (uint32_t)off;
            char hex[MOD_MAX_PATCH_BYTES * 3 + 8];
            if (!get_str(p, "bytes", hex, sizeof(hex), true, err, err_size)) return false;
            const int n = parse_hex(hex, o->bytes, MOD_MAX_PATCH_BYTES);
            if (n <= 0) { snprintf(err, err_size, "%s: \"bytes\" must be 1-%d hex bytes", where, MOD_MAX_PATCH_BYTES); return false; }
            o->len = (uint16_t)n;
            hex[0] = 0;
            if (!get_str(p, "expect", hex, sizeof(hex), false, err, err_size)) return false;
            if (hex[0]) {
                if (parse_hex(hex, o->expect, MOD_MAX_PATCH_BYTES) != n) {
                    snprintf(err, err_size, "%s: \"expect\" must have as many bytes as \"bytes\"", where);
                    return false;
                }
                o->has_expect = true;
            }
            m->patch_count++;
        }
    }
    char names[MOD_MAX_FILES][64];
    int nf = 0;
    if (!get_list(top, "patch_files", &names[0][0], 64, MOD_MAX_FILES, &nf, err, err_size)) return false;
    for (int i = 0; i < nf; i++) {
        const char* n = names[i];
        const size_t len = strlen(n);
        mod_file_t* f = &m->files[m->file_count];
        if (len > 4 && !strcmp(n + len - 4, ".ips")) f->kind = MOD_FILE_IPS;
        else if (len > 4 && !strcmp(n + len - 4, ".bps")) f->kind = MOD_FILE_BPS;
        else { snprintf(err, err_size, "patch_files: \"%.60s\" must be a .ips or .bps file", n); return false; }
        snprintf(f->name, sizeof(f->name), "%.63s", n);   // list entries are < 64 chars
        char ferr[160];
        if (!read) { snprintf(err, err_size, "patch_files: no files can be read here"); return false; }
        f->data = read(user, n, &f->size, ferr, sizeof(ferr));
        if (!f->data) { snprintf(err, err_size, "patch_files: %s", ferr); return false; }
        m->file_count++;
    }
    if (m->patch_count == 0 && m->file_count == 0) {
        snprintf(err, err_size, "a patch mod needs [[patch]] entries or patch_files");
        return false;
    }
    return true;
}

static bool parse_script(toml_datum_t top, mod_t* m, mod_read_fn read, void* user, char* err, size_t err_size) {
    snprintf(m->script_name, sizeof(m->script_name), "main.lua");
    if (!get_str(top, "script", m->script_name, sizeof(m->script_name), false, err, err_size)) return false;
    const size_t n = strlen(m->script_name);
    if (n < 5 || strcmp(m->script_name + n - 4, ".lua")) {
        snprintf(err, err_size, "script: \"%s\" must be a .lua file", m->script_name);
        return false;
    }
    if (!read) { snprintf(err, err_size, "script: no files can be read here"); return false; }
    char ferr[160];
    size_t size = 0;
    uint8_t* data = read(user, m->script_name, &size, ferr, sizeof(ferr));
    if (!data) { snprintf(err, err_size, "script: %s", ferr); return false; }
    if (size > MOD_MAX_SCRIPT) {
        free(data);
        snprintf(err, err_size, "script: %s is larger than %d KB", m->script_name, MOD_MAX_SCRIPT / 1024);
        return false;
    }
    m->script = malloc(size + 1);
    if (!m->script) { free(data); snprintf(err, err_size, "out of memory"); return false; }
    memcpy(m->script, data, size);
    m->script[size] = 0;
    m->script_size = size;
    free(data);
    if (memchr(m->script, 0, size) || (size > 0 && m->script[0] == 0x1B)) {
        snprintf(err, err_size, "script: %s must be Lua source text (no compiled chunks)", m->script_name);
        return false;
    }
    return true;
}

static bool parse_cheats(toml_datum_t top, mod_t* m, tu_warn_t* w, char* err, size_t err_size) {
    toml_datum_t arr = toml_get(top, "cheat");
    if (arr.type != TOML_ARRAY || arr.u.arr.size == 0) { snprintf(err, err_size, "a cheat mod needs [[cheat]] entries"); return false; }
    if (arr.u.arr.size > MOD_MAX_CHEATS) { snprintf(err, err_size, "more than %d [[cheat]] entries", MOD_MAX_CHEATS); return false; }
    for (int i = 0; i < arr.u.arr.size; i++) {
        toml_datum_t c = arr.u.arr.elem[i];
        char where[32];
        snprintf(where, sizeof(where), "cheat %d", i + 1);
        if (c.type != TOML_TABLE) { snprintf(err, err_size, "%s: expected a table", where); return false; }
        static const char* const keys[] = { "address", "value", "bytes", "mode", "label", NULL };
        check_keys(c, keys, w, "cheat");
        mod_cheat_t* o = &m->cheats[m->cheat_count];
        int64_t addr;
        if (!get_uint(c, "address", 0, 0xFFFF, &addr, err, err_size, where)) return false;
        o->address = (uint16_t)addr;
        if (toml_get(c, "value").type != TOML_UNKNOWN) {
            int64_t v;
            if (!get_uint(c, "value", 0, 255, &v, err, err_size, where)) return false;
            o->bytes[0] = (uint8_t)v;
            o->len = 1;
        } else {
            char hex[MOD_MAX_CHEAT_BYTES * 3 + 8];
            if (!get_str(c, "bytes", hex, sizeof(hex), true, err, err_size)) {
                snprintf(err, err_size, "%s: needs \"value\" (0-255) or \"bytes\" (hex)", where);
                return false;
            }
            const int n = parse_hex(hex, o->bytes, MOD_MAX_CHEAT_BYTES);
            if (n <= 0) { snprintf(err, err_size, "%s: \"bytes\" must be 1-%d hex bytes", where, MOD_MAX_CHEAT_BYTES); return false; }
            o->len = (uint8_t)n;
        }
        if ((uint32_t)o->address + o->len > 0x10000) { snprintf(err, err_size, "%s: runs past $FFFF", where); return false; }
        char mode[16] = "freeze";
        if (!get_str(c, "mode", mode, sizeof(mode), false, err, err_size)) return false;
        if (!strcmp(mode, "freeze")) o->freeze = true;
        else if (!strcmp(mode, "once")) o->freeze = false;
        else { snprintf(err, err_size, "%s: \"mode\" must be \"freeze\" or \"once\"", where); return false; }
        if (!get_str(c, "label", o->label, sizeof(o->label), false, err, err_size)) return false;
        m->cheat_count++;
    }
    return true;
}

static bool parse_palette(toml_datum_t top, mod_t* m, tu_warn_t* w, char* err, size_t err_size) {
    toml_datum_t p = toml_get(top, "palette");
    if (p.type != TOML_TABLE) { snprintf(err, err_size, "a palette mod needs a [palette] table"); return false; }
    static const char* const keys[] = { "name", "colors", NULL };
    check_keys(p, keys, w, "palette");
    if (!get_str(p, "name", m->palette_name, sizeof(m->palette_name), false, err, err_size)) return false;
    if (!m->palette_name[0]) snprintf(m->palette_name, sizeof(m->palette_name), "%.63s", m->name);
    toml_datum_t c = toml_get(p, "colors");
    if (c.type != TOML_ARRAY || c.u.arr.size != 16) { snprintf(err, err_size, "[palette] colors must list 16 colours"); return false; }
    for (int i = 0; i < 16; i++) {
        toml_datum_t e = c.u.arr.elem[i];
        const char* s = e.type == TOML_STRING ? e.u.s : "";
        if (s[0] == '#') s++;
        uint8_t rgb[3];
        if (strlen(s) != 6 || parse_hex(s, rgb, 3) != 3) {
            snprintf(err, err_size, "[palette] colour %d must look like \"#RRGGBB\"", i);
            return false;
        }
        m->palette[i] = (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2];
    }
    return true;
}

bool mod_parse(const char* text, size_t len, mod_read_fn read, void* user, mod_t* out,
               char* err, size_t err_size, char* warnings, size_t warnings_size) {
    memset(out, 0, sizeof(*out));
    tu_warn_t w;
    tu_warn_init(&w, warnings, warnings_size);
    char e[256] = "";
    toml_result_t r = toml_parse(text, (int)len);
    if (!r.ok) {
        snprintf(err, err_size, "mod.toml: %s", r.errmsg);
        toml_free(r);
        return false;
    }
    const toml_datum_t top = r.toptab;
    bool ok = false;
    char type[16] = "";
    static const char* const keys[] = { "id", "name", "version", "author", "description", "game",
        "min_launcher_version", "requires_sha1", "dependencies", "conflicts", "plugins", "type",
        "patch", "patch_files", "cheat", "palette", "script", NULL };
    check_keys(top, keys, &w, "mod");
    if (!get_str(top, "id", out->id, sizeof(out->id), true, e, sizeof(e))) goto done;
    if (!mod_id_valid(out->id)) { snprintf(e, sizeof(e), "id \"%s\" may only use a-z 0-9 - _ .", out->id); goto done; }
    if (!get_str(top, "name", out->name, sizeof(out->name), true, e, sizeof(e))) goto done;
    if (!get_str(top, "version", out->version, sizeof(out->version), true, e, sizeof(e))) goto done;
    if (!is_version(out->version)) { snprintf(e, sizeof(e), "version \"%s\" isn't like 1.0.0", out->version); goto done; }
    if (!get_str(top, "author", out->author, sizeof(out->author), false, e, sizeof(e))) goto done;
    if (!get_str(top, "description", out->description, sizeof(out->description), false, e, sizeof(e))) goto done;
    if (!get_str(top, "game", out->game, sizeof(out->game), true, e, sizeof(e))) goto done;
    if (!get_str(top, "min_launcher_version", out->min_launcher_version, sizeof(out->min_launcher_version), false, e, sizeof(e))) goto done;
    if (out->min_launcher_version[0] && !is_version(out->min_launcher_version)) {
        snprintf(e, sizeof(e), "min_launcher_version \"%s\" isn't like 0.3.0", out->min_launcher_version);
        goto done;
    }
    if (!get_list(top, "requires_sha1", &out->requires_sha1[0][0], 41, MOD_MAX_LIST, &out->requires_count, e, sizeof(e))) goto done;
    for (int i = 0; i < out->requires_count; i++) {
        if (!is_sha1(out->requires_sha1[i])) { snprintf(e, sizeof(e), "requires_sha1: entry %d isn't a SHA-1", i + 1); goto done; }
        for (char* c = out->requires_sha1[i]; *c; c++) *c = (char)tolower((unsigned char)*c);
    }
    char deps[MOD_MAX_LIST][MOD_ID_MAX + 32];
    int ndeps = 0;
    if (!get_list(top, "dependencies", &deps[0][0], MOD_ID_MAX + 32, MOD_MAX_LIST, &ndeps, e, sizeof(e))) goto done;
    for (int i = 0; i < ndeps; i++) {
        // "other-mod" or "other-mod>=1.2.0"
        char* ge = strstr(deps[i], ">=");
        mod_dep_t* d = &out->deps[out->dep_count];
        if (ge) {
            *ge = 0;
            if (strlen(ge + 2) >= sizeof(d->min_version)) { snprintf(e, sizeof(e), "dependencies: version too long in entry %d", i + 1); goto done; }
            snprintf(d->min_version, sizeof(d->min_version), "%.31s", ge + 2);
            if (!is_version(d->min_version)) { snprintf(e, sizeof(e), "dependencies: bad version in entry %d", i + 1); goto done; }
        }
        if (strlen(deps[i]) >= MOD_ID_MAX) { snprintf(e, sizeof(e), "dependencies: entry %d is too long", i + 1); goto done; }
        snprintf(d->id, sizeof(d->id), "%.63s", deps[i]);
        if (!mod_id_valid(d->id)) { snprintf(e, sizeof(e), "dependencies: \"%s\" isn't a mod id", d->id); goto done; }
        out->dep_count++;
    }
    if (!get_list(top, "conflicts", &out->conflicts[0][0], MOD_ID_MAX, MOD_MAX_LIST, &out->conflict_count, e, sizeof(e))) goto done;
    if (!get_list(top, "plugins", &out->plugins[0][0], MOD_ID_MAX, MOD_MAX_LIST, &out->plugin_count, e, sizeof(e))) goto done;
    if (!get_str(top, "type", type, sizeof(type), true, e, sizeof(e))) goto done;
    if (!strcmp(type, "patch")) {
        out->type = MOD_TYPE_PATCH;
        if (!parse_patches(top, out, read, user, &w, e, sizeof(e))) goto done;
    } else if (!strcmp(type, "cheat")) {
        out->type = MOD_TYPE_CHEAT;
        if (!parse_cheats(top, out, &w, e, sizeof(e))) goto done;
    } else if (!strcmp(type, "palette")) {
        out->type = MOD_TYPE_PALETTE;
        if (!parse_palette(top, out, &w, e, sizeof(e))) goto done;
    } else if (!strcmp(type, "script")) {
        out->type = MOD_TYPE_SCRIPT;
        if (!parse_script(top, out, read, user, e, sizeof(e))) goto done;
    } else {
        snprintf(e, sizeof(e), "type \"%s\" must be patch, cheat, palette or script", type);
        goto done;
    }
    ok = true;
done:
    toml_free(r);
    if (!ok) {
        snprintf(err, err_size, "%s", e);
        mod_free(out);
    }
    return ok;
}

// ---- packages and folders ------------------------------------------------------

// Data files a package may contain. Anything else (executables, libraries,
// scripts for later phases excepted) is refused, so a package can't smuggle in code.
static bool allowed_file(const char* name) {
    static const char* const ext[] = { ".toml", ".ips", ".bps", ".txt", ".md", ".png", ".lua", NULL };
    const size_t n = strlen(name);
    for (int i = 0; ext[i]; i++) {
        const size_t e = strlen(ext[i]);
        if (n > e) {
            bool same = true;
            for (size_t k = 0; k < e && same; k++) same = tolower((unsigned char)name[n - e + k]) == ext[i][k];
            if (same) return true;
        }
    }
    return false;
}

static uint8_t* zip_reader(void* user, const char* path, size_t* size, char* err, size_t err_size) {
    const zip_t* z = user;
    const int i = zip_find(z, path);
    if (i < 0) { snprintf(err, err_size, "%s isn't in the package", path); return NULL; }
    return zip_extract(z, i, size, err, err_size);
}

bool mod_load_package(const uint8_t* data, size_t size, mod_t* out, char* err, size_t err_size,
                      char* warnings, size_t warnings_size) {
    memset(out, 0, sizeof(*out));
    static zip_t z;     // large; packages load one at a time on the main thread
    if (!zip_open(&z, data, size, err, err_size)) return false;
    for (int i = 0; i < z.count; i++) {
        if (!z.entries[i].is_dir && !allowed_file(z.entries[i].name)) {
            snprintf(err, err_size, "the package contains \"%s\"; mods may only contain data files "
                     "(.toml .ips .bps .txt .md .png .lua)", z.entries[i].name);
            return false;
        }
    }
    const int mi = zip_find(&z, "mod.toml");
    if (mi < 0) { snprintf(err, err_size, "the package has no mod.toml at its top level"); return false; }
    if (z.entries[mi].size > MOD_MAX_MANIFEST) { snprintf(err, err_size, "mod.toml is too large"); return false; }
    size_t n;
    uint8_t* toml = zip_extract(&z, mi, &n, err, err_size);
    if (!toml) return false;
    const bool ok = mod_parse((const char*)toml, n, zip_reader, &z, out, err, err_size, warnings, warnings_size);
    free(toml);
    return ok;
}

typedef struct { const char* dir; } dir_ctx_t;

static uint8_t* dir_reader(void* user, const char* path, size_t* size, char* err, size_t err_size) {
    const dir_ctx_t* d = user;
    if (strstr(path, "..") || path[0] == '/' || path[0] == '\\') { snprintf(err, err_size, "%s: bad path", path); return NULL; }
    char full[1200];
    snprintf(full, sizeof(full), "%s/%s", d->dir, path);
    uint8_t* data = NULL;
    if (!rom_read_file(full, MOD_MAX_DATA_FILE, &data, size, err, err_size)) return NULL;
    return data;
}

bool mod_load_dir(const char* dir, mod_t* out, char* err, size_t err_size, char* warnings, size_t warnings_size) {
    memset(out, 0, sizeof(*out));
    dir_ctx_t ctx = { dir };
    size_t n;
    uint8_t* toml = dir_reader(&ctx, "mod.toml", &n, err, err_size);
    if (!toml) return false;
    const bool ok = mod_parse((const char*)toml, n, dir_reader, &ctx, out, err, err_size, warnings, warnings_size);
    free(toml);
    return ok;
}
