cmake_minimum_required(VERSION 3.20)
project(breadbin-6502 VERSION 0.4.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)          # UI layer only
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Windows: static C/C++ runtime, so release zips need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(BB_BUILD_TESTS "Build unit / smoke tests" ON)
# Branded builds (game repos): build a game pack into the executable and rename it.
set(BB_GAMEPACK_DIR "" CACHE PATH "Game pack folder (game.toml, icon.png, banner.png) to embed; empty = none")
set(BB_OUTPUT_NAME "breadbin-6502" CACHE STRING "File name of the executable")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/embed.cmake)

# Warnings apply to our code only; vendored headers come in as SYSTEM includes.
if(MSVC)
    set(BB_WARNINGS /W4 /wd4100)
else()
    set(BB_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- vendored headers ------------------------------------------------------
add_library(bb_external_chips INTERFACE)
target_include_directories(bb_external_chips SYSTEM INTERFACE external/chips)
add_library(bb_external_sokol INTERFACE)
target_include_directories(bb_external_sokol SYSTEM INTERFACE external/sokol external/sokol/util)

# tomlc17 (MIT): compiled with its own (relaxed) flags
add_library(bb_external_toml STATIC external/tomlc17/tomlc17.c)
target_include_directories(bb_external_toml SYSTEM PUBLIC external/tomlc17)
set_target_properties(bb_external_toml PROPERTIES C_EXTENSIONS ON)

# Lua 5.4 (MIT) for script mods: one unit (src/mods/lua_impl.c) so its config can
# be set without editing external/lua; only the sandbox's libraries are compiled.
add_library(bb_external_lua STATIC src/mods/lua_impl.c)
target_include_directories(bb_external_lua SYSTEM PUBLIC external/lua)
set_target_properties(bb_external_lua PROPERTIES C_EXTENSIONS ON)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # false positive in lauxlib.c at -O2/-O3 (vendored, unmodified)
    target_compile_options(bb_external_lua PRIVATE -Wno-stringop-overflow)
endif()

# stb_image (public domain / MIT), PNG decoding for game pack branding
add_library(bb_external_stb INTERFACE)
target_include_directories(bb_external_stb SYSTEM INTERFACE external/stb)

# Dear ImGui (MIT), UI only
add_library(bb_external_imgui STATIC
    external/imgui/imgui.cpp external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp external/imgui/imgui_widgets.cpp)
target_include_directories(bb_external_imgui SYSTEM PUBLIC external/imgui)

# nativefiledialog-extended (zlib). Linux/BSD: xdg-desktop-portal over D-Bus (no GTK).
set(BB_HAVE_NFD OFF)
if(NOT EMSCRIPTEN)
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
    set(NFD_WAYLAND OFF CACHE BOOL "" FORCE)    # sokol_app uses X11 (XWayland on Wayland)
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/nfd EXCLUDE_FROM_ALL)
    set(BB_HAVE_NFD ON)
endif()

# ---- embedded default system ROMs (MEGA65 Open ROMs) -----------------------
set(BB_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BB_GEN_DIR})
bb_embed(${BB_GEN_DIR}/openroms_kernal.c  bb_openroms_kernal  ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/kernal_generic.rom)
bb_embed(${BB_GEN_DIR}/openroms_basic.c   bb_openroms_basic   ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/basic_generic.rom)
bb_embed(${BB_GEN_DIR}/openroms_chargen.c bb_openroms_chargen ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/chargen_openroms.rom)

# ---- embedded game pack (branded builds) -----------------------------------
set(BB_GAMEPACK_TOML "")
set(BB_GAMEPACK_ICON "")
set(BB_GAMEPACK_BANNER "")
if(BB_GAMEPACK_DIR)
    get_filename_component(BB_GAMEPACK_ABS "${BB_GAMEPACK_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    if(NOT EXISTS "${BB_GAMEPACK_ABS}/game.toml")
        message(FATAL_ERROR "BB_GAMEPACK_DIR: no game.toml in ${BB_GAMEPACK_ABS}")
    endif()
    message(STATUS "Embedding game pack: ${BB_GAMEPACK_ABS}")
    set(BB_GAMEPACK_TOML "${BB_GAMEPACK_ABS}/game.toml")
    set(BB_GAMEPACK_ICON "${BB_GAMEPACK_ABS}/icon.png")
    set(BB_GAMEPACK_BANNER "${BB_GAMEPACK_ABS}/banner.png")
endif()
bb_embed_optional(${BB_GEN_DIR}/gamepack_toml.c   bb_gamepack_toml   "${BB_GAMEPACK_TOML}")
bb_embed_optional(${BB_GEN_DIR}/gamepack_icon.c   bb_gamepack_icon   "${BB_GAMEPACK_ICON}")
bb_embed_optional(${BB_GEN_DIR}/gamepack_banner.c bb_gamepack_banner "${BB_GAMEPACK_BANNER}")

# ---- core: headless emulator + system ROMs (no window/audio deps) ----------
add_library(bb_core STATIC
    src/core/core_chips.c
    src/core/cart.c
    src/media/media.c
    src/media/d64.c
    src/media/t64.c
    src/media/tap.c
    src/media/autostart.c
    src/rom/sysroms.c
    src/rom/sha1.c
    src/rom/rom_file.c
    src/platform/paths.c
    src/platform/gamepad.c
    src/platform/gamepad_evdev.c
    src/platform/gamepad_xinput.c
    src/settings/settings.c
    src/settings/toml_util.c
    src/gamepack/gamepack.c
    src/gamepack/gamepack_load.c
    src/input/keynames.c
    src/util/crc32.c
    src/video/palettes.c
    src/video/view.c
    src/util/stb_impl.c
    src/mods/zip.c
    src/mods/mod.c
    src/mods/patch.c
    src/mods/modset.c
    src/replay/replay.c
    src/state/savestate.c
    src/mods/script.c
    src/mods/plugin.c
    ${BB_GEN_DIR}/openroms_kernal.c
    ${BB_GEN_DIR}/openroms_basic.c
    ${BB_GEN_DIR}/openroms_chargen.c
)
target_include_directories(bb_core PUBLIC src)
target_link_libraries(bb_core PRIVATE bb_external_chips bb_external_sokol bb_external_stb bb_external_lua)
target_link_libraries(bb_core PUBLIC bb_external_toml)
target_compile_options(bb_core PRIVATE ${BB_WARNINGS})
if(NOT MSVC)
    target_link_libraries(bb_core PUBLIC m)
endif()
if(APPLE)
    # macOS gamepads (Objective-C; checked only by the macOS CI build)
    target_sources(bb_core PRIVATE src/platform/gamepad_gc.m)
    target_link_libraries(bb_core PUBLIC "-framework GameController" "-framework Foundation")
endif()

# ---- application -----------------------------------------------------------
set(BB_SOKOL_IMPL src/platform/sokol_impl.c)
if(APPLE)
    set_source_files_properties(${BB_SOKOL_IMPL} PROPERTIES LANGUAGE OBJC)
endif()

add_executable(breadbin-6502 WIN32
    src/main.c
    src/video/video.c
    src/video/crt.c
    src/audio/audio.c
    src/input/input.c
    src/ui/ui.cpp
    src/platform/filedialog.c
    src/video/image.c
    ${BB_SOKOL_IMPL}
    ${BB_GEN_DIR}/gamepack_toml.c
    ${BB_GEN_DIR}/gamepack_icon.c
    ${BB_GEN_DIR}/gamepack_banner.c
)
set_target_properties(breadbin-6502 PROPERTIES OUTPUT_NAME "${BB_OUTPUT_NAME}")
target_link_libraries(breadbin-6502 PRIVATE bb_core bb_external_sokol bb_external_imgui bb_external_stb)
if(BB_HAVE_NFD)
    target_link_libraries(breadbin-6502 PRIVATE nfd)
    target_compile_definitions(breadbin-6502 PRIVATE BB_HAVE_NFD)
endif()
target_compile_options(breadbin-6502 PRIVATE ${BB_WARNINGS})

# Trusted plugins (docs/plugins.md): native code a game repo builds into its
# executable. A file name.c defines `const bb_plugin_t bb_plugin_name`; the
# generated plugins_registry.c registers them at startup. Paths are relative to
# the top-level project.
set(BB_PLUGIN_SOURCES "" CACHE STRING "Trusted plugin sources to build in (name.c defines bb_plugin_name)")
set(BB_PLUGIN_DECLS "")
set(BB_PLUGIN_LIST "")
foreach(_bb_src IN LISTS BB_PLUGIN_SOURCES)
    get_filename_component(_bb_abs "${_bb_src}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
    get_filename_component(_bb_name "${_bb_abs}" NAME_WE)
    if(NOT _bb_name MATCHES "^[a-z0-9_]+$")
        message(FATAL_ERROR "BB_PLUGIN_SOURCES: ${_bb_src}: plugin file names may only use a-z 0-9 _")
    endif()
    if(NOT EXISTS "${_bb_abs}")
        message(FATAL_ERROR "BB_PLUGIN_SOURCES: ${_bb_abs} doesn't exist")
    endif()
    target_sources(breadbin-6502 PRIVATE "${_bb_abs}")
    string(APPEND BB_PLUGIN_DECLS "extern const bb_plugin_t bb_plugin_${_bb_name};\n")
    string(APPEND BB_PLUGIN_LIST "&bb_plugin_${_bb_name}, ")
    message(STATUS "Built-in plugin: ${_bb_name}")
endforeach()
configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake/plugins_registry.c.in ${BB_GEN_DIR}/plugins_registry.c @ONLY)
target_sources(breadbin-6502 PRIVATE ${BB_GEN_DIR}/plugins_registry.c)
target_compile_definitions(breadbin-6502 PRIVATE BB_VERSION="${PROJECT_VERSION}")

if(EMSCRIPTEN)
    # Optional future web build (Phase 9): keep this branch compiling-in-principle.
    target_compile_definitions(breadbin-6502 PRIVATE SOKOL_GLES3)
elseif(APPLE)
    target_compile_definitions(breadbin-6502 PRIVATE SOKOL_METAL)
    target_link_libraries(breadbin-6502 PRIVATE
        "-framework Cocoa" "-framework QuartzCore" "-framework Metal"
        "-framework MetalKit" "-framework AudioToolbox")
elseif(WIN32)
    target_compile_definitions(breadbin-6502 PRIVATE SOKOL_GLCORE)
    if(MINGW)
        target_link_libraries(breadbin-6502 PRIVATE gdi32 ole32 opengl32)
    endif()
else()
    # Linux / FreeBSD (X11 + GL; XWayland on Wayland desktops)
    target_compile_definitions(breadbin-6502 PRIVATE SOKOL_GLCORE)
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(breadbin-6502 PRIVATE
        X11::X11 X11::Xi X11::Xcursor OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
    # sokol_audio's default Linux/FreeBSD backend (works on PipeWire too)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple libpulse)
    target_link_libraries(breadbin-6502 PRIVATE PkgConfig::PULSE)
endif()

# ---- install rules + release packages (zip / tar.gz, AppImage script) -------
include(${CMAKE_CURRENT_LIST_DIR}/cmake/packaging.cmake)

# ---- tests -----------------------------------------------------------------
if(BB_BUILD_TESTS)
    enable_testing()
    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE bb_core)
    target_compile_options(test_core PRIVATE ${BB_WARNINGS})
    add_test(NAME core COMMAND test_core)

    add_executable(test_cart tests/test_cart.c)
    target_link_libraries(test_cart PRIVATE bb_core)
    target_compile_options(test_cart PRIVATE ${BB_WARNINGS})
    add_test(NAME cart COMMAND test_cart)

    add_executable(test_cart_boot tests/test_cart_boot.c)
    target_link_libraries(test_cart_boot PRIVATE bb_core)
    target_compile_options(test_cart_boot PRIVATE ${BB_WARNINGS})
    add_test(NAME cart_boot COMMAND test_cart_boot)

    add_executable(test_settings tests/test_settings.c)
    target_link_libraries(test_settings PRIVATE bb_core)
    target_compile_options(test_settings PRIVATE ${BB_WARNINGS})
    add_test(NAME settings COMMAND test_settings)

    add_executable(test_gamepack tests/test_gamepack.c)
    target_link_libraries(test_gamepack PRIVATE bb_core)
    target_compile_options(test_gamepack PRIVATE ${BB_WARNINGS})
    add_test(NAME gamepack COMMAND test_gamepack ${CMAKE_CURRENT_SOURCE_DIR}/examples/gamepack/game.toml)

    add_executable(test_sha1 tests/test_sha1.c)
    target_link_libraries(test_sha1 PRIVATE bb_core)
    target_compile_options(test_sha1 PRIVATE ${BB_WARNINGS})
    add_test(NAME sha1 COMMAND test_sha1)

    add_executable(test_mods tests/test_mods.c)
    target_link_libraries(test_mods PRIVATE bb_core)
    target_compile_options(test_mods PRIVATE ${BB_WARNINGS})
    add_test(NAME mods COMMAND test_mods)

    add_executable(test_replay tests/test_replay.c)
    target_link_libraries(test_replay PRIVATE bb_core)
    target_compile_options(test_replay PRIVATE ${BB_WARNINGS})
    add_test(NAME replay COMMAND test_replay WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(test_gamepad tests/test_gamepad.c src/input/input.c)
    target_link_libraries(test_gamepad PRIVATE bb_core bb_external_sokol)
    target_compile_options(test_gamepad PRIVATE ${BB_WARNINGS})
    add_test(NAME gamepad COMMAND test_gamepad)

    add_executable(test_state tests/test_state.c)
    target_link_libraries(test_state PRIVATE bb_core)
    target_compile_options(test_state PRIVATE ${BB_WARNINGS})
    add_test(NAME state COMMAND test_state WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_executable(test_script tests/test_script.c)
    target_link_libraries(test_script PRIVATE bb_core)
    target_compile_options(test_script PRIVATE ${BB_WARNINGS})
    add_test(NAME script COMMAND test_script)

    add_executable(test_plugins tests/test_plugins.c examples/plugins/autofire.c)
    target_link_libraries(test_plugins PRIVATE bb_core)
    target_compile_options(test_plugins PRIVATE ${BB_WARNINGS})
    add_test(NAME plugins COMMAND test_plugins)

    add_executable(test_media tests/test_media.c)
    target_link_libraries(test_media PRIVATE bb_core)
    target_compile_options(test_media PRIVATE ${BB_WARNINGS})
    add_test(NAME media COMMAND test_media)
endif()
