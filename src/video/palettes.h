#pragma once
/*
    Built-in C64 colour palettes. Display only: applied to the copy sent to the
    screen, never to emulated state, memory or replay fingerprints.
    Palette ids in settings and game packs: a built-in id ("colodore", "pepto"),
    or "mod:<mod id>" for a palette mod.
*/
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PALETTE_DEFAULT "colodore"
#define PALETTE_ID_MAX 72       // "mod:" + a mod id

typedef struct {
    const char* id;
    const char* name;           // shown in the Video menu
    uint32_t rgb[16];           // 0xRRGGBB
} palette_t;

int palette_builtin_count(void);
const palette_t* palette_builtin(int index);
const palette_t* palette_find_builtin(const char* id);     // NULL if not built in
// True if id is a built-in id or "mod:<valid mod id>" (which may not be installed).
bool palette_id_valid(const char* id);

#ifdef __cplusplus
}
#endif
