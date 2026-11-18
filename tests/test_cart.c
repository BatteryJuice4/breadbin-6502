// Unit tests for the .crt / .bin cartridge parser (src/core/cart.c).
#include "core/cart.h"
#include "rom/sha1.h"
#include "crt_builder.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static crt_builder_t b;
static cart_t cart;
static char err[256];
static uint8_t rom_a[0x4000], rom_b[0x2000];

static bool parse(void) {
    err[0] = 0;
    return cart_parse_crt(b.buf, b.len, &cart, err, sizeof(err));
}

static void expect_fail(const char* what, const char* err_part) {
    CHECK(!parse(), "%s: accepted", what);
    CHECK(strstr(err, err_part) != NULL, "%s: error \"%s\" lacks \"%s\"", what, err, err_part);
}

static void test_8k(void) {
    crt_begin(&b, 0, 0, 1, "TEST 8K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    CHECK(parse(), "8K: %s", err);
    CHECK(cart.mode == CART_MODE_8K && cart.exrom && !cart.game, "8K: mode/lines");
    CHECK(cart.has_roml && !cart.has_romh, "8K: banks");
    CHECK(memcmp(cart.roml, rom_a, 0x2000) == 0, "8K: ROML data");
    CHECK(cart.romh[0] == 0xFF && cart.romh[0x1FFF] == 0xFF, "8K: ROMH not $FF-filled");
    CHECK(strcmp(cart.name, "TEST 8K") == 0, "8K: name '%s'", cart.name);

    // Same ROM as raw .bin -> same hash
    cart_t bin;
    CHECK(cart_from_bin(rom_a, 0x2000, 0x8000, &bin, err, sizeof(err)), "8K bin: %s", err);
    CHECK(strcmp(bin.sha1, cart.sha1) == 0, "8K: crt/bin hash mismatch %s vs %s", cart.sha1, bin.sha1);
    char h[41];
    sha1_hex(rom_a, 0x2000, h);
    CHECK(strcmp(h, cart.sha1) == 0, "8K: hash isn't SHA-1 of chip data");
}

// Old dumps often assert GAME by mistake: a "16K" header with ROM only at $8000
// runs as 8K, and says so.
static void test_wrong_16k_header(void) {
    crt_begin(&b, 0, 0, 0, "HEADER SAYS 16K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    CHECK(parse(), "wrong header: %s", err);
    CHECK(cart.mode == CART_MODE_8K && cart.exrom && !cart.game && cart.header_fixed, "wrong header: not run as 8K");
    crt_begin(&b, 0, 0, 1, "TEST 8K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    CHECK(parse() && !cart.header_fixed, "a correct 8K header was flagged");
}

static void test_4k_mirror(void) {
    crt_begin(&b, 0, 0, 1, "TEST 4K");
    crt_chip(&b, 0, 0, 0x8000, rom_b, 0x1000);
    CHECK(parse(), "4K: %s", err);
    CHECK(memcmp(cart.roml, rom_b, 0x1000) == 0 && memcmp(cart.roml + 0x1000, rom_b, 0x1000) == 0, "4K: not mirrored");
}

static void test_16k(void) {
    // one 16K chip
    crt_begin(&b, 0, 0, 0, "TEST 16K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x4000);
    CHECK(parse(), "16K single: %s", err);
    CHECK(cart.mode == CART_MODE_16K && cart.exrom && cart.game, "16K: mode/lines");
    CHECK(memcmp(cart.roml, rom_a, 0x2000) == 0 && memcmp(cart.romh, rom_a + 0x2000, 0x2000) == 0, "16K single: data");
    char single_hash[41];
    strcpy(single_hash, cart.sha1);

    // two 8K chips with the same bytes -> same placement and hash
    crt_begin(&b, 0, 0, 0, "TEST 16K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_chip(&b, 0, 0, 0xA000, rom_a + 0x2000, 0x2000);
    CHECK(parse(), "16K split: %s", err);
    CHECK(memcmp(cart.roml, rom_a, 0x2000) == 0 && memcmp(cart.romh, rom_a + 0x2000, 0x2000) == 0, "16K split: data");
    CHECK(strcmp(cart.sha1, single_hash) == 0, "16K: split/single hash mismatch");

    cart_t bin;
    CHECK(cart_from_bin(rom_a, 0x4000, 0x8000, &bin, err, sizeof(err)) && bin.mode == CART_MODE_16K, "16K bin: %s", err);
    CHECK(strcmp(bin.sha1, single_hash) == 0, "16K: crt/bin hash mismatch");
}

static void test_ultimax(void) {
    crt_begin(&b, 0, 1, 0, "TEST ULTIMAX");
    crt_chip(&b, 0, 0, 0xE000, rom_b, 0x2000);
    CHECK(parse(), "Ultimax: %s", err);
    CHECK(cart.mode == CART_MODE_ULTIMAX && !cart.exrom && cart.game, "Ultimax: mode/lines");
    CHECK(cart.has_romh && !cart.has_roml && memcmp(cart.romh, rom_b, 0x2000) == 0, "Ultimax: ROMH");

    // 4K at $F000, mirrored to $E000; plus ROML
    crt_begin(&b, 0, 1, 0, "TEST ULTIMAX 4K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_chip(&b, 0, 0, 0xF000, rom_b, 0x1000);
    CHECK(parse(), "Ultimax 4K: %s", err);
    CHECK(memcmp(cart.romh, rom_b, 0x1000) == 0 && memcmp(cart.romh + 0x1000, rom_b, 0x1000) == 0, "Ultimax 4K mirror");
    CHECK(cart.has_roml && memcmp(cart.roml, rom_a, 0x2000) == 0, "Ultimax ROML");

    cart_t bin;
    CHECK(cart_from_bin(rom_b, 0x1000, 0xF000, &bin, err, sizeof(err)) && bin.mode == CART_MODE_ULTIMAX, "Ultimax bin: %s", err);

    crt_begin(&b, 0, 1, 0, "NO RESET VECTOR");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    expect_fail("Ultimax without $E000", "no ROM at $E000");
}

// The data view: same bytes (and order) as the hash, writes go back to the right place.
static void check_view(const char* what) {
    static uint8_t view[0x4000];
    const size_t n = cart_data_size(&cart);
    CHECK(n > 0 && n <= sizeof(view), "%s: view size %zu", what, n);
    cart_data_get(&cart, view);
    char h[41];
    sha1_hex(view, n, h);
    CHECK(!strcmp(h, cart.sha1), "%s: view doesn't match the hash", what);
    for (size_t i = 0; i < n; i++) view[i] ^= 0x5A;
    cart_data_put(&cart, view);
    static uint8_t back[0x4000];
    cart_data_get(&cart, back);
    CHECK(!memcmp(back, view, n), "%s: put/get round trip", what);
}

static void test_data_view(void) {
    crt_begin(&b, 0, 0, 0, "V16");
    crt_chip(&b, 0, 0, 0xA000, rom_a + 0x2000, 0x2000);     // ROMH first in the file
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    CHECK(parse(), "view 16K: %s", err);
    check_view("16K reversed");
    CHECK(cart.romh[0] == (uint8_t)(rom_a[0x2000] ^ 0x5A) && cart.roml[5] == (uint8_t)(rom_a[5] ^ 0x5A), "16K reversed placement");

    crt_begin(&b, 0, 1, 0, "V4K");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_chip(&b, 0, 0, 0xF000, rom_b, 0x1000);
    CHECK(parse(), "view ultimax: %s", err);
    CHECK(cart_data_size(&cart) == 0x3000, "ultimax view size");
    check_view("Ultimax 4K");
    CHECK(cart.romh[3] == cart.romh[0x1003] && cart.romh[3] == (uint8_t)(rom_b[3] ^ 0x5A), "4K mirror after put");

    CHECK(cart_from_bin(rom_a, 0x4000, 0x8000, &cart, err, sizeof(err)), "bin: %s", err);
    check_view("16K bin");
}

static void test_errors(void) {
    memset(b.buf, 0, sizeof(b.buf));
    b.len = 100;
    expect_fail("no signature", "signature");

    crt_begin(&b, 0, 0, 1, "X");
    b.len = 0x30;
    expect_fail("short header", "truncated .crt header");

    crt_begin(&b, 5, 0, 0, "OCEAN");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    expect_fail("Ocean", "type 5 (Ocean type 1)");
    CHECK(strcmp(cart.name, "OCEAN") == 0, "name not kept for unsupported type");

    crt_begin(&b, 0, 1, 1, "OFF");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    expect_fail("no lines", "neither EXROM nor GAME");

    crt_begin(&b, 0, 0, 1, "EMPTY");
    expect_fail("no chips", "no ROM chips");

    crt_begin(&b, 0, 0, 1, "TRUNC");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    b.len -= 1;
    expect_fail("truncated chip", "truncated");

    crt_begin(&b, 0, 0, 1, "BAD LEN");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_put32(&b, 0x40 + 4, 0xFFFFFFF0u);
    expect_fail("packet length overflow", "invalid length");

    crt_begin(&b, 0, 0, 1, "BAD MAGIC");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    memcpy(b.buf + 0x40, "CHIQ", 4);
    expect_fail("bad CHIP magic", "expected CHIP");

    crt_begin(&b, 0, 0, 1, "BANKED");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_chip(&b, 0, 1, 0x8000, rom_a, 0x2000);
    expect_fail("bank 1", "bank-switching");

    crt_begin(&b, 0, 0, 1, "TWO ROML");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    expect_fail("duplicate window", "more than one ROM chip");

    crt_begin(&b, 0, 0, 1, "8K AT A000");
    crt_chip(&b, 0, 0, 0xA000, rom_a, 0x2000);
    expect_fail("8K chip at $A000", "doesn't fit a 8K");

    crt_begin(&b, 0, 0, 1, "ODD SIZE");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x0800);
    expect_fail("2K chip", "unsupported ROM chip size");

    crt_begin(&b, 0, 0, 1, "MISALIGNED");
    crt_chip(&b, 0, 0, 0x8800, rom_a, 0x1000);
    expect_fail("misaligned", "misaligned");

    cart_t bin;
    CHECK(!cart_from_bin(rom_a, 0x3000, 0x8000, &bin, err, sizeof(err)), "12K bin accepted");
    CHECK(!cart_from_bin(rom_a, 0x2000, 0xC000, &bin, err, sizeof(err)), "bin at $C000 accepted");

    // Every truncation of a valid image must fail cleanly (no crash, no accept),
    // except right after the $8000 chip: that's a complete "16K header, 8K ROM"
    // image, which runs as 8K (see test_wrong_16k_header; its SHA-1 differs).
    crt_begin(&b, 0, 0, 0, "FUZZ");
    crt_chip(&b, 0, 0, 0x8000, rom_a, 0x2000);
    const size_t first_chip_end = b.len;
    crt_chip(&b, 0, 0, 0xA000, rom_a, 0x2000);
    const size_t full = b.len;
    int accepted = 0;
    for (size_t n = 0; n < full; n++) {
        if (cart_parse_crt(b.buf, n, &cart, err, sizeof(err)) && !(n == first_chip_end && cart.header_fixed)) accepted++;
    }
    CHECK(accepted == 0, "%d truncated images accepted", accepted);
    CHECK(cart_parse_crt(b.buf, full, &cart, err, sizeof(err)), "full fuzz image rejected: %s", err);
}

int main(void) {
    for (unsigned i = 0; i < sizeof(rom_a); i++) rom_a[i] = (uint8_t)(i * 7 + 3);
    for (unsigned i = 0; i < sizeof(rom_b); i++) rom_b[i] = (uint8_t)(i * 13 + 1);
    test_8k();
    test_wrong_16k_header();
    test_4k_mirror();
    test_16k();
    test_ultimax();
    test_errors();
    test_data_view();
    if (failures == 0) printf("test_cart: all passed\n");
    return failures ? 1 : 0;
}
