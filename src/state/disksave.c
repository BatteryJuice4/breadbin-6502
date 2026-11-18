// Save copies of disks the game wrote to (see disksave.h).
#include "state/disksave.h"

#include "platform/paths.h"
#include "rom/rom_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISKSAVE_MAX (2u * 1024u * 1024u)   // a full 84-track .g64 is ~690 KB

bool disksave_path(const char* dir, const char* media_sha1, char* out, size_t out_size) {
    if (!dir || !dir[0] || !media_sha1 || strlen(media_sha1) != 40) return false;
    const int n = snprintf(out, out_size, "%s%c%s.g64", dir, platform_path_sep(), media_sha1);
    return n > 0 && (size_t)n < out_size;
}

bool disksave_write(const char* path, const core_disk_t* disk, char* err, size_t err_size) {
    const size_t n = gcr_g64_size(disk);
    uint8_t* data = malloc(n);
    if (!data) {
        snprintf(err, err_size, "out of memory");
        return false;
    }
    gcr_to_g64(disk, data, n);
    bool ok = false;
    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = NULL;
    if (!platform_make_parent_dirs(path)) {
        snprintf(err, err_size, "can't create the folder for %s", path);
    } else if (!(f = fopen(tmp, "wb")) || fwrite(data, 1, n, f) != n) {
        snprintf(err, err_size, "can't write %s", tmp);
    } else {
        ok = true;
    }
    if (f && fclose(f) != 0) ok = false;
    if (ok && !platform_replace_file(tmp, path)) {
        snprintf(err, err_size, "can't write %s", path);
        ok = false;
    }
    if (!ok) remove(tmp);
    free(data);
    return ok;
}

bool disksave_read(const char* path, gcr_image_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (err_size) err[0] = 0;
    if (!platform_file_exists(path)) return false;
    uint8_t* data = NULL;
    size_t size = 0;
    if (!rom_read_file(path, DISKSAVE_MAX, &data, &size, err, err_size)) return false;
    const bool ok = gcr_from_g64(data, size, out, err, err_size);
    free(data);
    return ok;
}
