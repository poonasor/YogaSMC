#!/bin/bash
# Manual kext build for hosts without Xcode (CommandLineTools only).
# Mirrors the flags from YogaSMC.xcodeproj (gnu++14, MacKernelSDK, Lilu/VirtualSMC SDKs).
set -e

REPO="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO"

SDK=$(xcrun --show-sdk-path)
KHDR="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
KPRIV="$SDK/System/Library/Frameworks/Kernel.framework/PrivateHeaders"

VERSION="1.5.3"
OUT=build/YogaSMC.kext
OBJS=build/objs
rm -rf build
mkdir -p "$OBJS" "$OUT/Contents/MacOS"

INC="-I MacKernelSDK/Headers \
 -I Lilu.kext/Contents/Resources \
 -I VirtualSMC.kext/Contents/Resources \
 -I YogaSMC \
 -I YogaSMC/YogaVPC \
 -I YogaSMC/YogaSMC \
 -I YogaSMC/YogaWMI"

DEFS="-DPRODUCT_NAME=YogaSMC -DMODULE_VERSION=$VERSION -DMODULE_NAME=\"org.zhen.YogaSMC\" -DMANUAL_KEXT_GLUE -DDEBUG -D__ACIDANTHERA_MAC_SDK"

CXXFLAGS="-arch x86_64 -mkernel -std=gnu++14 -fapple-kext \
 -fno-builtin -fno-exceptions -fno-use-cxa-atexit \
 -fno-stack-protector -Oz \
 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
 -isystem $KHDR -isystem $KPRIV \
 $INC $DEFS"

CFLAGS="-arch x86_64 -mkernel -fapple-kext -fno-builtin \
 -fno-stack-protector -Oz \
 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
 -isystem $KHDR -isystem $KPRIV \
 $INC -DPRODUCT_NAME=YogaSMC -DMODULE_VERSION=$VERSION -DMODULE_NAME=\"org.zhen.YogaSMC\" -DMANUAL_KEXT_GLUE -DDEBUG -D__ACIDANTHERA_MAC_SDK"

SOURCES="YogaSMC/KeyImplementations.cpp \
 YogaSMC/WMI.cpp \
 YogaSMC/YogaBaseService.cpp \
 YogaSMC/YogaSMC.cpp \
 YogaSMC/YogaSMCUserClient.cpp \
 YogaSMC/YogaVPC.cpp \
 YogaSMC/bmfparser.cpp \
 YogaSMC/bmfdec.c \
 YogaSMC/YogaWMI.cpp \
 YogaSMC/YogaWMI/IdeaWMI.cpp \
 YogaSMC/YogaWMI/DYWMI.cpp \
 YogaSMC/YogaVPC/DYVPC.cpp \
 YogaSMC/YogaVPC/IdeaVPC.cpp \
 YogaSMC/YogaVPC/ThinkVPC.cpp \
 YogaSMC/YogaVPC/ThinkCentre.cpp \
 YogaSMC/YogaVPC/YogaHIDD.cpp \
 YogaSMC/YogaSMC/IdeaSMC.cpp \
 YogaSMC/YogaSMC/ThinkSMC.cpp \
 YogaSMC/YogaSMC/DYSMC.cpp"

for src in $SOURCES; do
    obj="$OBJS/$(echo "$src" | tr '/' '_').o"
    case "$src" in
        *.c) clang $CFLAGS -c "$src" -o "$obj" ;;
        *) clang++ $CXXFLAGS -c "$src" -o "$obj" ;;
    esac
    echo "CC $src"
done

clang++ -arch x86_64 -mkernel -nostdlib -Wl,-kext \
    -o "$OUT/Contents/MacOS/YogaSMC" "$OBJS"/*.o
echo "LD done"

# preprocess Info.plist variables
sed -e "s/\$(DEVELOPMENT_LANGUAGE)/en/" \
    -e "s/\$(EXECUTABLE_NAME)/YogaSMC/" \
    -e "s/\$(PRODUCT_BUNDLE_IDENTIFIER)/org.zhen.YogaSMC/" \
    -e "s/\$(PRODUCT_NAME)/YogaSMC/" \
    -e "s/\$(PRODUCT_BUNDLE_PACKAGE_TYPE)/KEXT/" \
    -e "s/\$(MODULE_VERSION)/$VERSION/g" \
    YogaSMC/Info.plist > "$OUT/Contents/Info.plist"
plutil -lint "$OUT/Contents/Info.plist"

# NOTE: do not ad-hoc sign the kext; OpenCore-auxKC on SIP-enabled systems
# rejects kexts with invalid signatures while accepting fully unsigned ones.
rm -rf "$OUT/Contents/_CodeSignature"
echo "Built $OUT (unsigned)"
