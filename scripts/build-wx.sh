#!/usr/bin/env bash
# Build wxWidgets 3.3.4 (static, wxSTC) against the user-space GTK3 sysroot.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
# shellcheck disable=SC1091
source "$TP/env.sh"

WX_SRC="$TP/wx-src"
WX_BUILD="$TP/wx-build"
WX_PREFIX="$TP/wx-install"
WX_VERSION="3.3.4"
SCINTILLA_COMMIT="0b90f31ced23241054e8088abb50babe9a44ae67"  # Scintilla 5.0.0
LEXILLA_COMMIT="696ee989b52f718a54869c89576604354f30e153"    # Lexilla 5.5.4

download() {
    local url="$1" out="$2"
    echo "[wx] downloading $(basename "$out")"
    curl -fL --retry 5 --retry-delay 2 -o "$out.part" "$url"
    mv "$out.part" "$out"
}

# Drop a source tree from an older wx version (the previous release used the
# Debian 3.2.11 tree, which ships Scintilla 3.7.2 and lacks IME fixes).
if [[ -L "$WX_SRC" ]]; then
    rm -f "$WX_SRC"
elif [[ -f "$WX_SRC/include/wx/version.h" ]]; then
    WX_MAJOR=$(sed -n 's/^#define wxMAJOR_VERSION  *\([0-9][0-9]*\).*/\1/p' \
        "$WX_SRC/include/wx/version.h")
    WX_MINOR=$(sed -n 's/^#define wxMINOR_VERSION  *\([0-9][0-9]*\).*/\1/p' \
        "$WX_SRC/include/wx/version.h")
    WX_RELEASE=$(sed -n 's/^#define wxRELEASE_NUMBER  *\([0-9][0-9]*\).*/\1/p' \
        "$WX_SRC/include/wx/version.h")
    if [[ "$WX_MAJOR.$WX_MINOR.$WX_RELEASE" != "$WX_VERSION" ]]; then
        echo "[wx] removing stale wx $WX_MAJOR.$WX_MINOR.$WX_RELEASE source at $WX_SRC"
        rm -rf "$WX_SRC"
    fi
fi

if [[ ! -f "$WX_SRC/CMakeLists.txt" ]]; then
    ARCHIVE="$TP/wxWidgets-$WX_VERSION.tar.gz"
    [[ -f "$ARCHIVE" ]] || download \
        "https://codeload.github.com/wxWidgets/wxWidgets/tar.gz/refs/tags/v$WX_VERSION" \
        "$ARCHIVE"
    TMP="$TP/wx-src.extract"
    rm -rf "$TMP" "$WX_SRC"
    mkdir -p "$TMP"
    tar -xzf "$ARCHIVE" -C "$TMP" --strip-components=1
    mv "$TMP" "$WX_SRC"
else
    echo "[wx] reusing $WX_SRC"
fi

ensure_submodule() {
    local path="$1" commit="$2" repo="$3" expected="$4"
    if [[ -f "$path/version.txt" && "$(cat "$path/version.txt")" == "$expected" ]]; then
        return
    fi
    local archive="$TP/$(basename "$repo")-$commit.tar.gz"
    [[ -f "$archive" ]] || download \
        "https://codeload.github.com/wxWidgets/$repo/tar.gz/$commit" "$archive"
    rm -rf "$path"
    mkdir -p "$path"
    tar -xzf "$archive" -C "$path" --strip-components=1
}

ensure_submodule "$WX_SRC/src/stc/scintilla" "$SCINTILLA_COMMIT" scintilla 500
ensure_submodule "$WX_SRC/src/stc/lexilla" "$LEXILLA_COMMIT" lexilla 554

# Rebuild if the previous install/build tree came from another wx release.
for dir in "$WX_BUILD" "$WX_PREFIX"; do
    if [[ -e "$dir" && "$(cat "$dir/.wx-version" 2>/dev/null)" != "$WX_VERSION" ]]; then
        echo "[wx] removing stale build tree at $dir"
        rm -rf "$dir"
    fi
done

mkdir -p "$WX_BUILD" "$WX_PREFIX"

COMMON_FLAGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$WX_PREFIX"
    -DCMAKE_C_COMPILER=gcc
    -DCMAKE_CXX_COMPILER=g++
    -DPKG_CONFIG_EXECUTABLE="$PKG_CONFIG_EXECUTABLE"
    -DCMAKE_PREFIX_PATH="$SYSROOT/usr"
    -DCMAKE_INCLUDE_PATH="$SYSROOT/usr/include"
    "-DCMAKE_LIBRARY_PATH=$SYSROOT/usr/lib/x86_64-linux-gnu;/usr/lib/x86_64-linux-gnu"
    "-DCMAKE_EXE_LINKER_FLAGS=-L$SYSROOT/usr/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu -Wl,-rpath-link,$SYSROOT/usr/lib/x86_64-linux-gnu"
    "-DCMAKE_SHARED_LINKER_FLAGS=-L$SYSROOT/usr/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu"
    "-DCMAKE_MODULE_LINKER_FLAGS=-L$SYSROOT/usr/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu"
)

WX_FLAGS=(
    -DwxBUILD_SHARED=OFF
    -DwxBUILD_MONOLITHIC=OFF
    -DwxBUILD_TOOLKIT=gtk3
    -DwxBUILD_SAMPLES=OFF
    -DwxBUILD_TESTS=OFF
    -DwxBUILD_DEMOS=OFF
    -DwxBUILD_PRECOMP=ON
    -DwxUSE_UNICODE=ON
    -DwxUSE_GUI=ON
    -DwxUSE_STC=ON
    -DwxUSE_CMDLINE_PARSER=ON
    -DwxUSE_WEBVIEW=OFF
    -DwxUSE_MEDIACTRL=OFF
    -DwxUSE_WEBREQUEST=OFF
    -DwxUSE_SPELLCHECK=OFF
    -DwxUSE_OPENGL=OFF
    -DwxUSE_LIBLZMA=OFF
    # Without wxUSE_INTL, wxConvLocal is ISO-8859-1; src/app/main.cpp therefore
    # decodes the command line itself before calling wxEntry().
    -DwxUSE_INTL=OFF
    -DwxUSE_XRC=OFF
    -DwxUSE_RIBBON=OFF
    -DwxUSE_RICHTEXT=OFF
    -DwxUSE_PROPGRID=OFF
    -DwxUSE_AUI=OFF
    -DwxUSE_HTML=OFF
    -DwxUSE_WXHTML_HELP=OFF
    -DwxUSE_LIBJPEG=OFF
    -DwxUSE_LIBTIFF=OFF
    -DwxUSE_LIBWEBP=OFF
    -DwxUSE_NANOSVG=OFF
    -DwxUSE_LUNASVG=OFF
    -DwxUSE_REGEX=sys
    -DwxUSE_LIBPNG=sys
    -DwxUSE_ZLIB=sys
    -DwxUSE_EXPAT=sys
    -DwxUSE_SECRETSTORE=OFF
    -DwxUSE_NOTIFICATION_MESSAGE=OFF
)

"$CMAKE" -S "$WX_SRC" -B "$WX_BUILD" -G Ninja \
    "${COMMON_FLAGS[@]}" "${WX_FLAGS[@]}"
"$NINJA" -C "$WX_BUILD" install
printf '%s\n' "$WX_VERSION" > "$WX_PREFIX/.wx-version"

if [[ ! -x "$WX_PREFIX/bin/wx-config" ]]; then
    echo "[wx] ERROR: wx-config was not installed" >&2
    exit 1
fi

echo "[wx] version: $("$WX_PREFIX/bin/wx-config" --version)"
echo "[wx] Scintilla: $(cat "$WX_SRC/src/stc/scintilla/version.txt" 2>/dev/null || echo '?')"
echo "[wx] Lexilla:   $(cat "$WX_SRC/src/stc/lexilla/version.txt" 2>/dev/null || echo '?')"
