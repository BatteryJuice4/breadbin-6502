// .t64 tape archive reader (see t64.h).
#include "media/t64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

bool t64_is_t64(const uint8_t* data, size_t size) {
    return data && size >= 64 && !memcmp(data, "C64", 3);
}

static int by_offset(const void* a, const void* b) {
    const t64_entry_t* x = *(const t64_entry_t* const*)a;
    const t64_entry_t* y = *(const t64_entry_t* const*)b;
    return (x->offset > y->offset) - (x->offset < y->offset);
}

bool t64_open(const uint8_t* data, size_t size, t64_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (!t64_is_t64(data, size)) {
        if (err) snprintf(err, err_size, "not a .t64 tape archive");
        return false;
    }
    const unsigned max = rd16(data + 0x22);
    for (unsigned i = 0; i < max && out->count < T64_MAX_ENTRIES; i++) {
        const size_t eo = 0x40 + (size_t)i * 32;
        if (eo + 32 > size) break;
        const uint8_t* e = data + eo;
        if (e[0] == 0) continue;                // free slot
        t64_entry_t* o = &out->entries[out->count];
        o->start = rd16(e + 2);
        const uint16_t end = rd16(e + 4);
        o->offset = rd32(e + 8);
        o->size = (end > o->start) ? (uint32_t)(end - o->start) : 0;
        memcpy(o->name, e + 16, 16);
        o->name_len = 16;
        while (o->name_len > 0 && (o->name[o->name_len - 1] == 0x20 || o->name[o->name_len - 1] == 0xA0)) o->name_len--;
        if (o->offset >= size) continue;
        out->count++;
    }
    // Correct sizes: an entry can't run past the next entry's data or the file end.
    t64_entry_t* sorted[T64_MAX_ENTRIES];
    for (int i = 0; i < out->count; i++) sorted[i] = &out->entries[i];
    qsort(sorted, (size_t)out->count, sizeof(sorted[0]), by_offset);
    for (int i = 0; i < out->count; i++) {
        const uint32_t limit = (i + 1 < out->count) ? sorted[i + 1]->offset : (uint32_t)size;
        const uint32_t room = limit > sorted[i]->offset ? limit - sorted[i]->offset : 0;
        if (sorted[i]->size == 0 || sorted[i]->size > room) sorted[i]->size = room;
    }
    if (out->count == 0) {
        if (err) snprintf(err, err_size, "the .t64 archive has no files");
        return false;
    }
    out->data = data;
    out->size = size;
    return true;
}

static bool match(const uint8_t* pat, size_t plen, const uint8_t* name, int nlen) {
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '*') return true;
        if ((int)i >= nlen) return false;
        if (pat[i] != '?' && pat[i] != name[i]) return false;
    }
    return (int)plen == nlen;
}

bool t64_load(const t64_t* t, const uint8_t* name, size_t name_len, uint8_t* out, size_t cap, size_t* len) {
    for (int i = 0; i < t->count; i++) {
        const t64_entry_t* e = &t->entries[i];
        // Tape LOAD with an empty name loads the next file; prefix matches like the KERNAL.
        const bool ok = name_len == 0 || match(name, name_len, e->name, e->name_len) ||
                        (name_len <= (size_t)e->name_len && !memcmp(name, e->name, name_len));
        if (!ok) continue;
        if ((size_t)e->size + 2 > cap) return false;
        out[0] = (uint8_t)e->start;
        out[1] = (uint8_t)(e->start >> 8);
        memcpy(out + 2, t->data + e->offset, e->size);
        *len = (size_t)e->size + 2;
        return true;
    }
    return false;
}
