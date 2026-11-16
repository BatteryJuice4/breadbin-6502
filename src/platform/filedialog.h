#pragma once
// Native "open file" dialog (nativefiledialog-extended). Blocking; call from the
// main thread while the game is paused. When it's unavailable or fails, the UI
// falls back to its built-in file browser.
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILEDIALOG_OK = 0,
    FILEDIALOG_CANCEL,
    FILEDIALOG_ERROR,           // dialog unavailable/failed: use the fallback browser
} filedialog_result_t;

// Filters on C64 media (.crt, .bin). default_dir may be NULL.
filedialog_result_t platform_open_rom_dialog(const char* default_dir, char* out, size_t out_size,
                                             char* err, size_t err_size);

// False when the native dialog is compiled out or disabled (BREADBIN_NO_NATIVE_DIALOG=1).
bool platform_native_dialog_available(void);

#ifdef __cplusplus
}
#endif
