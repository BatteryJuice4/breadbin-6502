// Unit tests for game pack parsing (game.toml), version checks and ROM hash matching.
#include "gamepack/gamepack.h"
#include "input/keynames.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

static char err[256], warns[4096];

static bool parse(gamepack_t* p, settings_t* d, const char* text) {
    return gamepack_parse(text, strlen(text), p, d, err, sizeof(err), warns, sizeof(warns));
}

static const char* full_pack =
    "id = \"example-game\"\n"
    "title = \"Example Game\"\n"
    "media = \"cartridge\"\n"
    "video_standard = \"NTSC\"\n"
    "sid_model = \"8580\"\n"
    "joystick_port = 1\n"
    "min_launcher_version = \"0.1.0\"\n"
    "system_roms = \"any\"\n"
    "[rom]\n"
    "cart_type = \"16K\"\n"
    "load_address = 0x8000\n"
    "known_sha1 = [\"DA39A3EE5E6B4B0D3255BFEF95601890AFD80709\", \"0000000000000000000000000000000000000001\"]\n"
    "[input.default]\n"
    "up = \"Up\"\n"
    "down = \"Down\"\n"
    "left = \"Left\"\n"
    "right = \"Right\"\n"
    "fire = [\"Space\", \"LCtrl\", \"Gamepad_A\"]\n"
    "menu = \"F12\"\n"
    "[video]\n"
    "default_scale = 4\n"
    "crop_border = true\n"
    "filter = \"smooth\"\n"
    "palette = \"pepto\"\n"
    "extended_view_max_width = 0\n";

static void test_full(void) {
    gamepack_t p;
    settings_t d;
    settings_default(&d);
    CHECK(parse(&p, &d, full_pack), "full pack rejected: %s", err);
    CHECK(!strcmp(p.id, "example-game") && !strcmp(p.title, "Example Game"), "id/title");
    CHECK(p.media == GAMEPACK_MEDIA_CARTRIDGE && p.ntsc && p.sid_model == 8580, "media/video/sid");
    CHECK(p.cart_type == GAMEPACK_CART_16K && p.bin_load_address == 0x8000, "rom table");
    CHECK(p.known_sha1_count == 2 && !strcmp(p.known_sha1[0], "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "hashes lowercased");
    CHECK(!strcmp(p.palette, "pepto") && !strcmp(d.video.palette, "pepto") && p.extended_view_max_width == 0, "palette");
    // defaults layered onto settings
    CHECK(d.input.joystick_port == 1, "joystick_port default");
    CHECK(d.input.keys[INPUT_UP][0] == input_key_from_name("Up") && d.input.keys[INPUT_UP][1] == 0, "up binding replaces both slots");
    CHECK(d.input.keys[INPUT_FIRE][0] == input_key_from_name("Space") &&
          d.input.keys[INPUT_FIRE][1] == input_key_from_name("LCtrl"), "fire bindings");
    CHECK(d.input.menu_key == input_key_from_name("F12"), "menu key");
    CHECK(d.video.window_scale == 4 && d.video.crop_border && d.video.filter == VIDEO_FILTER_LINEAR, "video defaults");
    CHECK(d.video.integer_scaling, "untouched default kept");
    CHECK(strstr(warns, "Gamepad_A") != NULL, "gamepad binding should warn: %s", warns);
    CHECK(strstr(warns, "unknown key") == NULL, "no unknown-key warnings expected: %s", warns);
}

static void test_required_and_invalid(void) {
    gamepack_t p;
    settings_t d, before;
    settings_default(&d);
    before = d;
    CHECK(!parse(&p, &d, "title = \"x\"\n"), "missing id accepted");
    CHECK(!parse(&p, &d, "id = \"x\"\n"), "missing title accepted");
    CHECK(!parse(&p, &d, "id = \"../evil\"\ntitle = \"x\"\n"), "path traversal id accepted");
    CHECK(!parse(&p, &d, "id = \"MyGame\"\ntitle = \"x\"\n"), "uppercase id accepted");
    CHECK(!parse(&p, &d, "id = \"\"\ntitle = \"x\"\n"), "empty id accepted");
    CHECK(!parse(&p, &d, "id = 5\ntitle = \"x\"\n"), "numeric id accepted");
    CHECK(!parse(&p, &d, "id = \"x\"\ntitle = \"x\"\njoystick_port = 1\n[video\n"), "syntax error accepted");
    CHECK(!memcmp(&d, &before, sizeof(d)), "failed parse changed defaults");

    // bad optional values: warnings only, defaults untouched
    CHECK(parse(&p, &d,
        "id = \"x\"\ntitle = \"X\"\nmedia = \"floppy\"\nvideo_standard = \"SECAM\"\njoystick_port = 3\n"
        "min_launcher_version = \"soon\"\nsystem_roms = \"maybe\"\ntypo_key = 1\n"
        "[rom]\ncart_type = \"ocean\"\nload_address = 0x1234\nknown_sha1 = [\"abc\", 5]\n"
        "[input.default]\nup = \"NoSuchKey\"\nmenu = \"KP_8\"\njump = \"Space\"\n"
        "[video]\ndefault_scale = 20\ncrop_border = \"yes\"\npalette = \"neon\"\n"), "bad optional values rejected: %s", err);
    CHECK(p.media == GAMEPACK_MEDIA_CARTRIDGE && !p.ntsc && p.cart_type == GAMEPACK_CART_AUTO, "bad values not ignored");
    CHECK(p.known_sha1_count == 0 && p.bin_load_address == 0 && !p.min_launcher_version[0], "bad rom values not ignored");
    CHECK(d.input.joystick_port == 2 && d.video.window_scale == 3 && !d.video.crop_border, "bad defaults applied");
    CHECK(d.input.keys[INPUT_UP][0] == 0, "unknown-only binding should clear the action");
    // menu = KP_8 is fine, but KP_8 is also a default joystick key: removed from the joystick
    CHECK(d.input.menu_key == input_key_from_name("KP_8"), "menu key from pack");
    for (int a = 0; a < INPUT_ACTION_COUNT; a++)
        for (int b = 0; b < INPUT_MAX_BINDS; b++)
            CHECK(d.input.keys[a][b] != d.input.menu_key, "menu key still bound to action %d", a);
    const char* expect[] = { "media", "video_standard", "joystick_port", "min_launcher_version", "system_roms",
                             "typo_key", "cart_type", "load_address", "entry 1", "entry 2", "NoSuchKey", "jump",
                             "default_scale", "crop_border", "neon" };
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        CHECK(strstr(warns, expect[i]) != NULL, "missing warning about '%s' in:\n%s", expect[i], warns);
    }

    // minimal pack: generic defaults stay
    settings_default(&d);
    before = d;
    CHECK(parse(&p, &d, "id = \"min\"\ntitle = \"Minimal\"\n"), "minimal pack rejected");
    CHECK(!memcmp(&d, &before, sizeof(d)) && warns[0] == 0 && p.sid_model == 6581, "minimal pack changed defaults or warned");
    CHECK(parse(&p, NULL, "id = \"min\"\ntitle = \"Minimal\"\nmedia = \"disk\"\n") && p.media == GAMEPACK_MEDIA_DISK, "NULL defaults / disk");
}

static void test_versions(void) {
    bool ok;
    CHECK(gamepack_version_compare("0.1.0", "0.1.0", &ok) == 0 && ok, "equal");
    CHECK(gamepack_version_compare("0.2.0", "0.10.0", &ok) < 0 && ok, "numeric, not lexical");
    CHECK(gamepack_version_compare("1.0", "0.9.9", &ok) > 0 && ok, "missing patch");
    CHECK(gamepack_version_compare("1", "1.0.0", &ok) == 0 && ok, "major only");
    CHECK(gamepack_version_compare("0.3.0-rc1", "0.3.0", &ok) == 0 && ok, "suffix ignored");
    gamepack_version_compare("dev", "0.1.0", &ok);
    CHECK(!ok, "'dev' isn't a version");
    gamepack_version_compare("1..2", "0.1.0", &ok);
    CHECK(!ok, "'1..2' isn't a version");
    gamepack_version_compare("1.2.3.4", "0.1.0", &ok);
    CHECK(!ok, "four parts");

    gamepack_t p;
    memset(&p, 0, sizeof(p));
    CHECK(gamepack_launcher_ok(&p, "0.1.0"), "no minimum");
    strcpy(p.min_launcher_version, "0.3.0");
    CHECK(!gamepack_launcher_ok(&p, "0.1.0"), "older launcher accepted");
    CHECK(gamepack_launcher_ok(&p, "0.3.0") && gamepack_launcher_ok(&p, "1.0.0"), "newer launcher rejected");
}

static void test_hashes(void) {
    gamepack_t p;
    memset(&p, 0, sizeof(p));
    CHECK(!gamepack_knows_sha1(&p, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "empty list knows a hash");
    strcpy(p.known_sha1[p.known_sha1_count++], "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(gamepack_knows_sha1(&p, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "exact");
    CHECK(gamepack_knows_sha1(&p, "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709"), "case-insensitive");
    CHECK(!gamepack_knows_sha1(&p, "da39a3ee5e6b4b0d3255bfef95601890afd80708"), "different hash");
    CHECK(!gamepack_knows_sha1(&p, "da39a3ee"), "prefix");
    CHECK(!gamepack_knows_sha1(&p, NULL), "NULL");
}

// Key names promised in docs/gamepack.md
// [load]: disk and tape games
static void test_load(void) {
    gamepack_t p;
    settings_t d;
    settings_default(&d);
    CHECK(parse(&p, &d, "id = \"x\"\ntitle = \"X\"\nmedia = \"disk\"\n"
                        "[load]\nfile = \"GAME\"\ndone_pc = 0x0810\ndone_frames = 600\n"), "load table rejected: %s", err);
    CHECK(p.media == GAMEPACK_MEDIA_DISK && !strcmp(p.load_file, "GAME") && p.load_done_pc == 0x0810 &&
          p.load_done_frames == 600, "load values");
    CHECK(strstr(warns, "unknown") == NULL, "unexpected warnings: %s", warns);
    // Not set: done_pc is -1, done_frames 0, file empty.
    CHECK(parse(&p, &d, "id = \"x\"\ntitle = \"X\"\nmedia = \"tape\"\n"), "minimal tape pack");
    CHECK(p.media == GAMEPACK_MEDIA_TAPE && p.load_file[0] == 0 && p.load_done_pc == -1 && p.load_done_frames == 0, "load defaults");
    // Bad values warn and are ignored.
    CHECK(parse(&p, &d, "id = \"x\"\ntitle = \"X\"\n[load]\nfile = \"A NAME LONGER THAN 16\"\ndone_pc = 70000\n"
                        "done_frames = 0\nspeed = 2\n"), "bad load values must not reject the pack");
    CHECK(p.load_file[0] == 0 && p.load_done_pc == -1 && p.load_done_frames == 0, "bad load values ignored");
    CHECK(strstr(warns, "16 characters") && strstr(warns, "done_pc") && strstr(warns, "done_frames") &&
          strstr(warns, "unknown key \"speed\""), "load warnings: %s", warns);
}

static void test_documented_keys(void) {
    const char* names[] = { "A", "Z", "0", "9", "Space", "Enter", "Escape", "Tab", "Backspace", "Up", "Down",
                            "Left", "Right", "LCtrl", "RCtrl", "LShift", "RShift", "LAlt", "RAlt", "F1", "F12",
                            "KP_0", "KP_9", "KP_Enter" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        CHECK(input_key_from_name(names[i]) != 0, "documented key name '%s' unknown", names[i]);
    }
}

// Every pack file given on the command line (e.g. the shipped example) must parse cleanly.
static void test_files(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        static char text[256 * 1024];
        FILE* f = fopen(argv[i], "rb");
        CHECK(f != NULL, "can't open %s", argv[i]);
        if (!f) continue;
        size_t n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        text[n] = 0;
        gamepack_t p;
        settings_t d;
        settings_default(&d);
        CHECK(gamepack_parse(text, n, &p, &d, err, sizeof(err), warns, sizeof(warns)), "%s: %s", argv[i], err);
        CHECK(warns[0] == 0, "%s has warnings:\n%s", argv[i], warns);
    }
}

int main(int argc, char** argv) {
    test_documented_keys();
    test_files(argc, argv);
    test_full();
    test_required_and_invalid();
    test_versions();
    test_hashes();
    test_load();
    if (failures == 0) printf("test_gamepack: all passed\n");
    return failures ? 1 : 0;
}
