#!/usr/bin/env bash
# Builds the macOS package on this Mac like release.yml does: a universal (arm64 + x86_64) DMG.
# It has both the OpenGL and the Vulkan backend. Vulkan runs on Metal through a static MoltenVK
# that scripts/fetch_moltenvk.sh downloads, and MoltenVK needs macOS 12.0 or newer.
#
#   scripts/build_macos_release.sh [version]      e.g. scripts/build_macos_release.sh 1.0.31
#
# Needs: rustup target add aarch64-apple-darwin x86_64-apple-darwin, ninja, cmake,
# brew install glslang spirv-tools (shader compilers), and pip3 install dmgbuild pyobjc-framework-Quartz.
# Output: build-macos/out/CecenClient-macos.dmg
# SDL2 is pinned to the bundled universal framework: FindSDL2 would pick Homebrew's arm64-only one.
set -euo pipefail

VERSION="${1:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-macos}"

missing=0
for tool in ninja cmake cargo python3 curl glslangValidator spirv-opt; do
	command -v "$tool" > /dev/null || { echo "missing: $tool" >&2; missing=1; }
done
for target in aarch64-apple-darwin x86_64-apple-darwin; do
	rustup target list --installed 2> /dev/null | grep -qx "$target" || { echo "missing: rustup target add $target" >&2; missing=1; }
done
python3 -c "import dmgbuild" 2> /dev/null || { echo "missing: pip3 install dmgbuild pyobjc-framework-Quartz" >&2; missing=1; }
[ "$missing" = 0 ] || exit 1

VERSION_FILE="$ROOT/src/game/client/components/sixclient/version.h"
if [ -n "$VERSION" ]; then
	VERSION_BACKUP="$(mktemp)"
	cp "$VERSION_FILE" "$VERSION_BACKUP"
	trap 'cp "$VERSION_BACKUP" "$VERSION_FILE"; rm -f "$VERSION_BACKUP"' EXIT
	perl -pi -e "s/^#define SIXCLIENT_VERSION \".*\"/#define SIXCLIENT_VERSION \"$VERSION\"/" "$VERSION_FILE"
fi

MOLTENVK="$BUILD/moltenvk"
"$ROOT/scripts/fetch_moltenvk.sh" "$MOLTENVK"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release -DPREFER_BUNDLED_LIBS=ON -DPRECOMPILE_HEADERS=OFF \
	-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
	-DCMAKE_SYSTEM_IGNORE_PATH=/usr/lib -DCMAKE_FIND_FRAMEWORK=LAST \
	-DVULKAN=ON -DVULKAN_LIBRARIES="$MOLTENVK/lib/libMoltenVK.a" -DVULKAN_INCLUDE_DIRS="$MOLTENVK/include" \
	-DPNG_LIBRARY="$ROOT/ddnet-libs/png/mac/libfat/libpng16.16.dylib" \
	-DPNG_INCLUDEDIR="$ROOT/ddnet-libs/png/include" \
	-DSDL2_LIBRARY="$ROOT/ddnet-libs/sdl/mac/libfat/SDL2.framework" \
	-DSDL2_INCLUDEDIR="$ROOT/ddnet-libs/sdl/include/mac"
cmake --build "$BUILD" --target game-client
cmake --build "$BUILD" --target package_default

DMG="$(ls -t "$BUILD"/*-macos.dmg | head -n 1)"
mkdir -p "$BUILD/out"
cp "$DMG" "$BUILD/out/CecenClient-macos.dmg"
echo "done: $BUILD/out/CecenClient-macos.dmg"
