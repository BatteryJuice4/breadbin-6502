#include "settings/toml_util.h"
#include "input/keynames.h"
#include "platform/gamepad.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void tu_warn_init(tu_warn_t* w, char* buf, size_t size) {
    w->buf = buf;
    w->size = size;
    w->len = 0;
    if (buf && size) buf[0] = 0;
}

void tu_warn(tu_warn_t* w, const char* fmt, ...) {
    if (!w || !w->buf || w->size == 0 || w->len + 1 >= w->size) return;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(w->buf + w->len, w->size - w->len, fmt, args);
    va_end(args);
    if (n > 0) {
        w->len += (size_t)n;
        if (w->len >= w->size) w->len = w->size - 1;
        if (w->len + 1 < w->size) { w->buf[w->len++] = '\n'; w->buf[w->len] = 0; }
    }
}

bool tu_get_bool(toml_datum_t tab, const char* key, bool* out, tu_warn_t* w, const char* sect) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) return false;
    if (d.type != TOML_BOOLEAN) { tu_warn(w, "[%s] %s: expected true/false", sect, key); return false; }
    *out = d.u.boolean;
    return true;
}

bool tu_get_int(toml_datum_t tab, const char* key, int lo, int hi, int* out, tu_warn_t* w, const char* sect) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) return false;
    if (d.type != TOML_INT64 || d.u.int64 < lo || d.u.int64 > hi) {
        tu_warn(w, "[%s] %s: expected an integer %d..%d", sect, key, lo, hi);
        return false;
    }
    *out = (int)d.u.int64;
    return true;
}

bool tu_get_float(toml_datum_t tab, const char* key, double lo, double hi, float* out, tu_warn_t* w, const char* sect) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) return false;
    double v;
    if (d.type == TOML_FP64) v = d.u.fp64;
    else if (d.type == TOML_INT64) v = (double)d.u.int64;
    else { tu_warn(w, "[%s] %s: expected a number", sect, key); return false; }
    if (v < lo || v > hi) { tu_warn(w, "[%s] %s: expected %g..%g", sect, key, lo, hi); return false; }
    *out = (float)v;
    return true;
}

bool tu_get_string(toml_datum_t tab, const char* key, char* out, size_t out_size, tu_warn_t* w, const char* sect) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) return false;
    if (d.type != TOML_STRING) { tu_warn(w, "[%s] %s: expected a string", sect, key); return false; }
    if ((size_t)d.u.str.len >= out_size) { tu_warn(w, "[%s] %s: too long", sect, key); return false; }
    memcpy(out, d.u.s, (size_t)d.u.str.len + 1);
    return true;
}

bool tu_get_keys(toml_datum_t tab, const char* key, int* out, int max, tu_warn_t* w, const char* sect) {
    toml_datum_t d = toml_get(tab, key);
    if (d.type == TOML_UNKNOWN) return false;
    const char* names[8];
    int n = 0;
    if (d.type == TOML_STRING) {
        names[n++] = d.u.s;
    } else if (d.type == TOML_ARRAY) {
        for (int i = 0; i < d.u.arr.size && n < 8; i++) {
            if (d.u.arr.elem[i].type == TOML_STRING) names[n++] = d.u.arr.elem[i].u.s;
            else tu_warn(w, "[%s] %s: entries must be key name strings", sect, key);
        }
    } else {
        tu_warn(w, "[%s] %s: expected a key name or a list of key names", sect, key);
        return false;
    }
    int codes[8] = { 0 };
    int nc = 0;
    for (int i = 0; i < n; i++) {
        int code = input_key_from_name(names[i]);
        if (!code) {
            if (!strncmp(names[i], "Gamepad_", 8)) {
                tu_warn(w, "[%s] %s: unknown gamepad button \"%s\" ignored", sect, key, names[i]);
            } else {
                tu_warn(w, "[%s] %s: unknown key \"%s\" ignored", sect, key, names[i]);
            }
            continue;
        }
        if (nc < max && nc < 8) codes[nc++] = code;
        else tu_warn(w, "[%s] %s: only %d keys per action, \"%s\" ignored", sect, key, max, names[i]);
    }
    for (int i = 0; i < max; i++) out[i] = i < 8 ? codes[i] : 0;
    return true;
}

bool tu_get_c64_keys(toml_datum_t input_table, uint8_t pad_c64_key[32], tu_warn_t* w, const char* sect) {
    toml_datum_t t = toml_get(input_table, "c64_keys");
    if (t.type == TOML_UNKNOWN) return false;
    if (t.type != TOML_TABLE) {
        tu_warn(w, "[%s] c64_keys: expected a table of C64 key = gamepad button(s)", sect);
        return false;
    }
    memset(pad_c64_key, 0, 32);
    for (int i = 0; i < t.u.tab.size; i++) {
        const char* name = t.u.tab.key[i];
        const int c64 = input_c64_key_from_name(name);
        if (!c64) {
            tu_warn(w, "[%s.c64_keys] unknown C64 key \"%s\" ignored", sect, name);
            continue;
        }
        int codes[4] = { 0 };
        if (!tu_get_keys(t, name, codes, 4, w, sect)) continue;
        for (int c = 0; c < 4 && codes[c]; c++) {
            const int button = codes[c] - INPUT_GAMEPAD_BASE;
            if (INPUT_IS_GAMEPAD(codes[c]) && (button == GAMEPAD_START || button == GAMEPAD_GUIDE)) {
                tu_warn(w, "[%s.c64_keys] %s: Start and Guide always open the menu", sect, name);
            } else if (INPUT_IS_GAMEPAD(codes[c])) {
                pad_c64_key[button] = (uint8_t)c64;
            } else tu_warn(w, "[%s.c64_keys] %s: only gamepad buttons (the keyboard types C64 keys itself)", sect, name);
        }
    }
    return true;
}
