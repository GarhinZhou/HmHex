# ImHex OpenHarmony native build (compiled by DevEco Studio / hvigor)
# ===================================================================
# Builds libentry.so (XComponent/NAPI entry + GUI) and libimhex.so plus all
# bundled dependencies from the ImHex source tree. DevEco signs the produced
# libraries with the official toolchain, satisfying the system XPM code
# signature checks.
#
# All source locations are resolved relative to the repository root:
#   ImHex-1.38.1  - ImHex source tree (modified for OHOS)
#   ohos-port     - OHOS port layer (glfw shim, xcomponent entry, stubs)
#   deps          - third party builds (curl, mbedtls, freetype, ...)

cmake_minimum_required(VERSION 3.16)
project(imhex_native C CXX)

# Aggregation target referenced by the ImHex build files
add_custom_target(imhex_all)

# DevEco's BiSheng clang flags --gcc-toolchain as unused; do not elevate
# warnings to errors.
set(IMHEX_STRICT_WARNINGS OFF CACHE BOOL "" FORCE)
add_compile_options(-Wno-unused-command-line-argument)

# OHOS port toggle: consumed by the patched ImHex sources (default_paths.cpp,
# plugin_manager.cpp, window code, ...). Must be visible to every target,
# including libimhex and the plugins, so define it globally.
add_compile_definitions(IMHEX_OHOS_PORT=1)

# The BiSheng toolchain mixes the hms libcxx-ohos with the openharmony sysroot
# C headers, which breaks compilation (unresolved ::size_t etc.). The dev
# machine's harmonybrew SDK (same version 26.0.0.18_1) builds ImHex cleanly
# with one self-consistent header set, so point every header search at it:
#   - --sysroot        -> harmonybrew native sysroot (C headers, link libs)
#   - -nostdinc++      -> drop the BiSheng built-in C++ standard library paths
#   - -isystem libcxx  -> patched libcxx-ohos, exactly as the dev build used
#
# Override with -DHARMONYBREW_SDK=/path/to/ohos-sdk if installed elsewhere.
if (NOT DEFINED HARMONYBREW_SDK)
    set(HARMONYBREW_SDK "$ENV{HOME}/.harmonybrew/Cellar/ohos-sdk/26.0.0.18_1")
endif()
set(HARMONYBREW_SYSROOT "${HARMONYBREW_SDK}/native/sysroot")
if (EXISTS "${HARMONYBREW_SYSROOT}/usr/include/ctype.h")
    set(CMAKE_SYSROOT "${HARMONYBREW_SYSROOT}")
    set(PATCHED_LIBCXX_INCLUDE "${HARMONYBREW_SDK}/native/llvm/include/libcxx-ohos/include/c++/v1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -nostdinc++ -isystem ${PATCHED_LIBCXX_INCLUDE} -isystem ${HARMONYBREW_SYSROOT}/usr/include/aarch64-linux-ohos -isystem ${HARMONYBREW_SYSROOT}/usr/include")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF)

# Paths relative to the repository root (this file lives in imhex-hap/entry/).
set(IMHEX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../ImHex-1.38.1")
set(OHOS_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../ohos-port")
set(DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../deps")

# --- freetype + curl: built from source inside this project (static, PIC),
# so every library shipped in the HAP is produced by DevEco's toolchain and
# signed together with the app. curl uses the in-tree mbedTLS as its TLS
# backend; zlib is deliberately disabled (freetype FT_DISABLE_ZLIB, curl
# CURL_ZLIB=OFF) so no third-party zlib is needed at all.
#
# DevEco's toolchain flags this build as cross-compiling, which makes CMake
# refuse to run configure-time feature tests (try_run). This machine IS the
# target (OHOS PC), so declare non-cross to let curl/freetype run their
# detection programs natively.
set(CMAKE_CROSSCOMPILING FALSE)
set(FREETYPE_INCLUDE_DIRS "${DEPS_DIR}/freetype-2.14.3/include" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/freetype/libfreetype.a" CACHE FILEPATH "" FORCE)
set(FREETYPE_LIBRARY_RELEASE "${FREETYPE_LIBRARY}" CACHE FILEPATH "" FORCE)
set(FREETYPE_FOUND TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(DISABLE_FORCE_DEBUG_POSTFIX ON CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/freetype-2.14.3 ${CMAKE_CURRENT_BINARY_DIR}/deps/freetype)

set(MBEDTLS_INCLUDE_DIR "${DEPS_DIR}/mbedtls-3.6.2/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MBEDTLS_LIBRARY_RELEASE "${MBEDTLS_LIBRARY}" CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)
set(CURL_INCLUDE_DIRS "${DEPS_DIR}/curl-8.21.0/include" CACHE PATH "" FORCE)
set(CURL_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/curl/lib/libcurl.a" CACHE FILEPATH "" FORCE)
set(CURL_LIBRARIES "${CURL_LIBRARY}" CACHE FILEPATH "" FORCE)
set(CURL_FOUND TRUE CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CURL_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/curl-8.21.0 ${CMAKE_CURRENT_BINARY_DIR}/deps/curl)
if (NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl_static)
endif()

# OHOS port toggles consumed by the patched ImHex CMake files.
# Prefer the harmonybrew sysroot so headers AND link libraries stay in one
# consistent set; fall back to the DevEco-provided OHOS_SDK_NATIVE if the
# dev machine paths are absent.
set(IMHEX_OHOS_PORT ON)
set(IMHEX_OHOS_PORT_DIR "${OHOS_PORT_DIR}")
set(IMHEX_GLFW_INCLUDE_DIR "${OHOS_PORT_DIR}/glfw")
if (EXISTS "${HARMONYBREW_SYSROOT}/usr/include")
    set(IMHEX_OHOS_SYSROOT "${HARMONYBREW_SYSROOT}")
    set(IMHEX_GLES_INCLUDE_DIRS "${HARMONYBREW_SYSROOT}/usr/include")
elseif (DEFINED OHOS_SDK_NATIVE)
    set(IMHEX_OHOS_SYSROOT "${OHOS_SDK_NATIVE}/sysroot")
    set(IMHEX_GLES_INCLUDE_DIRS "${IMHEX_OHOS_SYSROOT}/usr/include")
else()
    set(IMHEX_OHOS_SYSROOT "")
    set(IMHEX_GLES_INCLUDE_DIRS "")
endif()

set(EXTERNAL_LIBS_FOLDER "${IMHEX_SRC}/lib/external")
set(THIRD_PARTY_LIBS_FOLDER "${IMHEX_SRC}/lib/third_party")

# ImHex helper macros (setupCompilerFlags etc.)
set(CMAKE_MODULE_PATH "${IMHEX_SRC}/cmake/modules")
include("${IMHEX_SRC}/cmake/build_helpers.cmake")

# --- bundled dependencies ----------------------------------------------------
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/fmt ${CMAKE_CURRENT_BINARY_DIR}/deps/fmt)
set(FMT_LIBRARIES fmt::fmt-header-only)

add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/nlohmann_json ${CMAKE_CURRENT_BINARY_DIR}/deps/nlohmann_json)
set(NLOHMANN_JSON_LIBRARIES nlohmann_json)

add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/boost ${CMAKE_CURRENT_BINARY_DIR}/deps/boost)
set(BOOST_LIBRARIES boost::regex)

add_subdirectory(${EXTERNAL_LIBS_FOLDER}/libwolv ${CMAKE_CURRENT_BINARY_DIR}/deps/libwolv)

set(LIBPL_BUILD_CLI_AS_EXECUTABLE OFF CACHE BOOL "" FORCE)
# plcli is built as an OBJECT library and linked into the builtin plugin
# (same configuration as the upstream desktop build, which leaves the
# LIBPL_ENABLE_CLI default ON and links plcli into builtin.hexplug).
set(LIBPL_ENABLE_CLI ON CACHE BOOL "" FORCE)
set(LIBPL_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(LIBPL_ENABLE_EXAMPLE OFF CACHE BOOL "" FORCE)
set(LIBPL_ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(LIBPL_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${EXTERNAL_LIBS_FOLDER}/pattern_language ${CMAKE_CURRENT_BINARY_DIR}/deps/pattern_language)

add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/imgui ${CMAKE_CURRENT_BINARY_DIR}/deps/imgui)
set(IMGUI_LIBRARIES imgui_imgui imgui_cimgui imgui_implot imgui_implot3d imgui_imnodes imgui_backend)

set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/lunasvg ${CMAKE_CURRENT_BINARY_DIR}/deps/lunasvg)
set(LUNASVG_LIBRARIES lunasvg)

add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/microtar ${CMAKE_CURRENT_BINARY_DIR}/deps/microtar)
add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/md4c ${CMAKE_CURRENT_BINARY_DIR}/deps/md4c)
# md4c::md4c alias expected by the ui plugin (normally provided by ImHex's
# addBundledLibraries macro, which we do not call to avoid re-adding deps).
add_library(md4c_lib INTERFACE)
add_library(md4c::md4c ALIAS md4c_lib)
target_include_directories(md4c_lib INTERFACE ${THIRD_PARTY_LIBS_FOLDER}/md4c/src)
target_link_libraries(md4c_lib INTERFACE md4c)
set(JTHREAD_LIBRARIES jthread)
add_subdirectory(${THIRD_PARTY_LIBS_FOLDER}/llvm-demangle ${CMAKE_CURRENT_BINARY_DIR}/deps/llvm-demangle)
add_subdirectory(${IMHEX_SRC}/lib/trace ${CMAKE_CURRENT_BINARY_DIR}/deps/trace)
add_subdirectory(${EXTERNAL_LIBS_FOLDER}/disassembler ${CMAKE_CURRENT_BINARY_DIR}/deps/disassembler)

set(XDGPP_INCLUDE_DIRS "${THIRD_PARTY_LIBS_FOLDER}/xdgpp")
set(LLVM_INCLUDE_DIRS "${THIRD_PARTY_LIBS_FOLDER}/llvm-demangle/include")

# --- mbedTLS (vendored source) -----------------------------------------------
if (NOT TARGET mbedtls)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(${DEPS_DIR}/mbedtls-3.6.2 ${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls)
    set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)
endif()

# --- libssh2 (vendored source, mbedTLS crypto backend) ------------------------
# The remote plugin's SSH support is built entirely from in-tree sources:
# libssh2 itself plus the mbedTLS library it uses for cryptography. No
# prebuilt OpenSSL/libssh2 binaries are linked, so every library in the HAP
# is compiled and signed by DevEco. (libssh2's CMake reads the MBEDTLS_*
# variables through its FindMbedTLS module; the static archive itself has no
# link step, so the mbedTLS libraries are linked explicitly by the remote
# plugin.)
set(MBEDTLS_INCLUDE_DIR "${DEPS_DIR}/mbedtls-3.6.2/include" CACHE PATH "" FORCE)
set(MBEDTLS_INCLUDE_DIRS "${DEPS_DIR}/mbedtls-3.6.2/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedtls.a" CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedx509.a" CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library/libmbedcrypto.a" CACHE FILEPATH "" FORCE)
set(MBEDTLS_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/deps/mbedtls/library" CACHE PATH "" FORCE)
set(CRYPTO_BACKEND "mbedTLS" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/libssh2-1.11.1 ${CMAKE_CURRENT_BINARY_DIR}/deps/libssh2)
set(LIBSSH2_INCLUDE_DIR "${DEPS_DIR}/libssh2-1.11.1/include" CACHE PATH "" FORCE)
set(LIBSSH2_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/libssh2/src/libssh2.a" CACHE FILEPATH "" FORCE)

# --- libmagic stub + native file dialog stub (system has neither; ImHex
# only needs the APIs; magic.h is a pure declaration header from the dev
# machine's libmagic) --------------------------------------------------------
set(MAGIC_INCLUDE_DIRS "${IMHEX_SRC}/../deps/magic" CACHE PATH "" FORCE)
add_library(magic STATIC ${OHOS_PORT_DIR}/nfd_stub/magic_stub.cpp)
target_include_directories(magic PUBLIC ${IMHEX_SRC}/lib/third_party/nativefiledialog/src/include)

add_library(nfd STATIC ${OHOS_PORT_DIR}/nfd_stub/nfd_stub.cpp)
target_include_directories(nfd PUBLIC ${IMHEX_SRC}/lib/third_party/nativefiledialog/src/include)

# --- GLFW compatibility layer (EGL + GLES3 + NativeWindow) --------------------
add_library(imhex_glfw_ohos STATIC ${OHOS_PORT_DIR}/glfw/glfw_ohos.cpp)
target_include_directories(imhex_glfw_ohos PUBLIC ${OHOS_PORT_DIR}/glfw)
target_compile_definitions(imhex_glfw_ohos PRIVATE GLFW_INCLUDE_ES3)
target_link_libraries(imhex_glfw_ohos PUBLIC EGL GLESv3 native_window pthread hilog_ndk.z)

# --- libromfs (GUI resources) -------------------------------------------------
# libromfs' generator is a host tool; DevEco's toolchain marks this build as
# cross-compiling and expects NATIVE_CMAKE_*_COMPILER to name a host compiler.
# On this machine host == target (OHOS device), so reuse the cross compiler
# and flags -- the produced generator binary runs natively.
set(NATIVE_CMAKE_C_COMPILER "${CMAKE_C_COMPILER}")
set(NATIVE_CMAKE_CXX_COMPILER "${CMAKE_CXX_COMPILER}")
set(NATIVE_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(NATIVE_CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# zlib for compressed romfs and the decompress plugin: built from source as a
# static PIC library (same pattern as freetype/curl) so no plugin in the HAP
# depends on a runtime libz.so, and everything is signed by DevEco.
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/zlib-1.3.1 ${CMAKE_CURRENT_BINARY_DIR}/deps/zlib)
set(ZLIB_INCLUDE_DIR "${DEPS_DIR}/zlib-1.3.1" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/deps/zlib/libz.a" CACHE FILEPATH "" FORCE)
set(ZLIB_LIBRARY_RELEASE "${ZLIB_LIBRARY}" CACHE FILEPATH "" FORCE)
set(ZLIB_LIBRARY_DEBUG "${ZLIB_LIBRARY}" CACHE FILEPATH "" FORCE)
set(ZLIB_LIBRARIES "${ZLIB_LIBRARY}" CACHE FILEPATH "" FORCE)
set(LIBROMFS_RESOURCE_LOCATION ${IMHEX_SRC}/main/gui/romfs)
set(LIBROMFS_PROJECT_NAME imhex)
set(LIBROMFS_COMPRESS_RESOURCES ON)
add_subdirectory(${IMHEX_SRC}/lib/external/libromfs ${CMAKE_CURRENT_BINARY_DIR}/deps/libromfs)
set(LIBROMFS_IMHEX_LIBRARY libromfs-imhex)

# --- libimhex ----------------------------------------------------------------
set(IMHEX_STATIC_LINK_PLUGINS ON CACHE BOOL "" FORCE)
add_compile_definitions(IMHEX_STATIC_LINK_PLUGINS)
set(IMHEX_VERSION_STRING "1.38.1" CACHE STRING "" FORCE)
set(IMHEX_COMMIT_HASH_LONG "devecobuild" CACHE STRING "" FORCE)
set(IMHEX_COMMIT_BRANCH "v1.38.1" CACHE STRING "" FORCE)
set(IMHEX_ENABLE_CXX_MODULES OFF CACHE BOOL "" FORCE)
set(IMHEX_ENABLE_UNITY_BUILD OFF CACHE BOOL "" FORCE)
set(IMHEX_ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(IMHEX_ENABLE_IMGUI_TEST_ENGINE OFF CACHE BOOL "" FORCE)
add_subdirectory(${IMHEX_SRC}/lib/libimhex ${CMAKE_CURRENT_BINARY_DIR}/libimhex)
target_link_directories(libimhex PRIVATE ${THIRD_PARTY_LIBS_FOLDER}/nativefiledialog/src/include)
target_include_directories(libimhex PRIVATE ${THIRD_PARTY_LIBS_FOLDER}/nativefiledialog/src/include)
target_link_libraries(libimhex PRIVATE nfd)
set(IMGUI_LIBRARIES imgui_imgui imgui_cimgui imgui_implot imgui_implot3d imgui_imnodes imgui_backend)

# --- ImHex plugins (statically linked into libimhex on OHOS: the sandbox
# linker namespace rejects dlopen from every application path, so plugins
# cannot be loaded dynamically; IMHEX_STATIC_LINK_PLUGINS merges them into
# libimhex and plugin-bundle.cpp registers them at startup) -------------------
set(IMHEX_PLUGIN_BUNDLE_SOURCES "" CACHE INTERNAL "")
set(IMHEX_BASE_FOLDER "${IMHEX_SRC}")
set(IMHEX_MAIN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugin-out")
set(IMHEX_ENABLE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(IMHEX_ENABLE_PLUGIN_TESTS OFF CACHE BOOL "" FORCE)
set(IMHEX_PLUGINS builtin decompress diffing disassembler fonts hashes remote ui visualizers)
set(IMHEX_RAWPLUGIN_STAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/main/resources/rawfile/imhex/plugins")
set(IMHEX_RAWPLUGIN_STRIP "${HARMONYBREW_SDK}/native/llvm/bin/llvm-strip")
foreach(plugin IN LISTS IMHEX_PLUGINS)
    add_subdirectory(${IMHEX_SRC}/plugins/${plugin} ${CMAKE_CURRENT_BINARY_DIR}/plugins/${plugin})
endforeach()
# builtin's command_line_interface.cpp includes pl/cli/cli.hpp (header-only
# declarations) from the pattern_language CLI component.
target_include_directories(builtin PRIVATE ${EXTERNAL_LIBS_FOLDER}/pattern_language/cli/include)

# --- entry library (XComponent + NAPI entry, GUI) -----------------------------
add_library(entry SHARED
    ${IMHEX_SRC}/main/gui/source/window/window.cpp
    ${IMHEX_SRC}/main/gui/source/window/platform/linux.cpp
    ${IMHEX_SRC}/main/gui/source/messaging/common.cpp
    ${IMHEX_SRC}/main/gui/source/messaging/linux.cpp
    ${IMHEX_SRC}/main/gui/source/init/splash_window.cpp
    ${IMHEX_SRC}/main/gui/source/init/tasks.cpp
    ${IMHEX_SRC}/main/gui/source/init/run/common.cpp
    ${IMHEX_SRC}/main/gui/source/init/run/desktop.cpp
    ${IMHEX_SRC}/main/gui/source/init/run/cli.cpp
    ${IMHEX_SRC}/main/gui/source/crash_handlers.cpp
    ${OHOS_PORT_DIR}/entry/xcomponent_entry.cpp
    ${IMHEX_PLUGIN_BUNDLE_SOURCES}
)

target_include_directories(entry PUBLIC
    ${IMHEX_SRC}/main/gui/include
    ${OHOS_PORT_DIR}/glfw
    ${IMHEX_SRC}/lib/libimhex/include
)

target_compile_definitions(entry PRIVATE
    IMHEX_PROJECT_NAME="imhex"
    LIBROMFS_PROJECT_NAME=imhex
    IMHEX_VERSION="1.38.1"
    IMHEX_PLUGIN_NAME=entry
    IMHEX_OHOS_PORT=1
)

target_link_libraries(entry PRIVATE
    libimhex
    libromfs-imhex
    imhex_glfw_ohos
    EGL GLESv3 native_window pthread
)

# Link against the ace/hilog/napi NDK libraries from the harmonybrew sysroot
if (EXISTS "${HARMONYBREW_SYSROOT}/usr/lib/aarch64-linux-ohos/libace_ndk.z.so")
    set(OHOS_LIB_DIR "${HARMONYBREW_SYSROOT}/usr/lib/aarch64-linux-ohos")
    target_link_libraries(entry PRIVATE
        "${OHOS_LIB_DIR}/libace_ndk.z.so"
        "${OHOS_LIB_DIR}/libhilog_ndk.z.so"
        "${OHOS_LIB_DIR}/libace_napi.z.so"
    )
elseif (DEFINED OHOS_SDK_NATIVE)
    set(OHOS_NATIVE "${OHOS_SDK_NATIVE}")
    set(OHOS_LIB_DIR "${OHOS_NATIVE}/sysroot/usr/lib/aarch64-linux-ohos")
    target_link_libraries(entry PRIVATE
        "${OHOS_LIB_DIR}/libace_ndk.z.so"
        "${OHOS_LIB_DIR}/libhilog_ndk.z.so"
        "${OHOS_LIB_DIR}/libace_napi.z.so"
    )
endif()
