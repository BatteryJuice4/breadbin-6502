#pragma once
// Which part of the emulator framebuffer is shown (pure; no graphics).
#include "core/core.h"
#include "settings/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

// Source rectangle for the crop / extended view settings. With crop_border the
// 320x200 display is shown; the extended view keeps that height and widens it
// evenly into the side borders, up to extended_max_width and the drawn screen.
void video_view_rect(const core_video_t* fb, const video_settings_t* s, int* x, int* y, int* w, int* h);

#ifdef __cplusplus
}
#endif
