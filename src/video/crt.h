#pragma once
// CRT-style filter: the picture drawn through a small shader (scanlines whose
// beam widens on bright pixels, a faint RGB mask, sharp-but-smooth horizontal
// sampling). Display only, like the palette. Part of video.c's frame.
#include "sokol_gfx.h"

#include <stdbool.h>

void crt_init(void);
void crt_shutdown(void);

typedef struct {
    sg_view texture;            // the picture (RGBA8), top-left aligned
    float tex_w, tex_h;         // texture size in texels
    float src_w, src_h;         // the part of the texture that holds the picture
    float x, y, w, h;           // target rectangle in framebuffer pixels
    float win_w, win_h;         // framebuffer size
    float dim;                  // 1 = full brightness (darker behind the menu)
} crt_frame_t;

// Draw inside the current render pass.
void crt_draw(const crt_frame_t* f);
