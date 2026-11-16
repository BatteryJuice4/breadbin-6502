// Headless smoke tests for the emulator core using the bundled Open ROMs.
#include "core/core.h"
#include "rom/sha1.h"
#include "rom/sysroms.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static sysroms_t roms;

static core_t* make_core(void) {
    return core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal,   .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic,     .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    });
}

static void run(core_t* c, int frames) {
    for (int i = 0; i < frames; i++) core_run_frame(c);
}

// ASCII -> C64 screen code (uppercase/graphics set) for the characters we search for.
static uint8_t screen_code(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (uint8_t)(ch - 'A' + 1);
    if (ch == '@') return 0;
    return (uint8_t)ch;     // space, digits and most punctuation map 1:1
}

// Screen RAM at $0400, 40x25. Returns the first row >= min_row containing `text`, or -1.
static int find_on_screen(const core_t* c, const char* text, int min_row) {
    const size_t len = strlen(text);
    for (int row = min_row; row < 25; row++) {
        for (int col = 0; col + (int)len <= 40; col++) {
            size_t i = 0;
            while (i < len && core_ram_read(c, (uint16_t)(0x0400 + row * 40 + col + i)) == screen_code(text[i])) i++;
            if (i == len) return row;
        }
    }
    return -1;
}

static void dump_screen(const core_t* c) {
    for (int row = 0; row < 25; row++) {
        char line[41];
        for (int col = 0; col < 40; col++) {
            uint8_t sc = core_ram_read(c, (uint16_t)(0x0400 + row * 40 + col)) & 0x7F;
            line[col] = (sc >= 1 && sc <= 26) ? (char)('A' + sc - 1) : (sc >= 0x20 && sc < 0x40) ? (char)sc : '.';
        }
        line[40] = 0;
        fprintf(stderr, "  |%s|\n", line);
    }
}

// Type a string: host lowercase letters are C64 unshifted, as in the input module.
static void type(core_t* c, const char* s) {
    for (; *s; s++) {
        int k = (unsigned char)*s;
        if (k >= 'a' && k <= 'z') k -= 0x20;
        else if (k == '\n') k = CORE_KEY_RETURN;
        core_key_down(c, k);
        run(c, 3);
        core_key_up(c, k);
        run(c, 3);
    }
}

static void hash_state(const core_t* c, char out[41]) {
    sha1_ctx_t ctx;
    uint8_t d[20];
    sha1_init(&ctx);
    for (unsigned a = 0; a < 0x10000; a++) {
        uint8_t b = core_ram_read(c, (uint16_t)a);
        sha1_update(&ctx, &b, 1);
    }
    core_video_t v = core_video(c);
    sha1_update(&ctx, v.pixels, (size_t)v.width * (size_t)v.height);
    sha1_final(&ctx, d);
    for (int i = 0; i < 20; i++) snprintf(out + i * 2, 3, "%02x", d[i]);
}

static void test_create_rejects_bad_input(void) {
    CHECK(core_create(NULL) == NULL, "NULL desc accepted");
    core_desc_t d = {
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal, .rom_kernal_size = 100,
        .rom_basic = roms.basic, .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
    };
    CHECK(core_create(&d) == NULL, "wrong kernal size accepted");
    d.rom_kernal_size = sizeof(roms.kernal);
    d.video_std = CORE_VIDEO_NTSC;
    CHECK(core_create(&d) == NULL, "NTSC accepted but not implemented");
}

static void test_timing(void) {
    core_t* c = make_core();
    CHECK(core_cycles_per_frame(c) == 19656, "PAL cycles/frame = %u", core_cycles_per_frame(c));
    double fps = core_frame_rate(c);
    CHECK(fps > 50.12 && fps < 50.13, "PAL frame rate = %f", fps);
    core_video_t v = core_video(c);
    CHECK(v.width >= v.screen_x + v.screen_w && v.height >= v.screen_y + v.screen_h, "screen rect outside framebuffer");
    core_destroy(c);
}

static void test_boot_and_basic(void) {
    core_t* c = make_core();
    run(c, 250);    // 5 seconds
    int ready_row = find_on_screen(c, "READY.", 0);
    CHECK(ready_row >= 0, "no READY. prompt after boot");

    // Open ROMs BASIC can't print numbers yet (print_FAC1 not done), so print a string.
    // "0" is included on purpose (see the keymap workaround in core_chips.c).
    type(c, "print \"hello 0\"\n");
    run(c, 25);
    const int cmd_row = find_on_screen(c, "PRINT \"HELLO 0\"", ready_row + 1);
    CHECK(cmd_row > ready_row, "typed command not echoed");
    CHECK(find_on_screen(c, "HELLO 0", cmd_row + 1) == cmd_row + 1, "PRINT output not on the next line");

    // Joystick: a tiny routine at $C000 copies CIA1 port A ($DC00) to $C100.
    //   LDA $DC00 / STA $C100 / RTS
    static const uint8_t prog[] = { 0xAD, 0x00, 0xDC, 0x8D, 0x00, 0xC1, 0x60 };
    for (unsigned i = 0; i < sizeof(prog); i++) core_ram_write(c, (uint16_t)(0xC000 + i), prog[i]);
    core_ram_write(c, 0xC100, 0x00);
    // Port A also drives the keyboard columns (real hardware behaviour), so hold
    // fire only while RETURN executes the line, or typed keys get dropped.
    type(c, "sys 49152");
    core_set_joystick(c, 2, CORE_JOY_FIRE | CORE_JOY_LEFT);
    type(c, "\n");
    run(c, 10);
    core_set_joystick(c, 2, 0);
    // KERNAL leaves $DC00 = $7F; fire (bit 4) and left (bit 2) pull lines low.
    const uint8_t pa = core_ram_read(c, 0xC100);
    CHECK(pa == (0x7F & ~(0x10 | 0x04)), "joystick 2 at $DC00 read $%02X, want $6B", pa);

    if (failures) dump_screen(c);

    core_reset(c);
    CHECK(core_frame_count(c) == 0, "frame count not reset");
    run(c, 250);
    CHECK(find_on_screen(c, "READY.", 0) >= 0, "no READY. after reset");
    core_destroy(c);
}

static void test_border_color(void) {
    core_t* c = make_core();
    run(c, 250);
    core_video_t v = core_video(c);
    const uint8_t before = v.pixels[(size_t)v.screen_y * v.width + v.screen_x] & 15;
    // LDA #$01 / STA $D020 / RTS  -> border white
    static const uint8_t prog[] = { 0xA9, 0x01, 0x8D, 0x20, 0xD0, 0x60 };
    for (unsigned i = 0; i < sizeof(prog); i++) core_ram_write(c, (uint16_t)(0xC000 + i), prog[i]);
    type(c, "sys 49152\n");
    run(c, 5);
    v = core_video(c);
    const uint8_t after = v.pixels[(size_t)v.screen_y * v.width + v.screen_x] & 15;
    printf("test_core: boot border color %u, after POKE %u\n", before, after);
    CHECK(after == 1, "border pixel is color %u after setting $D020=1", after);

    // The reported display window must be exactly where the background shows:
    // clear the screen to spaces and set background black, border stays white.
    for (int a = 0x0400; a < 0x07E8; a++) core_ram_write(c, (uint16_t)a, 32);
    static const uint8_t bg_black[] = { 0xA9, 0x00, 0x8D, 0x21, 0xD0, 0x60 };
    for (unsigned i = 0; i < sizeof(bg_black); i++) core_ram_write(c, (uint16_t)(0xC000 + i), bg_black[i]);
    type(c, "sys 49152\n");
    for (int a = 0x0400; a < 0x07E8; a++) core_ram_write(c, (uint16_t)a, 32);
    run(c, 3);
    v = core_video(c);
    int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    for (int y = v.screen_y; y < v.screen_y + v.screen_h; y++) {
        for (int x = v.screen_x; x < v.screen_x + v.screen_w; x++) {
            if ((v.pixels[(size_t)y * v.width + x] & 15) == 0) {
                if (x < x0) x0 = x;
                if (y < y0) y0 = y;
                if (x > x1) x1 = x;
                if (y > y1) y1 = y;
            }
        }
    }
    CHECK(x0 == v.display_x && y0 == v.display_y && x1 - x0 + 1 == v.display_w && y1 - y0 + 1 == v.display_h,
          "display window reported %d,%d %dx%d, measured %d,%d %dx%d",
          v.display_x, v.display_y, v.display_w, v.display_h, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    core_destroy(c);
}

typedef struct { long count; float peak; } audio_probe_t;
static void probe_audio(const float* samples, int n, void* user) {
    audio_probe_t* p = (audio_probe_t*)user;
    for (int i = 0; i < n; i++) {
        float a = samples[i] < 0 ? -samples[i] : samples[i];
        if (a > p->peak) p->peak = a;
    }
    p->count += n;
}

static void test_audio(void) {
    audio_probe_t probe = { 0 };
    core_t* c = core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = roms.kernal,   .rom_kernal_size = sizeof(roms.kernal),
        .rom_basic = roms.basic,     .rom_basic_size = sizeof(roms.basic),
        .rom_chargen = roms.chargen, .rom_chargen_size = sizeof(roms.chargen),
        .audio_sample_rate = 44100,
        .audio_fn = probe_audio,
        .audio_user_data = &probe,
    });
    run(c, 250);
    // Sample rate: 250 PAL frames = 4.988 s -> ~219,960 samples at 44.1 kHz
    const double secs = 250.0 / core_frame_rate(c);
    CHECK(probe.count > secs * 44100 * 0.99 && probe.count < secs * 44100 * 1.01,
          "audio sample count %ld for %.3f s", probe.count, secs);
    const float idle_peak = probe.peak;
    // Voice 1: freq $1CD6 (~440 Hz), AD=$09, SR=$F0 (full sustain), volume 15, sawtooth+gate.
    static const uint8_t prog[] = {
        0xA9, 0xD6, 0x8D, 0x00, 0xD4,   // LDA #$D6 / STA $D400
        0xA9, 0x1C, 0x8D, 0x01, 0xD4,   // LDA #$1C / STA $D401
        0xA9, 0x09, 0x8D, 0x05, 0xD4,   // AD
        0xA9, 0xF0, 0x8D, 0x06, 0xD4,   // SR
        0xA9, 0x0F, 0x8D, 0x18, 0xD4,   // volume
        0xA9, 0x21, 0x8D, 0x04, 0xD4,   // sawtooth + gate
        0x60,
    };
    for (unsigned i = 0; i < sizeof(prog); i++) core_ram_write(c, (uint16_t)(0xC000 + i), prog[i]);
    type(c, "sys 49152\n");
    probe.peak = 0;
    run(c, 25);
    printf("test_core: audio peak idle %.3f, tone %.3f\n", idle_peak, probe.peak);
    CHECK(probe.peak > 0.05f && probe.peak > idle_peak * 4, "SID tone not audible (peak %.3f)", probe.peak);
    core_destroy(c);
}

static void test_determinism(void) {
    core_t* a = make_core();
    core_t* b = make_core();
    char ha[41], hb[41];
    for (int f = 0; f < 400; f++) {
        // identical scripted input on both
        if (f == 260) { core_key_down(a, 'A'); core_key_down(b, 'A'); }
        if (f == 263) { core_key_up(a, 'A');   core_key_up(b, 'A'); }
        if (f == 300) { core_set_joystick(a, 2, CORE_JOY_UP); core_set_joystick(b, 2, CORE_JOY_UP); }
        core_run_frame(a);
        core_run_frame(b);
        if (f % 50 == 49) {
            hash_state(a, ha);
            hash_state(b, hb);
            CHECK(strcmp(ha, hb) == 0, "state diverged at frame %d", f);
        }
    }
    core_destroy(a);
    core_destroy(b);
}

int main(void) {
    sysroms_load_default(&roms);
    test_create_rejects_bad_input();
    test_timing();
    test_boot_and_basic();
    test_border_color();
    test_audio();
    test_determinism();
    if (failures == 0) printf("test_core: all passed\n");
    return failures ? 1 : 0;
}
