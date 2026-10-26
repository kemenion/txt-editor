# wxWidgets, wxSTC and IME (hard gate)

wxWidgets is **pinned to 3.3.4**, built static with wxSTC into `third_party/wx-install`, from
`third_party/wx-src` (gitignored). Scintilla 5.0.0 / Lexilla 5.5.4 are vendored inside wx. Never
switch to the system wx, never bump the pin, never enable wx modules that `scripts/build-wx.sh`
turns off — ask first.

## How text input actually works in this stack

wxSTC does **not** use Scintilla's own GTK backend (`src/stc/scintilla/gtk/ScintillaGTK.cxx`).
`wxStyledTextCtrl` owns a `ScintillaWX` platform shim (`src/stc/ScintillaWX.cpp`), and wx 3.3
routes GTK's `GtkIMContext` through the generic `wxTextInputClient` interface:

- `wxStyledTextCtrl` sets `SetIMEInteraction(wxSTC_IME_INLINE)` in its constructor
  (`src/stc/stc.cpp:219`). Inline composition is the intended behaviour — do not override it,
  do not use `wxSTC_IME_WINDOWED`.
- `ScintillaWX::IsTextInputEnabled()` = `imeInteraction == imeInline && pdoc->IsCollectingUndo()`
  (`src/stc/ScintillaWX.h:198`). Enabling inline pre-edit for a window is
  `gtk_im_context_set_use_preedit()` in `wxUpdateTextInputClient()` (`src/gtk/window.cpp:1592`).
- Pre-edit text is written through `StartComposition` / `UpdateComposition` / `CommitComposition`
  and rolled back with `UndoCompositionText`. Composition needs a **writable document with undo
  collection**; otherwise `StartComposition()` returns false and fcitx5 falls back to its own
  floating window (this is what happens above 32 MiB and in read-only `DiffPane`s).

## The candidate-window anchor (the classic "IME popup in the wrong place" bug)

```
ScintillaWX::GetIMEContextRect()            // caret rect in client pixels
  ├─ ScintillaWX::UpdateSystemCaret()        // caret moved, not composing (ScintillaWX.cpp:716)
  └─ gtk_wxwindow_preedit_changed_cb()       // every pre-edit update (gtk/window.cpp:1685)
      → wxWindowBase::UpdateInputMethodCursorRect()   // shared static state
          → wxWindowGTK::DoUpdateInputMethodCursorRect()
              → wxWindowGTK::GTKUpdateIMCursorRect()  // gtk/window.cpp:1582
                  → gtk_im_context_set_cursor_location()
```

Rules that follow from this code:

- `wxRect::IsEmpty()` is `width <= 0 || height <= 0`, and stock `GTKUpdateIMCursorRect()` returned
  early on an empty rect. **Every rect handed to the IM must have `width >= 1` and `height >= 1`.**
  A caret is a line, not a box, so a zero-width rect is a legitimate way to describe it; rejecting
  it inside the GTK layer is the wrong fix. wx 3.3.4 ships both sides broken — width 0 in
  `ScintillaWX::GetIMEContextRect()` and the early return in the GTK layer — and
  `patches/wx-3.3.4-ime-caret-rect.patch` fixes both.
- Both call sites go through `GetIMEContextRect()`; fix the rect **there**, not in the app.
- `ms_imeCursorRect` is shared static state between all wx windows and is overwritten by every
  wxSTC caret move. Never try to patch the position from `src/` (`UpdateInputMethodCursorRect()` is
  public, but the next internal wxSTC call resets it to the broken rect). Prefer the wx fix.
- Symptoms: inline pre-edit at the caret + candidate window elsewhere ⇒ anchor bug (this code).
  No pre-edit at all ⇒ text-input/composition problem (undo collection, read-only, IM module).

## Changing wx: patch file or nothing

`third_party/` is gitignored and `scripts/build-wx.sh` re-downloads and re-extracts wx when the
pinned version changes or the tree is missing, so **a hand edit in `third_party/wx-src` does not
exist** for anyone else and will vanish on the next bootstrap.

1. Add `patches/wx-<version>-<topic>.patch`; `scripts/build-wx.sh` globs
   `patches/wx-$WX_VERSION-*.patch`, so the `wx-` prefix and the exact version are required.
   Generate it with `diff -u` from inside `third_party/wx-src`, using `a/` and `b/` path prefixes so
   it applies with `-p1` there.
2. `scripts/build-wx.sh` applies every matching patch with `git apply` after extraction and before
   configuring. Idempotency is a reverse check: `git apply --reverse --check` succeeding means the
   patch is already in the tree. A patch that no longer applies aborts the build instead of leaving
   a silently unpatched wx.
3. Document it in `docs/BUILD.md` §2 and mention it in `docs/ARCHITECTURE.md` if it changes a
   trade-off.
4. Patch only the wx layer (`src/stc/ScintillaWX.cpp`, `src/gtk/window.cpp`,
   `src/common/wincmn.cpp`, `include/wx/...`). Never patch vendored `scintilla/` or `lexilla/`
   sources — those are separate upstream projects pinned by commit.

Rebuild after touching wx (incremental, no re-bootstrap):

```bash
source third_party/env.sh
"$NINJA" -C third_party/wx-build install     # recompiles the changed object + archives
./scripts/build.sh
```

Confirm the app really uses the rebuilt static libs:

```bash
nm -C third_party/wx-install/lib/libwx_gtk3u_stc-3.3.a | grep GetIMEContextRect
ldd build/txt-editor | grep gtk-3          # GTK3 must stay shared
stat -c '%y %n' third_party/wx-install/lib/libwx_gtk3u_stc-3.3.a build/txt-editor
```

The archive timestamp change makes Ninja relink the app. If the second timestamp is older, the
install prefix is not the one CMake used — re-run `./scripts/build.sh` and check the configure
output, do not paper over it by deleting `build/` blindly.

## IME verification matrix (do not claim a fix without this)

```bash
GTK_IM_MODULE=fcitx GDK_BACKEND=wayland ./build/txt-editor /tmp/ime-test.txt
GTK_IM_MODULE=fcitx GDK_BACKEND=x11     ./build/txt-editor /tmp/ime-test.txt
./build/txt-editor /tmp/ime-test.txt    # GTK's own Wayland text-input-v3 path
```

Cases (also listed in `docs/PERFORMANCE.md`): caret at line start / middle / end; on a wrapped
line; after scrolling a long file; at the first and last visible line; after zoom; after
switching tabs mid-composition; with the window near a screen edge; type → arrow keys →
Backspace → Enter → Escape during composition; undo after commit; large file (>32 MiB) and
read-only compare pane.
Useful probes: `gdb -p $(pgrep -f build/txt-editor)` and break on
`gtk_im_context_set_cursor_location` (it must be hit with `width >= 1`), or
`WAYLAND_DEBUG=1` to see `set_cursor_rectangle`.
