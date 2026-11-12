#!/bin/bash
# Signing material exists only in this hosted job's temporary keychain/directory.
set -euo pipefail
cd "$(dirname "$0")/.."
for key in RELEASE_VERSION RELEASE_TAG RELEASE_COMMIT MACOS_BUNDLE_ID MACOS_TEAM_ID MACOS_SIGN_IDENTITY MACOS_CERTIFICATE_BASE64 MACOS_CERTIFICATE_PASSWORD APPLE_API_KEY_BASE64 APPLE_API_KEY_ID APPLE_API_ISSUER; do
    [[ -n ${!key:-} ]] || { echo "Missing $key" >&2; exit 1; }
done
[[ $MACOS_BUNDLE_ID != app.noitch.mac ]] || { echo 'Set a distribution bundle ID' >&2; exit 1; }
[[ $MACOS_SIGN_IDENTITY == 'Developer ID Application: '* ]] || exit 1
work=$(mktemp -d)
keychain="$work/release.keychain-db"
cleanup() {
    security delete-keychain "$keychain" >/dev/null 2>&1 || true
    rm -rf "$work"
}
trap cleanup EXIT
printf '%s' "$MACOS_CERTIFICATE_BASE64" | base64 -D > "$work/certificate.p12"
printf '%s' "$APPLE_API_KEY_BASE64" | base64 -D > "$work/key.p8"
password=$(openssl rand -hex 24)
security create-keychain -p "$password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$password" "$keychain"
security import "$work/certificate.p12" -P "$MACOS_CERTIFICATE_PASSWORD" -k "$keychain" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$password" "$keychain" >/dev/null
xcodebuild test -project Noitch.xcodeproj -scheme Noitch -destination 'platform=macOS,arch=arm64' -derivedDataPath build CODE_SIGN_IDENTITY=-
xcodebuild build -project Noitch.xcodeproj -scheme Noitch -configuration Release \
    -destination 'platform=macOS,arch=arm64' -derivedDataPath build ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
    CODE_SIGNING_ALLOWED=NO MARKETING_VERSION="$RELEASE_VERSION" CURRENT_PROJECT_VERSION="${GITHUB_RUN_NUMBER:-1}" \
    PRODUCT_BUNDLE_IDENTIFIER="$MACOS_BUNDLE_ID"
mkdir -p build/release "$work/image"
app="$work/image/Noitch.app"
ditto build/Build/Products/Release/Noitch.app "$app"
if nm "$app/Contents/MacOS/Noitch" | grep __llvm_profile >/dev/null; then
    echo 'Release contains coverage instrumentation' >&2; exit 1
fi
cp Vendor/MediaRemoteAdapter/LICENSE "$app/Contents/Resources/MediaRemoteAdapter-LICENSE.txt"
codesign --force --options runtime --timestamp --keychain "$keychain" --sign "$MACOS_SIGN_IDENTITY" "$app/Contents/Frameworks/MediaRemoteAdapter.framework"
codesign --force --options runtime --timestamp --keychain "$keychain" --sign "$MACOS_SIGN_IDENTITY" --entitlements Noitch.entitlements "$app"
notarize() {
    xcrun notarytool submit "$1" --key "$work/key.p8" --key-id "$APPLE_API_KEY_ID" \
        --issuer "$APPLE_API_ISSUER" --wait --timeout 30m --output-format json > "$work/notary.json"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d); sys.exit(0 if d.get("status")=="Accepted" else 1)' "$work/notary.json"
}
ditto -c -k --keepParent "$app" "$work/Noitch.zip"
notarize "$work/Noitch.zip"
xcrun stapler staple "$app"
ln -s /Applications "$work/image/Applications"
dmg="build/release/Noitch-$RELEASE_VERSION-macos-arm64.dmg"
# No overwrite: reruns use clean hosted runners and never replace published assets.
hdiutil create -volname Noitch -srcfolder "$work/image" -format UDZO "$dmg"
codesign --timestamp --keychain "$keychain" --sign "$MACOS_SIGN_IDENTITY" "$dmg"
notarize "$dmg"
xcrun stapler staple "$dmg"
bash scripts/verify-release.sh "$dmg"
xcodebuild -version > build/release-toolchain.txt
sw_vers >> build/release-toolchain.txt
shasum -a 256 Vendor/MediaRemoteAdapter/MediaRemoteAdapter.framework/Versions/A/MediaRemoteAdapter >> build/release-toolchain.txt
