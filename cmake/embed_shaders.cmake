# Embed compiled SPIR-V shaders into a generated C++ translation unit.
# Inputs:
#   -DINPUTS="file1;file2;..."
#   -DNAMES="name1;name2;..."   (same length as INPUTS)
#   -DOUTPUT_CPP="/abs/or/rel/path.cpp"

if(NOT DEFINED INPUTS OR NOT DEFINED NAMES OR NOT DEFINED OUTPUT_CPP)
    message(FATAL_ERROR "embed_shaders.cmake: missing INPUTS/NAMES/OUTPUT_CPP")
endif()

set(_inputs ${INPUTS})
set(_names ${NAMES})

list(LENGTH _inputs _inputs_len)
list(LENGTH _names _names_len)
if(NOT _inputs_len EQUAL _names_len)
    message(FATAL_ERROR "embed_shaders.cmake: INPUTS and NAMES must have same length")
endif()

# Build output
set(_out "// Auto-generated. Do not edit.\n")
string(APPEND _out "#include \"renderer/embedded_shaders.h\"\n")
string(APPEND _out "#include <cstddef>\n")
string(APPEND _out "#include <cstring>\n\n")
string(APPEND _out "namespace Atlas::EmbeddedShaders {\n\n")

# Emit byte arrays
math(EXPR _last "${_inputs_len} - 1")
foreach(_i RANGE 0 ${_last})
    list(GET _inputs ${_i} _input)
    list(GET _names  ${_i} _name)

    if(NOT EXISTS "${_input}")
        message(FATAL_ERROR "embed_shaders.cmake: input not found: ${_input}")
    endif()

    # Read shader as HEX and turn into comma-separated 0x.. bytes.
    file(READ "${_input}" _hex HEX)
    string(TOLOWER "${_hex}" _hex)

    string(LENGTH "${_hex}" _hex_len)
    if(_hex_len EQUAL 0)
        message(FATAL_ERROR "embed_shaders.cmake: empty input: ${_input}")
    endif()

    set(_bytes "")
    math(EXPR _nbytes "${_hex_len} / 2")
    math(EXPR _last_byte "${_nbytes} - 1")
    foreach(_b RANGE 0 ${_last_byte})
        math(EXPR _pos "${_b} * 2")
        string(SUBSTRING "${_hex}" ${_pos} 2 _byte)
        if(_b EQUAL 0)
            set(_bytes "0x${_byte}")
        else()
            string(APPEND _bytes ", 0x${_byte}")
        endif()
    endforeach()

    # Sanitize symbol name
    set(_sym "${_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _sym "${_sym}")

    string(APPEND _out "alignas(4) static const unsigned char ${_sym}[] = { ${_bytes} };\n")
endforeach()

# Table + lookup
string(APPEND _out "\nstruct Entry { const char* name; const unsigned char* data; size_t size; };\n")
string(APPEND _out "\nstatic const Entry kEntries[] = {\n")
foreach(_i RANGE 0 ${_last})
    list(GET _names ${_i} _name)
    set(_sym "${_name}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _sym "${_sym}")

    # Add both base name and shaders/<name>
    string(APPEND _out "    { \"${_name}.spv\", ${_sym}, sizeof(${_sym}) },\n")
    string(APPEND _out "    { \"shaders/${_name}.spv\", ${_sym}, sizeof(${_sym}) },\n")
endforeach()
string(APPEND _out "};\n\n")

string(APPEND _out "const ShaderData* find(const char* name) {\n")
string(APPEND _out "    static ShaderData out;\n")
string(APPEND _out "    if (!name) return nullptr;\n")
string(APPEND _out "    for (const auto& e : kEntries) {\n")
string(APPEND _out "        if (std::strcmp(e.name, name) == 0) {\n")
string(APPEND _out "            out.data = e.data;\n")
string(APPEND _out "            out.size = e.size;\n")
string(APPEND _out "            return &out;\n")
string(APPEND _out "        }\n")
string(APPEND _out "    }\n")
string(APPEND _out "    return nullptr;\n")
string(APPEND _out "}\n\n")
string(APPEND _out "}\n")

file(WRITE "${OUTPUT_CPP}" "${_out}")
