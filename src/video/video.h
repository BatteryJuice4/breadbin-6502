#pragma once
// Framebuffer -> texture -> window. Palette lookup happens here, on the copy
// sent to the screen only; emulated state is never touched.
#include "core/core.h"
#include "settings/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

void video_init(void);
void video_shutdown(void);
void video_set_settings(const video_settings_t* vs);
// The 16 colours used for display (0xRRGGBB). Starts as the default palette.
void video_set_palette(const uint32_t rgb[16]);

// Record the emulator picture for this frame. fb may be NULL (nothing drawn).
// dim darkens the picture (used behind the menu).
void video_draw(const core_video_t* fb, bool dim);

// Run the frame's render pass: the picture, then overlay() (e.g. the UI), then present.
void video_present(void (*overlay)(void));

// Window size for the given picture and settings (window_scale, crop, aspect).
void video_window_size(const core_video_t* fb, const video_settings_t* vs, int* out_w, int* out_h);

#ifdef __cplusplus
}
#endif
