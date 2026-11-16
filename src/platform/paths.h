#pragma once
#include <stdbool.h>
#include <stddef.h>

// Per-user config directory for Breadbin-6502 (not created by this call):
//   Linux/BSD: $XDG_CONFIG_HOME/breadbin-6502 (fallback ~/.config/breadbin-6502)
//   Windows:   %APPDATA%\breadbin-6502
//   macOS:     ~/Library/Application Support/breadbin-6502
// Returns false if no suitable base directory could be determined.
bool platform_config_dir(char* out, size_t out_size);

// <config dir>/roms/system: where user-supplied system ROMs are looked up.
bool platform_system_rom_dir(char* out, size_t out_size);
