#!/bin/sh
# Packages the decompiler plugin into the ImHex plugin directory.
# Usage: ./scripts/package.sh [plugin-dir]
# Default plugin dir: ~/.local/share/imhex/plugins

set -e

PLUGIN_NAME="decompiler.hexplug"
DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)/dist"
PLUGIN_DIR="${1:-$HOME/.local/share/imhex/plugins}"

if [ ! -f "${DIST_DIR}/${PLUGIN_NAME}" ]; then
    echo "error: ${DIST_DIR}/${PLUGIN_NAME} not found. Build the SDK first:" >&2
    echo "  cmake -S sdk-build -B build/sdk -DCMAKE_BUILD_TYPE=Release" >&2
    echo "  cmake --build build/sdk -j\$(nproc)" >&2
    exit 1
fi

mkdir -p "${PLUGIN_DIR}"
cp "${DIST_DIR}/${PLUGIN_NAME}" "${PLUGIN_DIR}/${PLUGIN_NAME}"
echo "Installed ${PLUGIN_NAME} -> ${PLUGIN_DIR}/"

# If libimhex.so was built locally (not provided by the system ImHex), install it
# into ImHex' libraries folder so the plugin can resolve its symbols.
LOCAL_LIBIMHEX="$(cd "$(dirname "$0")/../.." && pwd)/build/sdk/libimhex/libimhex.so"
if [ -f "${LOCAL_LIBIMHEX}" ]; then
    LIB_DIR="${HOME}/.local/share/imhex/lib"
    mkdir -p "${LIB_DIR}"
    cp "${LOCAL_LIBIMHEX}" "${LIB_DIR}/libimhex.so"
    echo "Installed libimhex.so -> ${LIB_DIR}/"
fi

echo "Done."
