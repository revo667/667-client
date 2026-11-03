#!/usr/bin/env bash
# Downloads the official universal (arm64 + x86_64) static MoltenVK that the macOS client links
# for its Vulkan backend. Homebrew's molten-vk only has the host architecture, so a universal
# build can't use it.
#
#   scripts/fetch_moltenvk.sh <dest>
#
# Result: <dest>/lib/libMoltenVK.a, <dest>/include (Vulkan + MoltenVK headers), <dest>/LICENSE.
# Pass them to CMake as -DVULKAN_LIBRARIES=<dest>/lib/libMoltenVK.a -DVULKAN_INCLUDE_DIRS=<dest>/include.
# MoltenVK 1.4.2 is built for macOS 12.0, so the client's deployment target must be 12.0 or newer.
set -euo pipefail

MOLTENVK_VERSION="1.4.2"
MOLTENVK_SHA256="f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e"
MOLTENVK_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/v${MOLTENVK_VERSION}/MoltenVK-macos.tar"

DEST="${1:?usage: $0 <dest>}"
STAMP="$DEST/.version"

if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$MOLTENVK_VERSION" ] && [ -f "$DEST/lib/libMoltenVK.a" ]; then
	echo "MoltenVK $MOLTENVK_VERSION already in $DEST"
	exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Downloading MoltenVK $MOLTENVK_VERSION"
curl -fsSL --retry 3 -o "$TMP/MoltenVK-macos.tar" "$MOLTENVK_URL"
echo "$MOLTENVK_SHA256  $TMP/MoltenVK-macos.tar" | shasum -a 256 -c - > /dev/null || {
	echo "MoltenVK download has the wrong checksum" >&2
	exit 1
}

tar -xf "$TMP/MoltenVK-macos.tar" -C "$TMP" MoltenVK/LICENSE MoltenVK/MoltenVK/static MoltenVK/MoltenVK/include
LIB="$TMP/MoltenVK/MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a"
for arch in arm64 x86_64; do
	lipo "$LIB" -verify_arch "$arch" || {
		echo "MoltenVK has no $arch slice" >&2
		exit 1
	}
done

rm -rf "$DEST"
mkdir -p "$DEST/lib"
cp "$LIB" "$DEST/lib/"
cp -R "$TMP/MoltenVK/MoltenVK/include" "$DEST/include"
cp "$TMP/MoltenVK/LICENSE" "$DEST/LICENSE"
echo "$MOLTENVK_VERSION" > "$STAMP"
echo "MoltenVK $MOLTENVK_VERSION in $DEST"
