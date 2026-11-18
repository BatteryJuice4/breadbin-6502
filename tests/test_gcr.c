// GCR disk surfaces (media/gcr.h): pure code, no ROMs needed. All images are
// generated here.
#include "media/d64.h"
#include "media/gcr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

#define D64_SECTORS 683

static uint8_t sector_byte(int index, int i) { return (uint8_t)(index * 31 + i * 7 + (i >> 3)); }

// 35-track image with recognizable sector contents and a BAM ID of "XY";
// errors (683 bytes) appended when given.
static uint8_t* make_d64(size_t* size, const uint8_t* errors) {
    *size = D64_SECTORS * 256 + (errors ? D64_SECTORS : 0);
    uint8_t* d = malloc(*size);
    for (int s = 0; s < D64_SECTORS; s++) {
        for (int i = 0; i < 256; i++) d[s * 256 + i] = sector_byte(s, i);
    }
    const int bam = 357;    // track 18 sector 0
    memset(d + bam * 256 + 0x90, 0xA0, 0x1B);
    d[bam * 256 + 0xA2] = 'X';
    d[bam * 256 + 0xA3] = 'Y';
    if (errors) memcpy(d + D64_SECTORS * 256, errors, D64_SECTORS);
    return d;
}

static int sector_index(int track, int sector) {
    int n = 0;
    for (int t = 1; t < track; t++) n += gcr_sectors_per_track(t);
    return n + sector;
}

static void test_code(void) {
    uint8_t in[4], g[5], out[4];
    for (int v = 0; v < 256; v++) {
        in[0] = (uint8_t)v; in[1] = (uint8_t)~v; in[2] = (uint8_t)(v * 3); in[3] = (uint8_t)(v ^ 0x5A);
        gcr_encode4(in, g);
        CHECK(gcr_decode4(g, out) && !memcmp(in, out, 4), "round trip %d", v);
        // GCR never has more than two 0 bits in a row (within the 40 bits).
        uint64_t bits = 0;
        for (int i = 0; i < 5; i++) bits = (bits << 8) | g[i];
        int zeros = 0, max_zeros = 0;
        for (int b = 39; b >= 0; b--) {
            zeros = (bits >> b) & 1 ? 0 : zeros + 1;
            if (zeros > max_zeros) max_zeros = zeros;
        }
        CHECK(max_zeros <= 2, "%d zero bits in a row for %d", max_zeros, v);
    }
    // 0x08 0x00 0x00 0x00 has a well-known encoding.
    gcr_encode4((const uint8_t[]){ 0x08, 0x00, 0x00, 0x00 }, g);
    CHECK(g[0] == 0x52 && g[1] == 0x54 && g[2] == 0xA5 && g[3] == 0x29 && g[4] == 0x4A,
          "header start %02X %02X %02X %02X %02X", g[0], g[1], g[2], g[3], g[4]);
    const uint8_t bad[5] = { 0, 0, 0, 0, 0 };
    CHECK(!gcr_decode4(bad, out), "invalid code accepted");
}

static void test_d64(void) {
    size_t size;
    uint8_t* img = make_d64(&size, NULL);
    d64_t d;
    char err[128];
    CHECK(d64_open(img, size, &d, err, sizeof(err)), "d64_open: %s", err);
    gcr_image_t g;
    CHECK(gcr_from_d64(&d, &g, err, sizeof(err)), "gcr_from_d64: %s", err);
    CHECK(g.disk.track[0] && g.disk.size[0] == 7692 && g.disk.size[34] == 7142 &&
          g.disk.size[48] == 6666 && g.disk.size[68] == 6250, "track sizes");
    CHECK(!g.disk.track[1] && !g.disk.track[70], "only whole tracks of a 35-track disk");
    int bad = 0;
    for (int t = 1; t <= 35; t++) {
        for (int s = 0; s < gcr_sectors_per_track(t); s++) {
            uint8_t buf[256];
            const int idx = sector_index(t, s);
            if (!gcr_read_sector(&g.disk, t, s, buf) || memcmp(buf, img + idx * 256, 256)) bad++;
        }
    }
    CHECK(bad == 0, "%d sectors didn't read back", bad);
    uint8_t buf[256];
    CHECK(!gcr_read_sector(&g.disk, 1, 21, buf), "sector 21 on track 1");
    CHECK(!gcr_read_sector(&g.disk, 36, 0, buf), "track 36 on a 35-track disk");

    // The header carries the BAM ID: first sync, then 08 chk 00 01 'Y' 'X' 0F 0F.
    uint8_t hdr[8];
    CHECK(g.disk.track[0][0] == 0xFF && g.disk.track[0][4] == 0xFF, "sync before the header");
    gcr_decode4(g.disk.track[0] + 5, hdr);
    gcr_decode4(g.disk.track[0] + 10, hdr + 4);
    CHECK(hdr[0] == 0x08 && hdr[2] == 0 && hdr[3] == 1 && hdr[4] == 'Y' && hdr[5] == 'X' && hdr[6] == 0x0F,
          "header %02X %02X %02X %02X %02X %02X", hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5]);
    CHECK(hdr[1] == (0 ^ 1 ^ 'Y' ^ 'X'), "header checksum");

    // A track turned by 3 bits (syncs no longer on byte boundaries) reads the same.
    const int ht = 34, n = g.disk.size[ht];
    uint8_t* turned = malloc((size_t)n);
    for (int i = 0; i < n; i++) {
        turned[i] = (uint8_t)((g.disk.track[ht][i] >> 3) | (g.disk.track[ht][(i + n - 1) % n] << 5));
    }
    memcpy(g.disk.track[ht], turned, (size_t)n);
    free(turned);
    CHECK(gcr_read_sector(&g.disk, 18, 7, buf) && !memcmp(buf, img + sector_index(18, 7) * 256, 256),
          "unaligned track");
    gcr_free(&g);
    free(img);
}

static void test_d64_errors(void) {
    uint8_t errors[D64_SECTORS];
    memset(errors, 0x01, sizeof(errors));
    errors[sector_index(1, 3)] = 0x02;      // header not found
    errors[sector_index(2, 4)] = 0x04;      // no data block
    errors[sector_index(3, 5)] = 0x05;      // data checksum
    errors[sector_index(4, 6)] = 0x09;      // header checksum
    errors[sector_index(5, 7)] = 0x03;      // no sync
    errors[sector_index(6, 8)] = 0x0B;      // ID mismatch
    size_t size;
    uint8_t* img = make_d64(&size, errors);
    d64_t d;
    char err[128];
    CHECK(d64_open(img, size, &d, err, sizeof(err)), "d64_open with errors: %s", err);
    gcr_image_t g;
    CHECK(gcr_from_d64(&d, &g, err, sizeof(err)), "gcr_from_d64: %s", err);
    uint8_t buf[256];
    CHECK(!gcr_read_sector(&g.disk, 1, 3, buf), "error 20");
    CHECK(!gcr_read_sector(&g.disk, 2, 4, buf), "error 22");
    CHECK(!gcr_read_sector(&g.disk, 3, 5, buf), "error 23");
    CHECK(!gcr_read_sector(&g.disk, 4, 6, buf), "error 27");
    CHECK(!gcr_read_sector(&g.disk, 5, 7, buf), "error 21");
    // An ID mismatch still reads (the DOS compares the ID; the sector is intact).
    CHECK(gcr_read_sector(&g.disk, 6, 8, buf), "error 29 sector readable");
    CHECK(gcr_read_sector(&g.disk, 1, 4, buf) && gcr_read_sector(&g.disk, 5, 6, buf), "neighbours intact");
    gcr_free(&g);
    free(img);
}

static void test_g64(void) {
    size_t size;
    uint8_t* img = make_d64(&size, NULL);
    d64_t d;
    char err[128];
    d64_open(img, size, &d, err, sizeof(err));
    gcr_image_t g;
    gcr_from_d64(&d, &g, err, sizeof(err));

    const size_t gsize = gcr_g64_size(&g.disk);
    uint8_t* g64 = malloc(gsize);
    CHECK(gcr_to_g64(&g.disk, g64, gsize - 1) == 0, "too small a buffer accepted");
    CHECK(gcr_to_g64(&g.disk, g64, gsize) == gsize, "write g64");
    CHECK(gcr_g64_signature(g64, gsize) && g64[9] == 84, "g64 header");
    CHECK(g64[12 + 4 * 84] == 3 && g64[12 + 4 * (84 + 68)] == 0, "speed zones");

    gcr_image_t back;
    CHECK(gcr_from_g64(g64, gsize, &back, err, sizeof(err)), "read g64: %s", err);
    int same = 1;
    for (int i = 0; i < CORE_DISK_HALFTRACKS; i++) {
        if (!g.disk.track[i] != !back.disk.track[i] || g.disk.size[i] != back.disk.size[i]) same = 0;
        else if (g.disk.track[i] && memcmp(g.disk.track[i], back.disk.track[i], g.disk.size[i])) same = 0;
    }
    CHECK(same, "g64 round trip");
    uint8_t buf[256];
    CHECK(gcr_read_sector(&back.disk, 35, 16, buf) && !memcmp(buf, img + sector_index(35, 16) * 256, 256),
          "sector from g64");

    // The identity hash ignores layout and padding: the same tracks stored in
    // 9000-byte slots, in reverse order, hash the same.
    char h1[41], h2[41];
    gcr_g64_sha1(g64, gsize, h1);
    const size_t table = 12 + 84 * 8;
    const size_t bsize = table + 35 * (2 + 9000);
    uint8_t* big = calloc(1, bsize);
    memcpy(big, g64, table);
    big[10] = (uint8_t)9000; big[11] = (uint8_t)(9000 >> 8);
    for (int t = 0; t < 35; t++) {
        const int ht = t * 2;
        const size_t pos = table + (size_t)(34 - t) * (2 + 9000);
        big[12 + 4 * ht] = (uint8_t)pos; big[13 + 4 * ht] = (uint8_t)(pos >> 8);
        big[14 + 4 * ht] = (uint8_t)(pos >> 16); big[15 + 4 * ht] = 0;
        big[pos] = (uint8_t)g.disk.size[ht]; big[pos + 1] = (uint8_t)(g.disk.size[ht] >> 8);
        memcpy(big + pos + 2, g.disk.track[ht], g.disk.size[ht]);
    }
    gcr_g64_sha1(big, bsize, h2);
    CHECK(!strcmp(h1, h2) && strlen(h1) == 40, "g64 hash depends on layout");
    big[table + 34 * (2 + 9000) + 2 + 100] ^= 1;   // a data byte of track 1
    gcr_g64_sha1(big, bsize, h2);
    CHECK(strcmp(h1, h2) != 0, "g64 hash covers track data");
    free(big);

    // Damaged images are refused.
    big = malloc(gsize);
    gcr_image_t tmp;
    memcpy(big, g64, gsize);
    big[8] = 1;
    CHECK(!gcr_from_g64(big, gsize, &tmp, err, sizeof(err)), "version 1 accepted");
    memcpy(big, g64, gsize);
    big[12] = 0xF0; big[13] = 0xFF; big[14] = 0xFF; big[15] = 0x7F;  // track 1 offset past the end
    CHECK(!gcr_from_g64(big, gsize, &tmp, err, sizeof(err)), "bad offset accepted");
    memcpy(big, g64, gsize);
    big[9] = 200;
    CHECK(!gcr_from_g64(big, gsize, &tmp, err, sizeof(err)), "200 tracks accepted");
    CHECK(!gcr_from_g64(g64, 11, &tmp, err, sizeof(err)), "truncated header accepted");

    free(big);
    free(g64);
    gcr_free(&back);
    gcr_free(&g);
    free(img);
}

int main(void) {
    test_code();
    test_d64();
    test_d64_errors();
    test_g64();
    if (failures == 0) printf("test_gcr: all passed\n");
    return failures ? 1 : 0;
}
