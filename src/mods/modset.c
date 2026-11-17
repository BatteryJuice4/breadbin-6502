// Available/enabled mods, validation and runtime hooks (see modset.h).
#include "mods/modset.h"

#include "gamepack/gamepack.h"
#include "mods/patch.h"
#include "platform/paths.h"
#include "rom/rom_file.h"
#include "rom/sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOD_PACKAGE_MAX (32u * 1024u * 1024u)

void modset_init(modset_t* s, const char* game_id, const char* launcher_version) {
    memset(s, 0, sizeof(*s));
    snprintf(s->game_id, sizeof(s->game_id), "%s", game_id);
    snprintf(s->launcher_version, sizeof(s->launcher_version), "%s", launcher_version);
}

static void clear(modset_t* s) {
    for (int i = 0; i < s->count; i++) {
        mod_free(&s->entries[i]->mod);
        free(s->entries[i]);
    }
    s->count = 0;
    s->order_count = 0;
    s->scan_errors[0] = 0;
}

void modset_free(modset_t* s) {
    clear(s);
}

static mod_entry_t* new_entry(modset_t* s, const char* path, mod_source_t src) {
    if (s->count >= MODSET_MAX) {
        snprintf(s->scan_errors, sizeof(s->scan_errors), "More than %d mods found; the rest are ignored.", MODSET_MAX);
        return NULL;
    }
    mod_entry_t* e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->source = src;
    // until it loads, name it after the file
    const char* base = path;
    for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(e->file_id, sizeof(e->file_id), "%s", base);
    char* dot = strrchr(e->file_id, '.');
    if (dot && dot != e->file_id) *dot = 0;
    s->entries[s->count++] = e;
    return e;
}

// Same id twice: the installed copy wins over a bundled one, else the first found.
// The other stays listed (as a duplicate) but can't be enabled.
static void after_load(modset_t* s, mod_entry_t* e) {
    if (!e->loaded) return;
    snprintf(e->file_id, sizeof(e->file_id), "%s", e->mod.id);
    for (int i = 0; i < s->count; i++) {
        mod_entry_t* o = s->entries[i];
        if (o != e && o->loaded && !strcmp(o->mod.id, e->mod.id)) {
            // an installed copy beats a bundled one; otherwise the first one found stays
            mod_entry_t* loser = (e->source == MOD_SOURCE_INSTALLED && o->source == MOD_SOURCE_BUNDLED) ? o : e;
            const mod_entry_t* winner = loser == e ? o : e;
            snprintf(loser->status, sizeof(loser->status), "Another copy of this mod (%s) is used instead.",
                     winner->source == MOD_SOURCE_INSTALLED ? "installed" : "bundled");
            loser->loaded = false;
            mod_free(&loser->mod);
        }
    }
}

mod_entry_t* modset_add_package(modset_t* s, const uint8_t* data, size_t size, const char* path, mod_source_t src) {
    mod_entry_t* e = new_entry(s, path, src);
    if (!e) return NULL;
    sha1_hex(data, size, e->sha1);
    char err[256], warns[1024];
    e->loaded = mod_load_package(data, size, &e->mod, err, sizeof(err), warns, sizeof(warns));
    if (!e->loaded) snprintf(e->status, sizeof(e->status), "Broken mod: %s", err);
    after_load(s, e);
    return e;
}

static void add_file(modset_t* s, const char* path, mod_source_t src) {
    uint8_t* data = NULL;
    size_t size = 0;
    char err[256];
    if (!rom_read_file(path, MOD_PACKAGE_MAX, &data, &size, err, sizeof(err))) {
        mod_entry_t* e = new_entry(s, path, src);
        if (e) snprintf(e->status, sizeof(e->status), "Can't read the mod: %s", err);
        return;
    }
    modset_add_package(s, data, size, path, src);
    free(data);
}

static void add_dir(modset_t* s, const char* path, mod_source_t src) {
    mod_entry_t* e = new_entry(s, path, src);
    if (!e) return;
    char toml[MODSET_PATH_MAX + 16];
    snprintf(toml, sizeof(toml), "%s/mod.toml", path);
    uint8_t* data = NULL;
    size_t size = 0;
    char err[256], warns[1024];
    if (rom_read_file(toml, 64 * 1024, &data, &size, err, sizeof(err))) {
        sha1_hex(data, size, e->sha1);
        free(data);
    }
    e->loaded = mod_load_dir(path, &e->mod, err, sizeof(err), warns, sizeof(warns));
    if (!e->loaded) snprintf(e->status, sizeof(e->status), "Broken mod: %s", err);
    after_load(s, e);
}

typedef struct {
    const char* dir;
    char names[MODSET_MAX * 2][256];
    bool is_dir[MODSET_MAX * 2];
    int n;
} listing_t;

static void collect(const char* name, bool is_dir, void* user) {
    listing_t* l = user;
    if (l->n >= MODSET_MAX * 2 || name[0] == '.') return;
    const size_t n = strlen(name);
    const bool pkg = !is_dir && n > 7 && !strcmp(name + n - 7, ".c64mod");
    if (!pkg && !is_dir) return;
    snprintf(l->names[l->n], sizeof(l->names[0]), "%s", name);
    l->is_dir[l->n] = is_dir;
    l->n++;
}

static int cmp_names(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static void scan_dir(modset_t* s, const char* dir, mod_source_t src) {
    if (!dir || !dir[0] || !platform_is_dir(dir)) return;
    static listing_t l;
    memset(&l, 0, sizeof(l));
    platform_list_dir(dir, collect, &l);
    // deterministic order regardless of the file system
    for (int i = 0; i < l.n; i++) {
        for (int j = i + 1; j < l.n; j++) {
            if (cmp_names(l.names[j], l.names[i]) < 0) {
                char t[256];
                memcpy(t, l.names[i], sizeof(t)); memcpy(l.names[i], l.names[j], sizeof(t)); memcpy(l.names[j], t, sizeof(t));
                const bool d = l.is_dir[i]; l.is_dir[i] = l.is_dir[j]; l.is_dir[j] = d;
            }
        }
    }
    for (int i = 0; i < l.n; i++) {
        char path[MODSET_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, l.names[i]);
        if (l.is_dir[i]) {
            char toml[MODSET_PATH_MAX + 16];
            snprintf(toml, sizeof(toml), "%s/mod.toml", path);
            if (platform_file_exists(toml)) add_dir(s, path, src);
        } else {
            add_file(s, path, src);
        }
    }
}

void modset_scan(modset_t* s, const char* installed_dir, const char* bundled_dir) {
    // keep the enabled ids across a rescan
    char ids[MODSET_MAX][MOD_ID_MAX];
    int n = 0;
    for (int i = 0; i < s->order_count; i++) snprintf(ids[n++], MOD_ID_MAX, "%s", s->entries[s->order[i]]->file_id);
    clear(s);
    scan_dir(s, installed_dir, MOD_SOURCE_INSTALLED);
    scan_dir(s, bundled_dir, MOD_SOURCE_BUNDLED);
    modset_set_enabled(s, (const char (*)[MOD_ID_MAX])ids, n);
}

int modset_find(const modset_t* s, const char* id) {
    for (int i = 0; i < s->count; i++) {
        if (s->entries[i]->loaded && !strcmp(s->entries[i]->mod.id, id)) return i;
    }
    for (int i = 0; i < s->count; i++) {
        if (!strcmp(s->entries[i]->file_id, id)) return i;
    }
    return -1;
}

void modset_set_enabled(modset_t* s, const char (*ids)[MOD_ID_MAX], int count) {
    s->order_count = 0;
    for (int i = 0; i < s->count; i++) s->entries[i]->enabled = false;
    for (int i = 0; i < count; i++) {
        const int k = modset_find(s, ids[i]);
        if (k < 0 || s->entries[k]->enabled) continue;
        s->entries[k]->enabled = true;
        s->order[s->order_count++] = k;
    }
}

void modset_enable(modset_t* s, int index, bool on) {
    if (index < 0 || index >= s->count) return;
    mod_entry_t* e = s->entries[index];
    if (on == e->enabled) return;
    e->enabled = on;
    if (on) {
        s->order[s->order_count++] = index;
    } else {
        int w = 0;
        for (int i = 0; i < s->order_count; i++) if (s->order[i] != index) s->order[w++] = s->order[i];
        s->order_count = w;
    }
}

void modset_move(modset_t* s, int pos, int delta) {
    const int to = pos + delta;
    if (pos < 0 || pos >= s->order_count || to < 0 || to >= s->order_count) return;
    const int t = s->order[pos];
    s->order[pos] = s->order[to];
    s->order[to] = t;
}

static int order_pos(const modset_t* s, int index) {
    for (int i = 0; i < s->order_count; i++) if (s->order[i] == index) return i;
    return -1;
}

static const char* title(const mod_entry_t* e) {
    return e->loaded ? e->mod.name : e->file_id;
}

// Static reasons (independent of other mods).
static bool own_check(const modset_t* s, mod_entry_t* e) {
    if (!e->loaded) return false;   // status already says why
    const mod_t* m = &e->mod;
    char* st = e->status;
    const size_t ss = sizeof(e->status);
    if (strcmp(m->game, s->game_id) != 0) {
        snprintf(st, ss, "Made for another game (\"%s\").", m->game);
        return false;
    }
    if (m->min_launcher_version[0]) {
        bool ok = true;
        if (gamepack_version_compare(s->launcher_version, m->min_launcher_version, &ok) < 0 && ok) {
            snprintf(st, ss, "Needs Breadbin-6502 %s or newer (this is %s).", m->min_launcher_version, s->launcher_version);
            return false;
        }
    }
    if (m->type == MOD_TYPE_SCRIPT) {
        snprintf(st, ss, "Script mods aren't supported by this version of Breadbin-6502 yet.");
        return false;
    }
    if (m->plugin_count > 0) {
        snprintf(st, ss, "Needs the built-in plugin \"%s\", which this game doesn't include.", m->plugins[0]);
        return false;
    }
    if (m->requires_count > 0) {
        bool match = false;
        for (int i = 0; i < m->requires_count && !match; i++) match = !strcmp(m->requires_sha1[i], s->rom_sha1);
        if (!match) {
            snprintf(st, ss, s->rom_sha1[0] ? "Made for a different dump of the game (the SHA-1 doesn't match yours)."
                                            : "Needs the game to be loaded first.");
            return false;
        }
    }
    if (e->failed) return false;    // status holds the load-time error
    return true;
}

static bool ranges_overlap(const mod_t* a, const mod_t* b) {
    for (int i = 0; i < a->patch_count; i++) {
        for (int j = 0; j < b->patch_count; j++) {
            const uint32_t a0 = a->patches[i].offset, a1 = a0 + a->patches[i].len;
            const uint32_t b0 = b->patches[j].offset, b1 = b0 + b->patches[j].len;
            if (a0 < b1 && b0 < a1) return true;
        }
    }
    return false;
}

void modset_evaluate(modset_t* s, const char* rom_sha1) {
    snprintf(s->rom_sha1, sizeof(s->rom_sha1), "%s", rom_sha1 ? rom_sha1 : "");
    for (int i = 0; i < s->count; i++) {
        mod_entry_t* e = s->entries[i];
        if (e->loaded && !e->failed) e->status[0] = 0;
        e->usable = e->enabled && own_check(s, e);
        if (!e->enabled && e->loaded && !e->failed && own_check(s, e)) e->status[0] = 0;
    }
    // Conflicts, dependencies: repeat until stable (a mod losing a dependency can
    // take its own dependents with it).
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < s->count; i++) {
            mod_entry_t* e = s->entries[i];
            if (!e->usable) continue;
            const mod_t* m = &e->mod;
            for (int c = 0; c < s->count && e->usable; c++) {
                mod_entry_t* o = s->entries[c];
                if (o == e || !o->enabled || !o->loaded) continue;
                bool conflict = false;
                for (int k = 0; k < m->conflict_count; k++) conflict |= !strcmp(m->conflicts[k], o->mod.id);
                for (int k = 0; k < o->mod.conflict_count; k++) conflict |= !strcmp(o->mod.conflicts[k], m->id);
                // the one later in the load order gives way
                if (conflict && order_pos(s, c) < order_pos(s, i)) {
                    snprintf(e->status, sizeof(e->status), "Conflicts with \"%s\"; disable one of them.", title(o));
                    e->usable = false;
                    changed = true;
                }
            }
            for (int d = 0; d < m->dep_count && e->usable; d++) {
                const int k = modset_find(s, m->deps[d].id);
                const mod_entry_t* dep = k >= 0 ? s->entries[k] : NULL;
                if (!dep || !dep->loaded) {
                    snprintf(e->status, sizeof(e->status), "Needs the mod \"%s\", which isn't installed.", m->deps[d].id);
                } else if (m->deps[d].min_version[0] &&
                           gamepack_version_compare(dep->mod.version, m->deps[d].min_version, &(bool){ true }) < 0) {
                    snprintf(e->status, sizeof(e->status), "Needs \"%s\" %s or newer (installed: %s).",
                             title(dep), m->deps[d].min_version, dep->mod.version);
                } else if (!dep->enabled) {
                    snprintf(e->status, sizeof(e->status), "Needs \"%s\" to be enabled too.", title(dep));
                } else if (!dep->usable) {
                    snprintf(e->status, sizeof(e->status), "Needs \"%s\", which can't run.", title(dep));
                } else if (order_pos(s, k) > order_pos(s, i)) {
                    snprintf(e->status, sizeof(e->status), "Must load after \"%s\"; move it further down.", title(dep));
                } else {
                    continue;
                }
                e->usable = false;
                changed = true;
            }
        }
    }
    // Notes for overlapping patch mods (allowed; the later one wins).
    for (int p = 0; p < s->order_count; p++) {
        mod_entry_t* e = s->entries[s->order[p]];
        if (!e->usable || e->mod.type != MOD_TYPE_PATCH) continue;
        for (int q = 0; q < p; q++) {
            const mod_entry_t* o = s->entries[s->order[q]];
            if (o->usable && o->mod.type == MOD_TYPE_PATCH && ranges_overlap(&e->mod, &o->mod)) {
                snprintf(e->status, sizeof(e->status), "Changes some of the same bytes as \"%s\" (this one wins).", title(o));
                break;
            }
        }
    }
}

bool modset_has_patches(const modset_t* s) {
    for (int i = 0; i < s->order_count; i++) {
        const mod_entry_t* e = s->entries[s->order[i]];
        if (e->usable && e->mod.type == MOD_TYPE_PATCH) return true;
    }
    return false;
}

int modset_apply_patches(modset_t* s, uint8_t* buf, size_t size) {
    int applied = 0;
    for (int i = 0; i < s->order_count; i++) {
        mod_entry_t* e = s->entries[s->order[i]];
        if (!e->usable || e->mod.type != MOD_TYPE_PATCH) continue;
        char err[256];
        if (patch_apply_mod(buf, size, &e->mod, err, sizeof(err))) {
            applied++;
        } else {
            e->failed = true;
            e->usable = false;
            snprintf(e->status, sizeof(e->status), "Couldn't apply: %s", err);
        }
    }
    return applied;
}

void modset_apply_cheats(const modset_t* s, core_t* core, bool first) {
    for (int i = 0; i < s->order_count; i++) {
        const mod_entry_t* e = s->entries[s->order[i]];
        if (!e->usable || e->mod.type != MOD_TYPE_CHEAT) continue;
        for (int c = 0; c < e->mod.cheat_count; c++) {
            const mod_cheat_t* ch = &e->mod.cheats[c];
            if (!ch->freeze && !first) continue;
            for (int b = 0; b < ch->len; b++) core_ram_write(core, (uint16_t)(ch->address + b), ch->bytes[b]);
        }
    }
}

bool modset_install(const char* dir, const char* package_path, const char* game_id, char* msg, size_t msg_size) {
    uint8_t* data = NULL;
    size_t size = 0;
    char err[256], warns[1024];
    if (!rom_read_file(package_path, MOD_PACKAGE_MAX, &data, &size, err, sizeof(err))) {
        snprintf(msg, msg_size, "Can't read the mod: %s", err);
        return false;
    }
    mod_t m;
    if (!mod_load_package(data, size, &m, err, sizeof(err), warns, sizeof(warns))) {
        snprintf(msg, msg_size, "Not installed: %s", err);
        free(data);
        return false;
    }
    bool ok = false;
    if (strcmp(m.game, game_id) != 0) {
        snprintf(msg, msg_size, "Not installed: \"%s\" is a mod for another game (\"%s\").", m.name, m.game);
        goto done;
    }
    char path[MODSET_PATH_MAX], tmp[MODSET_PATH_MAX + 8];
    snprintf(path, sizeof(path), "%s/%s.c64mod", dir, m.id);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    const bool existed = platform_file_exists(path);
    if (!platform_make_parent_dirs(path)) {
        snprintf(msg, msg_size, "Not installed: can't create %s", dir);
        goto done;
    }
    FILE* f = fopen(tmp, "wb");
    if (!f || fwrite(data, 1, size, f) != size) {
        if (f) fclose(f);
        remove(tmp);
        snprintf(msg, msg_size, "Not installed: can't write %s", tmp);
        goto done;
    }
    if (fclose(f) != 0 || !platform_replace_file(tmp, path)) {
        remove(tmp);
        snprintf(msg, msg_size, "Not installed: can't write %s", path);
        goto done;
    }
    snprintf(msg, msg_size, "%s \"%s\" %s. Enable it in the list below.", existed ? "Updated" : "Installed", m.name, m.version);
    ok = true;
done:
    mod_free(&m);
    free(data);
    return ok;
}

bool modset_uninstall(modset_t* s, int index, char* msg, size_t msg_size) {
    if (index < 0 || index >= s->count) return false;
    mod_entry_t* e = s->entries[index];
    if (e->source != MOD_SOURCE_INSTALLED || platform_is_dir(e->path)) {
        snprintf(msg, msg_size, "\"%s\" comes with the game and can't be uninstalled (only disabled).", title(e));
        return false;
    }
    if (remove(e->path) != 0) {
        snprintf(msg, msg_size, "Couldn't delete %s", e->path);
        return false;
    }
    snprintf(msg, msg_size, "Uninstalled \"%s\".", title(e));
    modset_enable(s, index, false);
    return true;
}
