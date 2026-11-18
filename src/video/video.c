#include "video/video.h"
#include "video/crt.h"
#include "video/palettes.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_gl.h"

#include <math.h>
#include <stdint.h>

// Framebuffer upper bound (VIC-II framebuffer sized for NTSC lines: 520 x 312). Checked at draw time.
#define VIDEO_MAX_W 528
#define VIDEO_MAX_H 320

// Display palette, packed as 0xAABBGGRR for RGBA8 upload (see video/palettes.h).
static uint32_t palette_rgba[16];

static struct {
    sg_image img;
    sg_view view;
    sg_sampler smp_nearest;
    sg_sampler smp_linear;
    video_settings_t settings;
    bool crt_pending;           // this frame's picture goes through the CRT shader
    crt_frame_t crt;
    uint32_t rgba[VIDEO_MAX_W * VIDEO_MAX_H];
} vs;

void video_set_palette(const uint32_t rgb[16]) {
    for (int i = 0; i < 16; i++) {
        const uint32_t c = rgb[i];
        palette_rgba[i] = 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF);
    }
}

void video_init(void) {
    video_set_palette(palette_find_builtin(PALETTE_DEFAULT)->rgb);
    sg_setup(&(sg_desc){
        .environment = sglue_environment(),
        .logger.func = slog_func,
    });
    sgl_setup(&(sgl_desc_t){ .logger.func = slog_func });
    crt_init();
    vs.img = sg_make_image(&(sg_image_desc){
        .width = VIDEO_MAX_W,
        .height = VIDEO_MAX_H,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .usage.write_transient = true,
        .label = "c64-framebuffer",
    });
    vs.view = sg_make_view(&(sg_view_desc){ .texture.image = vs.img });
    vs.smp_nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_NEAREST, .mag_filter = SG_FILTER_NEAREST,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    vs.smp_linear = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    settings_t def;
    settings_default(&def);
    vs.settings = def.video;
}

void video_shutdown(void) {
    crt_shutdown();
    sgl_shutdown();
    sg_shutdown();
}

void video_set_settings(const video_settings_t* s) {
    vs.settings = *s;
}

// Source rectangle inside the framebuffer according to the crop setting.
static void source_rect(const core_video_t* fb, bool crop, int* x, int* y, int* w, int* h) {
    if (crop) {
        *x = fb->display_x; *y = fb->display_y; *w = fb->display_w; *h = fb->display_h;
    } else {
        *x = fb->screen_x; *y = fb->screen_y; *w = fb->screen_w; *h = fb->screen_h;
    }
}

void video_window_size(const core_video_t* fb, const video_settings_t* s, int* out_w, int* out_h) {
    int x, y, w, h;
    source_rect(fb, s->crop_border, &x, &y, &w, &h);
    const float aspect = s->aspect_correction ? fb->pixel_aspect : 1.0f;
    *out_w = (int)lroundf((float)w * aspect * (float)s->window_scale);
    *out_h = h * s->window_scale;
}

void video_draw(const core_video_t* fb, bool dim) {
    const float win_w = sapp_widthf(), win_h = sapp_heightf();
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f, win_w, win_h, 0.0f, -1.0f, 1.0f);
    vs.crt_pending = false;
    if (!fb || !fb->pixels || fb->width > VIDEO_MAX_W || fb->height > VIDEO_MAX_H) {
        return;
    }
    int sx, sy, sw, sh;
    source_rect(fb, vs.settings.crop_border, &sx, &sy, &sw, &sh);
    if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > fb->width || sy + sh > fb->height) {
        return;
    }
    // Convert only the source rect, packed at the texture origin.
    for (int y = 0; y < sh; y++) {
        const uint8_t* src = fb->pixels + (size_t)(sy + y) * fb->width + sx;
        uint32_t* dst = vs.rgba + (size_t)y * VIDEO_MAX_W;
        for (int x = 0; x < sw; x++) {
            dst[x] = palette_rgba[src[x] & 15];
        }
    }
    sg_write_image_transient(&(sg_write_image_desc){
        .src.data = { .ptr = vs.rgba, .size = sizeof(vs.rgba) },
        .dst.image = vs.img,
    });

    // Target size: aspect-corrected source, scaled to fit the window.
    const float aspect = vs.settings.aspect_correction ? fb->pixel_aspect : 1.0f;
    const float base_w = (float)sw * aspect, base_h = (float)sh;
    float scale = fminf(win_w / base_w, win_h / base_h);
    if (vs.settings.integer_scaling && scale >= 1.0f) {
        scale = floorf(scale);
    }
    const float w = roundf(base_w * scale), h = roundf(base_h * scale);
    const float x0 = floorf((win_w - w) * 0.5f), y0 = floorf((win_h - h) * 0.5f);
    const float u1 = (float)sw / VIDEO_MAX_W, v1 = (float)sh / VIDEO_MAX_H;
    const float c = dim ? 0.35f : 1.0f;

    if (vs.settings.filter == VIDEO_FILTER_CRT) {
        vs.crt = (crt_frame_t){
            .texture = vs.view, .tex_w = VIDEO_MAX_W, .tex_h = VIDEO_MAX_H,
            .src_w = (float)sw, .src_h = (float)sh,
            .x = x0, .y = y0, .w = w, .h = h, .win_w = win_w, .win_h = win_h, .dim = c,
        };
        vs.crt_pending = true;
        return;
    }
    sgl_enable_texture();
    sgl_texture(vs.view, vs.settings.filter == VIDEO_FILTER_LINEAR ? vs.smp_linear : vs.smp_nearest);
    sgl_begin_quads();
    sgl_v2f_t2f_c3f(x0,     y0,     0.0f, 0.0f, c, c, c);
    sgl_v2f_t2f_c3f(x0 + w, y0,     u1,   0.0f, c, c, c);
    sgl_v2f_t2f_c3f(x0 + w, y0 + h, u1,   v1,   c, c, c);
    sgl_v2f_t2f_c3f(x0,     y0 + h, 0.0f, v1,   c, c, c);
    sgl_end();
}

void video_present(void (*overlay)(void)) {
    sg_begin_pass(&(sg_pass){
        .action = {
            .colors[0] = { .load_action = SG_LOADACTION_CLEAR, .clear_value = { 0.0f, 0.0f, 0.0f, 1.0f } },
        },
        .swapchain = sglue_swapchain(),
    });
    if (vs.crt_pending) crt_draw(&vs.crt);
    sgl_draw();
    if (overlay) {
        overlay();
    }
    sg_end_pass();
    sg_commit();
}
