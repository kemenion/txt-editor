# Build and packaging

The host does **not** need root, CMake, Ninja, GTK3 headers, or wxWidgets. The scripts bootstrap
everything into `third_party/` (ignored by git).

## 0. Host requirements

- Debian 13 / Ubuntu 24.04+ x86_64 (or equivalent), glibc.
- `g++` >= 13 with C++20, `git`, `curl`, `unzip`, `dpkg-deb`, `tar`, `xz`.
- Network access to GitHub and the Debian mirror.
- For running: GTK3 runtime (`libgtk-3-0t64` or `libgtk-3-0`), fontconfig, a monospace font.

`third_party/` is a local cache: about 1.4 GB once wx is built. It is gitignored and can be
deleted at any time; re-run §1 and §2 to recreate it. Only `scripts/`, `patches/` and the
project sources are versioned.

## 1. Bootstrap (once)

```bash
./scripts/bootstrap-local-deps.sh
```

What it does:

1. Downloads Debian `.deb` packages for **CMake**, **Ninja**, `pkgconf`, the GTK3 development
   dependency closure and the matching GTK3 runtime libraries into `third_party/debs/`.
2. Extracts every package into `third_party/sysroot/` with `dpkg-deb -x` (no system writes), so
   the tools end up in `third_party/sysroot/usr/bin/`.
3. Writes `third_party/env.sh`, which exports the absolute paths of the sysroot, the wx install
   prefix and those tools.
4. Verifies `third_party/sysroot/usr/bin/cmake`, `.../ninja` and `.../pkg-config`.

Important: the sysroot contains headers, `.pc` files, development symlinks **and** the runtime
`.so.0` files. The runtime files make the development symlinks resolvable during static-wx linking;
the resulting application still uses the host's GTK3 at run time.

If your distribution mirror differs, set `APT_MIRROR` or edit the package list in the script.
The script is idempotent; re-run after changing the package list.

## 2. Build wxWidgets 3.3.4 (once)

```bash
./scripts/build-wx.sh
```

- Downloads the pinned wxWidgets 3.3.4 source tarball from GitHub and extracts it to
  `third_party/wx-src/`.
- Downloads the exact Scintilla and Lexilla submodule commits used by wx 3.3.4
  (Scintilla 5.0.0 / Lexilla 5.5.4) and unpacks them under `third_party/wx-src/src/stc/`.
  The commits are pinned in the script; no `git clone` is required.
- Applies the local patches from `patches/` to `third_party/wx-src/` with `git apply`. The step is
  idempotent (a patch counts as applied when it reverses cleanly) and aborts the build when a patch
  no longer applies, so a broken patch cannot silently produce an unpatched wx.
- Removes `third_party/wx-build/` and `third_party/wx-install/` automatically when they were
  produced by a different wx release.
- Configures **static**, non-monolithic wx with GTK3 and wxSTC:

```text
-DwxBUILD_SHARED=OFF
-DwxBUILD_MONOLITHIC=OFF
-DwxBUILD_TOOLKIT=gtk3
-DwxBUILD_SAMPLES=OFF -DwxBUILD_TESTS=OFF -DwxBUILD_DEMOS=OFF
-DwxUSE_STC=ON
-DwxUSE_WEBVIEW=OFF -DwxUSE_MEDIACTRL=OFF -DwxUSE_WEBREQUEST=OFF
-DwxUSE_SPELLCHECK=OFF -DwxUSE_OPENGL=OFF -DwxUSE_LIBLZMA=OFF
-DwxUSE_INTL=OFF -DwxUSE_XRC=OFF -DwxUSE_RIBBON=OFF -DwxUSE_RICHTEXT=OFF
-DwxUSE_PROPGRID=OFF -DwxUSE_AUI=OFF -DwxUSE_HTML=OFF
-DwxUSE_LIBJPEG=OFF -DwxUSE_LIBTIFF=OFF -DwxUSE_LIBWEBP=OFF
-DwxUSE_NANOSVG=OFF -DwxUSE_LUNASVG=OFF -DwxUSE_REGEX=sys
-DwxUSE_LIBPNG=sys -DwxUSE_ZLIB=sys -DwxUSE_EXPAT=sys
```

- Installs to `third_party/wx-install/`.
- Uses `PKG_CONFIG_SYSROOT_DIR`, `PKG_CONFIG_PATH`, `CPATH`, `LIBRARY_PATH` so GTK3 resolves
  from the sysroot.
- Expected: a few minutes on a modern machine, ~2 GB of build artifacts.

Verification:

```bash
third_party/wx-install/bin/wx-config --version
# 3.3.4
third_party/wx-install/bin/wx-config --selected-config
# gtk3-unicode-static-3.3
cat third_party/wx-src/src/stc/scintilla/version.txt   # 500
cat third_party/wx-src/src/stc/lexilla/version.txt     # 554
grep -c 'const int caretWidth' third_party/wx-src/src/stc/ScintillaWX.cpp   # 1
grep -c 'wxMax(rect.width, 1)' third_party/wx-src/src/gtk/window.cpp        # 1
```

### 2.1 Local patches

| Patch | Touches | Why |
|---|---|---|
| `patches/wx-3.3.4-ime-caret-rect.patch` | `src/stc/ScintillaWX.cpp`, `src/gtk/window.cpp` | Keeps the IME caret rectangle non-empty so the input method can place its candidate window. |

`ScintillaWX::GetIMEContextRect()` described the caret with a zero-width rectangle, and
`wxWindowGTK::GTKUpdateIMCursorRect()` dropped every empty rectangle.
`gtk_im_context_set_cursor_location()` was therefore never called: inline pre-edit still
worked, but fcitx5 painted the candidate window at its default position. The patch sends a 1 px
wide caret rectangle from wxSTC and treats only a missing height as "no position" in wxGTK.

Patches are globbed as `patches/wx-$WX_VERSION-*.patch`, so a new file must start with
`wx-3.3.4-`. They only touch the wx layer (`src/stc`, `src/gtk`, `include/wx`), never the vendored
`src/stc/scintilla` or `src/stc/lexilla` trees. After editing a patch, re-run
`./scripts/build-wx.sh`. To iterate faster, re-apply the patch and rebuild just the affected
objects with

```bash
source third_party/env.sh && "$NINJA" -C third_party/wx-build install
```

then `./scripts/build.sh` to relink the application.

## 3. Build the application

```bash
./scripts/build.sh                 # Release
BUILD_TYPE=Debug ./scripts/build.sh
TXT_EDITOR_NATIVE=ON ./scripts/build.sh   # local benchmark only
```

Manual equivalent:

```bash
SYSROOT=$PWD/third_party/sysroot
WX=$PWD/third_party/wx-install
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu/pkgconfig:$SYSROOT/usr/share/pkgconfig"
export LD_LIBRARY_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$SYSROOT/usr/bin/cmake" -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$WX;$SYSROOT/usr" \
  -DCMAKE_INCLUDE_PATH="$SYSROOT/usr/include" \
  -DCMAKE_LIBRARY_PATH="$SYSROOT/usr/lib/x86_64-linux-gnu;/usr/lib/x86_64-linux-gnu" \
  -DCMAKE_EXE_LINKER_FLAGS="-L$SYSROOT/usr/lib/x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu -Wl,-rpath-link,$SYSROOT/usr/lib/x86_64-linux-gnu" \
  -DPKG_CONFIG_EXECUTABLE="$SYSROOT/usr/bin/pkgconf"

"$SYSROOT/usr/bin/ninja" -C build
```

`wx-config --static` emits absolute paths for its dependencies, including the GTK3 `.a`
files inside the sysroot. `CMakeLists.txt` rewrites the non-wx absolute archives to
`-l<name>` so only wxWidgets is linked statically and GTK3 stays dynamic. It also sets
`CMAKE_SKIP_RPATH`, so no build-machine paths end up in the packaged binary.

Run:

```bash
./build/txt-editor
./build/txt-editor README.md
TXT_EDITOR_PROFILE=1 ./build/txt-editor README.md
```

## 4. Tests

```bash
./scripts/test.sh
```

Uses CTest and a framework-free `tests/test_core.cpp`. Tests cover encoding detection,
UTF-16 conversion, EOL detection, language detection and large-file detection. GUI behaviour is
verified manually with the checklist in `docs/PERFORMANCE.md`.

## 5. Packaging

```bash
./scripts/package.sh
```

Produces all three artifacts in `dist/`:

- `txt-editor-0.1.0-x86_64.AppImage` — Type 2 AppImage built from the appimagetool runtime
  plus a zstd squashfs (falls back to xz), with no FUSE requirement for creation.
- `txt-editor_0.1.0_amd64.deb` — installs `/usr/bin/txt-editor`, desktop file and scalable icon.
- `txt-editor-0.1.0-linux-x86_64.tar.gz` — portable AppDir-style tarball.

GTK3 is **not** bundled; it remains a host runtime dependency.

```bash
chmod +x dist/txt-editor-0.1.0-x86_64.AppImage
./dist/txt-editor-0.1.0-x86_64.AppImage README.md
```

The `.deb` declares `libgtk-3-0t64 | libgtk-3-0`, plus the GTK stack and C/C++ runtime
dependencies.

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `wx-config: command not found` | Run `scripts/build-wx.sh`; pass `CMAKE_PREFIX_PATH`. |
| `Could not find wxWidgets` | The wx install prefix is wrong, or wx was built shared. Check `third_party/wx-install/lib/cmake/wxWidgets-3.3/wxWidgetsConfig.cmake` (may be under `lib64`). |
| `gtk+-3.0.pc not found` | `PKG_CONFIG_PATH`/`PKG_CONFIG_SYSROOT_DIR` not exported; re-run the script, not a bare cmake command. |
| Linker cannot find `libgtk-3.so.0` | Add `/usr/lib/x86_64-linux-gnu` to `CMAKE_LIBRARY_PATH` and linker flags. |
| AppImage build says `mksquashfs not found` | The script extracts `squashfs-tools` into the sysroot; ensure `PATH` includes it, or install `squashfs-tools` on the host. |
| Chinese IME does not show preedit | Verify `GTK_IM_MODULE=fcitx`, `fcitx5-frontend-gtk3`, and test with `GDK_BACKEND=wayland` and `x11`. See `docs/PERFORMANCE.md`. |
| Candidate window sits in a corner, away from the caret | The wx caret-rectangle patch is missing or wx was built before it landed. Check the two `grep -c` lines in §2, re-run `scripts/build-wx.sh`, then `scripts/build.sh`. |
| Blank window on Wayland | Try `GDK_BACKEND=x11` to isolate a compositor/wxGTK issue; report both results. |
