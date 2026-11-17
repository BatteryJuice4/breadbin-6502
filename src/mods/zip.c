// Minimal strict zip reader (see zip.h).
#include "mods/zip.h"

#include "util/crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"  // raw deflate decoder (implemented in util/stb_impl.c)

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

#define FAIL(...) do { if (err) snprintf(err, err_size, __VA_ARGS__); return false; } while (0)

static bool name_ok(const char* n) {
    if (!n[0] || n[0] == '/' || strchr(n, '\\') || strchr(n, ':')) return false;
    // no "." or ".." path parts
    const char* p = n;
    while (*p) {
        const char* e = strchr(p, '/');
        const size_t len = e ? (size_t)(e - p) : strlen(p);
        if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return false;
        if (!e) break;
        p = e + 1;
    }
    for (const unsigned char* c = (const unsigned char*)n; *c; c++) {
        if (*c < 0x20) return false;
    }
    return true;
}

bool zip_open(zip_t* z, const uint8_t* data, size_t size, char* err, size_t err_size) {
    memset(z, 0, sizeof(*z));
    if (!data || size < 22) FAIL("not a zip file");
    // End of central directory: last 22 bytes + up to 64 KB comment.
    size_t eocd = (size_t)-1;
    const size_t min = size > 22 + 0xFFFF ? size - 22 - 0xFFFF : 0;
    for (size_t i = size - 22 + 1; i-- > min;) {
        if (rd32(data + i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd == (size_t)-1) FAIL("not a zip file (no central directory)");
    const uint8_t* e = data + eocd;
    if (rd16(e + 4) != 0 || rd16(e + 6) != 0) FAIL("multi-part zip files aren't supported");
    const unsigned n = rd16(e + 10);
    const uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (n == 0xFFFF || cd_off == 0xFFFFFFFFu) FAIL("zip64 archives aren't supported");
    if (n > ZIP_MAX_ENTRIES) FAIL("too many files in the package (%u, at most %d)", n, ZIP_MAX_ENTRIES);
    if ((size_t)cd_off + cd_size > eocd) FAIL("damaged zip file (central directory)");
    size_t pos = cd_off;
    uint64_t total = 0;
    for (unsigned i = 0; i < n; i++) {
        if (pos + 46 > eocd || rd32(data + pos) != 0x02014b50) FAIL("damaged zip file (entry %u)", i + 1);
        const uint8_t* c = data + pos;
        zip_entry_t* o = &z->entries[z->count];
        const uint16_t flags = rd16(c + 8);
        o->method = rd16(c + 10);
        o->crc = rd32(c + 16);
        o->comp_size = rd32(c + 20);
        o->size = rd32(c + 24);
        const unsigned nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32);
        o->local_offset = rd32(c + 42);
        if (pos + 46 + nlen + xlen + clen > eocd) FAIL("damaged zip file (entry %u)", i + 1);
        if (nlen == 0 || nlen > ZIP_MAX_NAME) FAIL("file name too long in the package");
        memcpy(o->name, c + 46, nlen);
        o->name[nlen] = 0;
        if (memchr(o->name, 0, nlen)) FAIL("bad file name in the package");
        if (flags & 1) FAIL("encrypted packages aren't supported (%s)", o->name);
        if (o->size == 0xFFFFFFFFu || o->comp_size == 0xFFFFFFFFu) FAIL("zip64 entries aren't supported");
        o->is_dir = o->name[nlen - 1] == '/';
        if (o->is_dir) o->name[nlen - 1] = 0;
        if (o->name[0] && !name_ok(o->name)) FAIL("unsafe file name in the package: %s", o->name);
        if (!o->is_dir) {
            if (o->method != 0 && o->method != 8) FAIL("%s: unsupported compression (method %u)", o->name, o->method);
            if (o->size > ZIP_MAX_ENTRY_SIZE) FAIL("%s is too large (%u bytes)", o->name, o->size);
            total += o->size;
            if (total > ZIP_MAX_TOTAL_SIZE) FAIL("the package is too large when unpacked");
        }
        if (o->name[0]) z->count++;
        pos += 46 + nlen + xlen + clen;
    }
    z->data = data;
    z->size = size;
    return true;
}

int zip_find(const zip_t* z, const char* name) {
    for (int i = 0; i < z->count; i++) {
        if (!z->entries[i].is_dir && !strcmp(z->entries[i].name, name)) return i;
    }
    return -1;
}

uint8_t* zip_extract(const zip_t* z, int index, size_t* size, char* err, size_t err_size) {
#undef FAIL
#define FAIL(...) do { if (err) snprintf(err, err_size, __VA_ARGS__); return NULL; } while (0)
    if (index < 0 || index >= z->count || z->entries[index].is_dir) FAIL("no such file in the package");
    const zip_entry_t* e = &z->entries[index];
    const size_t lo = e->local_offset;
    if (lo + 30 > z->size || rd32(z->data + lo) != 0x04034b50) FAIL("%s: damaged zip entry", e->name);
    const size_t start = lo + 30 + rd16(z->data + lo + 26) + rd16(z->data + lo + 28);
    if (start > z->size || e->comp_size > z->size - start) FAIL("%s: damaged zip entry", e->name);
    uint8_t* out = malloc((size_t)e->size + 1);
    if (!out) FAIL("out of memory");
    const uint8_t* src = z->data + start;
    if (e->method == 0) {
        if (e->comp_size != e->size) { free(out); FAIL("%s: damaged zip entry", e->name); }
        memcpy(out, src, e->size);
    } else if (e->size > 0) {
        // Fixed-size output: inflating more than the declared size fails.
        const int n = stbi_zlib_decode_noheader_buffer((char*)out, (int)e->size, (const char*)src, (int)e->comp_size);
        if (n != (int)e->size) { free(out); FAIL("%s: can't unpack (damaged or wrong size)", e->name); }
    }
    if (crc32_of(out, e->size) != e->crc) { free(out); FAIL("%s: checksum mismatch (damaged package)", e->name); }
    out[e->size] = 0;
    *size = e->size;
    return out;
}
