#include "video/video.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_gl.h"

#include <stdint.h>

// Framebuffer upper bound (PAL VIC-II: 504 x 312). Checked at draw time.
#define VIDEO_MAX_W 512
#define VIDEO_MAX_H 320

// Default palette: Colodore (colodore.com). Packed as 0xAABBGGRR for RGBA8 upload.
// Selectable palettes come in Phase 7; this table is display-only.
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
static const uint32_t palette_colodore[16] = {
    RGB(0x00, 0x00, 0x00), RGB(0xFF, 0xFF, 0xFF), RGB(0x81, 0x33, 0x38), RGB(0x75, 0xCE, 0xC8),
    RGB(0x8E, 0x3C, 0x97), RGB(0x56, 0xAC, 0x4D), RGB(0x2E, 0x2C, 0x9B), RGB(0xED, 0xF1, 0x71),
    RGB(0x8E, 0x50, 0x29), RGB(0x55, 0x38, 0x00), RGB(0xC4, 0x6C, 0x71), RGB(0x4A, 0x4A, 0x4A),
    RGB(0x7B, 0x7B, 0x7B), RGB(0xA9, 0xFF, 0x9F), RGB(0x70, 0x6D, 0xEB), RGB(0xB2, 0xB2, 0xB2),
};
#undef RGB

static struct {
    sg_image img;
    sg_view view;
    sg_sampler smp;
    uint32_t rgba[VIDEO_MAX_W * VIDEO_MAX_H];
} vs;

void video_init(void) {
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    vs.img = sg_make_image(&(sg_image_desc){
        .width = VIDEO_MAX_W,
        .height = VIDEO_MAX_H,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.write_transient = true,
        .label = "c64-framebuffer",
    });
    vs.view = sg_make_view(&(sg_view_desc){ .texture.image = vs.img });
    vs.smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST,
        .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
}

void video_shutdown(void) {
    sgl_shutdown();
    sg_shutdown();
}

void video_window_size(int screen_w, int screen_h, int scale, int* out_w, int* out_h) {
    *out_w = screen_w * scale;
    *out_h = screen_h * scale;
}

void video_draw(const core_video_t* fb) {
    const int sw = fb->screen_w, sh = fb->screen_h;
    const bool fb_ok = fb->pixels && fb->width <= VIDEO_MAX_W && fb->height <= VIDEO_MAX_H &&
                       sw > 0 && sh > 0;
    if (fb_ok) {
        // Convert only the visible rect, packed at the texture origin.
        for (int y = 0; y < sh; y++) {
            const uint8_t* src = fb->pixels + (size_t)(fb->screen_y + y) * fb->width + fb->screen_x;
            uint32_t* dst = vs.rgba + (size_t)y * VIDEO_MAX_W;
            for (int x = 0; x < sw; x++) {
                dst[x] = palette_colodore[src[x] & 15];
            }
        }
        sg_write_image_transient(&(sg_write_image_desc){
            .src.data = { .ptr = vs.rgba, .size = sizeof(vs.rgba) },
            .dst.image = vs.img,
        });
    }

    const float win_w = sapp_widthf(), win_h = sapp_heightf();
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f, win_w, win_h, 0.0f, -1.0f, 1.0f);
    if (fb_ok) {
        // Integer scale that fits, falling back to fractional if the window is tiny.
        float scale = (float)((int)(win_w / sw) < (int)(win_h / sh) ? (int)(win_w / sw) : (int)(win_h / sh));
        if (scale < 1.0f) {
            scale = (win_w / sw) < (win_h / sh) ? (win_w / sw) : (win_h / sh);
        }
        const float w = sw * scale, h = sh * scale;
        const float x0 = (win_w - w) * 0.5f, y0 = (win_h - h) * 0.5f;
        const float u1 = (float)sw / VIDEO_MAX_W, v1 = (float)sh / VIDEO_MAX_H;
        sgl_enable_texture();
        sgl_texture(vs.view, vs.smp);
        sgl_begin_quads();
        sgl_v2f_t2f(x0,     y0,     0.0f, 0.0f);
        sgl_v2f_t2f(x0 + w, y0,     u1,   0.0f);
        sgl_v2f_t2f(x0 + w, y0 + h, u1,   v1);
        sgl_v2f_t2f(x0,     y0 + h, 0.0f, v1);
        sgl_end();
    }

    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f } },
        },
        .swapchain = sglue_swapchain(),
    });
    sgl_draw();
    sg_end_pass();
    sg_commit();
}
