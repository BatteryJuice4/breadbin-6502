# Release packaging: install rules + CPack. Included from the main CMakeLists
# after the executable target exists. Game repos get packages named after their
# executable (BB_OUTPUT_NAME) and title automatically.
#
#   cmake --build build --config Release
#   cpack --config build/CPackConfig.cmake -C Release      # zip (Windows, macOS) / tar.gz (Linux)
#   packaging/linux/make-appimage.sh build                  # Linux AppImage (needs appimagetool)

set(BB_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Version in package file names")
set(BB_SOURCE_URL "" CACHE STRING "Where the corresponding source code is published (AGPL-3.0 section 6); written to NOTICE.txt")

# Title for the desktop entry / bundle: the embedded game pack's, else the launcher's.
set(BB_PACKAGE_TITLE "Breadbin-6502")
if(BB_GAMEPACK_TOML)
    file(READ "${BB_GAMEPACK_TOML}" _bb_toml)
    string(REGEX MATCH "\ntitle[ \t]*=[ \t]*\"([^\"]+)\"" _bb_m "\n${_bb_toml}")
    if(CMAKE_MATCH_1)
        set(BB_PACKAGE_TITLE "${CMAKE_MATCH_1}")
    endif()
endif()
set(BB_PACKAGE_ICON "${CMAKE_CURRENT_LIST_DIR}/../assets/breadbin-6502.png")
if(BB_GAMEPACK_ICON AND EXISTS "${BB_GAMEPACK_ICON}")
    set(BB_PACKAGE_ICON "${BB_GAMEPACK_ICON}")
endif()
get_filename_component(BB_PACKAGE_ICON "${BB_PACKAGE_ICON}" ABSOLUTE)

if(BB_SOURCE_URL)
    set(BB_SOURCE_TEXT "The complete source code of this program is available at:\n  ${BB_SOURCE_URL}")
else()
    set(BB_SOURCE_TEXT "The complete source code of this program is available from whoever gave you\nthis copy (AGPL-3.0 section 6), and from the Breadbin-6502 project.")
endif()
set(BB_PACKAGE_NAME "${BB_OUTPUT_NAME}")
configure_file(${CMAKE_CURRENT_LIST_DIR}/../packaging/NOTICE.txt.in ${CMAKE_BINARY_DIR}/packaging/NOTICE.txt @ONLY)

# Where things go
if(WIN32)
    set(_bb_bin ".")
    set(_bb_doc ".")
elseif(APPLE)
    set(_bb_bin ".")            # the .app bundle at the archive root
    set(_bb_doc ".")
else()
    include(GNUInstallDirs)
    set(_bb_bin "${CMAKE_INSTALL_BINDIR}")
    set(_bb_doc "${CMAKE_INSTALL_DOCDIR}")
endif()

if(APPLE)
    set_target_properties(breadbin-6502 PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "${BB_PACKAGE_TITLE}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.breadbin6502.${BB_OUTPUT_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BB_PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${BB_PACKAGE_VERSION}")
    # Bundle icon: .icns made from the PNG with the system's sips + iconutil.
    find_program(BB_SIPS sips)
    find_program(BB_ICONUTIL iconutil)
    if(BB_SIPS AND BB_ICONUTIL)
        set(_bb_iconset "${CMAKE_BINARY_DIR}/packaging/app.iconset")
        set(_bb_icns "${CMAKE_BINARY_DIR}/packaging/app.icns")
        set(_bb_cmds "")
        foreach(_s 16 32 128 256)
            math(EXPR _s2 "${_s} * 2")
            list(APPEND _bb_cmds
                COMMAND ${BB_SIPS} -z ${_s} ${_s} "${BB_PACKAGE_ICON}" --out "${_bb_iconset}/icon_${_s}x${_s}.png"
                COMMAND ${BB_SIPS} -z ${_s2} ${_s2} "${BB_PACKAGE_ICON}" --out "${_bb_iconset}/icon_${_s}x${_s}@2x.png")
        endforeach()
        add_custom_command(OUTPUT "${_bb_icns}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_bb_iconset}"
            ${_bb_cmds}
            COMMAND ${BB_ICONUTIL} -c icns "${_bb_iconset}" -o "${_bb_icns}"
            DEPENDS "${BB_PACKAGE_ICON}" VERBATIM)
        set_source_files_properties("${_bb_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(breadbin-6502 PRIVATE "${_bb_icns}")
        set_target_properties(breadbin-6502 PROPERTIES MACOSX_BUNDLE_ICON_FILE app.icns)
    endif()
endif()

install(TARGETS breadbin-6502
    RUNTIME DESTINATION ${_bb_bin}
    BUNDLE DESTINATION ${_bb_bin})

set(_bb_root "${CMAKE_CURRENT_LIST_DIR}/..")
install(FILES ${_bb_root}/LICENSE ${_bb_root}/README.md ${CMAKE_BINARY_DIR}/packaging/NOTICE.txt
    DESTINATION ${_bb_doc})
install(FILES ${_bb_root}/docs/gamepack.md ${_bb_root}/docs/mods.md DESTINATION ${_bb_doc}/docs)
# Third-party licenses (everything vendored that ends up in the executable)
install(FILES ${_bb_root}/external/chips/LICENSE DESTINATION ${_bb_doc}/licenses RENAME chips.txt)
install(FILES ${_bb_root}/external/sokol/LICENSE DESTINATION ${_bb_doc}/licenses RENAME sokol.txt)
install(FILES ${_bb_root}/external/imgui/LICENSE.txt DESTINATION ${_bb_doc}/licenses RENAME imgui.txt)
install(FILES ${_bb_root}/external/tomlc17/LICENSE DESTINATION ${_bb_doc}/licenses RENAME tomlc17.txt)
install(FILES ${_bb_root}/external/stb/LICENSE DESTINATION ${_bb_doc}/licenses RENAME stb.txt)
install(FILES ${_bb_root}/external/nfd/LICENSE DESTINATION ${_bb_doc}/licenses RENAME nativefiledialog-extended.txt)
install(FILES ${_bb_root}/external/open-roms/LICENSE DESTINATION ${_bb_doc}/licenses RENAME open-roms.txt)
install(FILES ${_bb_root}/external/open-roms/COPYING DESTINATION ${_bb_doc}/licenses RENAME open-roms-GPL-3.0.txt)
install(FILES ${_bb_root}/external/open-roms/COPYING.LESSER DESTINATION ${_bb_doc}/licenses RENAME open-roms-LGPL-3.0.txt)

# Linux desktop integration (also what the AppImage script uses)
if(UNIX AND NOT APPLE)
    configure_file(${_bb_root}/packaging/linux/app.desktop.in
                   ${CMAKE_BINARY_DIR}/packaging/${BB_OUTPUT_NAME}.desktop @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/packaging/${BB_OUTPUT_NAME}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${BB_PACKAGE_ICON} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME ${BB_OUTPUT_NAME}.png)
endif()

# CPack
if(WIN32)
    set(_bb_os windows)
elseif(APPLE)
    set(_bb_os macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_bb_os linux)
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _bb_os)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _bb_arch)
if(_bb_arch STREQUAL "amd64")
    set(_bb_arch x86_64)
endif()
set(CPACK_PACKAGE_NAME "${BB_OUTPUT_NAME}")
set(CPACK_PACKAGE_VERSION "${BB_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VENDOR "Breadbin-6502")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${BB_PACKAGE_TITLE}: Commodore 64 games as desktop games (bring your own ROM)")
set(CPACK_PACKAGE_FILE_NAME "${BB_OUTPUT_NAME}-${BB_PACKAGE_VERSION}-${_bb_os}-${_bb_arch}")
set(CPACK_RESOURCE_FILE_LICENSE "${_bb_root}/LICENSE")
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR TGZ)
else()
    set(CPACK_GENERATOR ZIP)
endif()
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
include(CPack)
