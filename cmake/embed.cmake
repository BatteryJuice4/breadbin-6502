# bb_embed(<out_c_file> <symbol> <input_file>)
# Generates a C source defining:
#   const unsigned char <symbol>[];
#   const unsigned long <symbol>_size;
# Runs at configure time; the input is registered as a configure dependency so
# edits re-trigger generation.
function(bb_embed out_file symbol in_file)
    file(READ "${in_file}" hex HEX)
    file(SIZE "${in_file}" size)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
    file(WRITE "${out_file}.tmp"
        "/* generated from ${in_file}, do not edit */\n"
        "const unsigned char ${symbol}[${size}] = {\n${bytes}\n};\n"
        "const unsigned long ${symbol}_size = ${size}UL;\n")
    file(COPY_FILE "${out_file}.tmp" "${out_file}" ONLY_IF_DIFFERENT)
    file(REMOVE "${out_file}.tmp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${in_file}")
endfunction()

# bb_embed_optional(<out_c_file> <symbol> <input_file>)
# Like bb_embed, but writes an empty definition (<symbol>_size = 0) when the input
# file doesn't exist. Re-run CMake after adding the file.
function(bb_embed_optional out_file symbol in_file)
    if(in_file AND EXISTS "${in_file}")
        bb_embed("${out_file}" "${symbol}" "${in_file}")
    else()
        file(WRITE "${out_file}.tmp"
            "/* generated: no input file, do not edit */\n"
            "const unsigned char ${symbol}[1] = { 0 };\n"
            "const unsigned long ${symbol}_size = 0UL;\n")
        file(COPY_FILE "${out_file}.tmp" "${out_file}" ONLY_IF_DIFFERENT)
        file(REMOVE "${out_file}.tmp")
    endif()
endfunction()
