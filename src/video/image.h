#pragma once
// PNG decoding for game pack branding (icon.png, banner.png), via stb_image.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width, height;
    uint8_t* rgba;      // width*height*4 bytes, free with image_free()
} image_t;

// Decodes PNG data to RGBA8. Rejects images larger than max_dim in either direction.
bool image_decode_png(const uint8_t* data, size_t size, int max_dim, image_t* out, char* err, size_t err_size);
void image_free(image_t* img);

#ifdef __cplusplus
}
#endif
