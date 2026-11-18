// Disk/tape autostart (see autostart.h).
#include "media/autostart.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define KERNAL_CHRIN    0xFFCF
#define KBD_BUFFER      0x0277  // 10-byte keyboard buffer
#define KBD_COUNT       0x00C6  // characters in it
#define KBD_CHUNK       8       // typed per frame (the buffer holds 10)

enum {
    AS_WAIT_PROMPT = 0,         // until BASIC asks for input
    AS_TYPE_LOAD,
    AS_WAIT_DEVICE,             // .tap or the real 1541: until the load is over and BASIC asks again
    AS_TYPE_RUN,                // trap loads: typed right after LOAD (the load is instant)
    AS_RUNNING,                 // until loading is over
    AS_DONE,
};

// Quiet time after the last load activity before loading counts as over.
#define IDLE_DISK_FRAMES 25
#define IDLE_TAPE_FRAMES 100
// Warp limits (emulated time), PAL frames.
#define LIMIT_DISK_FRAMES (2u * 60u * 50u)
#define LIMIT_TAPE_FRAMES (20u * 60u * 50u)
#define LIMIT_DRIVE_FRAMES (10u * 60u * 50u)
// The 1541 needs about a second after power-on before it answers on the bus.
#define DRIVE_READY_FRAMES 100

static void set_text(autostart_t* a, const char* s) {
    snprintf(a->text, sizeof(a->text), "%s", s);
    a->text_pos = 0;
}

// Feed the pending command into the keyboard buffer, a chunk at a time, whenever
// the screen editor has emptied it. Returns true when everything was handed over.
static bool type_more(autostart_t* a, core_t* core) {
    if (core_ram_read(core, KBD_COUNT) != 0) return false;
    const int left = (int)strlen(a->text) - a->text_pos;
    if (left <= 0) return true;
    const int n = left < KBD_CHUNK ? left : KBD_CHUNK;
    for (int i = 0; i < n; i++) {
        core_ram_write(core, (uint16_t)(KBD_BUFFER + i), (uint8_t)a->text[a->text_pos + i]);
    }
    core_ram_write(core, KBD_COUNT, (uint8_t)n);
    a->text_pos += n;
    return false;
}

void autostart_begin(autostart_t* a, core_t* core, media_format_t fmt, const autostart_opts_t* opts) {
    memset(a, 0, sizeof(*a));
    a->fmt = fmt;
    a->loading = true;
    core_clear_watches(core);
    a->ready_watch = core_add_watch(core, KERNAL_CHRIN);
    a->done_watch = (opts && opts->done_pc >= 0) ? core_add_watch(core, (uint16_t)opts->done_pc) : -1;
    a->done_frames = opts ? opts->done_frames : 0;
    const bool tape = media_format_kind(fmt) == MEDIA_KIND_TAPE;
    a->drive = !tape && fmt != MEDIA_FMT_PRG && core_has_drive(core);
    a->limit = tape ? LIMIT_TAPE_FRAMES : a->drive ? LIMIT_DRIVE_FRAMES : LIMIT_DISK_FRAMES;
    if (a->done_frames > a->limit) a->limit = a->done_frames;

    // PETSCII: unshifted letters are the ASCII capitals.
    char file[20] = "";
    if (opts && opts->file && opts->file[0]) {
        size_t i = 0;
        for (; opts->file[i] && i < 16; i++) file[i] = (char)toupper((unsigned char)opts->file[i]);
        file[i] = 0;
    } else if (!tape) {
        snprintf(file, sizeof(file), "*");
    }
    char cmd[64];
    if (fmt == MEDIA_FMT_TAP) {
        snprintf(cmd, sizeof(cmd), "LOAD\"%s\",1,1\r", file);
        core_tape_play(core);   // PLAY is down before LOAD asks for it
    } else {
        snprintf(cmd, sizeof(cmd), "LOAD\"%s\",%d,1\r", file, tape ? 1 : 8);
    }
    set_text(a, cmd);
    a->state = AS_WAIT_PROMPT;
}

void autostart_frame(autostart_t* a, core_t* core) {
    if (a->state == AS_DONE) return;
    a->frames++;
    switch (a->state) {
        case AS_WAIT_PROMPT:
            if (core_watch_hits(core, a->ready_watch) > 0 && (!a->drive || a->frames >= DRIVE_READY_FRAMES)) {
                a->state = AS_TYPE_LOAD;
            }
            break;
        case AS_TYPE_LOAD:
            if (type_more(a, core)) {
                set_text(a, "RUN\r");
                // Real tape and drive loads take a while, and the KERNAL may clear the
                // keyboard buffer meanwhile (Open ROMs does), so RUN waits for the next prompt.
                a->state = (a->fmt == MEDIA_FMT_TAP || a->drive) ? AS_WAIT_DEVICE : AS_TYPE_RUN;
            }
            break;
        case AS_WAIT_DEVICE:
            if (a->drive) {
                // The 1541 keeps spinning for a while after the load, so count the
                // prompts from when its motor started: LOAD itself never reads input.
                if (!a->motor_seen && core_drive_motor(core)) {
                    a->motor_seen = true;
                    a->prompt_hits = core_watch_hits(core, a->ready_watch);
                } else if (a->motor_seen && core_watch_hits(core, a->ready_watch) > a->prompt_hits) {
                    a->state = AS_TYPE_RUN;     // BASIC is back at its prompt
                }
            } else if (core_tape_motor_on(core)) {
                a->motor_seen = true;
                a->prompt_hits = core_watch_hits(core, a->ready_watch);
            } else if (a->motor_seen && core_watch_hits(core, a->ready_watch) > a->prompt_hits) {
                a->state = AS_TYPE_RUN;     // BASIC is back at its prompt
            }
            break;
        case AS_TYPE_RUN:
            if (type_more(a, core)) {
                a->state = AS_RUNNING;
                a->idle = 0;
            }
            break;
        default:
            break;
    }
    // Load activity: a file served by the LOAD trap, or the tape or drive motor running.
    const uint32_t loads = core_files_loaded(core);
    if (loads != a->last_loads || core_tape_motor_on(core) || core_drive_motor(core)) {
        a->idle = 0;
        a->last_loads = loads;
    } else {
        a->idle++;
    }
    if (a->state == AS_RUNNING) {
        bool over;
        if (a->done_watch >= 0) {
            over = core_watch_hits(core, a->done_watch) > 0;
        } else if (a->done_frames > 0) {
            over = a->frames >= a->done_frames;
        } else {
            const bool tape = media_format_kind(a->fmt) == MEDIA_KIND_TAPE;
            // A trap-loaded game counts as loaded once a file was served; a tape or the real
            // drive once its motor rested.
            const bool motor = tape || a->drive;
            over = a->idle >= (motor ? IDLE_TAPE_FRAMES : IDLE_DISK_FRAMES) && (motor ? a->motor_seen : a->last_loads > 0);
        }
        if (over) a->state = AS_DONE;
    }
    if (a->frames >= a->limit) a->state = AS_DONE;
    a->loading = a->state != AS_DONE;
}

bool autostart_loading(const autostart_t* a) {
    return a->loading;
}
