#pragma once
// Internal helpers shared by the settings and game pack parsers (tomlc17).
// Getters leave *out unchanged when the key is missing or invalid; invalid values
// add a warning line. They return true only when *out was set.
#include <stdbool.h>
#include <stddef.h>

#include "tomlc17.h"

typedef struct {
    char* buf;
    size_t size;
    size_t len;
} tu_warn_t;

void tu_warn_init(tu_warn_t* w, char* buf, size_t size);
void tu_warn(tu_warn_t* w, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

bool tu_get_bool(toml_datum_t tab, const char* key, bool* out, tu_warn_t* w, const char* sect);
bool tu_get_int(toml_datum_t tab, const char* key, int lo, int hi, int* out, tu_warn_t* w, const char* sect);
bool tu_get_float(toml_datum_t tab, const char* key, double lo, double hi, float* out, tu_warn_t* w, const char* sect);
// Copies a string value; too-long values are rejected with a warning.
bool tu_get_string(toml_datum_t tab, const char* key, char* out, size_t out_size, tu_warn_t* w, const char* sect);
// Key binding: "Name" or ["Name", ...]. Fills out[0..max-1] (0 = unbound) and returns
// true if the key was present (even if every name was unknown).
bool tu_get_keys(toml_datum_t tab, const char* key, int* out, int max, tu_warn_t* w, const char* sect);
// [input.c64_keys]: C64 key name = gamepad button(s), e.g. F1 = "Gamepad_X". Fills
// pad_c64_key (indexed by gamepad button) when the table is present; returns whether it was.
bool tu_get_c64_keys(toml_datum_t input_table, uint8_t pad_c64_key[32], tu_warn_t* w, const char* sect);
