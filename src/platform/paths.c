#include "platform/paths.h"

#include <stdio.h>
#include <stdlib.h>

#define BB_APP_DIR "breadbin-6502"

static bool fmt(char* out, size_t out_size, const char* a, const char* b) {
    int n = snprintf(out, out_size, "%s%s", a, b);
    return n > 0 && (size_t)n < out_size;
}

bool platform_config_dir(char* out, size_t out_size) {
#if defined(_WIN32)
    const char* appdata = getenv("APPDATA");
    return appdata && appdata[0] && fmt(out, out_size, appdata, "\\" BB_APP_DIR);
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    return home && home[0] && fmt(out, out_size, home, "/Library/Application Support/" BB_APP_DIR);
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') {
        return fmt(out, out_size, xdg, "/" BB_APP_DIR);
    }
    const char* home = getenv("HOME");
    return home && home[0] && fmt(out, out_size, home, "/.config/" BB_APP_DIR);
#endif
}

bool platform_system_rom_dir(char* out, size_t out_size) {
    char base[1024];
    if (!platform_config_dir(base, sizeof(base))) {
        return false;
    }
#if defined(_WIN32)
    return fmt(out, out_size, base, "\\roms\\system");
#else
    return fmt(out, out_size, base, "/roms/system");
#endif
}
