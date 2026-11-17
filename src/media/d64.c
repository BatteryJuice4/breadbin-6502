// .d64 disk image reader (see d64.h).
#include "media/d64.h"

#include <stdio.h>
#include <string.h>

#define D64_SECTORS_35 683
#define D64_SECTORS_40 768

static int sectors_in_track(int t) {
    if (t <= 17) return 21;
    if (t <= 24) return 19;
    if (t <= 30) return 18;
    return 17;
}

// Byte offset of a sector, or -1 if track/sector is out of range.
static long sector_offset(const d64_t* d, int t, int s) {
    if (t < 1 || t > d->tracks || s < 0 || s >= sectors_in_track(t)) return -1;
    long n = 0;
    for (int i = 1; i < t; i++) n += sectors_in_track(i);
    return (n + s) * 256;
}

bool d64_size_ok(size_t size) {
    return size == D64_SECTORS_35 * 256 || size == D64_SECTORS_35 * 257 ||
           size == D64_SECTORS_40 * 256 || size == D64_SECTORS_40 * 257;
}

bool d64_open(const uint8_t* data, size_t size, d64_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (!data || !d64_size_ok(size)) {
        if (err) snprintf(err, err_size, "not a .d64 image (unexpected size %zu bytes)", size);
        return false;
    }
    out->data = data;
    out->size = size;
    out->tracks = (size >= D64_SECTORS_40 * 256) ? 40 : 35;
    return true;
}

size_t d64_data_size(const d64_t* d) {
    return (size_t)(d->tracks == 40 ? D64_SECTORS_40 : D64_SECTORS_35) * 256;
}

int d64_dir(const d64_t* d, d64_entry_t* out, int max) {
    int n = 0, t = 18, s = 1, steps = 0;
    while (t != 0 && steps++ < 19) {    // track 18 has 19 sectors; more means a loop
        const long off = sector_offset(d, t, s);
        if (off < 0) break;
        const uint8_t* b = d->data + off;
        for (int i = 0; i < 8; i++) {
            const uint8_t* e = b + i * 32;
            if ((e[2] & 7) == 0 && !(e[2] & 0x80)) continue;   // scratched / empty
            if (n >= max) return n;
            d64_entry_t* o = &out[n++];
            o->type = e[2];
            o->track = e[3];
            o->sector = e[4];
            memcpy(o->name, e + 5, 16);
            o->name_len = 16;
            while (o->name_len > 0 && o->name[o->name_len - 1] == 0xA0) o->name_len--;
            o->blocks = (uint16_t)(e[30] | (e[31] << 8));
        }
        t = b[0];
        s = b[1];
    }
    return n;
}

void d64_disk_name(const d64_t* d, uint8_t name[16], uint8_t id[5]) {
    const uint8_t* bam = d->data + sector_offset(d, 18, 0);
    memcpy(name, bam + 0x90, 16);
    memcpy(id, bam + 0xA2, 5);
}

static bool match(const uint8_t* pat, size_t plen, const uint8_t* name, int nlen) {
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '*') return true;
        if ((int)i >= nlen) return false;
        if (pat[i] != '?' && pat[i] != name[i]) return false;
    }
    return (int)plen == nlen;
}

bool d64_find(const d64_t* d, const uint8_t* pat, size_t plen, d64_entry_t* out) {
    // Strip a drive prefix ("0:", ":"), and anything after a comma (",P,R").
    for (size_t i = 0; i < plen && i < 3; i++) {
        if (pat[i] == ':') {
            pat += i + 1;
            plen -= i + 1;
            break;
        }
    }
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == ',') {
            plen = i;
            break;
        }
    }
    if (plen == 0) return false;
    static d64_entry_t entries[D64_MAX_ENTRIES];
    const int n = d64_dir(d, entries, D64_MAX_ENTRIES);
    for (int i = 0; i < n; i++) {
        const uint8_t type = entries[i].type & 7;
        if (!(entries[i].type & 0x80) || (type != 2 && type != 3)) continue;   // closed PRG/USR
        if (match(pat, plen, entries[i].name, entries[i].name_len)) {
            *out = entries[i];
            return true;
        }
    }
    return false;
}

bool d64_read(const d64_t* d, const d64_entry_t* e, uint8_t* out, size_t cap, size_t* len) {
    int t = e->track, s = e->sector, steps = 0;
    size_t n = 0;
    while (steps++ < D64_SECTORS_40) {
        const long off = sector_offset(d, t, s);
        if (off < 0) return false;
        const uint8_t* b = d->data + off;
        const int last = (b[0] == 0) ? b[1] : 255;  // last sector: b[1] = index of the last byte
        if (b[0] == 0 && last < 1) return false;
        const size_t chunk = (size_t)(last - 1);
        if (n + chunk > cap) return false;
        memcpy(out + n, b + 2, chunk);
        n += chunk;
        if (b[0] == 0) {
            *len = n;
            return true;
        }
        t = b[0];
        s = b[1];
    }
    return false;   // chain loops
}

// BASIC line: link, line number, text, 0.
static bool put_line(uint8_t* out, size_t cap, size_t* n, uint16_t base, uint16_t num, const uint8_t* text, size_t tlen) {
    if (*n + 4 + tlen + 1 + 2 > cap) return false;
    const uint16_t next = (uint16_t)(base + (*n - 2) + 4 + tlen + 1);
    out[(*n)++] = (uint8_t)next;
    out[(*n)++] = (uint8_t)(next >> 8);
    out[(*n)++] = (uint8_t)num;
    out[(*n)++] = (uint8_t)(num >> 8);
    memcpy(out + *n, text, tlen);
    *n += tlen;
    out[(*n)++] = 0;
    return true;
}

static size_t quoted(uint8_t* dst, const uint8_t* name, int len) {
    size_t k = 0;
    dst[k++] = '"';
    for (int i = 0; i < len; i++) dst[k++] = name[i];
    dst[k++] = '"';
    return k;
}

static int free_blocks(const d64_t* d) {
    const uint8_t* bam = d->data + sector_offset(d, 18, 0);
    int n = 0;
    for (int t = 1; t <= 35; t++) {
        if (t != 18) n += bam[4 * t];
    }
    return n;
}

bool d64_load(const d64_t* d, const uint8_t* name, size_t name_len, uint8_t* out, size_t cap, size_t* len) {
    if (name_len == 1 && name[0] == '$') {
        const uint16_t base = 0x0401;   // the 1541 sends the listing for $0401
        size_t n = 0;
        if (cap < 2) return false;
        out[n++] = (uint8_t)base;
        out[n++] = (uint8_t)(base >> 8);
        uint8_t text[64], dname[16], id[5];
        d64_disk_name(d, dname, id);
        size_t k = 0;
        text[k++] = 0x12;   // reverse on
        int dlen = 16;
        while (dlen > 0 && dname[dlen - 1] == 0xA0) dlen--;
        k += quoted(text + k, dname, dlen);
        for (int i = dlen; i < 16; i++) text[k++] = ' ';
        text[k++] = ' ';
        for (int i = 0; i < 5; i++) text[k++] = id[i] == 0xA0 ? ' ' : id[i];
        if (!put_line(out, cap, &n, base, 0, text, k)) return false;
        static d64_entry_t entries[D64_MAX_ENTRIES];
        static const char* types[] = { "DEL", "SEQ", "PRG", "USR", "REL", "???", "???", "???" };
        const int count = d64_dir(d, entries, D64_MAX_ENTRIES);
        for (int i = 0; i < count; i++) {
            const d64_entry_t* e = &entries[i];
            k = 0;
            const int pad = e->blocks < 10 ? 3 : e->blocks < 100 ? 2 : 1;
            for (int p = 0; p < pad; p++) text[k++] = ' ';
            k += quoted(text + k, e->name, e->name_len);
            for (int p = e->name_len; p < 16; p++) text[k++] = ' ';
            text[k++] = (e->type & 0x80) ? ' ' : '*';
            memcpy(text + k, types[e->type & 7], 3);
            k += 3;
            if (e->type & 0x40) text[k++] = '<';
            if (!put_line(out, cap, &n, base, e->blocks, text, k)) return false;
        }
        static const char blocks_free[] = "BLOCKS FREE.";
        if (!put_line(out, cap, &n, base, (uint16_t)free_blocks(d), (const uint8_t*)blocks_free, sizeof(blocks_free) - 1)) {
            return false;
        }
        if (n + 2 > cap) return false;
        out[n++] = 0;   // end of program
        out[n++] = 0;
        *len = n;
        return true;
    }
    d64_entry_t e;
    return d64_find(d, name, name_len, &e) && d64_read(d, &e, out, cap, len);
}
