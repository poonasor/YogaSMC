#!/bin/bash
# Manual app build for hosts without Xcode (CommandLineTools only).
# Replaces the storyboard with programmatic menu construction (see main.swift).
set -e

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

VERSION="1.5.3"
OUT=build/YogaSMCNC.app
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

swiftc -O -module-name YogaSMCNC \
    -target x86_64-apple-macos10.13 \
    -import-objc-header YogaSMCUtils/YogaSMC-Bridging-Header.h \
    -F Frameworks \
    YogaSMCNC/*.swift \
    YogaSMCUtils/*.swift \
    -framework AppKit -framework Carbon -framework CoreGraphics \
    -framework Foundation -framework IOKit -framework IOBluetooth \
    -framework NotificationCenter -framework ServiceManagement \
    -Xlinker -undefined -Xlinker dynamic_lookup \
    -o "$OUT/Contents/MacOS/YogaSMCNC"
echo "SWIFT LD done"

sed -e "s/\$(DEVELOPMENT_LANGUAGE)/en/" \
    -e "s/\$(EXECUTABLE_NAME)/YogaSMCNC/" \
    -e "s/\$(PRODUCT_BUNDLE_IDENTIFIER)/org.zhen.YogaSMCNC/" \
    -e "s/\$(PRODUCT_NAME)/YogaSMCNC/" \
    -e "s/\$(PRODUCT_BUNDLE_PACKAGE_TYPE)/APPL/" \
    -e "s/\$(CURRENT_PROJECT_VERSION)/$VERSION/g" \
    -e "s/\$(MACOSX_DEPLOYMENT_TARGET)/10.13/" \
    -e "/NSMainStoryboardFile/,+1d" \
    YogaSMCNC/Info.plist > "$OUT/Contents/Info.plist"
plutil -lint "$OUT/Contents/Info.plist"

cp -R YogaSMCNC/Resources/ "$OUT/Contents/Resources/"
mkdir -p "$OUT/Contents/Resources/en.lproj"
cp en.lproj/Localizable.strings "$OUT/Contents/Resources/en.lproj/"

codesign --force --sign - --entitlements YogaSMCNC/YogaSMCNC.entitlements "$OUT" >/dev/null 2>&1
echo "Built $OUT"
