// Built-in palettes (see palettes.h).
#include "video/palettes.h"

#include <string.h>

static const palette_t builtins[] = {
    // Colodore by Philip "Pepto" Timmermann (colodore.com, 2017)
    { "colodore", "Colodore", {
        0x000000, 0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97, 0x56AC4D, 0x2E2C9B, 0xEDF171,
        0x8E5029, 0x553800, 0xC46C71, 0x4A4A4A, 0x7B7B7B, 0xA9FF9F, 0x706DEB, 0xB2B2B2 } },
    // Pepto's earlier PAL measurement (pepto.de/projects/colorvic, 2001)
    { "pepto", "Pepto (PAL)", {
        0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2, 0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
        0x6F4F25, 0x433900, 0x9A6759, 0x444444, 0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595 } },
};

int palette_builtin_count(void) {
    return (int)(sizeof(builtins) / sizeof(builtins[0]));
}

const palette_t* palette_builtin(int i) {
    return (i >= 0 && i < palette_builtin_count()) ? &builtins[i] : NULL;
}

const palette_t* palette_find_builtin(const char* id) {
    for (int i = 0; i < palette_builtin_count(); i++) {
        if (!strcmp(builtins[i].id, id)) return &builtins[i];
    }
    return NULL;
}

bool palette_id_valid(const char* id) {
    if (palette_find_builtin(id)) return true;
    if (strncmp(id, "mod:", 4) != 0) return false;
    const char* m = id + 4;
    const size_t n = strlen(m);
    if (n == 0 || n >= 64 || m[0] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        const char c = m[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}
