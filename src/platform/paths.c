#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE     // strict C11 hides POSIX (mkdir, stat, opendir) on glibc
#endif
#include "platform/paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define BB_APP_DIR "breadbin-6502"

static bool fmt(char* out, size_t out_size, const char* a, const char* b) {
    int n = snprintf(out, out_size, "%s%s", a, b);
    return n > 0 && (size_t)n < out_size;
}

char platform_path_sep(void) {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
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

bool platform_settings_path(const char* game_id, char* out, size_t out_size) {
    char base[1024];
    if (!game_id || !game_id[0] || !platform_config_dir(base, sizeof(base))) {
        return false;
    }
    const char s = platform_path_sep();
    int n = snprintf(out, out_size, "%s%cgames%c%s%csettings.toml", base, s, s, game_id, s);
    return n > 0 && (size_t)n < out_size;
}

static bool make_dir(const char* path) {
#if defined(_WIN32)
    return _mkdir(path) == 0 || platform_is_dir(path);
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

bool platform_make_parent_dirs(const char* path) {
    char buf[1024];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);
    // strip the file name
    char* last = NULL;
    for (char* p = buf; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (!last) return true;
    *last = 0;
    // create each component in turn (skip a leading separator / drive letter)
    for (char* p = buf + 1; *p; p++) {
        if ((*p == '/' || *p == '\\') && p[-1] != ':') {
            const char saved = *p;
            *p = 0;
            if (!platform_is_dir(buf) && !make_dir(buf)) return false;
            *p = saved;
        }
    }
    return platform_is_dir(buf) || make_dir(buf);
}

bool platform_replace_file(const char* src, const char* dst) {
#if defined(_WIN32)
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

bool platform_file_exists(const char* path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && !(st.st_mode & S_IFDIR);
}

bool platform_is_dir(const char* path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && (st.st_mode & S_IFDIR);
}

bool platform_list_dir(const char* dir, platform_dir_fn fn, void* user) {
#if defined(_WIN32)
    char pattern[1100];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
            fn(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, user);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return true;
#else
    DIR* d = opendir(dir);
    if (!d) return false;
    struct dirent* e;
    char full[2048];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        fn(e->d_name, platform_is_dir(full), user);
    }
    closedir(d);
    return true;
#endif
}

bool platform_home_dir(char* out, size_t out_size) {
#if defined(_WIN32)
    const char* h = getenv("USERPROFILE");
#else
    const char* h = getenv("HOME");
#endif
    if (!h || !h[0]) return false;
    int n = snprintf(out, out_size, "%s", h);
    return n > 0 && (size_t)n < out_size;
}
