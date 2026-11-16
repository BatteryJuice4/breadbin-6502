// Single translation unit holding the sokol implementations.
// Compiled as Objective-C on Apple platforms (see CMakeLists.txt).
// The graphics backend (SOKOL_GLCORE / SOKOL_METAL / SOKOL_GLES3) is set by CMake.
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE     // strict C11 hides POSIX APIs (clock_gettime, ...) on glibc
#endif
#define SOKOL_IMPL
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_audio.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"
#include "sokol_gl.h"
