// Patch mods: inline byte patches, IPS and BPS (see patch.h).
#include "mods/patch.h"

#include "util/crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) do { if (err) snprintf(err, err_size, __VA_ARGS__); goto fail; } while (0)

bool patch_apply_inline(uint8_t* buf, size_t size, const mod_patch_t* p, int count, char* err, size_t err_size) {
    // Check everything first, so a failing entry leaves the data untouched.
    for (int i = 0; i < count; i++) {
        if ((size_t)p[i].offset + p[i].len > size) {
            if (err) snprintf(err, err_size, "patch %d (offset 0x%X) is outside the game data (%zu bytes)", i + 1, (unsigned)p[i].offset, size);
            return false;
        }
        if (p[i].has_expect && memcmp(buf + p[i].offset, p[i].expect, p[i].len) != 0) {
            if (err) snprintf(err, err_size, "patch %d: the original bytes at 0x%X don't match (different version of the game?)", i + 1, (unsigned)p[i].offset);
            return false;
        }
    }
    for (int i = 0; i < count; i++) memcpy(buf + p[i].offset, p[i].bytes, p[i].len);
    return true;
}

bool patch_apply_ips(uint8_t* buf, size_t size, const uint8_t* ips, size_t n, char* err, size_t err_size) {
    uint8_t* out = NULL;
    if (n < 8 || memcmp(ips, "PATCH", 5) != 0) FAIL("not an IPS patch");
    out = malloc(size ? size : 1);
    if (!out) FAIL("out of memory");
    memcpy(out, buf, size);
    size_t pos = 5;
    for (;;) {
        if (pos + 3 > n) FAIL("IPS patch ends early");
        if (!memcmp(ips + pos, "EOF", 3)) {
            pos += 3;
            break;
        }
        const size_t off = (size_t)ips[pos] << 16 | (size_t)ips[pos + 1] << 8 | ips[pos + 2];
        pos += 3;
        if (pos + 2 > n) FAIL("IPS patch ends early");
        size_t len = (size_t)ips[pos] << 8 | ips[pos + 1];
        pos += 2;
        if (len == 0) {     // run-length record
            if (pos + 3 > n) FAIL("IPS patch ends early");
            len = (size_t)ips[pos] << 8 | ips[pos + 1];
            const uint8_t v = ips[pos + 2];
            pos += 3;
            if (off + len > size) FAIL("IPS patch writes past the end of the game data (0x%zX)", off + len);
            memset(out + off, v, len);
        } else {
            if (pos + len > n) FAIL("IPS patch ends early");
            if (off + len > size) FAIL("IPS patch writes past the end of the game data (0x%zX)", off + len);
            memcpy(out + off, ips + pos, len);
            pos += len;
        }
    }
    // Optional 3-byte truncation size: only allowed if it doesn't change the size.
    if (pos + 3 <= n) {
        const size_t trunc = (size_t)ips[pos] << 16 | (size_t)ips[pos + 1] << 8 | ips[pos + 2];
        if (trunc != size) FAIL("IPS patch would change the size of the game data");
    }
    memcpy(buf, out, size);
    free(out);
    return true;
fail:
    free(out);
    return false;
}

static bool bps_num(const uint8_t* p, size_t end, size_t* pos, uint64_t* out) {
    uint64_t data = 0, shift = 1;
    for (int i = 0; i < 10; i++) {
        if (*pos >= end) return false;
        const uint8_t x = p[(*pos)++];
        data += (uint64_t)(x & 0x7F) * shift;
        if (x & 0x80) {
            *out = data;
            return true;
        }
        shift <<= 7;
        data += shift;
    }
    return false;
}

static uint32_t rd32le(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

bool patch_apply_bps(uint8_t* buf, size_t size, const uint8_t* bps, size_t n, char* err, size_t err_size) {
    uint8_t* out = NULL;
    if (n < 4 + 3 + 12 || memcmp(bps, "BPS1", 4) != 0) FAIL("not a BPS patch");
    if (crc32_of(bps, n - 4) != rd32le(bps + n - 4)) FAIL("BPS patch is damaged (checksum)");
    if (crc32_of(buf, size) != rd32le(bps + n - 12)) FAIL("BPS patch was made for different game data (source checksum)");
    const size_t end = n - 12;
    size_t pos = 4;
    uint64_t src_size, dst_size, meta;
    if (!bps_num(bps, end, &pos, &src_size) || !bps_num(bps, end, &pos, &dst_size) || !bps_num(bps, end, &pos, &meta)) FAIL("BPS header is damaged");
    if (src_size != size) FAIL("BPS patch was made for %llu bytes of data, not %zu", (unsigned long long)src_size, size);
    if (dst_size != size) FAIL("BPS patch would change the size of the game data");
    if (meta > end - pos) FAIL("BPS header is damaged");
    pos += (size_t)meta;
    out = malloc(size ? size : 1);
    if (!out) FAIL("out of memory");
    size_t o = 0;
    int64_t src_rel = 0, dst_rel = 0;
    while (pos < end) {
        uint64_t d;
        if (!bps_num(bps, end, &pos, &d)) FAIL("BPS patch is damaged");
        const unsigned cmd = (unsigned)(d & 3);
        const uint64_t len = (d >> 2) + 1;
        if (len > size - o) FAIL("BPS patch writes past the end");
        switch (cmd) {
            case 0:     // SourceRead
                memcpy(out + o, buf + o, (size_t)len);
                o += (size_t)len;
                break;
            case 1:     // TargetRead
                if (len > end - pos) FAIL("BPS patch is damaged");
                memcpy(out + o, bps + pos, (size_t)len);
                pos += (size_t)len;
                o += (size_t)len;
                break;
            case 2:     // SourceCopy
            case 3: {   // TargetCopy
                uint64_t v;
                if (!bps_num(bps, end, &pos, &v)) FAIL("BPS patch is damaged");
                const int64_t delta = (int64_t)(v >> 1) * ((v & 1) ? -1 : 1);
                int64_t* rel = cmd == 2 ? &src_rel : &dst_rel;
                *rel += delta;
                if (*rel < 0 || (uint64_t)*rel + len > (cmd == 2 ? size : o + len)) FAIL("BPS patch is damaged (copy range)");
                if (cmd == 2) {
                    memcpy(out + o, buf + *rel, (size_t)len);
                    o += (size_t)len;
                    *rel += (int64_t)len;
                } else {
                    // byte by byte: the source may overlap what's being written (RLE-style)
                    for (uint64_t k = 0; k < len; k++) {
                        if ((uint64_t)*rel >= o) FAIL("BPS patch is damaged (copy range)");
                        out[o++] = out[(*rel)++];
                    }
                }
                break;
            }
        }
    }
    if (o != size) FAIL("BPS patch is incomplete");
    if (crc32_of(out, size) != rd32le(bps + n - 8)) FAIL("BPS result doesn't match the expected checksum");
    memcpy(buf, out, size);
    free(out);
    return true;
fail:
    free(out);
    return false;
}

bool patch_apply_mod(uint8_t* buf, size_t size, const mod_t* m, char* err, size_t err_size) {
    // Work on a copy so the mod applies entirely or not at all.
    uint8_t* tmp = malloc(size ? size : 1);
    if (!tmp) {
        if (err) snprintf(err, err_size, "out of memory");
        return false;
    }
    memcpy(tmp, buf, size);
    bool ok = patch_apply_inline(tmp, size, m->patches, m->patch_count, err, err_size);
    for (int i = 0; ok && i < m->file_count; i++) {
        char e[200];
        const mod_file_t* f = &m->files[i];
        ok = f->kind == MOD_FILE_IPS ? patch_apply_ips(tmp, size, f->data, f->size, e, sizeof(e))
                                     : patch_apply_bps(tmp, size, f->data, f->size, e, sizeof(e));
        if (!ok && err) snprintf(err, err_size, "%s: %s", f->name, e);
    }
    if (ok) memcpy(buf, tmp, size);
    free(tmp);
    return ok;
}
