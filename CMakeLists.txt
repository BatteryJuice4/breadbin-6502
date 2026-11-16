cmake_minimum_required(VERSION 3.20)
project(breadbin-6502 VERSION 0.1.0 LANGUAGES C)

if(APPLE)
    enable_language(OBJC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(BB_BUILD_TESTS "Build unit / smoke tests" ON)

include(cmake/embed.cmake)

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

# ---- embedded default system ROMs (MEGA65 Open ROMs) -----------------------
set(BB_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${BB_GEN_DIR})
bb_embed(${BB_GEN_DIR}/openroms_kernal.c  bb_openroms_kernal  ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/kernal_generic.rom)
bb_embed(${BB_GEN_DIR}/openroms_basic.c   bb_openroms_basic   ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/basic_generic.rom)
bb_embed(${BB_GEN_DIR}/openroms_chargen.c bb_openroms_chargen ${CMAKE_CURRENT_SOURCE_DIR}/external/open-roms/chargen_openroms.rom)

# ---- core: headless emulator + system ROMs (no window/audio deps) ----------
add_library(bb_core STATIC
    src/core/core_chips.c
    src/core/cart.c
    src/rom/sysroms.c
    src/rom/sha1.c
    src/rom/rom_file.c
    src/platform/paths.c
    ${BB_GEN_DIR}/openroms_kernal.c
    ${BB_GEN_DIR}/openroms_basic.c
    ${BB_GEN_DIR}/openroms_chargen.c
)
target_include_directories(bb_core PUBLIC src)
target_link_libraries(bb_core PRIVATE bb_external_chips)
target_compile_options(bb_core PRIVATE ${BB_WARNINGS})
if(NOT MSVC)
    target_link_libraries(bb_core PUBLIC m)
endif()

# ---- application -----------------------------------------------------------
set(BB_SOKOL_IMPL src/platform/sokol_impl.c)
if(APPLE)
    set_source_files_properties(${BB_SOKOL_IMPL} PROPERTIES LANGUAGE OBJC)
endif()

add_executable(breadbin-6502 WIN32
    src/main.c
    src/video/video.c
    src/audio/audio.c
    src/input/input.c
    ${BB_SOKOL_IMPL}
)
target_link_libraries(breadbin-6502 PRIVATE bb_core bb_external_sokol)
target_compile_options(breadbin-6502 PRIVATE ${BB_WARNINGS})
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

    add_executable(test_sha1 tests/test_sha1.c)
    target_link_libraries(test_sha1 PRIVATE bb_core)
    target_compile_options(test_sha1 PRIVATE ${BB_WARNINGS})
    add_test(NAME sha1 COMMAND test_sha1)
endif()
