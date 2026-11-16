#pragma once
// Platform-specific filesystem helpers. All platform #ifdefs live in platform/.
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Per-user config directory for Breadbin-6502 (not created by this call):
//   Linux/BSD: $XDG_CONFIG_HOME/breadbin-6502 (fallback ~/.config/breadbin-6502)
//   Windows:   %APPDATA%\breadbin-6502
//   macOS:     ~/Library/Application Support/breadbin-6502
// Returns false if no suitable base directory could be determined.
bool platform_config_dir(char* out, size_t out_size);

// <config dir>/roms/system: where user-supplied system ROMs are looked up.
bool platform_system_rom_dir(char* out, size_t out_size);

// <config dir>/games/<game_id>/settings.toml
bool platform_settings_path(const char* game_id, char* out, size_t out_size);

// Create all missing parent directories of `path`.
bool platform_make_parent_dirs(const char* path);

// Atomically replace dst with src (rename that overwrites, also on Windows).
bool platform_replace_file(const char* src, const char* dst);

bool platform_file_exists(const char* path);
bool platform_is_dir(const char* path);

// Directory listing for the built-in file browser (fallback when the native
// dialog is unavailable). Calls fn for each entry except "." and "..".
typedef void (*platform_dir_fn)(const char* name, bool is_dir, void* user);
bool platform_list_dir(const char* dir, platform_dir_fn fn, void* user);

// Home directory (or a sensible starting folder for the file browser).
bool platform_home_dir(char* out, size_t out_size);

// Path separator for display/joining.
char platform_path_sep(void);

#ifdef __cplusplus
}
#endif
