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
#include "video/palettes.h"
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
    bool browser_mods = false;      // listing .c64mod packages instead of games
    bool browser_refresh = false;
    char browser_dir[SETTINGS_PATH_MAX] = {};
    std::vector<BrowserEntry> browser_entries;
    int browser_selected = -1;
    std::string browser_error;

    // game pack banner
    sg_image banner_img = {};
    sg_view banner_view = {};
    int banner_w = 0, banner_h = 0;
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

bool has_mod_ext(const std::string& n) {
    if (n.size() < 7) return false;
    std::string e = n.substr(n.size() - 7);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == ".c64mod";
}

bool has_rom_ext(const std::string& n) {
    if (g.browser_mods) return has_mod_ext(n);
    if (n.size() < 4) return false;
    std::string e = n.substr(n.size() - 4);
    for (char& c : e) c = (char)tolower((unsigned char)c);
    return e == ".crt" || e == ".bin" || e == ".d64" || e == ".prg" || e == ".t64" || e == ".tap";
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

std::string rom_status_text(const ui_state_t* st) {
    switch (st->rom_status) {
        case ROM_STATUS_GENERIC:    return "Generic mode (no game pack): any cartridge, disk or tape is accepted.";
        case ROM_STATUS_KNOWN:      return std::string("Verified: a known good dump of ") + st->game_title + ".";
        case ROM_STATUS_UNKNOWN:    return std::string("Not a known dump of ") + st->game_title + ". Mods may not work.";
        case ROM_STATUS_UNVERIFIED: return "Not checked: this game pack doesn't list known dumps yet.";
        default:                    return "Nothing loaded.";
    }
}

// Banner scaled to the available width (and at most max_h pixels high), centered.
bool draw_banner(float max_h) {
    if (g.banner_view.id == SG_INVALID_ID || g.banner_w <= 0) return false;
    const float avail = ImGui::GetContentRegionAvail().x;
    float w = avail, h = avail * (float)g.banner_h / (float)g.banner_w;
    if (h > max_h) { h = max_h; w = h * (float)g.banner_w / (float)g.banner_h; }
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) * 0.5f);
    ImGui::Image((ImTextureID)simgui_imtextureid(g.banner_view), ImVec2(w, h));
    return true;
}

void draw_pack_notes(const ui_state_t* st) {
    if (st->pack_notes[0]) dim_text("%s", st->pack_notes);
}

void confirm_window(ui_state_t* st) {
    if (!st->confirm_open) return;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.75f, 0.0f), ImGuiCond_Always);
    ImGui::OpenPopup("Unknown ROM");
    if (ImGui::BeginPopupModal("Unknown ROM", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", st->confirm_text);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (ImGui::Button("Play anyway")) {
            st->act_confirm_play = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Choose another file")) {
            st->act_confirm_cancel = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void file_browser_window(ui_state_t* st) {
    if (!g.browser_open) return;
    if (g.browser_refresh) {
        browser_list();
        g.browser_refresh = false;
    }
    center_next_window(0.8f, 0.8f);
    const char* title = g.browser_mods ? "Install mod###browser" : "Select ROM file###browser";
    ImGui::OpenPopup("###browser");
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
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
        ImGui::TextColored(kDimColor, g.browser_mods ? "Showing folders and .c64mod files."
                                                     : "Showing folders and .crt .bin .d64 .prg .t64 .tap files.");
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
            if (g.browser_mods) st->act_mod_install_path = true; else st->act_load_path = true;
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
    if (!draw_banner(ImGui::GetIO().DisplaySize.y * 0.3f)) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.0f);
        ImGui::TextUnformatted(st->game_title);
        ImGui::PopFont();
    }
    ImGui::Separator();
    ImGui::Spacing();
    const bool blocked = st->blocked[0] != 0;
    if (blocked) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("%s", st->blocked);
        ImGui::PopStyleColor();
    } else if (st->has_pack) {
        const char* m = st->pack_media ? st->pack_media : "cartridge";
        const char* ext = !strcmp(m, "disk") ? ".d64 or .prg" : !strcmp(m, "tape") ? ".tap or .t64" : ".crt or .bin";
        ImGui::TextWrapped("To play %s, choose your own copy of the %s (%s).", st->game_title, m, ext);
        dim_text("You can also drag the file onto this window.");
    } else {
        ImGui::TextWrapped("Choose a Commodore 64 cartridge (.crt, .bin), disk (.d64, .prg) or tape (.tap, .t64) image to play.");
        dim_text("You can also drag the file onto this window.");
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(blocked);
    if (ImGui::Button("Select ROM...")) st->act_select_rom = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Settings...")) {
        st->menu_return = UI_SCREEN_SELECT_ROM;
        st->screen = UI_SCREEN_MENU;
    }
    if (st->allow_no_cart && !blocked) {
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
    if (st->has_pack) {
        draw_banner(ImGui::GetIO().DisplaySize.y * 0.15f);
        ImGui::Text("Game pack: %s (%s)", st->game_title, st->pack_id);
        dim_text("From: %s", st->pack_source);
        draw_pack_notes(st);
        ImGui::Separator();
    }
    if (st->cart_loaded) {
        ImGui::Text("%s: %s", st->media_kind ? st->media_kind : "Cartridge", st->rom_name[0] ? st->rom_name : "(unnamed)");
        ImGui::TextWrapped("File: %s", st->rom_path[0] ? st->rom_path : "(command line)");
        ImGui::Text("Type: %s", st->rom_mode ? st->rom_mode : "?");
        ImGui::Text("SHA-1: %s", st->rom_sha1);
    } else {
        ImGui::TextUnformatted("Nothing loaded (BASIC).");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, st->rom_status == ROM_STATUS_UNKNOWN ? kErrorColor :
                                         st->rom_status == ROM_STATUS_KNOWN ? kOkColor : kDimColor);
    ImGui::TextWrapped("%s", rom_status_text(st).c_str());
    ImGui::PopStyleColor();
    ImGui::Text("System ROMs: %s", st->system_roms);
    ImGui::Spacing();
    if (ImGui::Button("Select ROM...")) st->act_select_rom = true;
    if (st->can_swap) {
        ImGui::SameLine();
        const bool tape = st->media_kind && !strcmp(st->media_kind, "Tape");
        if (ImGui::Button(tape ? "Swap tape..." : "Swap disk...")) st->act_swap_media = true;
        dim_text(tape ? "Swapping inserts another tape without restarting the game (for games on several tapes)."
                      : "Swapping inserts another disk without restarting the game (for games on several disks or sides).");
    }
    ImGui::Spacing();
    draw_message(st);
}

// Small centered notice while a disk or tape game loads at warp speed.
void loading_overlay(const ui_state_t* st) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::Text("Loading %s...", st->rom_name[0] ? st->rom_name : st->game_title);
    if (st->loading_seconds >= 1.0f) dim_text("%.0f s of %s time", st->loading_seconds,
                                              st->media_kind && !strcmp(st->media_kind, "Tape") ? "tape" : "loading");
    ImGui::End();
}

std::string palette_label(const ui_state_t* st, const char* id) {
    if (const palette_t* p = palette_find_builtin(id)) return p->name;
    if (!strncmp(id, "mod:", 4) && st->mods) {
        const int i = modset_find(st->mods, id + 4);
        if (i >= 0) {
            const mod_entry_t* e = st->mods->entries[i];
            if (e->usable && e->mod.type == MOD_TYPE_PALETTE) return std::string(e->mod.palette_name) + " (mod)";
        }
    }
    return std::string(id) + " (not available)";
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
    // built-in palettes, then enabled palette mods
    std::string current = palette_label(st, v.palette);
    if (ImGui::BeginCombo("Palette", current.c_str())) {
        for (int i = 0; i < palette_builtin_count(); i++) {
            const palette_t* p = palette_builtin(i);
            if (ImGui::Selectable(p->name, !strcmp(v.palette, p->id))) {
                snprintf(v.palette, sizeof(v.palette), "%s", p->id);
                ch = true;
            }
        }
        const modset_t* ms = st->mods;
        for (int i = 0; ms && i < ms->count; i++) {
            const mod_entry_t* e = ms->entries[i];
            if (!e->usable || e->mod.type != MOD_TYPE_PALETTE) continue;
            char id[sizeof(v.palette)];
            snprintf(id, sizeof(id), "mod:%s", e->mod.id);
            std::string label = std::string(e->mod.palette_name) + " (mod)##" + e->mod.id;
            if (ImGui::Selectable(label.c_str(), !strcmp(v.palette, id))) {
                snprintf(v.palette, sizeof(v.palette), "%s", id);
                ch = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!strncmp(v.palette, "mod:", 4) && current.find("(not available)") != std::string::npos) {
        dim_text("That palette's mod isn't enabled, so the default palette is shown.");
    }
    dim_text("Palette mods appear here once enabled in the Mods tab.");
    ImGui::Spacing();
    if (ImGui::Button("Reset video to defaults##video") && st->defaults) {
        const bool fs = v.fullscreen;   // fullscreen is a window state, not a game default
        v = st->defaults->video;
        v.fullscreen = fs;
        ch = true;
    }
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
    ImGui::Spacing();
    if (ImGui::Button("Reset controls to defaults##input") && st->defaults) {
        in = st->defaults->input;
        st->act_input_changed = true;
    }
    if (st->has_pack) {
        ImGui::SameLine();
        dim_text("(the defaults come from the game pack)");
    }
}

// One mod row: checkbox, name, version, type; load-order arrows; details on expand.
void mod_row(ui_state_t* st, int index, int order_pos) {
    modset_t* ms = st->mods;
    mod_entry_t* e = ms->entries[index];
    ImGui::PushID(index);
    bool on = e->enabled;
    const bool can_toggle = e->loaded || e->enabled;   // broken mods can only be switched off
    ImGui::BeginDisabled(!can_toggle);
    if (ImGui::Checkbox("##on", &on)) {
        modset_enable(ms, index, on);
        st->act_mods_changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const char* name = e->loaded ? e->mod.name : e->file_id;
    char head[256];
    if (e->loaded) {
        snprintf(head, sizeof(head), "%s %s  [%s]%s###mod", name, e->mod.version, mod_type_name(e->mod.type),
                 e->source == MOD_SOURCE_BUNDLED ? "  (comes with the game)" : "");
    } else {
        snprintf(head, sizeof(head), "%s###mod", name);
    }
    const bool open = ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_SpanAvailWidth);
    if (order_pos >= 0) {
        // arrows at the right edge
        const float w = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 2.0f * w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginDisabled(order_pos == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { modset_move(ms, order_pos, -1); st->act_mods_changed = true; }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(order_pos == ms->order_count - 1);
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { modset_move(ms, order_pos, 1); st->act_mods_changed = true; }
        ImGui::EndDisabled();
    }
    if (e->status[0]) {
        ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        if (e->enabled && !e->usable) {
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::TextWrapped("%s", e->status);
            ImGui::PopStyleColor();
        } else {
            dim_text("%s", e->status);
        }
        ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
    }
    if (open) {
        if (e->loaded) {
            const mod_t* m = &e->mod;
            if (m->description[0]) ImGui::TextWrapped("%s", m->description);
            if (m->author[0]) dim_text("By %s", m->author);
            dim_text("Id: %s", m->id);
            if (m->dep_count) {
                std::string d;
                for (int i = 0; i < m->dep_count; i++) {
                    d += (i ? ", " : "") + std::string(m->deps[i].id);
                    if (m->deps[i].min_version[0]) d += std::string(" ") + m->deps[i].min_version + "+";
                }
                dim_text("Needs: %s", d.c_str());
            }
            if (m->type == MOD_TYPE_CHEAT) {
                for (int i = 0; i < m->cheat_count; i++) {
                    const mod_cheat_t* c = &m->cheats[i];
                    dim_text("  %s$%04X (%u byte%s, %s)", c->label[0] ? (std::string(c->label) + ": ").c_str() : "",
                             c->address, c->len, c->len == 1 ? "" : "s", c->freeze ? "every frame" : "once");
                }
            }
        }
        dim_text("File: %s", e->path);
        if (e->source == MOD_SOURCE_INSTALLED && !platform_is_dir(e->path)) {
            if (ImGui::SmallButton("Uninstall")) {
                st->mod_index = index;
                st->act_mod_uninstall = true;
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void tab_mods(ui_state_t* st) {
    modset_t* ms = st->mods;
    if (!ms) {
        dim_text("Mods aren't available.");
        return;
    }
    if (ImGui::Button("Install mod...")) st->act_mod_install = true;
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) st->act_mods_rescan = true;
    if (st->mods_message[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, st->mods_message_is_error ? kErrorColor : kOkColor);
        ImGui::TextWrapped("%s", st->mods_message);
        ImGui::PopStyleColor();
    }
    if (st->mods_restart_needed) {
        ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        ImGui::TextWrapped("Patch mods changed. They apply when the game is loaded again.");
        ImGui::PopStyleColor();
        if (ImGui::Button("Restart the game now")) st->act_mods_restart = true;
    }
    if (ms->scan_errors[0]) dim_text("%s", ms->scan_errors);
    ImGui::Spacing();
    if (ms->count == 0) {
        dim_text("No mods installed. Mods come as .c64mod files: use \"Install mod...\" to add one. "
                 "Mods are off until you enable them here.");
        return;
    }
    ImGui::SeparatorText("Enabled (in load order: later mods win)");
    if (ms->order_count == 0) dim_text("None. Tick a mod below to enable it.");
    for (int p = 0; p < ms->order_count; p++) mod_row(st, ms->order[p], p);
    ImGui::SeparatorText("Available");
    int shown = 0;
    for (int i = 0; i < ms->count; i++) {
        if (ms->entries[i]->enabled) continue;
        mod_row(st, i, -1);
        shown++;
    }
    if (!shown) dim_text("All mods are enabled.");
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
    // tab contents scroll above the version footer
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() * 1.6f;
    auto tab = [&](const char* name, void (*fn)(ui_state_t*)) {
        if (ImGui::BeginTabItem(name)) {
            ImGui::BeginChild("##tabbody", ImVec2(0, -footer_h));
            fn(st);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    };
    if (ImGui::BeginTabBar("tabs")) {
        tab("ROM", tab_rom);
        tab("Video", tab_video);
        tab("Audio", tab_audio);
        tab("Input", tab_input);
        tab("Mods", tab_mods);
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
    if (g.banner_view.id != SG_INVALID_ID) sg_destroy_view(g.banner_view);
    if (g.banner_img.id != SG_INVALID_ID) sg_destroy_image(g.banner_img);
    simgui_shutdown();
}

void ui_set_banner(const unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return;
    if (g.banner_view.id != SG_INVALID_ID) sg_destroy_view(g.banner_view);
    if (g.banner_img.id != SG_INVALID_ID) sg_destroy_image(g.banner_img);
    sg_image_desc d = {};
    d.width = width;
    d.height = height;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.ptr = rgba;
    d.data.size = (size_t)width * (size_t)height * 4;
    d.label = "gamepack-banner";
    g.banner_img = sg_make_image(&d);
    sg_view_desc vd = {};
    vd.texture.image = g.banner_img;
    g.banner_view = sg_make_view(&vd);
    g.banner_w = width;
    g.banner_h = height;
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
    g.browser_mods = false;
}

void ui_open_mod_browser(const char* start_dir) {
    ui_open_file_browser(start_dir);
    g.browser_mods = true;
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
        confirm_window(st);
    } else {
        g.browser_open = false;
        if (st->loading) loading_overlay(st);
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
