// GCR disk surfaces: .d64 -> GCR, .g64 in and out, sector decoding. See gcr.h.
#include "media/gcr.h"

#include "rom/sha1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- GCR code ---------------------------------------------------------------------
static const uint8_t gcr_enc[16] = {
    0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
    0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15,
};

static int gcr_dec(uint8_t code) {
    for (int i = 0; i < 16; i++) {
        if (gcr_enc[i] == code) return i;
    }
    return -1;
}

void gcr_encode4(const uint8_t in[4], uint8_t out[5]) {
    uint64_t bits = 0;  // 40 bits, first byte's high nybble in the top bits
    for (int i = 0; i < 4; i++) {
        bits = (bits << 10) | ((uint64_t)gcr_enc[in[i] >> 4] << 5) | gcr_enc[in[i] & 15];
    }
    for (int i = 4; i >= 0; i--) {
        out[i] = (uint8_t)bits;
        bits >>= 8;
    }
}

bool gcr_decode4(const uint8_t in[5], uint8_t out[4]) {
    uint64_t bits = 0;
    for (int i = 0; i < 5; i++) bits = (bits << 8) | in[i];
    for (int i = 3; i >= 0; i--) {
        const int lo = gcr_dec((uint8_t)(bits & 31));
        const int hi = gcr_dec((uint8_t)((bits >> 5) & 31));
        if (lo < 0 || hi < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
        bits >>= 10;
    }
    return true;
}

// ---- standard layout --------------------------------------------------------------
int gcr_speed_zone(int track) {
    return track <= 17 ? 3 : track <= 24 ? 2 : track <= 30 ? 1 : 0;
}

int gcr_sectors_per_track(int track) {
    static const int spt[4] = { 17, 18, 19, 21 };
    return spt[gcr_speed_zone(track)];
}

int gcr_track_bytes(int track) {
    // 300 rpm = 200 ms per revolution; bit cells of 4.00/3.75/3.50/3.25 us.
    static const int bytes[4] = { 6250, 6666, 7142, 7692 };
    return bytes[gcr_speed_zone(track)];
}

// ---- .d64 -> GCR ------------------------------------------------------------------
#define SYNC_BYTES   5
#define HEADER_GCR   10     // 8 bytes
#define HEADER_GAP   9      // $55 between header and data block
#define DATA_GCR     325    // 260 bytes
#define SECTOR_BYTES (SYNC_BYTES + HEADER_GCR + HEADER_GAP + SYNC_BYTES + DATA_GCR)

// d64 error-byte codes (one per sector after the sector data; $00/$01 = no error).
enum {
    D64E_NO_HEADER = 0x02,      // 20 READ ERROR: header block not found
    D64E_NO_SYNC = 0x03,        // 21: no sync mark
    D64E_NO_DATA = 0x04,        // 22: data block not present
    D64E_DATA_CHECKSUM = 0x05,  // 23: checksum error in data block
    D64E_HEADER_CHECKSUM = 0x09,// 27: checksum error in header
    D64E_ID_MISMATCH = 0x0B,    // 29: disk ID mismatch
};

static uint8_t* put_gcr(uint8_t* p, const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; i += 4) {
        gcr_encode4(bytes + i, p);
        p += 5;
    }
    return p;
}

static uint8_t* write_sector(uint8_t* p, int track, int sector, const uint8_t* data,
                             uint8_t id1, uint8_t id2, uint8_t error) {
    const uint8_t sync = error == D64E_NO_SYNC ? 0x55 : 0xFF;
    memset(p, sync, SYNC_BYTES);
    p += SYNC_BYTES;
    if (error == D64E_ID_MISMATCH) id1 ^= 0xFF;
    uint8_t hdr[8] = { 0x08, 0, (uint8_t)sector, (uint8_t)track, id2, id1, 0x0F, 0x0F };
    hdr[1] = (uint8_t)(hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]);
    if (error == D64E_NO_HEADER) hdr[0] = 0x00;
    if (error == D64E_HEADER_CHECKSUM) hdr[1] ^= 0xFF;
    p = put_gcr(p, hdr, sizeof(hdr));
    memset(p, 0x55, HEADER_GAP);
    p += HEADER_GAP;
    memset(p, sync, SYNC_BYTES);
    p += SYNC_BYTES;
    uint8_t blk[260];
    blk[0] = error == D64E_NO_DATA ? 0x00 : 0x07;
    memcpy(blk + 1, data, 256);
    uint8_t sum = 0;
    for (int i = 0; i < 256; i++) sum ^= data[i];
    blk[257] = error == D64E_DATA_CHECKSUM ? (uint8_t)(sum ^ 0xFF) : sum;
    blk[258] = blk[259] = 0;
    return put_gcr(p, blk, sizeof(blk));
}

bool gcr_from_d64(const d64_t* d, gcr_image_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    const size_t sectors = d64_data_size(d) / 256;
    const uint8_t* errors = d->size >= sectors * 257 ? d->data + sectors * 256 : NULL;
    size_t total = 0;
    for (int t = 1; t <= d->tracks; t++) total += (size_t)gcr_track_bytes(t);
    out->buf = malloc(total);
    if (!out->buf) {
        if (err) snprintf(err, err_size, "out of memory");
        return false;
    }
    // Disk ID from the BAM (track 18, sector 0, bytes $A2/$A3).
    uint8_t name[16], id[5];
    d64_disk_name(d, name, id);
    uint8_t* p = out->buf;
    size_t offset = 0;      // sector index into the image
    for (int t = 1; t <= d->tracks; t++) {
        const int ht = (t - 1) * 2;
        const int spt = gcr_sectors_per_track(t);
        const int size = gcr_track_bytes(t);
        out->disk.track[ht] = p;
        out->disk.size[ht] = (uint16_t)size;
        const int gap = (size - spt * SECTOR_BYTES) / spt;
        uint8_t* q = p;
        for (int s = 0; s < spt; s++, offset++) {
            const uint8_t e = errors ? errors[offset] : 0x01;
            q = write_sector(q, t, s, d->data + offset * 256, id[0], id[1], e);
            memset(q, 0x55, (size_t)gap);
            q += gap;
        }
        memset(q, 0x55, (size_t)(p + size - q));  // rest of the revolution
        p += size;
    }
    return true;
}

// ---- .g64 ---------------------------------------------------------------------------
#define G64_HEADER 12

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

bool gcr_g64_signature(const uint8_t* data, size_t size) {
    return size >= G64_HEADER && !memcmp(data, "GCR-1541", 8);
}

// Validates the image; calls fn(user, halftrack, bytes, len) for each present track.
typedef void (*g64_track_fn)(void* user, int ht, const uint8_t* bytes, uint16_t len);

static bool g64_walk(const uint8_t* data, size_t size, g64_track_fn fn, void* user, char* err, size_t err_size) {
    if (!gcr_g64_signature(data, size)) {
        if (err) snprintf(err, err_size, "not a .g64 image");
        return false;
    }
    if (data[8] != 0) {
        if (err) snprintf(err, err_size, ".g64 version %d is not supported", data[8]);
        return false;
    }
    const int n = data[9];
    if (n == 0 || n > CORE_DISK_HALFTRACKS || size < G64_HEADER + (size_t)n * 8) {
        if (err) snprintf(err, err_size, ".g64 with %d tracks is not supported (1541 images have up to %d)", n, CORE_DISK_HALFTRACKS);
        return false;
    }
    for (int i = 0; i < n; i++) {
        const uint32_t off = rd32(data + G64_HEADER + 4 * i);
        if (off == 0) continue;
        if (off > size - 2) goto bad;
        const uint16_t len = (uint16_t)(data[off] | (data[off + 1] << 8));
        if (len == 0 || len > CORE_DISK_TRACK_MAX || (size_t)len > size - 2 - off) goto bad;
        if (fn) fn(user, i, data + off + 2, len);
        continue;
    bad:
        if (err) snprintf(err, err_size, ".g64 track %d.%d is damaged", i / 2 + 1, (i & 1) * 5);
        return false;
    }
    return true;
}

typedef struct { gcr_image_t* img; size_t used; } g64_load_t;

static void g64_count(void* user, int ht, const uint8_t* bytes, uint16_t len) {
    (void)ht; (void)bytes;
    ((g64_load_t*)user)->used += len;
}

static void g64_copy(void* user, int ht, const uint8_t* bytes, uint16_t len) {
    g64_load_t* l = user;
    uint8_t* dst = l->img->buf + l->used;
    memcpy(dst, bytes, len);
    l->img->disk.track[ht] = dst;
    l->img->disk.size[ht] = len;
    l->used += len;
}

bool gcr_from_g64(const uint8_t* data, size_t size, gcr_image_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    g64_load_t l = { out, 0 };
    if (!g64_walk(data, size, g64_count, &l, err, err_size)) return false;
    out->buf = malloc(l.used ? l.used : 1);
    if (!out->buf) {
        if (err) snprintf(err, err_size, "out of memory");
        return false;
    }
    l.used = 0;
    g64_walk(data, size, g64_copy, &l, NULL, 0);
    return true;
}

typedef struct { sha1_ctx_t ctx; } g64_hash_t;

static void g64_hash(void* user, int ht, const uint8_t* bytes, uint16_t len) {
    (void)ht;
    const uint8_t le[2] = { (uint8_t)len, (uint8_t)(len >> 8) };
    sha1_update(&((g64_hash_t*)user)->ctx, le, 2);
    sha1_update(&((g64_hash_t*)user)->ctx, bytes, len);
}

void gcr_g64_sha1(const uint8_t* data, size_t size, char out[41]) {
    g64_hash_t h;
    sha1_init(&h.ctx);
    g64_walk(data, size, g64_hash, &h, NULL, 0);
    uint8_t digest[20];
    sha1_final(&h.ctx, digest);
    for (int i = 0; i < 20; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static uint16_t g64_max_track(const core_disk_t* disk) {
    uint16_t max = 7928;    // the size most tools use; never smaller
    for (int i = 0; i < CORE_DISK_HALFTRACKS; i++) {
        if (disk->track[i] && disk->size[i] > max) max = disk->size[i];
    }
    return max;
}

size_t gcr_g64_size(const core_disk_t* disk) {
    const size_t slot = 2u + g64_max_track(disk);
    size_t total = G64_HEADER + CORE_DISK_HALFTRACKS * 8;
    for (int i = 0; i < CORE_DISK_HALFTRACKS; i++) {
        if (disk->track[i]) total += slot;
    }
    return total;
}

size_t gcr_to_g64(const core_disk_t* disk, uint8_t* out, size_t cap) {
    const size_t total = gcr_g64_size(disk);
    if (cap < total) return 0;
    memset(out, 0, total);
    const uint16_t max = g64_max_track(disk);
    memcpy(out, "GCR-1541", 8);
    out[8] = 0;
    out[9] = CORE_DISK_HALFTRACKS;
    out[10] = (uint8_t)max;
    out[11] = (uint8_t)(max >> 8);
    size_t pos = G64_HEADER + CORE_DISK_HALFTRACKS * 8;
    for (int i = 0; i < CORE_DISK_HALFTRACKS; i++) {
        wr32(out + G64_HEADER + 4 * (CORE_DISK_HALFTRACKS + i), (uint32_t)gcr_speed_zone(i / 2 + 1));
        if (!disk->track[i]) continue;
        wr32(out + G64_HEADER + 4 * i, (uint32_t)pos);
        out[pos] = (uint8_t)disk->size[i];
        out[pos + 1] = (uint8_t)(disk->size[i] >> 8);
        memcpy(out + pos + 2, disk->track[i], disk->size[i]);
        pos += 2u + max;
    }
    return total;
}

void gcr_free(gcr_image_t* img) {
    free(img->buf);
    memset(img, 0, sizeof(*img));
}

// ---- reading sectors ------------------------------------------------------------------
// Bit reader over a track ring, the way the drive sees it: a SYNC is 10 or more 1
// bits; the first byte after it starts at the first 0 bit.
typedef struct {
    const uint8_t* data;
    uint32_t bits;      // track length in bits
    uint32_t pos;       // next bit
    uint32_t read;      // bits read so far (to stop after a couple of revolutions)
} bitreader_t;

static int next_bit(bitreader_t* r) {
    const int b = (r->data[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos = (r->pos + 1) % r->bits;
    r->read++;
    return b;
}

// Skip to just after the next SYNC. False if none within the limit.
static bool find_sync(bitreader_t* r, uint32_t limit) {
    int ones = 0;
    while (r->read < limit) {
        if (next_bit(r)) {
            ones++;
        } else {
            if (ones >= 10) {
                r->pos = (r->pos + r->bits - 1) % r->bits;   // un-read the 0 bit
                r->read--;
                return true;
            }
            ones = 0;
        }
    }
    return false;
}

static void read_bytes(bitreader_t* r, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) v = (uint8_t)((v << 1) | next_bit(r));
        out[i] = v;
    }
}

static bool decode_block(const uint8_t* gcr, size_t gcr_len, uint8_t* out) {
    for (size_t i = 0, o = 0; i < gcr_len; i += 5, o += 4) {
        if (!gcr_decode4(gcr + i, out + o)) return false;
    }
    return true;
}

bool gcr_read_sector(const core_disk_t* disk, int track, int sector, uint8_t out[256]) {
    if (track < 1 || track > CORE_DISK_HALFTRACKS / 2) return false;
    const int ht = (track - 1) * 2;
    if (!disk->track[ht] || disk->size[ht] < 16) return false;
    bitreader_t r = { disk->track[ht], (uint32_t)disk->size[ht] * 8, 0, 0 };
    const uint32_t limit = r.bits * 2 + 1024;
    while (find_sync(&r, limit)) {
        uint8_t g[HEADER_GCR], hdr[8];
        read_bytes(&r, g, sizeof(g));
        if (!decode_block(g, sizeof(g), hdr) || hdr[0] != 0x08) continue;
        if (hdr[2] != sector || hdr[3] != track) continue;
        if ((hdr[1] ^ hdr[2] ^ hdr[3] ^ hdr[4] ^ hdr[5]) != 0) return false;
        if (!find_sync(&r, limit)) return false;
        uint8_t gd[DATA_GCR], blk[260];
        read_bytes(&r, gd, sizeof(gd));
        if (!decode_block(gd, sizeof(gd), blk) || blk[0] != 0x07) return false;
        uint8_t sum = 0;
        for (int i = 1; i <= 256; i++) sum ^= blk[i];
        if (sum != blk[257]) return false;
        memcpy(out, blk + 1, 256);
        return true;
    }
    return false;
}
