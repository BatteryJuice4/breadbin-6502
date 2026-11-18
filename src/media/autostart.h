#pragma once
/*
    Autostart for disk and tape media: waits for the BASIC prompt, types the LOAD
    and RUN commands through the KERNAL keyboard buffer, and decides when loading
    is over (so the launcher can run at warp speed with no picture or sound until
    then: "no loading screens").

    Everything is driven by emulated state only (frame counts, RAM, PC watches),
    so it is deterministic and replays identically.

    Loading is over when, after RUN has been typed:
      - the game pack's done_pc was executed, if it sets one, or
      - the game pack's done_frames have passed, if it sets that, or
      - otherwise, no file was loaded and the tape (or real 1541) motor was off for
        a moment.
    With the real 1541 (core_has_drive), LOAD is typed once the drive has had time
    to start, and RUN once the load is over (as for tapes).
    A time limit ends warp anyway, so a game that never "finishes" still shows up.
*/
#include <stdbool.h>
#include <stdint.h>

#include "core/core.h"
#include "media/media.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    media_format_t fmt;
    int state;
    char text[96];              // command still to type (PETSCII)
    int text_pos;
    int ready_watch;            // KERNAL CHRIN ($FFCF): BASIC is waiting for input
    int done_watch;             // game pack done_pc, or -1
    uint32_t done_frames;       // game pack done_frames, or 0
    uint32_t frames;            // since autostart_begin
    uint32_t idle;              // frames without load activity
    uint32_t last_loads;
    uint32_t loads_before;      // files served when LOAD was typed (fast load with the drive)
    uint32_t limit;             // warp time limit in frames
    bool drive;                 // disk in the real 1541 (not the LOAD trap)
    bool motor_seen;            // .tap/1541: the motor has run
    uint32_t prompt_hits;       // .tap/1541: CHRIN hits while the motor last ran
    bool loading;
} autostart_t;

typedef struct {
    const char* file;           // file to load; NULL or "" = the first one ("*" / next on tape)
    int done_pc;                // -1 = not set
    uint32_t done_frames;       // 0 = not set
} autostart_opts_t;

// Call right after the media is inserted and the machine reset.
void autostart_begin(autostart_t* a, core_t* core, media_format_t fmt, const autostart_opts_t* opts);
// Call after every core_run_frame() while autostart_loading() is true.
void autostart_frame(autostart_t* a, core_t* core);
bool autostart_loading(const autostart_t* a);

#ifdef __cplusplus
}
#endif
