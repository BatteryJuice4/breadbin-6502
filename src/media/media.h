#pragma once
/*
    Disk and tape media: format detection, identity hashes, and serving files to
    the core's KERNAL LOAD trap. Cartridges stay in core/cart.h.

    Formats and what the media SHA-1 covers (so the same game matches no matter
    which header or padding a tool wrote):
      .d64  disk image   sector data, without the optional error bytes   (d64.h)
      .prg  program      the whole file (load address + data)
      .t64  tape archive each used entry as served, in directory order   (t64.h)
      .tap  raw tape     the pulse data after the header, as stored      (tap.h)

    Devices: .d64 and .prg are served as drive 8 (instantly, through the LOAD
    trap); .t64 as the tape device 1. .tap images play through the emulated
    datasette in real time (warp speed while loading), so turbo loaders work.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"
#include "media/d64.h"
#include "media/t64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEDIA_FMT_UNKNOWN = 0,
    MEDIA_FMT_CRT,
    MEDIA_FMT_BIN,
    MEDIA_FMT_D64,
    MEDIA_FMT_PRG,
    MEDIA_FMT_T64,
    MEDIA_FMT_TAP,
} media_format_t;

typedef enum {
    MEDIA_KIND_CARTRIDGE = 0,
    MEDIA_KIND_DISK,
    MEDIA_KIND_TAPE,
} media_kind_t;

// Detect by content first (.crt/.tap/.t64 signatures, .d64 sizes), then by the
// file extension (.prg, .bin). path may be NULL.
media_format_t media_detect(const char* path, const uint8_t* data, size_t size);
media_kind_t media_format_kind(media_format_t fmt);
const char* media_format_name(media_format_t fmt);     // ".d64", ...

// An opened disk/tape image. Owns its data.
typedef struct {
    media_format_t fmt;
    uint8_t* data;
    size_t size;
    char sha1[41];
    d64_t d64;
    t64_t t64;
    uint8_t* tap_v1;            // .tap converted for the core (owned)
    size_t tap_v1_size;
} media_image_t;

// Takes ownership of data (malloc'd) on success and on failure.
bool media_open(media_image_t* img, media_format_t fmt, uint8_t* data, size_t size, char* err, size_t err_size);
void media_close(media_image_t* img);

// core_file_fn for core_set_file_loader(); user_data is a media_image_t* (or
// NULL: nothing inserted, every LOAD goes to the real KERNAL).
core_file_result_t media_serve(void* user_data, uint8_t device, const uint8_t* name, size_t name_len,
                               uint8_t* buf, size_t buf_size, size_t* len);

// Device a LOAD should use for this image (8 or 1).
int media_device(const media_image_t* img);

#ifdef __cplusplus
}
#endif
