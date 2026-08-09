#!/bin/sh
# Signs all prebuilt .so files with binary-sign-tool (self-sign mode).
# The signed files replace the originals in entry/libs/arm64-v8a/.
set -e

LIBS_DIR="$(cd "$(dirname "$0")/.." && pwd)/entry/libs/arm64-v8a"
SIGN_TOOL="/storage/Users/currentUser/.harmonybrew/bin/binary-sign-tool"

for so in "${LIBS_DIR}"/lib*.so*; do
    case "$so" in *.unsigned) continue;; esac
    [ -f "$so" ] || continue
    echo "== Signing $(basename "$so")"
    [ -f "${so}.unsigned" ] || cp "$so" "${so}.unsigned"
    llvm-strip --strip-all "$so"
    "$SIGN_TOOL" sign \
        -selfSign 1 \
        -keyAlias debug -keyPwd 123456 \
        -inFile "$so" \
        -signAlg SHA256withECDSA \
        -keystoreFile "$so" \
        -outFile "$so" 2>&1 | grep -E "success|failed" | head -2
done
echo "All libraries signed."
