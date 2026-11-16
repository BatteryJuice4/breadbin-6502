// Launcher UI: Select-ROM screen, in-game menu, built-in file browser.
#include "ui/ui.h"

#include "imgui.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"

extern "C" {
#include "input/keynames.h"
#include "platform/paths.h"
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct BrowserEntry {
    std::string name;
    bool is_dir;
};

struct Ui {
    float scale = 0.0f;
    ImGuiStyle base_style;
    bool frame_begun = false;

    // built-in file browser
    bool browser_open = false;
    bool browser_refresh = false;
    char browser_dir[SETTINGS_PATH_MAX] = {};
    std::vector<BrowserEntry> browser_entries;
    int browser_selected = -1;
    std::string browser_error;
} g;

const ImVec4 kErrorColor(1.0f, 0.45f, 0.40f, 1.0f);
const ImVec4 kOkColor(0.55f, 0.85f, 0.55f, 1.0f);
const ImVec4 kDimColor(0.65f, 0.65f, 0.70f, 1.0f);

// case-insensitive "less than" for the browser sort
bool strcasecmp_portable(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return tolower((unsigned char)x) < tolower((unsigned char)y);
    });
}

bool has_rom_ext(const std::string& n) {
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == ".crt" || e == ".bin";
}

void browser_list() {
    g.browser_entries.clear();
    g.browser_selected = -1;
    g.browser_error.clear();
    auto add = [](const char* name, bool is_dir, void*) {
        if (name[0] == '.') return;     // hidden files
        if (is_dir || has_rom_ext(name)) g.browser_entries.push_back({ name, is_dir });
    };
    if (!platform_list_dir(g.browser_dir, add, nullptr)) {
        g.browser_error = "Can't read this folder.";
    }
    std::sort(g.browser_entries.begin(), g.browser_entries.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return strcasecmp_portable(a.name, b.name);
    });
}

void browser_up() {
    std::string d = g.browser_dir;
    while (d.size() > 1 && (d.back() == '/' || d.back() == '\\')) d.pop_back();
    size_t p = d.find_last_of("/\\");
    if (p == std::string::npos) return;
    d = (p == 0) ? d.substr(0, 1) : d.substr(0, p);
    if (d.size() == 2 && d[1] == ':') d += '\\';    // "C:" -> "C:\"
    snprintf(g.browser_dir, sizeof(g.browser_dir), "%s", d.c_str());
    g.browser_refresh = true;
}

std::string join(const char* dir, const std::string& name) {
    std::string d = dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += platform_path_sep();
    return d + name;
}

void center_next_window(float w_frac, float h_frac) {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * w_frac, io.DisplaySize.y * h_frac), ImGuiCond_Always);
}

void dim_text(const char* fmt, ...) IM_FMTARGS(1);
void dim_text(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
    ImGui::TextWrapped("%s", buf);
    ImGui::PopStyleColor();
}

void draw_message(const ui_state_t* st) {
    if (st->message[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, st->message_is_error ? kErrorColor : kOkColor);
        ImGui::TextWrapped("%s", st->message);
        ImGui::PopStyleColor();
    }
}

const char* rom_status_text(rom_status_t s) {
    switch (s) {
        case ROM_STATUS_GENERIC: return "Generic mode (no game pack): any cartridge is accepted.";
        case ROM_STATUS_KNOWN:   return "Known good dump.";
        case ROM_STATUS_UNKNOWN: return "Not a known dump of this game. Mods may not work.";
        default:                 return "No cartridge loaded.";
    }
}

void file_browser_window(ui_state_t* st) {
    if (!g.browser_open) return;
    if (g.browser_refresh) {
        browser_list();
        g.browser_refresh = false;
    }
    center_next_window(0.8f, 0.8f);
    ImGui::OpenPopup("Select ROM file");
    if (ImGui::BeginPopupModal("Select ROM file", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        if (ImGui::Button("Up")) browser_up();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##dir", g.browser_dir, sizeof(g.browser_dir), ImGuiInputTextFlags_EnterReturnsTrue)) {
            g.browser_refresh = true;
        }
        // footer: one line of text + the button row
        const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("entries", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        if (!g.browser_error.empty()) ImGui::TextColored(kErrorColor, "%s", g.browser_error.c_str());
        std::string open_dir;
        std::string open_file;
        for (int i = 0; i < (int)g.browser_entries.size(); i++) {
            const BrowserEntry& e = g.browser_entries[i];
            std::string label = e.is_dir ? "[" + e.name + "]" : e.name;
            if (ImGui::Selectable(label.c_str(), g.browser_selected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                g.browser_selected = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (e.is_dir) open_dir = e.name; else open_file = e.name;
                }
            }
        }
        ImGui::EndChild();
        ImGui::TextColored(kDimColor, "Showing folders and .crt/.bin files.");
        const bool can_open = g.browser_selected >= 0 && g.browser_selected < (int)g.browser_entries.size();
        ImGui::BeginDisabled(!can_open);
        if (ImGui::Button("Open")) {
            const BrowserEntry& e = g.browser_entries[g.browser_selected];
            if (e.is_dir) open_dir = e.name; else open_file = e.name;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g.browser_open = false;
            ImGui::CloseCurrentPopup();
        }
        if (!open_dir.empty()) {
            snprintf(g.browser_dir, sizeof(g.browser_dir), "%s", join(g.browser_dir, open_dir).c_str());
            g.browser_refresh = true;
        } else if (!open_file.empty()) {
            snprintf(st->load_path, sizeof(st->load_path), "%s", join(g.browser_dir, open_file).c_str());
            st->act_load_path = true;
            g.browser_open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void select_rom_screen(ui_state_t* st) {
    center_next_window(0.85f, 0.0f);    // height 0 = fit content
    ImGui::Begin("##select", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
    ImGui::TextUnformatted(st->game_title);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextWrapped("Choose a Commodore 64 cartridge image (.crt or .bin) to play.");
    dim_text("You can also drag the file onto this window.");
    ImGui::Spacing();
    if (ImGui::Button("Select ROM...")) st->act_select_rom = true;
    ImGui::SameLine();
    if (ImGui::Button("Settings...")) {
        st->menu_return = UI_SCREEN_SELECT_ROM;
        st->screen = UI_SCREEN_MENU;
    }
    if (st->allow_no_cart) {
        ImGui::SameLine();
        if (ImGui::Button("Start without cartridge")) st->act_no_cart = true;
    }
    ImGui::Spacing();
    draw_message(st);
    ImGui::Spacing();
    dim_text("Your ROM files are only read. They are never copied, changed or uploaded.");
    ImGui::End();
}

// One key-binding button; returns true when clicked (start capture).
void binding_button(ui_state_t* st, int action, int slot, int keycode) {
    const bool capturing = st->capture_action == action && st->capture_slot == slot;
    const char* name = input_key_name(keycode);
    char label[64];
    snprintf(label, sizeof(label), "%s##b%d_%d", capturing ? "press a key..." : (name ? name : "-"), action, slot);
    if (ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 8.0f, 0))) {
        st->capture_action = action;
        st->capture_slot = slot;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && action < INPUT_ACTION_COUNT) {
        st->settings->input.keys[action][slot] = 0;
        st->act_input_changed = true;
    }
}

void tab_rom(ui_state_t* st) {
    if (st->cart_loaded) {
        ImGui::Text("Cartridge: %s", st->rom_name[0] ? st->rom_name : "(unnamed)");
        ImGui::TextWrapped("File: %s", st->rom_path[0] ? st->rom_path : "(command line)");
        ImGui::Text("Type: %s", st->rom_mode ? st->rom_mode : "?");
        ImGui::Text("SHA-1: %s", st->rom_sha1);
    } else {
        ImGui::TextUnformatted("No cartridge (BASIC).");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, st->rom_status == ROM_STATUS_UNKNOWN ? kErrorColor : kDimColor);
    ImGui::TextWrapped("%s", rom_status_text(st->rom_status));
    ImGui::PopStyleColor();
    ImGui::Text("System ROMs: %s", st->system_roms);
    ImGui::Spacing();
    if (ImGui::Button("Select ROM...")) st->act_select_rom = true;
    ImGui::Spacing();
    draw_message(st);
}

void tab_video(ui_state_t* st) {
    video_settings_t& v = st->settings->video;
    bool ch = false;
    ch |= ImGui::SliderInt("Window scale", &v.window_scale, 1, 8);
    if (v.window_scale != st->startup_window_scale) {
        dim_text("The new window size applies on the next launch.");
    }
    bool fs = v.fullscreen;
    if (ImGui::Checkbox("Fullscreen (F11)", &fs)) { v.fullscreen = fs; ch = true; }
    ch |= ImGui::Checkbox("Integer scaling", &v.integer_scaling);
    ch |= ImGui::Checkbox("Aspect correction (C64 pixels aren't square)", &v.aspect_correction);
    ch |= ImGui::Checkbox("Crop border", &v.crop_border);
    int filter = (int)v.filter;
    const char* filters[] = { "Nearest (sharp)", "Smooth" };
    if (ImGui::Combo("Filter", &filter, filters, 2)) { v.filter = (video_filter_t)filter; ch = true; }
    ImGui::BeginDisabled();
    int pal = 0;
    const char* palettes[] = { "Colodore" };
    ImGui::Combo("Palette", &pal, palettes, 1);
    ImGui::EndDisabled();
    dim_text("More palettes are planned.");
    if (ch) st->act_video_changed = true;
}

void tab_audio(ui_state_t* st) {
    audio_settings_t& a = st->settings->audio;
    bool ch = false;
    float vol = a.volume * 100.0f;
    if (ImGui::SliderFloat("Volume", &vol, 0.0f, 100.0f, "%.0f%%")) { a.volume = vol / 100.0f; ch = true; }
    ch |= ImGui::Checkbox("Mute", &a.mute);
    // Latency reopens the audio device, so only apply when the slider is released.
    ImGui::SliderInt("Latency", &a.latency_ms, 20, 250, "%d ms");
    if (ImGui::IsItemDeactivatedAfterEdit()) ch = true;
    ImGui::BeginDisabled();
    int sid = 0;
    const char* sids[] = { "6581" };
    ImGui::Combo("SID model", &sid, sids, 1);
    ImGui::EndDisabled();
    dim_text("The emulator core models the 6581 SID only for now.");
    if (ch) st->act_audio_changed = true;
}

void tab_input(ui_state_t* st) {
    input_settings_t& in = st->settings->input;
    ImGui::TextUnformatted("Joystick port:");
    ImGui::SameLine();
    if (ImGui::RadioButton("1", in.joystick_port == 1)) { in.joystick_port = 1; st->act_input_changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("2", in.joystick_port == 2)) { in.joystick_port = 2; st->act_input_changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Swap")) { in.joystick_port = in.joystick_port == 1 ? 2 : 1; st->act_input_changed = true; }
    dim_text("Most games use port 2. Click a key to rebind it, right-click to clear it.");
    ImGui::Spacing();
    static const char* names[INPUT_ACTION_COUNT] = { "Up", "Down", "Left", "Right", "Fire" };
    if (ImGui::BeginTable("binds", 3, ImGuiTableFlags_SizingFixedFit)) {
        for (int a = 0; a < INPUT_ACTION_COUNT; a++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(names[a]);
            for (int s = 0; s < INPUT_MAX_BINDS; s++) {
                ImGui::TableNextColumn();
                binding_button(st, a, s, in.keys[a][s]);
            }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Menu");
        ImGui::TableNextColumn();
        binding_button(st, UI_CAPTURE_MENU_KEY, 0, in.menu_key);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    dim_text("Other keys type on the C64 keyboard. Gamepad support is planned.");
}

void menu_screen(ui_state_t* st) {
    center_next_window(0.9f, 0.85f);
    char title[160];
    snprintf(title, sizeof(title), "%s##menu", st->game_title);
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    const bool in_game = st->menu_return == UI_SCREEN_NONE;
    if (ImGui::Button(in_game ? "Resume" : "Back")) st->act_resume = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!in_game);
    if (ImGui::Button("Reset")) st->act_reset = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Quit")) st->act_quit = true;
    ImGui::SameLine();
    const char* key = input_key_name(st->settings->input.menu_key);
    ImGui::TextColored(kDimColor, "  %s: %s", key ? key : "menu key", in_game ? "back to the game" : "back");
    ImGui::Separator();
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("ROM"))   { tab_rom(st);   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Video")) { tab_video(st); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Audio")) { tab_audio(st); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Input")) { tab_input(st); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() * 1.5f);
    ImGui::TextColored(kDimColor, "%s %s", "Breadbin-6502", st->version);
    ImGui::End();
}

void update_scale() {
    const float h = sapp_heightf(), w = sapp_widthf();
    float s = std::min(h / 540.0f, w / 720.0f);
    s = std::max(1.0f, std::min(s, 3.0f));
    s = std::floor(s * 4.0f) / 4.0f;    // quarter steps avoid re-layout jitter while resizing
    if (s != g.scale) {
        g.scale = s;
        ImGuiStyle& style = ImGui::GetStyle();
        style = g.base_style;
        style.ScaleAllSizes(s);
        style.FontScaleMain = s;
    }
}

// Key capture for rebinding: assign the pressed key, keeping bindings unique.
bool handle_capture(const sapp_event* ev, ui_state_t* st) {
    if (st->capture_action == UI_CAPTURE_NONE) return false;
    if (ev->type == SAPP_EVENTTYPE_CHAR || ev->type == SAPP_EVENTTYPE_KEY_UP) return true;
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN) {
        st->capture_action = UI_CAPTURE_NONE;   // clicking elsewhere cancels
        return false;
    }
    if (ev->type != SAPP_EVENTTYPE_KEY_DOWN || ev->key_repeat) return true;
    input_settings_t& in = st->settings->input;
    const int code = (int)ev->key_code;
    const char* name = input_key_name(code);
    if (st->capture_action == UI_CAPTURE_MENU_KEY) {
        if (name) {
            // the menu key can't also be a joystick key
            for (auto& action : in.keys) for (int& k : action) if (k == code) k = 0;
            in.menu_key = code;
            st->act_input_changed = true;
        }
    } else if (code == SAPP_KEYCODE_ESCAPE || code == in.menu_key) {
        // cancel (the menu key stays reserved for the menu)
    } else if (name && code != SAPP_KEYCODE_F11) {
        for (auto& action : in.keys) for (int& k : action) if (k == code) k = 0;
        in.keys[st->capture_action][st->capture_slot] = code;
        st->act_input_changed = true;
    }
    st->capture_action = UI_CAPTURE_NONE;
    return true;
}

} // namespace

extern "C" {

void ui_init(void) {
    simgui_desc_t desc = {};
    desc.logger.func = slog_func;
    desc.no_default_font = true;
    desc.ini_filename = nullptr;
    simgui_setup(&desc);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontDefaultVector();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(14, 12);
    style.ItemSpacing = ImVec2(10, 8);
    style.FontSizeBase = 16.0f;
    g.base_style = style;
}

void ui_shutdown(void) {
    simgui_shutdown();
}

bool ui_handle_event(const sapp_event* ev, ui_state_t* st) {
    if (st->screen == UI_SCREEN_NONE) {
        return false;
    }
    if (handle_capture(ev, st)) {
        return true;
    }
    simgui_handle_event(ev);
    return true;
}

void ui_open_file_browser(const char* start_dir) {
    if (start_dir && platform_is_dir(start_dir)) {
        snprintf(g.browser_dir, sizeof(g.browser_dir), "%s", start_dir);
    } else if (!platform_home_dir(g.browser_dir, sizeof(g.browser_dir))) {
        snprintf(g.browser_dir, sizeof(g.browser_dir), "%c", platform_path_sep());
    }
    g.browser_open = true;
    g.browser_refresh = true;
}

void ui_frame(ui_state_t* st) {
    simgui_frame_desc_t fd = {};
    fd.width = sapp_width();
    fd.height = sapp_height();
    fd.delta_time = sapp_frame_duration();
    fd.dpi_scale = sapp_dpi_scale();
    simgui_new_frame(&fd);
    g.frame_begun = true;
    update_scale();
    if (st->screen == UI_SCREEN_SELECT_ROM) {
        select_rom_screen(st);
    } else if (st->screen == UI_SCREEN_MENU) {
        menu_screen(st);
    }
    if (st->screen != UI_SCREEN_NONE) {
        file_browser_window(st);
    } else {
        g.browser_open = false;
    }
}

void ui_flush(void) {
    if (g.frame_begun) simgui_flush();
}

void ui_draw(void) {
    if (g.frame_begun) simgui_draw();
    g.frame_begun = false;
}

} // extern "C"
