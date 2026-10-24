#!/usr/bin/env bash
# Produces: AppImage (when runtime is available), .deb and a portable tarball.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="$ROOT/third_party"
# shellcheck disable=SC1091
source "$TP/env.sh"

VERSION="0.1.0"
ARCH="x86_64"
DIST="$ROOT/dist"
BIN="$ROOT/build/txt-editor"
APPDIR="$DIST/AppDir"
DEBROOT="$DIST/deb-root"

[[ -x "$BIN" ]] || { echo "[package] missing $BIN; run scripts/build.sh" >&2; exit 1; }

rm -rf "$APPDIR" "$DEBROOT"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
         "$DEBROOT/DEBIAN" \
         "$DEBROOT/usr/bin" \
         "$DEBROOT/usr/share/applications" \
         "$DEBROOT/usr/share/icons/hicolor/scalable/apps" \
         "$DEBROOT/usr/share/doc/txt-editor"

# The binary is statically linked against wxWidgets/Scintilla; only GTK3 and
# the C/C++ runtime remain dynamic. Strip it for a small, fast download.
if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$BIN" -o "$APPDIR/usr/bin/txt-editor"
else
    cp "$BIN" "$APPDIR/usr/bin/txt-editor"
fi
chmod 755 "$APPDIR/usr/bin/txt-editor"

cp "$ROOT/packaging/txt-editor.desktop" "$APPDIR/"
cp "$ROOT/packaging/txt-editor.svg" "$APPDIR/"
cp "$ROOT/packaging/txt-editor.desktop" "$APPDIR/usr/share/applications/"
cp "$ROOT/packaging/txt-editor.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"
ln -sf txt-editor.svg "$APPDIR/.DirIcon"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib:$HERE/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/usr/bin/txt-editor" "$@"
EOF
chmod 755 "$APPDIR/AppRun"

# --- Debian package ----------------------------------------------------------
cp "$APPDIR/usr/bin/txt-editor" "$DEBROOT/usr/bin/"
cp "$ROOT/packaging/txt-editor.desktop" "$DEBROOT/usr/share/applications/"
cp "$ROOT/packaging/txt-editor.svg" "$DEBROOT/usr/share/icons/hicolor/scalable/apps/"
cp "$ROOT/packaging/deb/DEBIAN/control" "$DEBROOT/DEBIAN/control"
cp "$ROOT/README.md" "$DEBROOT/usr/share/doc/txt-editor/"
dpkg-deb --build --root-owner-group "$DEBROOT" \
    "$DIST/txt-editor_${VERSION}_amd64.deb" >/dev/null

# --- portable tarball --------------------------------------------------------
tar -C "$APPDIR" -czf "$DIST/txt-editor-${VERSION}-linux-${ARCH}.tar.gz" .

# --- AppImage (Type 2: runtime + squashfs, no appimagetool required) ---------
RUNTIME="$TP/runtime-x86_64"
MKSQUASHFS="$SYSROOT/usr/bin/mksquashfs"
if [[ -s "$RUNTIME" && -x "$MKSQUASHFS" ]]; then
    echo "[package] building AppImage"
    if ! "$MKSQUASHFS" "$APPDIR" "$DIST/txt-editor.squashfs" \
            -root-owned -noappend -no-progress -comp zstd >/dev/null 2>&1; then
        "$MKSQUASHFS" "$APPDIR" "$DIST/txt-editor.squashfs" \
            -root-owned -noappend -no-progress -comp xz >/dev/null
    fi
    APPIMAGE="$DIST/txt-editor-${VERSION}-${ARCH}.AppImage"
    APPIMAGE_TMP="$DIST/.txt-editor-${VERSION}-${ARCH}.AppImage.tmp"
    cat "$RUNTIME" "$DIST/txt-editor.squashfs" > "$APPIMAGE_TMP"
    chmod 755 "$APPIMAGE_TMP"
    # Replace atomically: an older AppImage may still be running and keep the
    # previous file open, which makes in-place writes fail with ETXTBSY.
    mv -f "$APPIMAGE_TMP" "$APPIMAGE"
    rm -f "$DIST/txt-editor.squashfs"
else
    echo "[package] AppImage skipped: missing runtime-x86_64 or mksquashfs" >&2
fi

echo "[package] artifacts in $DIST"
ls -lh "$DIST"/*.deb "$DIST"/*.tar.gz "$DIST"/*.AppImage 2>/dev/null || true
