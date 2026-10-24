#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
# shellcheck disable=SC1091
source "$TP/env.sh"

if ! compgen -G "$WX_PREFIX/lib*/cmake/wxWidgets*/wxWidgetsConfig.cmake" >/dev/null; then
    echo "[build] wxWidgets is not installed at $WX_PREFIX" >&2
    echo "[build] run ./scripts/build-wx.sh first" >&2
    exit 1
fi

BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
NATIVE="${TXT_EDITOR_NATIVE:-OFF}"
mkdir -p "$BUILD_DIR"

ARGS=(
    -S "$ROOT"
    -B "$BUILD_DIR"
    -G Ninja
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_PREFIX_PATH="$WX_PREFIX;$SYSROOT/usr"
    -DwxWidgets_CONFIG_EXECUTABLE="$WX_PREFIX/bin/wx-config"
    -DwxWidgets_USE_STATIC=ON
    -DCMAKE_INCLUDE_PATH="$SYSROOT/usr/include"
    "-DCMAKE_LIBRARY_PATH=$SYSROOT/usr/lib/x86_64-linux-gnu;/usr/lib/x86_64-linux-gnu"
    "-DCMAKE_EXE_LINKER_FLAGS=-L$SYSROOT/usr/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu -Wl,-rpath-link,$SYSROOT/usr/lib/x86_64-linux-gnu"
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_EXECUTABLE"
    -DTXT_EDITOR_BUILD_TESTS=ON
    -DTXT_EDITOR_NATIVE_OPTIMIZATION="$NATIVE"
)

"$CMAKE" "${ARGS[@]}"
"$NINJA" -C "$BUILD_DIR"

echo "[build] binary: $BUILD_DIR/txt-editor"
