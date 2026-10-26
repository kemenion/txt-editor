# Project rules: what txt-editor is and must stay

Native Linux/KDE plain-text editor. wxWidgets 3.3.4 (static) + wxSTC with vendored Scintilla
5.0.0 / Lexilla 5.5.4. C++20, GTK3 linked dynamically. Wayland first, X11 supported, Windows is a
planned second target. Status: **M0 skeleton** — correctness, startup latency, large-file
survivability and native CJK input matter more than feature count.

## Non-negotiable product gates

- **Startup**: no config scan, no plugin loading, no file read before first paint.
- **Large files**: one buffer. Above 32 MiB (`txt::kLargeFileThreshold`) no lexer, no folding, no
  undo collection. 100 MB must open in ≤1.5 s (see `docs/PERFORMANCE.md`).
- **IME**: fcitx5 must work on Wayland *and* X11 with inline pre-edit and a candidate window that
  follows the caret. This is a hard gate; see `20-wx-ime-and-scintilla.md`.
- **Save safety**: a failed save must leave the original file untouched (temp file, flush,
  `rename`). Never truncate in place.

If a change would trade away one of these gates, stop and ask instead of "optimising" around it.

## Layers (dependencies point down only)

- `src/core/` — encoding, EOL, language detection, diff, session format, file I/O.
  Must not: include wxWidgets, touch the GUI, throw, use POSIX-only APIs.
- `src/editor/` — `EditorWidget`, the wxSTC adapter and lexer/palette setup.
  Must not: know about menus, paths, tabs or dialogs.
- `src/ui/` — `MainFrame`, `TabBook`, `FindPanel`, `CompareFrame`, `DiffPane`,
  `SaveChangesDialog`. Must not: call Scintilla directly (go through `EditorWidget`).
- `src/app/` — `wxApp`, command line, profiling marks. Must not: contain feature logic.
- `tests/` — framework-free core tests. Must not: depend on wxWidgets or open windows.
- `scripts/`, `docs/`, `packaging/` — tooling only. Must not: contain compiled output.

`src/core/` has **zero** wxWidgets includes and is tested without a GUI. `EditorWidget` is the
single place that calls Scintilla APIs; every new Scintilla call belongs there, not in `ui/`.

## Data-flow invariants

- Text inside the application is always UTF-8 `std::string`. `wxString` never enters `core/`.
  Convert only at the boundary with `wxString::FromUTF8(...)` and `.utf8_string()`.
- Load: `Document::Load` decodes once → `EditorWidget::LoadText` uses `SetTextRaw` (no copy).
  Save: `EditorWidget::GetUtf8` uses `GetTextRaw` → `Document::Save` encodes once.
- Never rewrite a file's encoding, EOL style or BOM on save unless the user changed it.
- Session state: `$XDG_STATE_HOME/txt-editor/session` (fallback
  `~/.local/state/txt-editor/session`), directory `0700`, snapshot files `0600`, lazy tab loading,
  no snapshot for clean named tabs.
- No background I/O in M0. If profiling forces a worker, deliver via `CallAfter` and keep `core/`
  thread-agnostic — and ask first.

## Scope guard

- Do not add plugins, LSP, split view, minimap, multi-cursor, telemetry, a second GUI toolkit or
  new third-party dependencies.
- Do not replace wxWidgets/Scintilla, do not move to wx 3.2.x, do not enable wx modules that
  `scripts/build-wx.sh` deliberately disables (WebView, media, HTML, AUI, propgrid, ribbon…).
- Keep the Windows port possible: `core/` must not need `#ifdef _WIN32`.

## Where the truth lives

`README.md` (scope, shortcuts) · `docs/ARCHITECTURE.md` (design, trade-offs) · `docs/BUILD.md`
(toolchain, packaging, troubleshooting) · `docs/PERFORMANCE.md` (budgets, manual checklists,
measured numbers). When code and docs disagree, the docs describe intent: reconcile both in the
same change instead of leaving a contradiction behind.
