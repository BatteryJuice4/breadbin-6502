// Unit tests for settings (TOML load/save) and key names.
#include "settings/settings.h"
#include "video/palettes.h"
#include "input/keynames.h"
#include "platform/gamepad.h"
#include "platform/paths.h"
#include "video/view.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static char err[256], warns[2048];

static bool parse(settings_t* s, const char* text) {
    return settings_parse(s, text, strlen(text), err, sizeof(err), warns, sizeof(warns));
}

static void test_keynames(void) {
    int named = 0;
    for (int code = 1; code < INPUT_MAX_KEYCODE; code++) {
        const char* n = input_key_name(code);
        if (!n) continue;
        named++;
        CHECK(input_key_from_name(n) == code, "key name '%s' doesn't round-trip", n);
    }
    CHECK(named > 100, "only %d named keys", named);
    CHECK(input_key_from_name("lctrl") == input_key_from_name("LCtrl"), "names not case-insensitive");
    CHECK(input_key_from_name("Ctrl") == input_key_from_name("LCtrl"), "alias Ctrl");
    CHECK(input_key_from_name("Return") == input_key_from_name("Enter"), "alias Return");
    CHECK(input_key_from_name("Gamepad_A") == INPUT_GAMEPAD_BASE + GAMEPAD_A, "Gamepad_A");
    CHECK(input_key_from_name("gamepad_lstick_up") == INPUT_GAMEPAD_BASE + GAMEPAD_LS_UP, "gamepad names case-insensitive");
    CHECK(input_key_from_name("Gamepad_Nope") == 0 && input_key_from_name("Gamepad_") == 0, "unknown gamepad button");
    for (int b = 0; b < GAMEPAD_BUTTON_COUNT; b++) {
        const char* n = input_key_name(INPUT_GAMEPAD_BASE + b);
        CHECK(n && input_key_from_name(n) == INPUT_GAMEPAD_BASE + b, "gamepad name round trip %d", b);
    }
    CHECK(input_key_from_name(NULL) == 0 && input_key_from_name("") == 0, "empty names");
}

static void test_roundtrip(void) {
    settings_t a, b;
    settings_default(&a);
    strcpy(a.rom_path, "C:\\Games\\C64\\\"quoted\" name\tx.crt");
    a.video = (video_settings_t){ 5, true, false, false, true, VIDEO_FILTER_LINEAR, "mod:my-palette", true, 0 };
    a.mods_enabled_count = 2;
    strcpy(a.mods_enabled[0], "zeta");
    strcpy(a.mods_enabled[1], "alpha.v2");
    a.audio = (audio_settings_t){ 0.25f, true, 120 };
    a.input.keys[INPUT_FIRE][0] = input_key_from_name("Space");
    a.input.keys[INPUT_FIRE][1] = input_key_from_name("LCtrl");
    a.input.keys[INPUT_UP][1] = input_key_from_name("W");
    a.input.joystick_port = 1;
    a.input.menu_key = input_key_from_name("F12");
    a.input.quick_save_key = input_key_from_name("F5");
    a.input.quick_load_key = 0;     // unbound

    char text[4096];
    size_t need = settings_serialize(&a, NULL, 0);
    CHECK(need > 0 && need < sizeof(text), "serialize size %zu", need);
    CHECK(settings_serialize(&a, text, sizeof(text)) == need, "serialize size mismatch");

    settings_default(&b);
    CHECK(parse(&b, text), "re-parse failed: %s\n%s", err, text);
    CHECK(warns[0] == 0, "warnings on round-trip: %s", warns);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0 || (
          !strcmp(a.rom_path, b.rom_path) &&
          !memcmp(&a.video, &b.video, sizeof(a.video)) &&
          a.audio.mute == b.audio.mute && a.audio.latency_ms == b.audio.latency_ms &&
          a.audio.volume > b.audio.volume - 0.001f && a.audio.volume < b.audio.volume + 0.001f &&
          !memcmp(&a.input, &b.input, sizeof(a.input))), "round-trip mismatch:\n%s", text);
    CHECK(b.mods_enabled_count == 2 && !strcmp(b.mods_enabled[0], "zeta") && !strcmp(b.mods_enabled[1], "alpha.v2"),
          "mod order round-trip");
    CHECK(!strcmp(a.rom_path, b.rom_path), "rom path '%s' vs '%s'", a.rom_path, b.rom_path);

    // Truncated buffer: snprintf-style, NUL-terminated, same reported size
    char small[16];
    CHECK(settings_serialize(&a, small, sizeof(small)) == need && small[15] == 0, "truncated serialize");
}

static void test_quick_keys(void) {
    settings_t s;
    settings_default(&s);
    CHECK(s.input.quick_save_key == input_key_from_name("F9") && s.input.quick_load_key == input_key_from_name("F10"),
          "quick save/load default to F9/F10");
    CHECK(parse(&s, "[input]\nquick_save = \"F2\"\nquick_load = []\n"), "parse: %s", err);
    CHECK(s.input.quick_save_key == input_key_from_name("F2") && s.input.quick_load_key == 0, "rebind / unbind");
    settings_default(&s);
    CHECK(parse(&s, "[input]\nmenu = \"F12\"\n"), "parse: %s", err);
    CHECK(s.input.quick_save_key == input_key_from_name("F9"), "older settings files keep the default keys");
}

static void test_partial_and_invalid(void) {
    settings_t s;
    settings_default(&s);
    const settings_t def = s;
    CHECK(parse(&s, "[video]\ncrop_border = true\n"), "partial: %s", err);
    CHECK(s.video.crop_border && s.video.window_scale == def.video.window_scale, "partial override");
    CHECK(!memcmp(&s.input, &def.input, sizeof(s.input)), "partial changed input");

    settings_default(&s);
    CHECK(parse(&s,
        "[video]\nscale = 99\nfullscreen = \"yes\"\nfilter = \"blurry\"\n"
        "[audio]\nvolume = 2.5\nlatency_ms = 5\n"
        "[input]\nfire = [\"Space\", \"Gamepad_Nope\", \"LCtrl\", \"Gamepad_A\", \"X\", \"Y\"]\njoystick_port = 3\nup = 7\nmenu = \"NoSuchKey\"\n"
        "[unknown]\nfoo = 1\n"
        "[mods]\nenabled = [\"good\", \"Bad Id\", 3, \"also-good\"]\n"), "invalid values must not be a parse error: %s", err);
    CHECK(s.mods_enabled_count == 2 && !strcmp(s.mods_enabled[1], "also-good"), "bad mod ids not skipped");
    CHECK(s.video.window_scale == def.video.window_scale, "scale 99 accepted");
    CHECK(s.video.fullscreen == def.video.fullscreen, "fullscreen string accepted");
    CHECK(s.audio.volume == def.audio.volume && s.audio.latency_ms == def.audio.latency_ms, "bad audio accepted");
    CHECK(s.input.joystick_port == 2, "joystick_port 3 accepted");
    CHECK(s.input.keys[INPUT_FIRE][0] == input_key_from_name("Space") &&
          s.input.keys[INPUT_FIRE][1] == input_key_from_name("LCtrl") &&
          s.input.keys[INPUT_FIRE][2] == input_key_from_name("Gamepad_A") &&
          s.input.keys[INPUT_FIRE][3] == input_key_from_name("X"), "fire keys: unknown not skipped / max not honoured");
    CHECK(s.input.keys[INPUT_UP][0] == def.input.keys[INPUT_UP][0], "non-string 'up' accepted");
    CHECK(s.input.menu_key == def.input.menu_key, "unknown menu key replaced the menu key");
    CHECK(strstr(warns, "scale") && strstr(warns, "Gamepad_Nope") && strstr(warns, "\"Y\" ignored") &&
          strstr(warns, "menu") && strstr(warns, "[mods]"), "missing warnings:\n%s", warns);
    settings_default(&s);
    CHECK(parse(&s, "[video]\npalette = \"psychedelic\"\n") && !strcmp(s.video.palette, PALETTE_DEFAULT) &&
          strstr(warns, "psychedelic"), "unknown palette accepted");
    CHECK(parse(&s, "[video]\npalette = \"pepto\"\n") && !strcmp(s.video.palette, "pepto"), "pepto rejected");
    CHECK(parse(&s, "[video]\nfilter = \"crt\"\n") && s.video.filter == VIDEO_FILTER_CRT, "crt filter rejected");
    char out[4096];
    settings_serialize(&s, out, sizeof(out));
    CHECK(strstr(out, "filter = \"crt\"") != NULL, "crt filter not written");

    // Syntax error: settings untouched
    settings_default(&s);
    s.video.window_scale = 7;
    CHECK(!parse(&s, "[video\nscale = 2\n"), "syntax error accepted");
    CHECK(s.video.window_scale == 7 && err[0], "settings changed on syntax error");

    // Empty fire list unbinds (in files that say their lists include gamepad buttons)
    settings_default(&s);
    CHECK(parse(&s, "[input]\ngamepad_binds = true\nfire = []\n") && s.input.keys[INPUT_FIRE][0] == 0 &&
          s.input.keys[INPUT_FIRE][1] == 0 && s.input.keys[INPUT_FIRE][2] == 0, "empty list should unbind");
    // Older files (no gamepad_binds): actions without gamepad buttons get the defaults
    settings_default(&s);
    CHECK(parse(&s, "[input]\nfire = [\"Space\"]\nup = [\"W\", \"Gamepad_Y\"]\n"), "parse: %s", err);
    CHECK(s.input.keys[INPUT_FIRE][0] == input_key_from_name("Space") &&
          s.input.keys[INPUT_FIRE][1] == input_key_from_name("Gamepad_A") &&
          s.input.keys[INPUT_FIRE][2] == input_key_from_name("Gamepad_B"), "old file: default gamepad fire added");
    CHECK(s.input.keys[INPUT_UP][1] == input_key_from_name("Gamepad_Y") && s.input.keys[INPUT_UP][2] == 0,
          "an action that lists a gamepad button keeps exactly that");
    // defaults: d-pad + left stick, A/B
    settings_default(&s);
    CHECK(s.input.keys[INPUT_LEFT][1] == input_key_from_name("Gamepad_Left") &&
          s.input.keys[INPUT_LEFT][2] == input_key_from_name("Gamepad_LStick_Left") &&
          s.input.keys[INPUT_FIRE][2] == input_key_from_name("Gamepad_A"), "default gamepad bindings");
}

static void test_files(void) {
    // Writes into the test's working directory (the build dir under ctest).
    const char* path = "test_settings_tmp/nested/dir/settings.toml";
    settings_t a, b;
    settings_default(&a);
    strcpy(a.rom_path, "/tmp/some game.crt");
    a.video.crop_border = true;
    a.video.extended_view = true;
    a.video.extended_max_width = 384;      // from the game pack: never saved
    CHECK(settings_save_file(&a, path, err, sizeof(err)), "save: %s", err);
    CHECK(platform_file_exists(path), "saved file missing");
    // overwrite (exercises atomic replace of an existing file)
    a.video.window_scale = 4;
    CHECK(settings_save_file(&a, path, err, sizeof(err)), "re-save: %s", err);
    bool missing = true;
    settings_default(&b);
    CHECK(settings_load_file(&b, path, &missing, err, sizeof(err), warns, sizeof(warns)) && !missing, "load: %s", err);
    CHECK(b.video.window_scale == 4 && b.video.crop_border && !strcmp(b.rom_path, a.rom_path), "loaded values");
    CHECK(b.video.extended_view && b.video.extended_max_width == 0, "extended view saved, pack width not");
    CHECK(!settings_load_file(&b, "test_settings_tmp/none.toml", &missing, err, sizeof(err), NULL, 0) && missing,
          "missing file not reported");
    remove(path);

    char p[1024];
    CHECK(platform_settings_path("example-game", p, sizeof(p)) && strstr(p, "example-game") && strstr(p, "settings.toml"), "settings path '%s'", p);
    CHECK(!platform_settings_path("", p, sizeof(p)), "empty game id accepted");
}

static void test_view_rect(void) {
    // PAL geometry as the core reports it
    const core_video_t fb = { .width = 520, .height = 312, .screen_x = 0, .screen_y = 0, .screen_w = 392,
                              .screen_h = 272, .display_x = 32, .display_y = 36, .display_w = 320, .display_h = 200 };
    video_settings_t s = { .crop_border = false, .extended_view = true, .extended_max_width = 384 };
    int x, y, w, h;
    video_view_rect(&fb, &s, &x, &y, &w, &h);
    CHECK(x == 0 && y == 0 && w == 392 && h == 272, "uncropped: %d,%d %dx%d", x, y, w, h);
    s.crop_border = true;
    s.extended_view = false;
    video_view_rect(&fb, &s, &x, &y, &w, &h);
    CHECK(x == 32 && y == 36 && w == 320 && h == 200, "cropped: %d,%d %dx%d", x, y, w, h);
    s.extended_view = true;
    video_view_rect(&fb, &s, &x, &y, &w, &h);
    CHECK(x == 0 && y == 36 && w == 384 && h == 200, "extended 384: %d,%d %dx%d", x, y, w, h);
    s.extended_max_width = 1000;            // clamped to what the core draws, kept inside it
    video_view_rect(&fb, &s, &x, &y, &w, &h);
    CHECK(x == 0 && w == 392 && h == 200, "extended max: %d %d", x, w);
    s.extended_max_width = 0;               // pack doesn't support it: setting ignored
    video_view_rect(&fb, &s, &x, &y, &w, &h);
    CHECK(x == 32 && w == 320, "unsupported: %d %d", x, w);
}

int main(void) {
    test_view_rect();
    test_keynames();
    test_roundtrip();
    test_quick_keys();
    test_partial_and_invalid();
    test_files();
    if (failures == 0) printf("test_settings: all passed\n");
    return failures ? 1 : 0;
}
