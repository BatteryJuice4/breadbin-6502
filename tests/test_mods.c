// Unit tests for the mod system: zip reader, manifests, packages, patches, mod set.
// Every package and patch is generated here; no third-party files are needed.
#include "core/core.h"
#include "mods/mod.h"
#include "mods/modset.h"
#include "mods/patch.h"
#include "mods/zip.h"
#include "rom/sysroms.h"
#include "util/crc32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

// ---- tiny zip writer (stored entries, or a given pre-deflated payload) ---------

typedef struct {
    uint8_t buf[1 << 16];
    size_t len;
    uint8_t cd[1 << 14];
    size_t cd_len;
    int n;
} zw_t;

static void put16(uint8_t* p, size_t* l, unsigned v) { p[(*l)++] = (uint8_t)v; p[(*l)++] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, size_t* l, uint32_t v) { put16(p, l, v & 0xFFFF); put16(p, l, v >> 16); }

// method 0: data stored; method 8: comp is the raw deflate stream of data.
static void zw_add_ex(zw_t* z, const char* name, const void* data, size_t size, int method,
                      const void* comp, size_t comp_size, uint32_t crc, uint16_t flags) {
    const size_t nl = strlen(name);
    const uint32_t off = (uint32_t)z->len;
    if (method == 0) { comp = data; comp_size = size; }
    put32(z->buf, &z->len, 0x04034b50); put16(z->buf, &z->len, 20); put16(z->buf, &z->len, flags);
    put16(z->buf, &z->len, (unsigned)method); put32(z->buf, &z->len, 0);
    put32(z->buf, &z->len, crc); put32(z->buf, &z->len, (uint32_t)comp_size); put32(z->buf, &z->len, (uint32_t)size);
    put16(z->buf, &z->len, (unsigned)nl); put16(z->buf, &z->len, 0);
    memcpy(z->buf + z->len, name, nl); z->len += nl;
    memcpy(z->buf + z->len, comp, comp_size); z->len += comp_size;
    put32(z->cd, &z->cd_len, 0x02014b50); put16(z->cd, &z->cd_len, 20); put16(z->cd, &z->cd_len, 20);
    put16(z->cd, &z->cd_len, flags); put16(z->cd, &z->cd_len, (unsigned)method); put32(z->cd, &z->cd_len, 0);
    put32(z->cd, &z->cd_len, crc); put32(z->cd, &z->cd_len, (uint32_t)comp_size); put32(z->cd, &z->cd_len, (uint32_t)size);
    put16(z->cd, &z->cd_len, (unsigned)nl); put16(z->cd, &z->cd_len, 0); put16(z->cd, &z->cd_len, 0);
    put16(z->cd, &z->cd_len, 0); put16(z->cd, &z->cd_len, 0); put32(z->cd, &z->cd_len, 0); put32(z->cd, &z->cd_len, off);
    memcpy(z->cd + z->cd_len, name, nl); z->cd_len += nl;
    z->n++;
}

static void zw_add(zw_t* z, const char* name, const void* data, size_t size) {
    zw_add_ex(z, name, data, size, 0, NULL, 0, crc32_of(data, size), 0);
}

static void zw_add_str(zw_t* z, const char* name, const char* s) { zw_add(z, name, s, strlen(s)); }

static size_t zw_finish(zw_t* z) {
    const uint32_t cd_off = (uint32_t)z->len;
    memcpy(z->buf + z->len, z->cd, z->cd_len);
    z->len += z->cd_len;
    put32(z->buf, &z->len, 0x06054b50); put16(z->buf, &z->len, 0); put16(z->buf, &z->len, 0);
    put16(z->buf, &z->len, (unsigned)z->n); put16(z->buf, &z->len, (unsigned)z->n);
    put32(z->buf, &z->len, (uint32_t)z->cd_len); put32(z->buf, &z->len, cd_off); put16(z->buf, &z->len, 0);
    return z->len;
}

static zw_t zw;
static void zw_reset(void) { memset(&zw, 0, sizeof(zw)); }

// "Hello hello hello hello, deflate test for breadbin mods! " x4, raw deflate (zlib level 9)
static const uint8_t deflated[] = {
    0xf3,0x48,0xcd,0xc9,0xc9,0x57,0xc8,0x40,0x27,0x75,0x14,0x52,0x52,0xd3,0x72,0x12,0x4b,0x52,0x15,0x4a,
    0x52,0x8b,0x4b,0x14,0xd2,0xf2,0x8b,0x14,0x92,0x8a,0x52,0x13,0x53,0x92,0x32,0xf3,0x14,0x72,0xf3,0x53,
    0x8a,0x15,0x15,0x3c,0x86,0x8e,0x46,0x00 };

static void test_crc32(void) {
    CHECK(crc32_of("123456789", 9) == 0xCBF43926u, "crc32 check value");
    CHECK(crc32_update(crc32_of("1234", 4), "56789", 5) == 0xCBF43926u, "crc32 incremental");
}

static void test_zip(void) {
    char err[256];
    static zip_t z;
    char plain[300] = "";
    for (int i = 0; i < 4; i++) strcat(plain, "Hello hello hello hello, deflate test for breadbin mods! ");
    const size_t plen = strlen(plain);
    CHECK(plen == 228 && crc32_of(plain, plen) == 0xe7b2f52bu, "sample text");

    zw_reset();
    zw_add_str(&zw, "mod.toml", "id = \"x\"\n");
    zw_add_ex(&zw, "notes.txt", plain, plen, 8, deflated, sizeof(deflated), crc32_of(plain, plen), 0);
    zw_add(&zw, "empty.txt", "", 0);
    size_t n = zw_finish(&zw);
    CHECK(zip_open(&z, zw.buf, n, err, sizeof(err)), "open: %s", err);
    CHECK(z.count == 3, "3 entries, got %d", z.count);
    int i = zip_find(&z, "notes.txt");
    size_t sz = 0;
    uint8_t* d = zip_extract(&z, i, &sz, err, sizeof(err));
    CHECK(d && sz == plen && !memcmp(d, plain, plen), "deflate entry: %s", err);
    free(d);
    d = zip_extract(&z, zip_find(&z, "mod.toml"), &sz, err, sizeof(err));
    CHECK(d && sz == 9 && !strcmp((char*)d, "id = \"x\"\n"), "stored entry");
    free(d);
    d = zip_extract(&z, zip_find(&z, "empty.txt"), &sz, err, sizeof(err));
    CHECK(d && sz == 0, "empty entry: %s", err);
    free(d);
    CHECK(zip_find(&z, "missing") == -1, "missing name");

    // declared size larger than the real inflated data -> fails, nothing grows
    zw_reset();
    zw_add_ex(&zw, "bomb.txt", plain, plen + 1000, 8, deflated, sizeof(deflated), crc32_of(plain, plen), 0);
    n = zw_finish(&zw);
    CHECK(zip_open(&z, zw.buf, n, err, sizeof(err)), "open bomb");
    d = zip_extract(&z, 0, &sz, err, sizeof(err));
    CHECK(!d, "wrong declared size accepted");
    free(d);

    // declared size smaller than the inflated data -> fails
    zw_reset();
    zw_add_ex(&zw, "small.txt", plain, 10, 8, deflated, sizeof(deflated), crc32_of(plain, 10), 0);
    n = zw_finish(&zw);
    CHECK(zip_open(&z, zw.buf, n, err, sizeof(err)), "open small");
    d = zip_extract(&z, 0, &sz, err, sizeof(err));
    CHECK(!d, "overlong deflate stream accepted");
    free(d);

    // CRC mismatch
    zw_reset();
    zw_add_ex(&zw, "a.txt", "abc", 3, 0, NULL, 0, 0x12345678, 0);
    n = zw_finish(&zw);
    CHECK(zip_open(&z, zw.buf, n, err, sizeof(err)), "open crc");
    d = zip_extract(&z, 0, &sz, err, sizeof(err));
    CHECK(!d && strstr(err, "checksum"), "bad CRC accepted (%s)", err);
    free(d);

    // unsafe names, encryption, junk
    static const char* const bad_names[] = { "../evil.txt", "a/../../b.txt", "/abs.txt", "c:\\x.txt", "a\\b.txt", "./x.txt" };
    for (size_t k = 0; k < sizeof(bad_names) / sizeof(bad_names[0]); k++) {
        zw_reset();
        zw_add_str(&zw, bad_names[k], "x");
        n = zw_finish(&zw);
        CHECK(!zip_open(&z, zw.buf, n, err, sizeof(err)), "unsafe name accepted: %s", bad_names[k]);
    }
    zw_reset();
    zw_add_ex(&zw, "secret.txt", "x", 1, 0, NULL, 0, crc32_of("x", 1), 1);
    n = zw_finish(&zw);
    CHECK(!zip_open(&z, zw.buf, n, err, sizeof(err)), "encrypted entry accepted");
    CHECK(!zip_open(&z, (const uint8_t*)"PK not really a zip file at all.....", 36, err, sizeof(err)), "junk accepted");
    CHECK(!zip_open(&z, NULL, 0, err, sizeof(err)), "NULL accepted");

    // truncated archives must fail cleanly at every length
    zw_reset();
    zw_add_str(&zw, "mod.toml", "id = \"x\"\n");
    zw_add_ex(&zw, "notes.txt", plain, plen, 8, deflated, sizeof(deflated), crc32_of(plain, plen), 0);
    n = zw_finish(&zw);
    for (size_t cut = 0; cut < n; cut++) {
        if (zip_open(&z, zw.buf, cut, err, sizeof(err))) {
            for (int k = 0; k < z.count; k++) { d = zip_extract(&z, k, &sz, err, sizeof(err)); free(d); }
        }
    }
}

// ---- manifests -------------------------------------------------------------------

static bool parse(const char* toml, mod_t* m, char* err, size_t es, char* warns, size_t ws) {
    return mod_parse(toml, strlen(toml), NULL, NULL, m, err, es, warns, ws);
}

static uint8_t* script_reader(void* user, const char* path, size_t* size, char* err, size_t err_size) {
    (void)user;
    const char* text = NULL;
    size_t n = 0;
    if (!strcmp(path, "main.lua")) { text = "function on_frame() end\n"; n = strlen(text); }
    else if (!strcmp(path, "compiled.lua")) { text = "\x1bLua\x54"; n = 5; }
    if (!text) { snprintf(err, err_size, "%s isn't there", path); return NULL; }
    uint8_t* d = malloc(n);
    memcpy(d, text, n);
    *size = n;
    return d;
}

#define HEAD "id = \"t-mod\"\nname = \"Test\"\nversion = \"1.0.0\"\ngame = \"demo\"\n"

static void test_manifest(void) {
    char err[256], warns[1024];
    mod_t m;
    CHECK(parse(HEAD "author = \"me\"\nmin_launcher_version = \"0.3.0\"\n"
                "requires_sha1 = [\"4D24D5ED3D8B38235D9AE5965B675FFECE0981CC\"]\n"
                "dependencies = [\"base\", \"other>=1.2.0\"]\nconflicts = [\"rival\"]\nsurprise = 1\n"
                "type = \"patch\"\n[[patch]]\noffset = 0x10\nbytes = \"A9 05 ea\"\nexpect = \"00 00 00\"\n"
                "[[patch]]\noffset = 3\nbytes = \"FF\"\n",
                &m, err, sizeof(err), warns, sizeof(warns)), "patch manifest: %s", err);
    CHECK(m.type == MOD_TYPE_PATCH && m.patch_count == 2, "patch count");
    CHECK(m.patches[0].offset == 0x10 && m.patches[0].len == 3 && m.patches[0].bytes[0] == 0xA9 && m.patches[0].bytes[2] == 0xEA, "patch bytes");
    CHECK(m.patches[0].has_expect && !m.patches[1].has_expect, "expect");
    CHECK(!strcmp(m.requires_sha1[0], "4d24d5ed3d8b38235d9ae5965b675ffece0981cc"), "sha1 lowercased");
    CHECK(m.dep_count == 2 && !strcmp(m.deps[1].id, "other") && !strcmp(m.deps[1].min_version, "1.2.0"), "deps");
    CHECK(m.conflict_count == 1 && !strcmp(m.min_launcher_version, "0.3.0"), "conflicts");
    CHECK(strstr(warns, "surprise") != NULL, "unknown key warning: %s", warns);
    mod_free(&m);

    CHECK(parse(HEAD "type = \"cheat\"\n[[cheat]]\naddress = 0x0400\nvalue = 9\nlabel = \"lives\"\n"
                "[[cheat]]\naddress = 0xC000\nbytes = \"01 02\"\nmode = \"once\"\n",
                &m, err, sizeof(err), warns, sizeof(warns)), "cheat manifest: %s", err);
    CHECK(m.cheat_count == 2 && m.cheats[0].freeze && !m.cheats[1].freeze && m.cheats[1].len == 2, "cheats");
    mod_free(&m);

    char pal[1024];
    strcpy(pal, HEAD "type = \"palette\"\n[palette]\nname = \"Grey\"\ncolors = [");
    for (int i = 0; i < 16; i++) {
        char c[16];
        snprintf(c, sizeof(c), "%s\"#%02x%02x%02x\"", i ? "," : "", i * 16, i * 16, i * 16);
        strcat(pal, c);
    }
    strcat(pal, "]\n");
    CHECK(parse(pal, &m, err, sizeof(err), warns, sizeof(warns)), "palette: %s", err);
    CHECK(m.palette[15] == 0xF0F0F0 && m.palette[1] == 0x101010 && !strcmp(m.palette_name, "Grey"), "palette values");
    mod_free(&m);

    CHECK(mod_parse(HEAD "type = \"script\"\n", strlen(HEAD "type = \"script\"\n"), script_reader, NULL, &m,
                    err, sizeof(err), warns, sizeof(warns)), "script: %s", err);
    CHECK(!strcmp(m.script_name, "main.lua") && m.script && strstr(m.script, "on_frame"), "script source");
    mod_free(&m);
    const char* other = HEAD "type = \"script\"\nscript = \"other.lua\"\n";
    CHECK(!mod_parse(other, strlen(other), script_reader, NULL, &m, err, sizeof(err), warns, sizeof(warns)) &&
          strstr(err, "other.lua"), "missing script accepted (%s)", err);
    const char* bin = HEAD "type = \"script\"\nscript = \"compiled.lua\"\n";
    CHECK(!mod_parse(bin, strlen(bin), script_reader, NULL, &m, err, sizeof(err), warns, sizeof(warns)) &&
          strstr(err, "compiled"), "compiled chunk accepted (%s)", err);
    const char* exe = HEAD "type = \"script\"\nscript = \"main.exe\"\n";
    CHECK(!mod_parse(exe, strlen(exe), script_reader, NULL, &m, err, sizeof(err), warns, sizeof(warns)), "non-.lua script accepted");

    static const char* const bad[] = {
        "name = \"x\"\nversion = \"1\"\ngame = \"g\"\ntype = \"cheat\"\n",           // no id
        "id = \"Bad Id\"\nname = \"x\"\nversion = \"1\"\ngame = \"g\"\ntype = \"script\"\n",
        "id = \"x\"\nname = \"x\"\nversion = \"1\"\ntype = \"script\"\n",             // no game
        "id = \"x\"\nname = \"x\"\nversion = \"one\"\ngame = \"g\"\ntype = \"script\"\n",
        HEAD "type = \"magic\"\n",
        HEAD "type = \"patch\"\n",                                                   // no patches
        HEAD "type = \"patch\"\n[[patch]]\noffset = 1\nbytes = \"A9 0\"\n",          // odd nibble
        HEAD "type = \"patch\"\n[[patch]]\noffset = 1\nbytes = \"zz\"\n",
        HEAD "type = \"patch\"\n[[patch]]\noffset = 1\nbytes = \"A9 05\"\nexpect = \"00\"\n",
        HEAD "type = \"patch\"\n[[patch]]\noffset = -1\nbytes = \"00\"\n",
        HEAD "type = \"patch\"\npatch_files = [\"x.exe\"]\n",
        HEAD "type = \"patch\"\npatch_files = [\"x.ips\"]\n",                       // no reader
        HEAD "type = \"cheat\"\n",
        HEAD "type = \"cheat\"\n[[cheat]]\naddress = 0x10000\nvalue = 1\n",
        HEAD "type = \"cheat\"\n[[cheat]]\naddress = 0xFFFF\nbytes = \"01 02\"\n",   // past $FFFF
        HEAD "type = \"cheat\"\n[[cheat]]\naddress = 1\nvalue = 256\n",
        HEAD "type = \"cheat\"\n[[cheat]]\naddress = 1\nvalue = 1\nmode = \"sometimes\"\n",
        HEAD "type = \"palette\"\n[palette]\ncolors = [\"#000000\"]\n",
        HEAD "requires_sha1 = [\"abc\"]\ntype = \"script\"\n",
        HEAD "dependencies = [\"Base!\"]\ntype = \"script\"\n",
        HEAD "type = \"script\"\n[broken",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        err[0] = 0;
        const bool ok = parse(bad[i], &m, err, sizeof(err), warns, sizeof(warns));
        CHECK(!ok, "bad manifest %zu accepted", i);
        CHECK(err[0], "bad manifest %zu: no reason given", i);
        if (ok) mod_free(&m);
    }
}

// ---- patches -----------------------------------------------------------------

static size_t make_ips(uint8_t* p) {
    size_t n = 0;
    memcpy(p, "PATCH", 5); n = 5;
    // record: offset 2, 3 bytes
    p[n++] = 0; p[n++] = 0; p[n++] = 2; p[n++] = 0; p[n++] = 3; p[n++] = 0xAA; p[n++] = 0xBB; p[n++] = 0xCC;
    // RLE: offset 10, 4 x 0x55
    p[n++] = 0; p[n++] = 0; p[n++] = 10; p[n++] = 0; p[n++] = 0; p[n++] = 0; p[n++] = 4; p[n++] = 0x55;
    memcpy(p + n, "EOF", 3); n += 3;
    return n;
}

static void bps_put_num(uint8_t* p, size_t* n, uint64_t v) {
    for (;;) {
        const uint8_t x = v & 0x7F;
        v >>= 7;
        if (v == 0) { p[(*n)++] = 0x80 | x; break; }
        p[(*n)++] = x;
        v--;
    }
}

static void put32le(uint8_t* p, size_t* n, uint32_t v) { for (int i = 0; i < 4; i++) p[(*n)++] = (uint8_t)(v >> (8 * i)); }

// src (32 bytes) -> dst: bytes 0-7 SourceRead, 8-11 TargetRead "WXYZ", 12-15 TargetCopy
// of 8-11, 16-23 SourceCopy from src offset 0, 24-31 SourceRead.
static size_t make_bps(uint8_t* p, const uint8_t* src, uint8_t* dst_out) {
    size_t n = 0;
    memcpy(p, "BPS1", 4); n = 4;
    bps_put_num(p, &n, 32); bps_put_num(p, &n, 32);
    bps_put_num(p, &n, 2); p[n++] = 'h'; p[n++] = 'i';     // metadata
    uint8_t dst[32];
    memcpy(dst, src, 32);
    bps_put_num(p, &n, (8 - 1) << 2 | 0);
    bps_put_num(p, &n, (4 - 1) << 2 | 1); memcpy(p + n, "WXYZ", 4); n += 4; memcpy(dst + 8, "WXYZ", 4);
    bps_put_num(p, &n, (4 - 1) << 2 | 3); bps_put_num(p, &n, 8 << 1); memcpy(dst + 12, dst + 8, 4);
    bps_put_num(p, &n, (8 - 1) << 2 | 2); bps_put_num(p, &n, 0 << 1); memcpy(dst + 16, src, 8);
    // SourceRead copies from the same output offset in the source
    bps_put_num(p, &n, (8 - 1) << 2 | 0);
    put32le(p, &n, crc32_of(src, 32));
    put32le(p, &n, crc32_of(dst, 32));
    put32le(p, &n, crc32_of(p, n));
    memcpy(dst_out, dst, 32);
    return n;
}

static void test_patches(void) {
    char err[256];
    uint8_t data[32], orig[32];
    for (int i = 0; i < 32; i++) orig[i] = (uint8_t)i;

    mod_patch_t p[2] = { { .offset = 4, .len = 2, .bytes = { 0xA9, 0x05 } }, { .offset = 30, .len = 2, .bytes = { 1, 2 }, .has_expect = true, .expect = { 30, 31 } } };
    memcpy(data, orig, 32);
    CHECK(patch_apply_inline(data, 32, p, 2, err, sizeof(err)), "inline: %s", err);
    CHECK(data[4] == 0xA9 && data[5] == 5 && data[30] == 1 && data[31] == 2 && data[6] == 6, "inline result");
    p[1].expect[0] = 99;
    memcpy(data, orig, 32);
    CHECK(!patch_apply_inline(data, 32, p, 2, err, sizeof(err)), "expect mismatch accepted");
    CHECK(!memcmp(data, orig, 32), "failed inline patch changed data");
    p[1].expect[0] = 30;
    p[1].offset = 31;
    CHECK(!patch_apply_inline(data, 32, p, 2, err, sizeof(err)) && !memcmp(data, orig, 32), "out of range accepted");

    uint8_t ips[64];
    size_t in = make_ips(ips);
    memcpy(data, orig, 32);
    CHECK(patch_apply_ips(data, 32, ips, in, err, sizeof(err)), "ips: %s", err);
    CHECK(data[2] == 0xAA && data[4] == 0xCC && data[5] == 5 && data[10] == 0x55 && data[13] == 0x55 && data[14] == 14, "ips result");
    memcpy(data, orig, 32);
    CHECK(!patch_apply_ips(data, 12, ips, in, err, sizeof(err)) && !memcmp(data, orig, 32), "ips past end accepted");
    for (size_t cut = 0; cut < in; cut++) {
        memcpy(data, orig, 32);
        CHECK(!patch_apply_ips(data, 32, ips, cut, err, sizeof(err)) && !memcmp(data, orig, 32), "truncated ips (%zu) accepted", cut);
    }
    ips[in] = 0; ips[in + 1] = 0; ips[in + 2] = 16;     // truncation record that shrinks
    CHECK(!patch_apply_ips(data, 32, ips, in + 3, err, sizeof(err)), "ips size change accepted");
    ips[in + 2] = 32;
    CHECK(patch_apply_ips(data, 32, ips, in + 3, err, sizeof(err)), "ips same-size truncation rejected: %s", err);

    uint8_t bps[128], want[32];
    const size_t bn = make_bps(bps, orig, want);
    memcpy(data, orig, 32);
    CHECK(patch_apply_bps(data, 32, bps, bn, err, sizeof(err)), "bps: %s", err);
    CHECK(!memcmp(data, want, 32), "bps result");
    CHECK(!patch_apply_bps(data, 32, bps, bn, err, sizeof(err)) && !memcmp(data, want, 32), "bps applied to patched data");
    memcpy(data, orig, 32);
    bps[10] ^= 1;
    CHECK(!patch_apply_bps(data, 32, bps, bn, err, sizeof(err)) && !memcmp(data, orig, 32), "damaged bps accepted");
    bps[10] ^= 1;
    for (size_t cut = 0; cut < bn; cut++) {
        CHECK(!patch_apply_bps(data, 32, bps, cut, err, sizeof(err)), "truncated bps accepted");
    }

    // a whole mod applies all or nothing
    mod_t m = { 0 };
    mod_patch_t mp = { .offset = 0, .len = 1, .bytes = { 0xEE } };
    m.patches = &mp;
    m.patch_count = 1;
    m.file_count = 1;
    snprintf(m.files[0].name, sizeof(m.files[0].name), "bad.ips");
    m.files[0].kind = MOD_FILE_IPS;
    m.files[0].data = ips;
    m.files[0].size = 4;    // broken
    memcpy(data, orig, 32);
    CHECK(!patch_apply_mod(data, 32, &m, err, sizeof(err)) && data[0] == 0 && strstr(err, "bad.ips"), "partial mod applied (%s)", err);
}

// ---- packages ------------------------------------------------------------------

static void test_packages(void) {
    char err[256], warns[1024];
    mod_t m;
    uint8_t ips[64];
    const size_t in = make_ips(ips);
    zw_reset();
    zw_add_str(&zw, "mod.toml", HEAD "type = \"patch\"\npatch_files = [\"fix.ips\"]\n");
    zw_add(&zw, "fix.ips", ips, in);
    zw_add_str(&zw, "README.md", "hi");
    size_t n = zw_finish(&zw);
    CHECK(mod_load_package(zw.buf, n, &m, err, sizeof(err), warns, sizeof(warns)), "package: %s", err);
    CHECK(m.file_count == 1 && m.files[0].size == in && m.files[0].kind == MOD_FILE_IPS, "package ips");
    mod_free(&m);

    zw_reset();
    zw_add_str(&zw, "mod.toml", HEAD "type = \"script\"\n");
    zw_add_str(&zw, "libcheat.so", "\x7f" "ELF");
    n = zw_finish(&zw);
    CHECK(!mod_load_package(zw.buf, n, &m, err, sizeof(err), warns, sizeof(warns)) && strstr(err, "libcheat.so"), "native code accepted (%s)", err);

    zw_reset();
    zw_add_str(&zw, "sub/mod.toml", HEAD "type = \"script\"\n");
    n = zw_finish(&zw);
    CHECK(!mod_load_package(zw.buf, n, &m, err, sizeof(err), warns, sizeof(warns)) && strstr(err, "mod.toml"), "nested manifest accepted");

    zw_reset();
    zw_add_str(&zw, "mod.toml", HEAD "type = \"patch\"\npatch_files = [\"gone.ips\"]\n");
    n = zw_finish(&zw);
    CHECK(!mod_load_package(zw.buf, n, &m, err, sizeof(err), warns, sizeof(warns)) && strstr(err, "gone.ips"), "missing patch file accepted");
}

// ---- mod set ----------------------------------------------------------------------

static mod_entry_t* add(modset_t* s, const char* toml) {
    zw_reset();
    zw_add_str(&zw, "mod.toml", toml);
    const size_t n = zw_finish(&zw);
    return modset_add_package(s, zw.buf, n, "mem.c64mod", MOD_SOURCE_INSTALLED);
}

#define M(id, extra) "id = \"" id "\"\nname = \"" id "\"\nversion = \"1.0.0\"\ngame = \"demo\"\n" extra

static void test_modset(void) {
    modset_t s;
    modset_init(&s, "demo", "0.3.0");
    const char* sha = "4d24d5ed3d8b38235d9ae5965b675ffece0981cc";
    add(&s, M("base", "type = \"patch\"\n[[patch]]\noffset = 0\nbytes = \"11 22\"\n"));
    add(&s, M("addon", "dependencies = [\"base>=1.0.0\"]\ntype = \"patch\"\n[[patch]]\noffset = 1\nbytes = \"33\"\n"));
    add(&s, M("rival", "conflicts = [\"base\"]\ntype = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n"));
    add(&s, M("other-game", "type = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n"));
    add(&s, "id = \"wrong\"\nname = \"w\"\nversion = \"1.0.0\"\ngame = \"zork\"\ntype = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n");
    add(&s, M("future", "min_launcher_version = \"9.0.0\"\ntype = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n"));
    add(&s, M("dumped", "requires_sha1 = [\"4d24d5ed3d8b38235d9ae5965b675ffece0981cc\"]\ntype = \"cheat\"\n[[cheat]]\naddress = 3\nvalue = 8\n"));
    add(&s, M("plug", "plugins = [\"widescreen\"]\ntype = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n"));
    add(&s, "this is not toml [");
    add(&s, M("base", "type = \"cheat\"\n[[cheat]]\naddress = 2\nvalue = 7\n"));   // duplicate id
    CHECK(s.count == 10, "count %d", s.count);
    CHECK(!s.entries[8]->loaded && strstr(s.entries[8]->status, "Broken"), "broken mod listed: %s", s.entries[8]->status);
    CHECK(!s.entries[9]->loaded && strstr(s.entries[9]->status, "Another copy"), "duplicate: %s", s.entries[9]->status);
    CHECK(s.entries[0]->loaded, "first copy kept");

    const char ids[][MOD_ID_MAX] = { "addon", "base", "nonexistent", "other-game", "wrong", "future", "dumped", "plug" };
    modset_set_enabled(&s, ids, 8);
    CHECK(s.order_count == 7, "unknown id dropped: %d", s.order_count);
    modset_evaluate(&s, "");
    const int addon = modset_find(&s, "addon"), base = modset_find(&s, "base");
    CHECK(s.entries[base]->usable, "base usable: %s", s.entries[base]->status);
    CHECK(!s.entries[addon]->usable && strstr(s.entries[addon]->status, "load after"), "addon order: %s", s.entries[addon]->status);
    CHECK(s.entries[modset_find(&s, "other-game")]->usable, "plain cheat usable: %s", s.entries[modset_find(&s, "other-game")]->status);
    CHECK(strstr(s.entries[modset_find(&s, "wrong")]->status, "another game"), "wrong game");
    CHECK(strstr(s.entries[modset_find(&s, "future")]->status, "9.0.0"), "launcher version");
    CHECK(strstr(s.entries[modset_find(&s, "dumped")]->status, "loaded first"), "no game loaded: %s", s.entries[modset_find(&s, "dumped")]->status);
    CHECK(strstr(s.entries[modset_find(&s, "plug")]->status, "widescreen"), "plugin");

    modset_move(&s, 0, 1);  // base before addon
    modset_evaluate(&s, sha);
    CHECK(s.entries[addon]->usable, "addon now usable: %s", s.entries[addon]->status);
    CHECK(strstr(s.entries[addon]->status, "same bytes"), "overlap note: %s", s.entries[addon]->status);
    CHECK(s.entries[modset_find(&s, "dumped")]->usable, "dumped with right sha");
    modset_evaluate(&s, "0000000000000000000000000000000000000000");
    CHECK(strstr(s.entries[modset_find(&s, "dumped")]->status, "different dump"), "wrong sha");
    modset_evaluate(&s, sha);

    // conflicts: the later one gives way; dependents follow
    const int rival = modset_find(&s, "rival");
    modset_enable(&s, rival, true);
    modset_evaluate(&s, sha);
    CHECK(!s.entries[rival]->usable && strstr(s.entries[rival]->status, "Conflicts"), "rival: %s", s.entries[rival]->status);
    modset_enable(&s, base, false);
    modset_evaluate(&s, sha);
    CHECK(s.entries[rival]->usable, "rival alone: %s", s.entries[rival]->status);
    CHECK(!s.entries[addon]->usable && strstr(s.entries[addon]->status, "enabled too"), "addon without base: %s", s.entries[addon]->status);
    modset_enable(&s, base, true);  // now after rival: base gives way, addon follows
    modset_evaluate(&s, sha);
    CHECK(!s.entries[base]->usable && !s.entries[addon]->usable, "chain");
    CHECK(strstr(s.entries[addon]->status, "can't run") || strstr(s.entries[addon]->status, "load after"), "addon: %s", s.entries[addon]->status);
    modset_enable(&s, rival, false);
    modset_enable(&s, addon, false);
    modset_enable(&s, addon, true);
    modset_evaluate(&s, sha);
    CHECK(s.entries[base]->usable && s.entries[addon]->usable, "restored: %s / %s", s.entries[base]->status, s.entries[addon]->status);

    // patches in load order
    uint8_t data[8] = { 0 };
    CHECK(modset_has_patches(&s), "has patches");
    CHECK(modset_apply_patches(&s, data, sizeof(data)) == 2, "2 applied");
    CHECK(data[0] == 0x11 && data[1] == 0x33, "later mod wins: %02x %02x", data[0], data[1]);
    uint8_t tiny[1] = { 0 };
    CHECK(modset_apply_patches(&s, tiny, 1) == 0 && s.entries[base]->failed && strstr(s.entries[base]->status, "Couldn't apply"), "failed patch: %s", s.entries[base]->status);
    modset_evaluate(&s, sha);
    CHECK(!s.entries[base]->usable && !s.entries[addon]->usable, "failure sticks, dependents stop");
    modset_free(&s);

    // cheats on a real core
    modset_init(&s, "demo", "0.3.0");
    add(&s, M("lives", "type = \"cheat\"\n[[cheat]]\naddress = 0x0340\nvalue = 9\n[[cheat]]\naddress = 0x0341\nbytes = \"AA BB\"\nmode = \"once\"\n"));
    modset_enable(&s, 0, true);
    modset_evaluate(&s, "");
    static sysroms_t roms;
    sysroms_load_default(&roms);
    core_t* c = core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
    CHECK(c != NULL, "core");
    if (c) {
        modset_apply_cheats(&s, c, true);
        CHECK(core_ram_read(c, 0x340) == 9 && core_ram_read(c, 0x341) == 0xAA && core_ram_read(c, 0x342) == 0xBB, "cheats written");
        core_ram_write(c, 0x340, 1);
        core_ram_write(c, 0x341, 1);
        modset_apply_cheats(&s, c, false);
        CHECK(core_ram_read(c, 0x340) == 9 && core_ram_read(c, 0x341) == 1, "freeze vs once");
        core_destroy(c);
    }
    modset_free(&s);
}

int main(void) {
    test_crc32();
    test_zip();
    test_manifest();
    test_patches();
    test_packages();
    test_modset();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("mods: all tests passed\n");
    return 0;
}
