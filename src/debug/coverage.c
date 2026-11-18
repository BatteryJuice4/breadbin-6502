// Coverage maps on disk, see coverage.h.
#include "debug/coverage.h"
#include "core/core.h"
#include "platform/paths.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "BBCOV1\n"
#define MAGIC_LEN 7
#define HEADER_LEN (MAGIC_LEN + 41)
#define FILE_SIZE (HEADER_LEN + CORE_COVERAGE_SIZE * 4)

static bool sha1_ok(const char* s) {
    if (!s || strlen(s) != 40) return false;
    for (int i = 0; i < 40; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

bool coverage_path(const char* dir, const char* sha1, char* out, size_t out_size) {
    if (!dir || !dir[0] || !sha1_ok(sha1)) return false;
    const int n = snprintf(out, out_size, "%s%c%s.bbcov", dir, platform_path_sep(), sha1);
    return n > 0 && (size_t)n < out_size;
}

bool coverage_load(const char* path, char sha1[41], uint32_t* map, char* err, size_t err_size) {
    if (err_size) err[0] = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t* buf = malloc(FILE_SIZE + 1);
    const size_t n = buf ? fread(buf, 1, FILE_SIZE + 1, f) : 0;
    fclose(f);
    bool ok = buf && n == FILE_SIZE && !memcmp(buf, MAGIC, MAGIC_LEN) && buf[HEADER_LEN - 1] == '\n';
    if (ok) {
        memcpy(sha1, buf + MAGIC_LEN, 40);
        sha1[40] = 0;
        ok = sha1_ok(sha1);
    }
    if (ok) {
        const uint8_t* p = buf + HEADER_LEN;
        for (size_t i = 0; i < CORE_COVERAGE_SIZE; i++, p += 4) {
            map[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
    } else {
        snprintf(err, err_size, "%s isn't a coverage map", path);
    }
    free(buf);
    return ok;
}

bool coverage_merge_save(const char* path, const char* sha1, const uint32_t* map, char* err, size_t err_size) {
    if (!sha1_ok(sha1)) {
        snprintf(err, err_size, "no game SHA-1 for the coverage map");
        return false;
    }
    uint32_t* old = calloc(CORE_COVERAGE_SIZE, sizeof(uint32_t));
    uint8_t* buf = malloc(FILE_SIZE);
    if (!old || !buf) {
        free(old);
        free(buf);
        snprintf(err, err_size, "out of memory");
        return false;
    }
    char old_sha[41];
    bool ok = true;
    if (coverage_load(path, old_sha, old, err, err_size)) {
        if (strcmp(old_sha, sha1) != 0) {
            snprintf(err, err_size, "%s belongs to another game (%s)", path, old_sha);
            ok = false;
        }
    } else if (err[0]) {
        ok = false;
    }
    if (ok) {
        memcpy(buf, MAGIC, MAGIC_LEN);
        memcpy(buf + MAGIC_LEN, sha1, 40);
        buf[HEADER_LEN - 1] = '\n';
        uint8_t* p = buf + HEADER_LEN;
        for (size_t i = 0; i < CORE_COVERAGE_SIZE; i++, p += 4) {
            const uint32_t v = old[i] | map[i];
            p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
        }
        char tmp[1100];
        ok = platform_make_parent_dirs(path) && snprintf(tmp, sizeof(tmp), "%s.tmp", path) < (int)sizeof(tmp);
        FILE* f = ok ? fopen(tmp, "wb") : NULL;
        ok = f && fwrite(buf, 1, FILE_SIZE, f) == FILE_SIZE;
        if (f && fclose(f) != 0) ok = false;
        if (ok) ok = platform_replace_file(tmp, path);
        else if (f) remove(tmp);
        if (!ok) snprintf(err, err_size, "couldn't write %s", path);
    }
    free(old);
    free(buf);
    return ok;
}

uint32_t coverage_count(const uint32_t* map) {
    uint32_t n = 0;
    for (size_t i = 0; i < CORE_COVERAGE_SIZE; i++) n += map[i] != 0;
    return n;
}
