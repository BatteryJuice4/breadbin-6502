#pragma once
/*
    Script mods: sandboxed Lua 5.4 scripts that run with every emulated frame.
    The full script API is in docs/mods.md; in short, a script gets a `bb` table
    (read/write RAM, joystick input for the coming frame, write watches, overlay
    text, log) and may define global hooks:

      on_frame()             before each frame (input can still be changed)
      on_after_frame()       after each frame
      on_write(addr, value)  after a frame, once per watched CPU write, in order

    Sandbox: no io/os/package/debug libraries, text chunks only, a memory cap and an
    instruction budget per hook call. A script that errors or runs too long is
    stopped and reported (the caller disables its mod); it can't crash the launcher.

    Determinism: everything a script sees comes from the emulated machine, Lua's
    string hash and math.random have fixed seeds, and hooks run at fixed points in
    the frame, so a replay re-runs scripts exactly. Scripts restart (fresh Lua
    state) whenever the game is loaded, reset or a save state is loaded.

    Pure C on top of core.h; the Lua build itself is lua_impl.c.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_MAX 16                   // scripts running at once
#define SCRIPT_MAX_TEXT 32              // overlay texts per script per frame
#define SCRIPT_TEXT_LEN 96
#define SCRIPT_MAX_WATCHES 64           // write watches per script
#define SCRIPT_MEMORY_LIMIT (16u * 1024u * 1024u)
#define SCRIPT_INSTRUCTION_LIMIT 5000000 // per hook call (main chunk included)

typedef struct {
    int x, y;                   // pixels, relative to the top-left of the 320x200 screen area
    uint32_t rgb;               // 0xRRGGBB
    char text[SCRIPT_TEXT_LEN];
} script_text_t;

typedef struct scriptset scriptset_t;

scriptset_t* scriptset_create(void);
void scriptset_destroy(scriptset_t* s, core_t* core);
// Stop every script (and remove their write watches from the core).
void scriptset_clear(scriptset_t* s, core_t* core);
// Start a script: compile and run its main chunk. On failure returns false with
// err set and nothing is left running for it.
bool scriptset_add(scriptset_t* s, core_t* core, const char* id, const char* name,
                   const char* source, size_t len, char* err, size_t err_size);
int scriptset_count(const scriptset_t* s);

// Before a frame: on_frame hooks in load order. joy[0..1] (ports 1 and 2) is the
// input for the coming frame; scripts may change it.
void scriptset_before_frame(scriptset_t* s, core_t* core, uint8_t joy[2]);
// After a frame: on_write for each watched write, then on_after_frame.
void scriptset_after_frame(scriptset_t* s, core_t* core);

// A script that failed since the last call (it has been stopped). Returns false
// when there are none left to report.
bool scriptset_take_failure(scriptset_t* s, char* id, size_t id_size, char* msg, size_t msg_size);

// Overlay texts from the last frame, all scripts together (display only).
int scriptset_texts(const scriptset_t* s, const script_text_t** out);

// Log lines (from print / bb.log), for stderr: "[id] text". Returns false when empty.
bool scriptset_take_log(scriptset_t* s, char* line, size_t size);

#ifdef __cplusplus
}
#endif
