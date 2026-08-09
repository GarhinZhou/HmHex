#!/bin/sh
# Manual HAP packaging without DevEco/hvigor, using the full OpenHarmony SDK
# toolchains (es2abc + restool + hap-sign-tool).
#
# Prerequisite: full OpenHarmony SDK (e.g. 5.0.0-Release) downloaded and
# extracted, e.g.:
#   tar xzf ohos-sdk-windows_linux-public.tar.gz -C deps/
#   SDK_TOOLCHAIN=deps/sdk-pkg/ohos-sdk/toolchains
#
# Usage: ./scripts/package_hap_manual.sh <toolchains-dir>

set -e

TOOLS="$1"
if [ -z "$TOOLS" ] || [ ! -d "$TOOLS" ]; then
    echo "usage: $0 <openharmony-sdk-toolchains-dir>" >&2
    echo "  (contains es2abc, restool, hap-sign-tool.jar)" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${ROOT}/build/hap-stage"
OUT="${ROOT}/build/imhex-unsigned.hap"

rm -rf "${STAGE}"
mkdir -p "${STAGE}/ets" "${STAGE}/libs/arm64-v8a" "${STAGE}/assets/entry/resources/rawfile/imhex/plugins"

echo "== 1. Compile ArkTS -> abc =="
ES2ABC=$(find "$TOOLS" -name es2abc -type f | head -1)
if [ -z "$ES2ABC" ]; then
    echo "ERROR: es2abc not found in $TOOLS" >&2
    exit 1
fi
# Index.ets contains the page + ability bootstrap; compile the whole ets dir
find "${ROOT}/entry/src/main/ets" -name "*.ets" | while read -r f; do
    rel="${f#${ROOT}/entry/src/main/ets/}"
    "$ES2ABC" "${f}" -o "${STAGE}/ets/${rel%.ets}.abc" \
        --module-record-name "${rel%.ets}" --merge-abc
done

echo "== 2. Compile resources -> resources.index =="
RESTOOL=$(find "$TOOLS" -name restool -type f | head -1)
if [ -z "$RESTOOL" ]; then
    echo "WARNING: restool not found, resources.index will be missing" >&2
else
    "$RESTOOL" --directory "${ROOT}/entry/src/main/resources" \
        --output "${STAGE}" \
        --module-name entry \
        --hqf "${ROOT}/entry/src/main/module.json5" \
        --rpf "${ROOT}/AppScope/resources"
fi

echo "== 3. Copy native libs =="
cp "${ROOT}"/entry/libs/arm64-v8a/*.so "${STAGE}/libs/arm64-v8a/"

echo "== 4. Copy rawfile plugins =="
cp "${ROOT}"/entry/src/main/resources/rawfile/imhex/plugins/* "${STAGE}/assets/entry/resources/rawfile/imhex/plugins/"

echo "== 5. Copy module.json =="
cp "${ROOT}/entry/src/main/module.json5" "${STAGE}/module.json"

echo "== 6. Pack HAP (zip) =="
cd "${STAGE}"
# module.json at root, pack.info required by newer runtimes
cat > pack.info <<'EOF'
{"summary":{},"packages":{"entry":{"module":{"mainAbility":"EntryAbility","deviceType":["phone","tablet","2in1"],"distro":{"moduleName":"entry","moduleType":"entry","installationFree":false,"deliveryWithInstall":true},"abilities":[{"name":"EntryAbility"}],"apiVersion":{"compatible":12,"target":12}}}}}
EOF
zip -qr "${OUT}" .

echo "== 7. Sign HAP =="
SIGN_TOOL=$(find "$TOOLS" -name "hap-sign-tool.jar" | head -1)
if [ -z "$SIGN_TOOL" ]; then
    echo "WARNING: hap-sign-tool.jar not found; unsigned HAP at ${OUT}"
    echo "  -> install with: hdc install ${OUT}  (debug device only)"
    exit 0
fi
java -jar "$SIGN_TOOL" sign-app \
    -keyAlias "openharmony application release" \
    -signAlg "SHA256withECDSA" \
    -mode "localSign" \
    -appCertFile "${TOOLS}/../toolchains/lib/OpenHarmony.p12" \
    -profileFile "${TOOLS}/../toolchains/lib/OpenHarmonyProfile.p7b" \
    -inFile "${OUT}" \
    -keystoreFile "${TOOLS}/../toolchains/lib/OpenHarmony.p12" \
    -outFile "${ROOT}/build/imhex-signed.hap" \
    -keystorePwd "123456" -keyPwd "123456" || {
    echo "Signing failed; unsigned HAP at ${OUT}" >&2
    exit 1
}
echo "Signed HAP: ${ROOT}/build/imhex-signed.hap"
