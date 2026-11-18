#pragma once
/*
    Disk and tape media: format detection, identity hashes, and serving files to
    the core's KERNAL LOAD trap. Cartridges stay in core/cart.h.

    Formats and what the media SHA-1 covers (so the same game matches no matter
    which header or padding a tool wrote):
      .d64  disk image   sector data, without the optional error bytes   (d64.h)
      .g64  raw GCR disk every recorded half-track: length + bytes      (gcr.h)
      .prg  program      the whole file (load address + data)
      .t64  tape archive each used entry as served, in directory order   (t64.h)
      .tap  raw tape     the pulse data after the header, as stored      (tap.h)

    Devices: .d64, .g64 and .prg are drive 8; .t64 is the tape device 1. With the
    user's 1541 ROM the core emulates a real drive and disks go into it (use_drive:
    the trap leaves them alone, so fastloaders and protections work); otherwise
    .d64/.g64/.prg are served instantly through the LOAD trap (a .g64 through the
    sectors decoded from it, so protected originals may not load that way).
    .t64 is always served by the trap. .tap images play through the emulated
    datasette in real time (warp speed while loading), so turbo loaders work.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"
#include "media/d64.h"
#include "media/gcr.h"
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
    MEDIA_FMT_G64,
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
    gcr_image_t gcr;            // .d64/.g64: the disk surface for the 1541 (owned)
    uint8_t* g64_sectors;       // .g64: decoded sectors behind `d64` (owned)
    bool use_drive;             // disks go to the emulated 1541, not the LOAD trap
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

// The disk surface for core_insert_disk(), or NULL (not a .d64/.g64).
core_disk_t* media_disk(media_image_t* img);
// Replace the surface (e.g. with a save copy of this disk); takes ownership.
void media_set_surface(media_image_t* img, gcr_image_t* gcr);
// .d64: build the surface again from the sector data (after patch mods changed it).
bool media_rebuild_surface(media_image_t* img, char* err, size_t err_size);

#ifdef __cplusplus
}
#endif
