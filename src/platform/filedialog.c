#include "platform/filedialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(BB_HAVE_NFD)
#include "nfd.h"
#endif

bool platform_native_dialog_available(void) {
#if defined(BB_HAVE_NFD)
    const char* off = getenv("BREADBIN_NO_NATIVE_DIALOG");
    return !(off && off[0] && strcmp(off, "0") != 0);
#else
    return false;
#endif
}

filedialog_result_t platform_open_rom_dialog(const char* default_dir, char* out, size_t out_size,
                                             char* err, size_t err_size) {
    if (err && err_size) err[0] = 0;
    if (!platform_native_dialog_available()) {
        if (err) snprintf(err, err_size, "native file dialog not available");
        return FILEDIALOG_ERROR;
    }
#if defined(BB_HAVE_NFD)
    if (NFD_Init() != NFD_OKAY) {
        if (err) snprintf(err, err_size, "file dialog: %s", NFD_GetError() ? NFD_GetError() : "init failed");
        return FILEDIALOG_ERROR;
    }
    nfdu8filteritem_t filters[] = {
        { "C64 games", "crt,bin,d64,prg,t64,tap" },
        { "Cartridges", "crt,bin" },
        { "Disks", "d64,prg" },
        { "Tapes", "tap,t64" },
    };
    nfdu8char_t* path = NULL;
    nfdopendialogu8args_t args = { 0 };
    args.filterList = filters;
    args.filterCount = 4;
    args.defaultPath = (default_dir && default_dir[0]) ? default_dir : NULL;
    nfdresult_t r = NFD_OpenDialogU8_With(&path, &args);
    filedialog_result_t res;
    if (r == NFD_OKAY) {
        int n = snprintf(out, out_size, "%s", path);
        res = (n > 0 && (size_t)n < out_size) ? FILEDIALOG_OK : FILEDIALOG_ERROR;
        if (res != FILEDIALOG_OK && err) snprintf(err, err_size, "path too long");
        NFD_FreePathU8(path);
    } else if (r == NFD_CANCEL) {
        res = FILEDIALOG_CANCEL;
    } else {
        if (err) snprintf(err, err_size, "file dialog: %s", NFD_GetError() ? NFD_GetError() : "failed");
        res = FILEDIALOG_ERROR;
    }
    NFD_Quit();
    return res;
#else
    (void)default_dir; (void)out; (void)out_size;
    return FILEDIALOG_ERROR;
#endif
}
