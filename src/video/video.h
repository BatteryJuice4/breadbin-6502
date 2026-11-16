#pragma once
// Framebuffer -> texture -> window. Palette lookup happens here, on the copy
// sent to the screen only; emulated state is never touched.
#include "core/core.h"

void video_init(void);
void video_shutdown(void);
// Convert the core framebuffer with the current palette and draw it
// letterboxed with integer scaling into the current window.
void video_draw(const core_video_t* fb);
// Suggested initial window size for the given visible area and scale.
void video_window_size(int screen_w, int screen_h, int scale, int* out_w, int* out_h);
