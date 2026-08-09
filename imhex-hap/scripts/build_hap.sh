#!/bin/sh
# Assembles the ImHex OpenHarmony HAP content.
# =============================================
# 1. Copies the built native libraries (libentry.so, libimhex.so) and the
#    bundled plugins into the HAP project.
# 2. Produces the final HAP with the OpenHarmony packaging toolchain.
#
# Prerequisites (one of):
#   A) DevEco Studio / full OpenHarmony SDK with hvigor + arkts toolchain
#      -> run:  hvigorw assembleHap   (in imhex-hap/)
#   B) This script's stage-1 only: copies all artifacts so the project can be
#      built on any machine with the full toolchain.
#
# Usage: ./scripts/build_hap.sh [stage]

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OHOS_BUILD="${ROOT}/../build/imhex-ohos"
HAP_DIR="${ROOT}"
LIBS_DIR="${HAP_DIR}/entry/libs/arm64-v8a"

STAGE="${1:-all}"

echo "== Stage 1: native libraries =="
mkdir -p "${LIBS_DIR}"
cp "${OHOS_BUILD}/hap-libs/libentry.so"        "${LIBS_DIR}/"
# libentry.so links against the versioned name libimhex.so.1.38.1
cp "${OHOS_BUILD}/lib/libimhex/libimhex.so.1.38.1" "${LIBS_DIR}/libimhex.so.1.38.1"
cp "${OHOS_BUILD}/lib/libimhex/libimhex.so.1.38.1" "${LIBS_DIR}/libimhex.so"

# Runtime libraries the sandbox does not provide (built on the dev machine)
BREW_LIB="/storage/Users/currentUser/.harmonybrew/lib"
for lib in libmagic.so.1 libcurl.so.4 libfreetype.so.6; do
    if [ -f "${BREW_LIB}/${lib}" ]; then
        cp -L "${BREW_LIB}/${lib}" "${LIBS_DIR}/"
    fi
done

# libc++ runtime required by the toolchain
LIBCXX_SHARED="/storage/Users/currentUser/.harmonybrew/Cellar/ohos-sdk/26.0.0.18_1/native/llvm/lib/aarch64-linux-ohos/libc++_shared.so"
if [ -f "${LIBCXX_SHARED}" ]; then
    cp "${LIBCXX_SHARED}" "${LIBS_DIR}/libc++_shared.so"
fi
echo "   libs -> ${LIBS_DIR}"
ls -lh "${LIBS_DIR}"

echo "== Stage 1: plugins (rawfile) =="
PLUGIN_RAW="${HAP_DIR}/entry/src/main/resources/rawfile/imhex/plugins"
mkdir -p "${PLUGIN_RAW}"
cp "${OHOS_BUILD}"/plugins/*.hexplug* "${PLUGIN_RAW}/"
echo "   plugins -> ${PLUGIN_RAW}"

if [ "${STAGE}" = "stage1" ]; then
    echo "Stage 1 done. Copy imhex-hap/ to a machine with the full OpenHarmony"
    echo "toolchain (DevEco Studio) and run: hvigorw assembleHap"
    exit 0
fi

echo "== Stage 2: HAP assembly (requires hvigor + arkts toolchain) =="
if ! command -v hvigorw >/dev/null 2>&1; then
    echo "ERROR: hvigorw not found. Install DevEco Studio / full OpenHarmony SDK,"
    echo "       or run this script with 'stage1' and build elsewhere."
    exit 1
fi

cd "${HAP_DIR}"
hvigorw assembleHap --mode module -p product=default -p buildMode=release
echo "HAP output: ${HAP_DIR}/entry/build/default/outputs/default/entry-default-signed.hap"
