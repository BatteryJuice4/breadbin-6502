#include "settings/settings.h"
#include "input/keynames.h"
#include "platform/paths.h"

#include "settings/toml_util.h"
#include "mods/mod.h"
#include "video/palettes.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* action_keys[INPUT_ACTION_COUNT] = { "up", "down", "left", "right", "fire" };

const char* settings_action_key(input_action_t a) {
    return (a >= 0 && a < INPUT_ACTION_COUNT) ? action_keys[a] : "?";
}

static bool mod_id_ok(const char* id) {
    return strlen(id) < SETTINGS_MOD_ID_MAX && mod_id_valid(id);
}

void settings_default(settings_t* s) {
    memset(s, 0, sizeof(*s));
    s->video = (video_settings_t){
        .window_scale = 3,
        .fullscreen = false,
        .integer_scaling = true,
        .aspect_correction = true,
        .crop_border = false,
        .filter = VIDEO_FILTER_NEAREST,
        .palette = PALETTE_DEFAULT,
    };
    s->audio = (audio_settings_t){ .volume = 0.8f, .mute = false, .latency_ms = 50 };
    // Generic defaults: keypad joystick, so the cursor keys stay C64 cursor keys
    // for typing. Game packs override these (e.g. arrows + Space).
    input_settings_t* in = &s->input;
    in->keys[INPUT_UP][0] = input_key_from_name("KP_8");
    in->keys[INPUT_DOWN][0] = input_key_from_name("KP_2");
    in->keys[INPUT_LEFT][0] = input_key_from_name("KP_4");
    in->keys[INPUT_RIGHT][0] = input_key_from_name("KP_6");
    in->keys[INPUT_FIRE][0] = input_key_from_name("KP_0");
    in->keys[INPUT_FIRE][1] = input_key_from_name("RCtrl");
    in->joystick_port = 2;
    in->menu_key = input_key_from_name("Escape");
}

// ---- parsing ---------------------------------------------------------------

bool settings_parse(settings_t* s, const char* text, size_t len,
                    char* err, size_t err_size, char* warnings, size_t warnings_size) {
    tu_warn_t w;
    tu_warn_init(&w, warnings, warnings_size);
    toml_result_t r = toml_parse(text, (int)len);
    if (!r.ok) {
        if (err) snprintf(err, err_size, "%s", r.errmsg);
        toml_free(r);
        return false;
    }
    settings_t t = *s;   // apply to a copy, then commit

    toml_datum_t rom = toml_get(r.toptab, "rom");
    if (rom.type == TOML_TABLE) {
        toml_datum_t p = toml_get(rom, "path");
        if (p.type == TOML_STRING) {
            if ((size_t)p.u.str.len < sizeof(t.rom_path)) memcpy(t.rom_path, p.u.s, (size_t)p.u.str.len + 1);
            else tu_warn(&w, "[rom] path: too long");
        } else if (p.type != TOML_UNKNOWN) {
            tu_warn(&w, "[rom] path: expected a string");
        }
    }

    toml_datum_t v = toml_get(r.toptab, "video");
    if (v.type == TOML_TABLE) {
        tu_get_int(v, "scale", 1, 8, &t.video.window_scale, &w, "video");
        tu_get_bool(v, "fullscreen", &t.video.fullscreen, &w, "video");
        tu_get_bool(v, "integer_scaling", &t.video.integer_scaling, &w, "video");
        tu_get_bool(v, "aspect_correction", &t.video.aspect_correction, &w, "video");
        tu_get_bool(v, "crop_border", &t.video.crop_border, &w, "video");
        toml_datum_t f = toml_get(v, "filter");
        if (f.type == TOML_STRING) {
            if (!strcmp(f.u.s, "nearest")) t.video.filter = VIDEO_FILTER_NEAREST;
            else if (!strcmp(f.u.s, "smooth")) t.video.filter = VIDEO_FILTER_LINEAR;
            else tu_warn(&w, "[video] filter: expected \"nearest\" or \"smooth\"");
        } else if (f.type != TOML_UNKNOWN) {
            tu_warn(&w, "[video] filter: expected a string");
        }
        char pal[sizeof(t.video.palette)] = { 0 };     // zero tail: settings compare with memcmp
        if (tu_get_string(v, "palette", pal, sizeof(pal), &w, "video")) {
            if (palette_id_valid(pal)) memcpy(t.video.palette, pal, sizeof(pal));
            else tu_warn(&w, "[video] palette: unknown palette \"%s\"", pal);
        }
    }

    toml_datum_t a = toml_get(r.toptab, "audio");
    if (a.type == TOML_TABLE) {
        tu_get_float(a, "volume", 0.0, 1.0, &t.audio.volume, &w, "audio");
        tu_get_bool(a, "mute", &t.audio.mute, &w, "audio");
        tu_get_int(a, "latency_ms", 20, 250, &t.audio.latency_ms, &w, "audio");
    }

    toml_datum_t in = toml_get(r.toptab, "input");
    if (in.type == TOML_TABLE) {
        for (int i = 0; i < INPUT_ACTION_COUNT; i++) {
            tu_get_keys(in, action_keys[i], t.input.keys[i], INPUT_MAX_BINDS, &w, "input");
        }
        tu_get_int(in, "joystick_port", 1, 2, &t.input.joystick_port, &w, "input");
        int menu[1] = { t.input.menu_key };
        tu_get_keys(in, "menu", menu, 1, &w, "input");
        if (menu[0]) t.input.menu_key = menu[0];
        else if (toml_get(in, "menu").type != TOML_UNKNOWN) tu_warn(&w, "[input] menu: a menu key is required, keeping the old one");
    }

    toml_datum_t mods = toml_get(r.toptab, "mods");
    if (mods.type == TOML_TABLE) {
        toml_datum_t en = toml_get(mods, "enabled");
        if (en.type == TOML_ARRAY) {
            t.mods_enabled_count = 0;
            for (int i = 0; i < en.u.arr.size; i++) {
                toml_datum_t e = en.u.arr.elem[i];
                if (e.type != TOML_STRING || !mod_id_ok(e.u.s)) {
                    tu_warn(&w, "[mods] enabled: entry %d isn't a mod id", i + 1);
                } else if (t.mods_enabled_count >= SETTINGS_MAX_MODS) {
                    tu_warn(&w, "[mods] enabled: more than %d mods, extra ignored", SETTINGS_MAX_MODS);
                    break;
                } else {
                    snprintf(t.mods_enabled[t.mods_enabled_count++], SETTINGS_MOD_ID_MAX, "%s", e.u.s);
                }
            }
        } else if (en.type != TOML_UNKNOWN) {
            tu_warn(&w, "[mods] enabled: expected a list of mod ids");
        }
    }

    toml_free(r);
    *s = t;
    return true;
}

// ---- serialization ---------------------------------------------------------

typedef struct {
    char* buf;
    size_t cap;
    size_t len;     // total that would be written
} out_t;

static void put(out_t* o, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char* dst = (o->buf && o->len < o->cap) ? o->buf + o->len : NULL;
    size_t room = dst ? o->cap - o->len : 0;
    int n = vsnprintf(dst, room, fmt, args);
    va_end(args);
    if (n > 0) o->len += (size_t)n;
}

static void put_string(out_t* o, const char* s) {
    put(o, "\"");
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(o, "\\%c", c);
        else if (c < 0x20 || c == 0x7F) put(o, "\\u%04X", c);
        else put(o, "%c", c);
    }
    put(o, "\"");
}

static void put_keys(out_t* o, const char* key, const int* codes, int n) {
    put(o, "%s = [", key);
    bool first = true;
    for (int i = 0; i < n; i++) {
        const char* name = input_key_name(codes[i]);
        if (!name) continue;
        put(o, first ? "" : ", ");
        put_string(o, name);
        first = false;
    }
    put(o, "]\n");
}

size_t settings_serialize(const settings_t* s, char* buf, size_t cap) {
    out_t o = { buf, cap, 0 };
    if (buf && cap) buf[0] = 0;
    put(&o, "# Breadbin-6502 settings. Edit while the launcher is closed.\n\n");
    put(&o, "[rom]\npath = ");
    put_string(&o, s->rom_path);
    put(&o, "\n\n[video]\n");
    put(&o, "scale = %d\n", s->video.window_scale);
    put(&o, "fullscreen = %s\n", s->video.fullscreen ? "true" : "false");
    put(&o, "integer_scaling = %s\n", s->video.integer_scaling ? "true" : "false");
    put(&o, "aspect_correction = %s\n", s->video.aspect_correction ? "true" : "false");
    put(&o, "crop_border = %s\n", s->video.crop_border ? "true" : "false");
    put(&o, "filter = \"%s\"\n", s->video.filter == VIDEO_FILTER_LINEAR ? "smooth" : "nearest");
    put(&o, "palette = ");
    put_string(&o, s->video.palette);
    put(&o, "\n");
    put(&o, "\n[audio]\n");
    put(&o, "volume = %.2f\n", (double)s->audio.volume);
    put(&o, "mute = %s\n", s->audio.mute ? "true" : "false");
    put(&o, "latency_ms = %d\n", s->audio.latency_ms);
    put(&o, "\n[input]\n");
    put(&o, "joystick_port = %d\n", s->input.joystick_port);
    for (int i = 0; i < INPUT_ACTION_COUNT; i++) {
        put_keys(&o, action_keys[i], s->input.keys[i], INPUT_MAX_BINDS);
    }
    put(&o, "menu = ");
    put_string(&o, input_key_name(s->input.menu_key) ? input_key_name(s->input.menu_key) : "Escape");
    put(&o, "\n\n[mods]\n# enabled mods, in load order\nenabled = [");
    for (int i = 0; i < s->mods_enabled_count; i++) {
        put(&o, i ? ", " : "");
        put_string(&o, s->mods_enabled[i]);
    }
    put(&o, "]\n");
    return o.len;
}

// ---- files -----------------------------------------------------------------

bool settings_load_file(settings_t* s, const char* path, bool* missing,
                        char* err, size_t err_size, char* warnings, size_t warnings_size) {
    if (missing) *missing = false;
    if (err && err_size) err[0] = 0;
    if (warnings && warnings_size) warnings[0] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (missing) *missing = true;
        return false;
    }
    char* text = malloc(256 * 1024);
    size_t n = text ? fread(text, 1, 256 * 1024 - 1, f) : 0;
    fclose(f);
    if (!text) {
        if (err) snprintf(err, err_size, "out of memory");
        return false;
    }
    text[n] = 0;
    char perr[256];
    bool ok = settings_parse(s, text, n, perr, sizeof(perr), warnings, warnings_size);
    free(text);
    if (!ok && err) snprintf(err, err_size, "%s: %s", path, perr);
    return ok;
}

bool settings_save_file(const settings_t* s, const char* path, char* err, size_t err_size) {
    if (!platform_make_parent_dirs(path)) {
        snprintf(err, err_size, "can't create the folder for %s", path);
        return false;
    }
    const size_t need = settings_serialize(s, NULL, 0);
    char* text = malloc(need + 1);
    if (!text) {
        snprintf(err, err_size, "out of memory");
        return false;
    }
    settings_serialize(s, text, need + 1);
    char tmp[SETTINGS_PATH_MAX + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(text, 1, need, f) == need;
    if (f && fclose(f) != 0) ok = false;
    free(text);
    if (!ok) {
        remove(tmp);
        snprintf(err, err_size, "can't write %s", tmp);
        return false;
    }
    if (!platform_replace_file(tmp, path)) {
        remove(tmp);
        snprintf(err, err_size, "can't replace %s", path);
        return false;
    }
    return true;
}
