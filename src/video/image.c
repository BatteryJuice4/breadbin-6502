#include "video/image.h"

#include <stdio.h>
#include <string.h>

// stb_image is implemented once in util/stb_impl.c (PNG only, no stdio, 4096 px cap).
#include "stb_image.h"

bool image_decode_png(const uint8_t* data, size_t size, int max_dim, image_t* out, char* err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (!data || size == 0 || size > 0x7FFFFFFF) {
        if (err) snprintf(err, err_size, "empty image");
        return false;
    }
    int w, h, comp;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &comp)) {
        if (err) snprintf(err, err_size, "not a PNG image (%s)", stbi_failure_reason());
        return false;
    }
    if (w <= 0 || h <= 0 || w > max_dim || h > max_dim) {
        if (err) snprintf(err, err_size, "image is %dx%d, the limit is %dx%d", w, h, max_dim, max_dim);
        return false;
    }
    uint8_t* px = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!px) {
        if (err) snprintf(err, err_size, "can't decode PNG (%s)", stbi_failure_reason());
        return false;
    }
    out->width = w;
    out->height = h;
    out->rgba = px;
    return true;
}

void image_free(image_t* img) {
    if (img->rgba) stbi_image_free(img->rgba);
    memset(img, 0, sizeof(*img));
}
