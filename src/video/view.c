#include "video/view.h"

void video_view_rect(const core_video_t* fb, const video_settings_t* s, int* x, int* y, int* w, int* h) {
    if (!s->crop_border) {
        *x = fb->screen_x; *y = fb->screen_y; *w = fb->screen_w; *h = fb->screen_h;
        return;
    }
    *x = fb->display_x; *y = fb->display_y; *w = fb->display_w; *h = fb->display_h;
    if (!s->extended_view || s->extended_max_width <= fb->display_w) return;
    const int ew = s->extended_max_width < fb->screen_w ? s->extended_max_width : fb->screen_w;
    int ex = fb->display_x - (ew - fb->display_w) / 2;
    if (ex < fb->screen_x) ex = fb->screen_x;
    if (ex + ew > fb->screen_x + fb->screen_w) ex = fb->screen_x + fb->screen_w - ew;
    *x = ex;
    *w = ew;
}
