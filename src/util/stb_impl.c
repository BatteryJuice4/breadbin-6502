// The one stb_image implementation unit (public domain / MIT): PNG decoding for
// game pack art (video/image.c) and raw deflate for .c64mod zips (mods/zip.c).
// No stdio: data always comes from memory.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096    // game packs are untrusted input: cap memory use
#include "stb_image.h"
