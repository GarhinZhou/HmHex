#!/bin/sh
# 64KB-align + strip + self-sign all prebuilt .so files.
# Mirrors the ohos-pip-autosign ELF normalization flow (the OHOS musl
# loader requires 64KB page alignment for PT_LOAD segments).
set -e

LIBS_DIR="$(cd "$(dirname "$0")/.." && pwd)/entry/libs/arm64-v8a"
SIGN_TOOL="/storage/Users/currentUser/.harmonybrew/bin/binary-sign-tool"

for so in "${LIBS_DIR}"/lib*.so*; do
    case "$so" in *.unsigned) continue;; esac
    echo "== Processing $(basename "$so")"

    # 1. Strip (forces PHT rebuild)
    llvm-strip --strip-all "$so"

    # 2. Align every allocatable section to 64KB
    SECTIONS=$(llvm-readelf -S "$so" 2>/dev/null | awk '
        /^  \[/ {
            name=$2; gsub(/^\[|\]$/, "", name);
            # second line contains flags; track alloc sections
        }
        /] [A-Z][A-Z0-9_]* +[A-Z]/ {
            if ($0 ~ / A / || $0 ~ /^  [A-Z][A-Z0-9_]* +[A-Z]+ +A/) {}
        }')
    # simpler: readelf lists flags; collect sections with 'A'
    ALLOC_SECTIONS=$(llvm-readelf -S "$so" 2>/dev/null | python3 -c "
import sys, re
lines = sys.stdin.read().splitlines()
sections = []
for i, line in enumerate(lines):
    m = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s*([A-Z]{2,})?\s*(.*)', line)
    if m:
        name = m.group(2)
        flags = (m.group(8) or '')
        # flags like AX, WA, etc; allocatable = contains A
        if 'A' in flags and name.startswith('.'):
            sections.append(name)
print(' '.join(sections))
")
    for sec in ${ALLOC_SECTIONS}; do
        llvm-objcopy --set-section-alignment "$sec"=0x10000 "$so" 2>/dev/null || true
    done

    # 3. Pad the end of the file to a 64KB boundary
    SIZE=$(stat -c %s "$so")
    PAD=$(( (0x10000 - (SIZE % 0x10000)) % 0x10000 ))
    if [ "$PAD" -gt 0 ]; then
        dd if=/dev/zero bs=1 count="$PAD" >> "$so" 2>/dev/null
    fi

    # 4. Sign (self-sign mode)
    "$SIGN_TOOL" sign \
        -selfSign 1 \
        -keyAlias debug -keyPwd 123456 \
        -inFile "$so" \
        -signAlg SHA256withECDSA \
        -keystoreFile "$so" \
        -outFile "$so" 2>&1 | grep -E "success|failed" | head -1
done
echo "All libraries aligned and signed."
