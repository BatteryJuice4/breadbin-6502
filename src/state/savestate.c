// Save state files (see savestate.h).
#include "state/savestate.h"

#include "platform/paths.h"
#include "rom/rom_file.h"
#include "tomlc17.h"
#include "util/crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "BBSTATE"     // 7 chars + NUL = 8 bytes
#define VERSION 1u
#define MAX_HEADER (64u * 1024u)
#define MAX_STATE (16u * 1024u * 1024u)
#define MAX_FILE (MAX_HEADER + MAX_STATE + 64u)

// ---- header (TOML text) -----------------------------------------------------

typedef struct {
    char* buf;
    size_t len, cap;
    bool failed;
} sb_t;

static void sb_put(sb_t* b, const char* s, size_t n) {
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->len + n + 1) cap *= 2;
        char* nb = realloc(b->buf, cap);
        if (!nb) { b->failed = true; return; }
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

static void sb_str(sb_t* b, const char* s) { sb_put(b, s, strlen(s)); }

static void sb_quoted(sb_t* b, const char* s) {
    sb_str(b, "\"");
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        char e[8];
        if (*p == '"' || *p == '\\') { e[0] = '\\'; e[1] = (char)*p; e[2] = 0; }
        else if (*p < 0x20 || *p == 0x7F) snprintf(e, sizeof(e), "\\u%04X", *p);
        else { e[0] = (char)*p; e[1] = 0; }
        sb_str(b, e);
    }
    sb_str(b, "\"");
}

static void sb_kv(sb_t* b, const char* key, const char* val) {
    sb_str(b, key);
    sb_str(b, " = ");
    sb_quoted(b, val);
    sb_str(b, "\n");
}

static char* header_toml(const savestate_header_t* h) {
    sb_t b = { 0 };
    char frame[32];
    snprintf(frame, sizeof(frame), "%llu", (unsigned long long)h->frame);
    sb_str(&b, "# Breadbin-6502 save state\n");
    sb_kv(&b, "launcher_version", h->launcher_version);
    sb_kv(&b, "layout", h->layout);
    sb_kv(&b, "game_id", h->game_id);
    sb_kv(&b, "rom_name", h->rom_name);
    sb_kv(&b, "rom_sha1", h->rom_sha1);
    sb_kv(&b, "media", h->media);
    sb_kv(&b, "video_standard", h->ntsc ? "NTSC" : "PAL");
    sb_str(&b, "system_roms = [");
    for (int i = 0; i < 3; i++) {
        if (i) sb_str(&b, ", ");
        sb_quoted(&b, h->sysrom_sha1[i]);
    }
    sb_str(&b, "]\n");
    sb_kv(&b, "patches", h->patches);
    sb_kv(&b, "saved_at", h->saved_at);
    sb_kv(&b, "frame", frame);      // a string: TOML integers are signed 64-bit
    if (b.failed) {
        free(b.buf);
        return NULL;
    }
    return b.buf;
}

static void get_s(toml_datum_t t, const char* key, char* out, size_t size) {
    toml_datum_t d = toml_get(t, key);
    if (d.type == TOML_STRING) snprintf(out, size, "%s", d.u.s);
}

static bool parse_header(const char* text, size_t len, savestate_header_t* h, char* err, size_t err_size) {
    memset(h, 0, sizeof(*h));
    toml_result_t r = toml_parse(text, (int)len);
    if (!r.ok) {
        snprintf(err, err_size, "save state header: %s", r.errmsg);
        toml_free(r);
        return false;
    }
    toml_datum_t t = r.toptab;
    get_s(t, "launcher_version", h->launcher_version, sizeof(h->launcher_version));
    get_s(t, "layout", h->layout, sizeof(h->layout));
    get_s(t, "game_id", h->game_id, sizeof(h->game_id));
    get_s(t, "rom_name", h->rom_name, sizeof(h->rom_name));
    get_s(t, "rom_sha1", h->rom_sha1, sizeof(h->rom_sha1));
    get_s(t, "media", h->media, sizeof(h->media));
    char vs[8] = "";
    get_s(t, "video_standard", vs, sizeof(vs));
    h->ntsc = !strcmp(vs, "NTSC");
    toml_datum_t a = toml_get(t, "system_roms");
    if (a.type == TOML_ARRAY) {
        for (int i = 0; i < a.u.arr.size && i < 3; i++) {
            if (a.u.arr.elem[i].type == TOML_STRING) snprintf(h->sysrom_sha1[i], 41, "%s", a.u.arr.elem[i].u.s);
        }
    }
    get_s(t, "patches", h->patches, sizeof(h->patches));
    get_s(t, "saved_at", h->saved_at, sizeof(h->saved_at));
    char frame[32] = "";
    get_s(t, "frame", frame, sizeof(frame));
    h->frame = strtoull(frame, NULL, 10);
    toml_free(r);
    if (strlen(h->rom_sha1) != 40 || strlen(h->layout) != 16) {
        snprintf(err, err_size, "save state header is incomplete");
        return false;
    }
    return true;
}

// ---- file ---------------------------------------------------------------------

static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t r32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

uint8_t* savestate_serialize(const savestate_header_t* h, const uint8_t* state, size_t state_size, size_t* size) {
    if (!state || state_size == 0 || state_size > MAX_STATE) return NULL;
    char* text = header_toml(h);
    if (!text) return NULL;
    const size_t hl = strlen(text);
    const size_t n = 8 + 4 + 4 + hl + 4 + state_size + 4;
    uint8_t* d = malloc(n);
    if (!d) {
        free(text);
        return NULL;
    }
    uint8_t* p = d;
    memcpy(p, MAGIC, 8); p += 8;
    w32(p, VERSION); p += 4;
    w32(p, (uint32_t)hl); p += 4;
    memcpy(p, text, hl); p += hl;
    w32(p, (uint32_t)state_size); p += 4;
    memcpy(p, state, state_size); p += state_size;
    w32(p, crc32_of(d, (size_t)(p - d)));
    free(text);
    *size = n;
    return d;
}

bool savestate_parse(const uint8_t* d, size_t size, savestate_header_t* h, uint8_t** state, size_t* state_size,
                     char* err, size_t err_size) {
    if (state) *state = NULL;
    if (size < 8 + 4 + 4 + 4 + 4 || memcmp(d, MAGIC, 8) != 0) {
        snprintf(err, err_size, "not a Breadbin-6502 save state");
        return false;
    }
    if (r32(d + 8) != VERSION) {
        snprintf(err, err_size, "save state format %u isn't supported", r32(d + 8));
        return false;
    }
    const uint32_t hl = r32(d + 12);
    if (hl > MAX_HEADER || (size_t)hl > size - 16 - 4 - 4) {
        snprintf(err, err_size, "damaged save state (header)");
        return false;
    }
    const uint8_t* p = d + 16 + hl;
    const uint32_t sl = r32(p);
    if (sl == 0 || sl > MAX_STATE || (size_t)sl != size - 16 - hl - 4 - 4) {
        snprintf(err, err_size, "damaged save state (size)");
        return false;
    }
    if (crc32_of(d, size - 4) != r32(d + size - 4)) {
        snprintf(err, err_size, "damaged save state (checksum)");
        return false;
    }
    if (!parse_header((const char*)d + 16, hl, h, err, err_size)) return false;
    if (state) {
        *state = malloc(sl);
        if (!*state) {
            snprintf(err, err_size, "out of memory");
            return false;
        }
        memcpy(*state, p + 4, sl);
        *state_size = sl;
    }
    return true;
}

bool savestate_save(const char* path, const savestate_header_t* h, const uint8_t* state, size_t state_size,
                    char* err, size_t err_size) {
    size_t n = 0;
    uint8_t* data = savestate_serialize(h, state, state_size, &n);
    if (!data) { snprintf(err, err_size, "out of memory"); return false; }
    bool ok = false;
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = NULL;
    if (!platform_make_parent_dirs(path)) {
        snprintf(err, err_size, "can't create the folder for %s", path);
    } else if (!(f = fopen(tmp, "wb")) || fwrite(data, 1, n, f) != n) {
        snprintf(err, err_size, "can't write %s", tmp);
    } else {
        ok = true;
    }
    if (f && fclose(f) != 0) ok = false;
    if (ok && !platform_replace_file(tmp, path)) {
        snprintf(err, err_size, "can't write %s", path);
        ok = false;
    }
    if (!ok) remove(tmp);
    free(data);
    return ok;
}

bool savestate_load(const char* path, savestate_header_t* h, uint8_t** state, size_t* state_size,
                    char* err, size_t err_size) {
    uint8_t* data = NULL;
    size_t size = 0;
    if (state) *state = NULL;
    if (!rom_read_file(path, MAX_FILE, &data, &size, err, err_size)) return false;
    const bool ok = savestate_parse(data, size, h, state, state_size, err, err_size);
    free(data);
    return ok;
}

bool savestate_compatible(const savestate_header_t* s, const savestate_header_t* now, char* why, size_t why_size) {
    if (strcmp(s->layout, now->layout) != 0) {
        snprintf(why, why_size, "It was saved by Breadbin-6502 %s, and save states only load in the version "
                 "that made them.", s->launcher_version);
    } else if (strcmp(s->rom_sha1, now->rom_sha1) != 0) {
        snprintf(why, why_size, "It was saved with a different %s (%s).", s->media[0] ? s->media : "game", s->rom_name);
    } else if (s->ntsc != now->ntsc) {
        snprintf(why, why_size, "It was saved on a %s machine; this one is %s.", s->ntsc ? "NTSC" : "PAL",
                 now->ntsc ? "NTSC" : "PAL");
    } else if (memcmp(s->sysrom_sha1, now->sysrom_sha1, sizeof(s->sysrom_sha1)) != 0) {
        snprintf(why, why_size, "It was saved with different system ROMs.");
    } else if (strcmp(s->patches, now->patches) != 0) {
        snprintf(why, why_size, "It was saved with %s patch mods than the game has now. Match them in the "
                 "Mods tab and restart the game.", s->patches[0] && now->patches[0] ? "different" : s->patches[0] ? "more" : "fewer");
    } else {
        return true;
    }
    return false;
}
