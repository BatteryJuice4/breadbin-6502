#include "rom/sha1.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s: got %s want %s\n", name, got, want);
        failures++;
    }
}

int main(void) {
    char hex[41];

    sha1_hex("", 0, hex);
    check("empty", hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    sha1_hex("abc", 3, hex);
    check("abc", hex, "a9993e364706816aba3e25717850c26c9cd0d89d");

    const char* two_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha1_hex(two_block, strlen(two_block), hex);
    check("448-bit", hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // One million 'a', fed in odd-sized chunks to exercise buffering.
    {
        static char buf[1000];
        memset(buf, 'a', sizeof(buf));
        sha1_ctx_t ctx;
        uint8_t d[20];
        sha1_init(&ctx);
        size_t left = 1000000;
        size_t chunk = 1;
        while (left) {
            size_t n = chunk < left ? chunk : left;
            if (n > sizeof(buf)) n = sizeof(buf);
            sha1_update(&ctx, buf, n);
            left -= n;
            chunk = chunk * 7 % 997 + 1;
        }
        sha1_final(&ctx, d);
        for (int i = 0; i < 20; i++) sprintf(hex + i * 2, "%02x", d[i]);
        check("million-a", hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    }

    // Embedded Open ROMs match the hashes recorded in external/VENDORED.md
    sysroms_t roms;
    sysroms_load_default(&roms);
    check("openroms kernal", roms.kernal_sha1, "2ecf1bf1553bf77ba8a007a18a1a7c291d215b33");
    check("openroms basic", roms.basic_sha1, "4468852d4fde3394ca750f45d1c83914e218dac5");
    check("openroms chargen", roms.chargen_sha1, "466e399a5e994b52f6cef6019b32b9f6b504cd14");

    if (failures == 0) printf("test_sha1: all passed\n");
    return failures ? 1 : 0;
}
