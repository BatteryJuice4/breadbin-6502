// Disk and tape media (see media.h).
#include "media/media.h"

#include "core/cart.h"
#include "media/tap.h"
#include "rom/sha1.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_ext(const char* path, const char* ext) {
    if (!path) return false;
    const size_t n = strlen(path), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        if (tolower((unsigned char)path[n - e + i]) != ext[i]) return false;
    }
    return true;
}

media_format_t media_detect(const char* path, const uint8_t* data, size_t size) {
    if (!data || size == 0) return MEDIA_FMT_UNKNOWN;
    if (cart_is_crt(data, size)) return MEDIA_FMT_CRT;
    if (tap_is_tap(data, size)) return MEDIA_FMT_TAP;
    if (t64_is_t64(data, size) && !has_ext(path, ".prg")) return MEDIA_FMT_T64;
    if (d64_size_ok(size) && !has_ext(path, ".prg") && !has_ext(path, ".bin")) return MEDIA_FMT_D64;
    if (has_ext(path, ".prg") && size > 2 && size <= CORE_FILE_MAX) return MEDIA_FMT_PRG;
    if (has_ext(path, ".bin")) return MEDIA_FMT_BIN;
    return MEDIA_FMT_UNKNOWN;
}

media_kind_t media_format_kind(media_format_t fmt) {
    switch (fmt) {
        case MEDIA_FMT_D64: case MEDIA_FMT_PRG: return MEDIA_KIND_DISK;
        case MEDIA_FMT_T64: case MEDIA_FMT_TAP: return MEDIA_KIND_TAPE;
        default: return MEDIA_KIND_CARTRIDGE;
    }
}

const char* media_format_name(media_format_t fmt) {
    switch (fmt) {
        case MEDIA_FMT_CRT: return ".crt";
        case MEDIA_FMT_BIN: return ".bin";
        case MEDIA_FMT_D64: return ".d64";
        case MEDIA_FMT_PRG: return ".prg";
        case MEDIA_FMT_T64: return ".t64";
        case MEDIA_FMT_TAP: return ".tap";
        default: return "unknown";
    }
}

static void hash_t64(const t64_t* t, char out[41]) {
    sha1_ctx_t ctx;
    uint8_t d[20];
    sha1_init(&ctx);
    for (int i = 0; i < t->count; i++) {
        const t64_entry_t* e = &t->entries[i];
        const uint8_t la[2] = { (uint8_t)e->start, (uint8_t)(e->start >> 8) };
        sha1_update(&ctx, la, 2);
        sha1_update(&ctx, t->data + e->offset, e->size);
    }
    sha1_final(&ctx, d);
    for (int i = 0; i < 20; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
}

bool media_open(media_image_t* img, media_format_t fmt, uint8_t* data, size_t size, char* err, size_t err_size) {
    memset(img, 0, sizeof(*img));
    img->fmt = fmt;
    img->data = data;
    img->size = size;
    bool ok = false;
    switch (fmt) {
        case MEDIA_FMT_D64:
            ok = d64_open(data, size, &img->d64, err, err_size);
            if (ok) sha1_hex(data, d64_data_size(&img->d64), img->sha1);
            break;
        case MEDIA_FMT_PRG:
            ok = size > 2 && size <= CORE_FILE_MAX;
            if (ok) sha1_hex(data, size, img->sha1);
            else if (err) snprintf(err, err_size, "not a program file (%zu bytes)", size);
            break;
        case MEDIA_FMT_T64:
            ok = t64_open(data, size, &img->t64, err, err_size);
            if (ok) hash_t64(&img->t64, img->sha1);
            break;
        case MEDIA_FMT_TAP: {
            const uint8_t* p;
            size_t n;
            ok = tap_check(data, size, &p, &n, err, err_size);
            if (ok) {
                sha1_hex(p, n, img->sha1);
                img->tap_v1 = tap_to_v1(data, size, &img->tap_v1_size, err, err_size);
                ok = img->tap_v1 != NULL;
            }
            break;
        }
        default:
            if (err) snprintf(err, err_size, "not a disk or tape image");
            break;
    }
    if (!ok) media_close(img);
    return ok;
}

void media_close(media_image_t* img) {
    free(img->data);
    free(img->tap_v1);
    memset(img, 0, sizeof(*img));
}

int media_device(const media_image_t* img) {
    return media_format_kind(img->fmt) == MEDIA_KIND_TAPE ? 1 : 8;
}

core_file_result_t media_serve(void* user_data, uint8_t device, const uint8_t* name, size_t name_len,
                               uint8_t* buf, size_t buf_size, size_t* len) {
    const media_image_t* img = user_data;
    if (!img || !img->data) return CORE_FILE_NOT_HANDLED;
    switch (img->fmt) {
        case MEDIA_FMT_D64:
            if (device != 8) return CORE_FILE_NOT_HANDLED;
            return d64_load(&img->d64, name, name_len, buf, buf_size, len) ? CORE_FILE_OK : CORE_FILE_NOT_FOUND;
        case MEDIA_FMT_PRG:
            // A single program: every name loads it (except the directory).
            if (device != 8) return CORE_FILE_NOT_HANDLED;
            if (name_len == 1 && name[0] == '$') return CORE_FILE_NOT_FOUND;
            if (img->size > buf_size) return CORE_FILE_NOT_FOUND;
            memcpy(buf, img->data, img->size);
            *len = img->size;
            return CORE_FILE_OK;
        case MEDIA_FMT_T64:
            if (device != 1) return CORE_FILE_NOT_HANDLED;
            return t64_load(&img->t64, name, name_len, buf, buf_size, len) ? CORE_FILE_OK : CORE_FILE_NOT_FOUND;
        default:
            return CORE_FILE_NOT_HANDLED;   // .tap plays through the datasette
    }
}
