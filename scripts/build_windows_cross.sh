#!/usr/bin/env bash
# Builds the Windows package (MSVC flavour, like release.yml) on macOS or Linux, without Windows:
# clang-cl and lld-link compile against the Windows SDK and MSVC runtime that xwin downloads.
#
#   scripts/build_windows_cross.sh [version]      e.g. scripts/build_windows_cross.sh 1.0.31
#
# Needs: LLVM with clang-cl, lld-link, llvm-lib, llvm-rc (macOS: brew install llvm lld),
# rustup target add x86_64-pc-windows-msvc, ninja, python3, glslang and spirv-tools for the
# Vulkan shaders, and once:
#   xwin --accept-license --arch x86_64 --cache-dir /tmp/xwin-cache splat --output ~/.xwin
# Output: build-windows/out/CecenClient-windows.zip and build-windows/out/CecenClient.exe
set -euo pipefail

VERSION="${1:-}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-windows}"
export XWIN_DIR="${XWIN_DIR:-$HOME/.xwin}"

for dir in /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/lld/bin /usr/local/opt/llvm/bin /usr/local/opt/lld/bin; do
	[ -d "$dir" ] && PATH="$dir:$PATH"
done
export PATH

missing=0
command -v glslangValidator > /dev/null || command -v glslang > /dev/null || { echo "missing: glslang" >&2; missing=1; }
for tool in clang-cl lld-link llvm-lib llvm-rc ninja cmake cargo python3 spirv-opt; do
	if ! command -v "$tool" > /dev/null; then
		echo "missing: $tool" >&2
		missing=1
	fi
done
[ -d "$XWIN_DIR/sdk/include/um" ] || { echo "missing: Windows SDK in $XWIN_DIR (run xwin splat)" >&2; missing=1; }
rustup target list --installed 2> /dev/null | grep -qx x86_64-pc-windows-msvc || { echo "missing: rustup target add x86_64-pc-windows-msvc" >&2; missing=1; }
[ "$missing" = 0 ] || exit 1

# What cargo needs to build C/C++ inside crates and to link for the MSVC target.
TARGET_FLAGS="--target=x86_64-pc-windows-msvc"
for dir in crt/include sdk/include/ucrt sdk/include/um sdk/include/shared sdk/include/winrt sdk/include/cppwinrt; do
	[ -d "$XWIN_DIR/$dir" ] && TARGET_FLAGS="$TARGET_FLAGS /imsvc$XWIN_DIR/$dir"
done
export CC_x86_64_pc_windows_msvc=clang-cl
export CXX_x86_64_pc_windows_msvc=clang-cl
export AR_x86_64_pc_windows_msvc=llvm-lib
export CFLAGS_x86_64_pc_windows_msvc="$TARGET_FLAGS"
export CXXFLAGS_x86_64_pc_windows_msvc="$TARGET_FLAGS"
export CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=lld-link
export CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS="-C target-feature=+crt-static -Lnative=$XWIN_DIR/crt/lib/x86_64 -Lnative=$XWIN_DIR/sdk/lib/um/x86_64 -Lnative=$XWIN_DIR/sdk/lib/ucrt/x86_64"

VERSION_FILE="$ROOT/src/game/client/components/sixclient/version.h"
if [ -n "$VERSION" ]; then
	VERSION_BACKUP="$(mktemp)"
	cp "$VERSION_FILE" "$VERSION_BACKUP"
	trap 'cp "$VERSION_BACKUP" "$VERSION_FILE"; rm -f "$VERSION_BACKUP"' EXIT
	perl -pi -e "s/^#define SIXCLIENT_VERSION \".*\"/#define SIXCLIENT_VERSION \"$VERSION\"/" "$VERSION_FILE"
fi

# Debug info goes into a .pdb next to the exe (not into the package), so the addresses in beta
# crash and hang reports can be turned into function names. /OPT:REF,ICF keep the release linking
# that /DEBUG would otherwise turn off.
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/msvc-xwin.toolchain" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG /Z7" \
	-DCMAKE_CXX_FLAGS_RELEASE="/O2 /Ob2 /DNDEBUG /Z7" \
	-DCMAKE_EXE_LINKER_FLAGS_RELEASE="/DEBUG /OPT:REF /OPT:ICF" \
	-DTOOLS=OFF -DDOWNLOAD_GTEST=OFF -DVULKAN=ON -DPREFER_BUNDLED_LIBS=ON
cmake --build "$BUILD" --target game-client sixclient-updater
cmake --build "$BUILD" --target package_default

PACKAGE="$(ls -t "$BUILD"/*-win64.zip | head -n 1)"
mkdir -p "$BUILD/out"
cp "$PACKAGE" "$BUILD/out/CecenClient-windows.zip"
unzip -o -j -q "$PACKAGE" "*/CecenClient.exe" -d "$BUILD/out"
# Not a pipe into grep -q: with pipefail its early exit would fail the pipeline.
LISTING="$(unzip -Z1 "$PACKAGE")"
[[ "$LISTING" == *CecenClient-spotify.exe* ]] || { echo "the package has no CecenClient-spotify.exe" >&2; exit 1; }
# Keep the symbols of every released build: a report names its version and commit.
SYMBOLS="$ROOT/symbols/windows/${VERSION:-dev}-$(git -C "$ROOT" rev-parse --short=9 HEAD)"
mkdir -p "$SYMBOLS"
cp "$BUILD/CecenClient.exe" "$SYMBOLS/"
[ -f "$BUILD/CecenClient.pdb" ] && cp "$BUILD/CecenClient.pdb" "$SYMBOLS/"
echo "done: $BUILD/out/CecenClient-windows.zip and $BUILD/out/CecenClient.exe, symbols in $SYMBOLS"
