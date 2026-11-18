// Diagnostic replays (see replay.h).
#include "replay/replay.h"

#include "platform/paths.h"
#include "rom/rom_file.h"
#include "tomlc17.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "BBREPLAY"
#define VERSION 1u
#define MAX_HEADER (64u * 1024u)
#define MAX_FILE (256u * 1024u * 1024u)

void replay_input_apply(core_t* core, const replay_input_t* in) {
    for (int i = 0; i < in->nkeys && i < REPLAY_MAX_KEYS; i++) {
        if (in->keys[i].down) core_key_down(core, in->keys[i].key);
        else core_key_up(core, in->keys[i].key);
    }
    core_set_joystick(core, 1, in->joy[0]);
    core_set_joystick(core, 2, in->joy[1]);
}

void replay_init(replay_t* r, const replay_header_t* h) {
    memset(r, 0, sizeof(*r));
    if (h) r->header = *h;
}

void replay_free(replay_t* r) {
    free(r->inputs);
    free(r->fingerprints);
    memset(r, 0, sizeof(*r));
}

bool replay_add(replay_t* r, const replay_input_t* in, uint64_t fp) {
    if (r->count >= REPLAY_MAX_FRAMES) return false;
    if (r->count == r->cap) {
        const uint32_t cap = r->cap ? r->cap * 2 : 4096;
        replay_input_t* ni = realloc(r->inputs, (size_t)cap * sizeof(*ni));
        if (!ni) return false;
        r->inputs = ni;
        uint64_t* nf = realloc(r->fingerprints, (size_t)cap * sizeof(*nf));
        if (!nf) return false;
        r->fingerprints = nf;
        r->cap = cap;
    }
    r->inputs[r->count] = *in;
    if (r->inputs[r->count].nkeys > REPLAY_MAX_KEYS) r->inputs[r->count].nkeys = REPLAY_MAX_KEYS;
    r->fingerprints[r->count] = fp;
    r->count++;
    return true;
}

// ---- header (TOML text) -----------------------------------------------------

typedef struct {
    char* buf;
    size_t len, cap;
} sb_t;

static void sb_put(sb_t* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->len + n + 1) cap *= 2;
        char* nb = realloc(b->buf, cap);
        if (!nb) return;
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

static char* header_toml(const replay_header_t* h) {
    sb_t b = { 0 };
    sb_str(&b, "# Breadbin-6502 diagnostic replay\n");
    sb_kv(&b, "launcher_version", h->launcher_version);
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
    sb_kv(&b, "drive_rom", h->drive_sha1);
    sb_str(&b, "mods = [");
    for (int i = 0; i < h->mod_count; i++) {
        if (i) sb_str(&b, ", ");
        sb_quoted(&b, h->mods[i]);
    }
    sb_str(&b, "]\n");
    return b.buf;
}

static void get_s(toml_datum_t t, const char* key, char* out, size_t size) {
    toml_datum_t d = toml_get(t, key);
    if (d.type == TOML_STRING) snprintf(out, size, "%s", d.u.s);
}

static bool parse_header(const char* text, size_t len, replay_header_t* h, char* err, size_t err_size) {
    memset(h, 0, sizeof(*h));
    toml_result_t r = toml_parse(text, (int)len);
    if (!r.ok) {
        snprintf(err, err_size, "replay header: %s", r.errmsg);
        toml_free(r);
        return false;
    }
    toml_datum_t t = r.toptab;
    get_s(t, "launcher_version", h->launcher_version, sizeof(h->launcher_version));
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
    get_s(t, "drive_rom", h->drive_sha1, sizeof(h->drive_sha1));
    a = toml_get(t, "mods");
    if (a.type == TOML_ARRAY) {
        for (int i = 0; i < a.u.arr.size && h->mod_count < REPLAY_MAX_MODS; i++) {
            if (a.u.arr.elem[i].type == TOML_STRING) snprintf(h->mods[h->mod_count++], sizeof(h->mods[0]), "%s", a.u.arr.elem[i].u.s);
        }
    }
    toml_free(r);
    if (strlen(h->rom_sha1) != 40) {
        snprintf(err, err_size, "replay header has no ROM SHA-1");
        return false;
    }
    return true;
}

// ---- file ---------------------------------------------------------------------

static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t r32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

uint8_t* replay_serialize(const replay_t* r, size_t* size) {
    char* hdr = header_toml(&r->header);
    if (!hdr) return NULL;
    const size_t hl = strlen(hdr);
    size_t n = 8 + 4 + 4 + hl + 4;
    for (uint32_t i = 0; i < r->count; i++) n += 3 + 2u * r->inputs[i].nkeys + 8;
    uint8_t* out = malloc(n);
    if (!out) { free(hdr); return NULL; }
    uint8_t* p = out;
    memcpy(p, MAGIC, 8); p += 8;
    w32(p, VERSION); p += 4;
    w32(p, (uint32_t)hl); p += 4;
    memcpy(p, hdr, hl); p += hl;
    free(hdr);
    w32(p, r->count); p += 4;
    for (uint32_t i = 0; i < r->count; i++) {
        const replay_input_t* in = &r->inputs[i];
        *p++ = in->joy[0];
        *p++ = in->joy[1];
        *p++ = in->nkeys;
        for (int k = 0; k < in->nkeys; k++) {
            *p++ = in->keys[k].key;
            *p++ = in->keys[k].down;
        }
        const uint64_t fp = r->fingerprints[i];
        w32(p, (uint32_t)fp); w32(p + 4, (uint32_t)(fp >> 32)); p += 8;
    }
    *size = n;
    return out;
}

bool replay_parse(const uint8_t* d, size_t size, replay_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (size < 20 || memcmp(d, MAGIC, 8) != 0) { snprintf(err, err_size, "not a Breadbin-6502 replay"); return false; }
    if (r32(d + 8) != VERSION) { snprintf(err, err_size, "replay format version %u isn't supported", r32(d + 8)); return false; }
    const uint32_t hl = r32(d + 12);
    if (hl > MAX_HEADER || (size_t)hl + 20 > size) { snprintf(err, err_size, "damaged replay (header)"); return false; }
    char* text = malloc((size_t)hl + 1);
    if (!text) { snprintf(err, err_size, "out of memory"); return false; }
    memcpy(text, d + 16, hl);
    text[hl] = 0;
    replay_header_t h;
    const bool hok = parse_header(text, hl, &h, err, err_size);
    free(text);
    if (!hok) return false;
    replay_init(out, &h);
    size_t pos = 16 + (size_t)hl;
    const uint32_t count = r32(d + pos);
    pos += 4;
    if (count > REPLAY_MAX_FRAMES) { snprintf(err, err_size, "damaged replay (too many frames)"); return false; }
    for (uint32_t i = 0; i < count; i++) {
        replay_input_t in = { 0 };
        if (pos + 3 > size) goto damaged;
        in.joy[0] = d[pos]; in.joy[1] = d[pos + 1]; in.nkeys = d[pos + 2];
        pos += 3;
        if (in.nkeys > REPLAY_MAX_KEYS || pos + 2u * in.nkeys + 8 > size) goto damaged;
        for (int k = 0; k < in.nkeys; k++) {
            in.keys[k].key = d[pos++];
            in.keys[k].down = d[pos++] ? 1 : 0;
        }
        const uint64_t fp = (uint64_t)r32(d + pos) | ((uint64_t)r32(d + pos + 4) << 32);
        pos += 8;
        if (!replay_add(out, &in, fp)) { replay_free(out); snprintf(err, err_size, "out of memory"); return false; }
    }
    if (pos != size) goto damaged;
    return true;
damaged:
    replay_free(out);
    snprintf(err, err_size, "damaged replay (frame data)");
    return false;
}

bool replay_save(const replay_t* r, const char* path, char* err, size_t err_size) {
    size_t n = 0;
    uint8_t* data = replay_serialize(r, &n);
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

bool replay_load(const char* path, replay_t* out, char* err, size_t err_size) {
    uint8_t* data = NULL;
    size_t size = 0;
    memset(out, 0, sizeof(*out));
    if (!rom_read_file(path, MAX_FILE, &data, &size, err, err_size)) return false;
    const bool ok = replay_parse(data, size, out, err, err_size);
    free(data);
    return ok;
}

// ---- playback -------------------------------------------------------------------

void replay_player_init(replay_player_t* p, const replay_t* r) {
    memset(p, 0, sizeof(*p));
    p->r = r;
}

bool replay_player_done(const replay_player_t* p) {
    return !p->r || p->pos >= p->r->count;
}

bool replay_player_input(const replay_player_t* p, replay_input_t* out) {
    if (replay_player_done(p)) return false;
    *out = p->r->inputs[p->pos];
    return true;
}

bool replay_player_check(replay_player_t* p, uint64_t fp) {
    if (replay_player_done(p)) return true;
    const bool same = p->r->fingerprints[p->pos] == fp;
    if (!same && !p->diverged) {
        p->diverged = true;
        p->diverged_at = p->pos;
    }
    p->pos++;
    return same;
}
