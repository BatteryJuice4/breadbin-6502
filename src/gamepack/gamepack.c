#include "gamepack/gamepack.h"
#include "settings/toml_util.h"
#include "video/palettes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* gamepack_media_name(gamepack_media_t m) {
    switch (m) {
        case GAMEPACK_MEDIA_CARTRIDGE: return "cartridge";
        case GAMEPACK_MEDIA_DISK: return "disk";
        case GAMEPACK_MEDIA_TAPE: return "tape";
    }
    return "?";
}

const char* gamepack_cart_name(gamepack_cart_t c) {
    switch (c) {
        case GAMEPACK_CART_AUTO: return "auto";
        case GAMEPACK_CART_8K: return "8k";
        case GAMEPACK_CART_16K: return "16k";
        case GAMEPACK_CART_ULTIMAX: return "ultimax";
    }
    return "?";
}

bool gamepack_id_valid(const char* id) {
    size_t n = strlen(id);
    if (n == 0 || n >= GAMEPACK_ID_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static bool parse_version(const char* s, long v[3]) {
    v[0] = v[1] = v[2] = 0;
    if (!s || !isdigit((unsigned char)*s)) return false;
    for (int i = 0; i < 3; i++) {
        char* end;
        v[i] = strtol(s, &end, 10);
        if (end == s) return false;
        s = end;
        if (*s == '.') {
            s++;
            if (i == 2 || !isdigit((unsigned char)*s)) return false;
        } else {
            break;
        }
    }
    return *s == 0 || *s == '-' || *s == '+';
}

int gamepack_version_compare(const char* a, const char* b, bool* ok) {
    long va[3], vb[3];
    bool good = parse_version(a, va) && parse_version(b, vb);
    if (ok) *ok = good;
    if (!good) return 0;
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}

bool gamepack_launcher_ok(const gamepack_t* p, const char* launcher_version) {
    if (!p->min_launcher_version[0]) return true;
    bool ok;
    int c = gamepack_version_compare(launcher_version, p->min_launcher_version, &ok);
    return ok && c >= 0;
}

static bool is_sha1_hex(const char* s) {
    if (strlen(s) != 40) return false;
    for (int i = 0; i < 40; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

bool gamepack_knows_sha1(const gamepack_t* p, const char* sha1) {
    if (!sha1 || !is_sha1_hex(sha1)) return false;
    for (int i = 0; i < p->known_sha1_count; i++) {
        bool eq = true;
        for (int k = 0; k < 40 && eq; k++) {
            eq = tolower((unsigned char)sha1[k]) == p->known_sha1[i][k];
        }
        if (eq) return true;
    }
    return false;
}

// Warn about keys we don't know (typos in game packs are otherwise silent).
static void check_keys(toml_datum_t tab, const char* const* known, tu_warn_t* w, const char* sect) {
    if (tab.type != TOML_TABLE) return;
    for (int i = 0; i < tab.u.tab.size; i++) {
        const char* k = tab.u.tab.key[i];
        bool found = false;
        for (int j = 0; known[j] && !found; j++) found = !strcmp(k, known[j]);
        if (!found) tu_warn(w, "[%s] unknown key \"%s\" ignored", sect, k);
    }
}

bool gamepack_parse(const char* text, size_t len, gamepack_t* out, settings_t* defaults,
                    char* err, size_t err_size, char* warnings, size_t warnings_size) {
    tu_warn_t w;
    tu_warn_init(&w, warnings, warnings_size);
    if (err && err_size) err[0] = 0;
    toml_result_t r = toml_parse(text, (int)len);
    if (!r.ok) {
        if (err) snprintf(err, err_size, "game.toml: %s", r.errmsg);
        toml_free(r);
        return false;
    }
    gamepack_t p;
    memset(&p, 0, sizeof(p));
    p.sid_model = 6581;
    p.load_done_pc = -1;
    settings_t d;
    if (defaults) d = *defaults; else settings_default(&d);
    bool ok = false;
    const toml_datum_t top = r.toptab;
    const char* T = "game";

    static const char* const top_keys[] = { "id", "title", "media", "video_standard", "sid_model",
        "joystick_port", "min_launcher_version", "system_roms", "rom", "load", "input", "video", NULL };
    check_keys(top, top_keys, &w, T);

    if (!tu_get_string(top, "id", p.id, sizeof(p.id), &w, T) || !gamepack_id_valid(p.id)) {
        if (err) snprintf(err, err_size, "game.toml: \"id\" is required: 1-63 characters, a-z 0-9 - _");
        goto done;
    }
    if (!tu_get_string(top, "title", p.title, sizeof(p.title), &w, T) || !p.title[0]) {
        if (err) snprintf(err, err_size, "game.toml: \"title\" is required");
        goto done;
    }

    char buf[64];
    if (tu_get_string(top, "media", buf, sizeof(buf), &w, T)) {
        if (!strcmp(buf, "cartridge")) p.media = GAMEPACK_MEDIA_CARTRIDGE;
        else if (!strcmp(buf, "disk")) p.media = GAMEPACK_MEDIA_DISK;
        else if (!strcmp(buf, "tape")) p.media = GAMEPACK_MEDIA_TAPE;
        else tu_warn(&w, "[game] media: expected \"cartridge\", \"disk\" or \"tape\"");
    }
    if (tu_get_string(top, "video_standard", buf, sizeof(buf), &w, T)) {
        if (!strcmp(buf, "PAL")) p.ntsc = false;
        else if (!strcmp(buf, "NTSC")) p.ntsc = true;
        else tu_warn(&w, "[game] video_standard: expected \"PAL\" or \"NTSC\"");
    }
    if (tu_get_string(top, "sid_model", buf, sizeof(buf), &w, T)) {
        if (!strcmp(buf, "6581")) p.sid_model = 6581;
        else if (!strcmp(buf, "8580")) p.sid_model = 8580;
        else tu_warn(&w, "[game] sid_model: expected \"6581\" or \"8580\"");
    }
    tu_get_int(top, "joystick_port", 1, 2, &d.input.joystick_port, &w, T);
    if (tu_get_string(top, "min_launcher_version", p.min_launcher_version, sizeof(p.min_launcher_version), &w, T)) {
        bool vok;
        gamepack_version_compare(p.min_launcher_version, "0.0.0", &vok);
        if (!vok) {
            tu_warn(&w, "[game] min_launcher_version: \"%s\" isn't a version like \"0.3.0\"", p.min_launcher_version);
            p.min_launcher_version[0] = 0;
        }
    }
    if (tu_get_string(top, "system_roms", buf, sizeof(buf), &w, T)) {
        if (!strcmp(buf, "original")) p.needs_original_roms = true;
        else if (!strcmp(buf, "any")) p.needs_original_roms = false;
        else tu_warn(&w, "[game] system_roms: expected \"any\" or \"original\"");
    }

    toml_datum_t rom = toml_get(top, "rom");
    if (rom.type == TOML_TABLE) {
        static const char* const rom_keys[] = { "cart_type", "load_address", "known_sha1", NULL };
        check_keys(rom, rom_keys, &w, "rom");
        if (tu_get_string(rom, "cart_type", buf, sizeof(buf), &w, "rom")) {
            for (char* c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);
            if (!strcmp(buf, "auto")) p.cart_type = GAMEPACK_CART_AUTO;
            else if (!strcmp(buf, "8k")) p.cart_type = GAMEPACK_CART_8K;
            else if (!strcmp(buf, "16k")) p.cart_type = GAMEPACK_CART_16K;
            else if (!strcmp(buf, "ultimax")) p.cart_type = GAMEPACK_CART_ULTIMAX;
            else tu_warn(&w, "[rom] cart_type: expected \"auto\", \"8k\", \"16k\" or \"ultimax\"");
        }
        int addr = 0;
        if (tu_get_int(rom, "load_address", 0, 0xFFFF, &addr, &w, "rom")) {
            if (addr == 0x8000 || addr == 0xA000 || addr == 0xE000 || addr == 0xF000) p.bin_load_address = (uint16_t)addr;
            else tu_warn(&w, "[rom] load_address: expected 0x8000, 0xA000, 0xE000 or 0xF000");
        }
        toml_datum_t h = toml_get(rom, "known_sha1");
        if (h.type == TOML_ARRAY) {
            for (int i = 0; i < h.u.arr.size; i++) {
                toml_datum_t e = h.u.arr.elem[i];
                if (e.type != TOML_STRING || !is_sha1_hex(e.u.s)) {
                    tu_warn(&w, "[rom] known_sha1: entry %d isn't a 40-digit hex SHA-1", i + 1);
                } else if (p.known_sha1_count >= GAMEPACK_MAX_SHA1) {
                    tu_warn(&w, "[rom] known_sha1: more than %d entries, extra ignored", GAMEPACK_MAX_SHA1);
                    break;
                } else {
                    char* dst = p.known_sha1[p.known_sha1_count++];
                    for (int k = 0; k < 40; k++) dst[k] = (char)tolower((unsigned char)e.u.s[k]);
                    dst[40] = 0;
                }
            }
        } else if (h.type != TOML_UNKNOWN) {
            tu_warn(&w, "[rom] known_sha1: expected a list of strings");
        }
    } else if (rom.type != TOML_UNKNOWN) {
        tu_warn(&w, "[game] rom: expected a table");
    }

    toml_datum_t ld = toml_get(top, "load");
    if (ld.type == TOML_TABLE) {
        static const char* const load_keys[] = { "file", "done_pc", "done_frames", NULL };
        check_keys(ld, load_keys, &w, "load");
        char file[64];
        if (tu_get_string(ld, "file", file, sizeof(file), &w, "load")) {
            if (strlen(file) <= 16) snprintf(p.load_file, sizeof(p.load_file), "%s", file);
            else tu_warn(&w, "[load] file: C64 file names have at most 16 characters");
        }
        tu_get_int(ld, "done_pc", 0, 0xFFFF, &p.load_done_pc, &w, "load");
        tu_get_int(ld, "done_frames", 1, 20 * 60 * 60, &p.load_done_frames, &w, "load");
    } else if (ld.type != TOML_UNKNOWN) {
        tu_warn(&w, "[game] load: expected a table");
    }

    toml_datum_t in = toml_get(top, "input");
    if (in.type == TOML_TABLE) {
        static const char* const in_keys[] = { "default", NULL };
        check_keys(in, in_keys, &w, "input");
        toml_datum_t def = toml_get(in, "default");
        if (def.type == TOML_TABLE) {
            static const char* const def_keys[] = { "up", "down", "left", "right", "fire", "menu", NULL };
            check_keys(def, def_keys, &w, "input.default");
            for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
                tu_get_keys(def, settings_action_key((input_action_t)a), d.input.keys[a], INPUT_MAX_BINDS, &w, "input.default");
            }
            settings_add_default_gamepad(&d.input);    // packs that list only keys still work with a pad
            int menu[1] = { 0 };
            if (tu_get_keys(def, "menu", menu, 1, &w, "input.default")) {
                if (menu[0]) d.input.menu_key = menu[0];
                else tu_warn(&w, "[input.default] menu: no usable key, keeping the default");
            }
            // The menu key can't double as a joystick key.
            for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
                for (int b = 0; b < INPUT_MAX_BINDS; b++) {
                    if (d.input.keys[a][b] == d.input.menu_key) {
                        tu_warn(&w, "[input.default] %s: the menu key can't be a joystick key, removed", settings_action_key((input_action_t)a));
                        d.input.keys[a][b] = 0;
                    }
                }
            }
        }
    }

    toml_datum_t v = toml_get(top, "video");
    if (v.type == TOML_TABLE) {
        static const char* const v_keys[] = { "default_scale", "integer_scaling", "aspect_correction", "crop_border",
            "filter", "palette", "extended_view_max_width", NULL };
        check_keys(v, v_keys, &w, "video");
        tu_get_int(v, "default_scale", 1, 8, &d.video.window_scale, &w, "video");
        tu_get_bool(v, "integer_scaling", &d.video.integer_scaling, &w, "video");
        tu_get_bool(v, "aspect_correction", &d.video.aspect_correction, &w, "video");
        tu_get_bool(v, "crop_border", &d.video.crop_border, &w, "video");
        if (tu_get_string(v, "filter", buf, sizeof(buf), &w, "video")) {
            if (!strcmp(buf, "nearest")) d.video.filter = VIDEO_FILTER_NEAREST;
            else if (!strcmp(buf, "smooth")) d.video.filter = VIDEO_FILTER_LINEAR;
            else if (!strcmp(buf, "crt")) d.video.filter = VIDEO_FILTER_CRT;
            else tu_warn(&w, "[video] filter: expected \"nearest\", \"smooth\" or \"crt\"");
        }
        if (tu_get_string(v, "palette", p.palette, sizeof(p.palette), &w, "video")) {
            if (palette_id_valid(p.palette)) snprintf(d.video.palette, sizeof(d.video.palette), "%s", p.palette);
            else tu_warn(&w, "[video] palette: unknown palette \"%s\" (built in: colodore, pepto; or \"mod:<id>\")", p.palette);
        }
        if (tu_get_int(v, "extended_view_max_width", 0, 1024, &p.extended_view_max_width, &w, "video") &&
            p.extended_view_max_width > 0 && p.extended_view_max_width <= 320) {
            tu_warn(&w, "[video] extended_view_max_width: use 0 (off) or more than 320 pixels");
            p.extended_view_max_width = 0;
        }
        d.video.extended_max_width = p.extended_view_max_width;
    }

    ok = true;
done:
    toml_free(r);
    if (ok) {
        *out = p;
        if (defaults) *defaults = d;
    }
    return ok;
}
