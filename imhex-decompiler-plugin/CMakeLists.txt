# ImHex Decompiler Plugin
# ========================
# A multi-architecture disassembler/decompiler tool plugin for ImHex v1.38.x
#
# This project is built as a subdirectory of the SDK build (../sdk-build),
# which provides the ImHex libraries (libimhex, fmt, imgui, libwolv, libpl)
# and the Capstone static library (../deps/prefix).

cmake_minimum_required(VERSION 3.20)
project(imhex-decompiler-plugin)

set(IMHEX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../ImHex-1.38.1")
set(CAPSTONE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../deps/prefix")

add_library(decompiler MODULE
    source/plugin_decompiler.cpp
    source/decompiler.cpp
    source/tool_decompiler.cpp
)

target_include_directories(decompiler PRIVATE
    source
    ${IMHEX_SRC}/lib/libimhex/include
    ${CAPSTONE_DIR}/include
)

target_compile_definitions(decompiler PRIVATE
    IMHEX_PROJECT_NAME="imhex-decompiler-plugin"
    IMHEX_PLUGIN_NAME=decompiler
    IMHEX_VERSION="1.38.1"
    IMHEX_PLUGIN_FEATURES_CONTENT=
)

target_link_libraries(decompiler PRIVATE
    libimhex
    fmt::fmt-header-only
    imgui_all_includes
    libwolv
    libpl_includes
    ${CAPSTONE_DIR}/lib/libcapstone.a
)

set_target_properties(decompiler PROPERTIES
    PREFIX ""
    OUTPUT_NAME "decompiler"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)
