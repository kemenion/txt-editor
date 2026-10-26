# txt-editor

A small, fast, native plain-text editor for Linux and KDE, built on **wxWidgets 3.3.4** with
**wxSTC** (bundled Scintilla 5.0.0 / Lexilla 5.5.4). Wayland is the primary target, X11 is
supported, and Windows is a planned second target from the same codebase.

> **Status: M0 skeleton.** Priorities are correctness, startup latency, large-file survivability
> and native CJK input, not feature count.

## Features

- **Editing** — tabs with per-tab close buttons, new/open/save/save as, recent files, undo and
  redo, find and replace with match-case and whole-word options, word wrap, zoom.
- **Navigation** — line numbers, current-line highlight, brace matching, code folding.
- **Highlighting** — Python, C/C++, JSON, Markdown, shell, XML, YAML and plain text.
- **Encodings** — UTF-8, UTF-8 BOM, UTF-16LE/BE and Latin-1; the detected encoding is preserved
  on save.
- **Line endings** — LF, CRLF and CR; detected on load and never rewritten on save.
- **Safe saves** — text goes to a temporary file that is renamed into place, so a failed save
  leaves the original intact. A file that changed on disk since it was loaded is replaced only
  after the user chooses to reload or to overwrite it.
- **Large files** — above 32 MiB the lexer, folding and undo collection are disabled so that a
  100 MB file stays editable.
- **Comparison** — a separate read-only window with two panes side by side, or a grid for more
  tabs, line-level change highlighting and synchronised scrolling.
- **Sessions** — tab order, active tab, view state and unsaved recovery buffers survive a
  restart; several windows can run at once and their sessions are merged on the next start.

Not in M0: plugins, LSP, split view, minimap, multi-cursor, telemetry.

## Session data

Recovery data is local only and owner-only: `0700` directories and `0600` files below
`$XDG_STATE_HOME/txt-editor/session`, falling back to `~/.local/state/txt-editor/session`. Each
window writes into its own `session/instances/<id>/`, so concurrent windows never overwrite or
prune each other's buffers; the next start merges the sessions of windows that are gone. Delete the
session directory to discard the saved state — the editor then opens a single blank tab.

## Quick start

The host needs only `g++`, `git`, `curl`, `unzip` and `dpkg-deb`. Everything else is bootstrapped
into `third_party/` (gitignored, about 1.4 GB including the wx build tree; delete it and re-run the
scripts to rebuild):

```bash
./scripts/bootstrap-local-deps.sh   # CMake, Ninja, pkgconf, GTK3 — user space, no root
./scripts/build-wx.sh               # wxWidgets 3.3.4, static, with wxSTC
./scripts/build.sh                  # application -> build/txt-editor
./scripts/test.sh                   # core tests
./build/txt-editor [file]
```

Packaging: `./scripts/package.sh` writes an AppImage, a `.deb` and a portable tarball to `dist/`.
The AppImage bundles the application only; **GTK3 is a host runtime dependency**. See
[docs/BUILD.md](docs/BUILD.md).

## Keyboard shortcuts

| Action | Shortcut |
|---|---|
| New / open / save / save as | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` |
| Close tab | `Ctrl+W`, tab `×`, middle-click, or `Ctrl`+click |
| Quit | `Ctrl+Q` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Find / replace | `Ctrl+F` / `Ctrl+H` |
| Find next / previous | `F3` / `Shift+F3` |
| Compare tabs | `Ctrl+Shift+C` |
| Refresh comparison | `F5` in the comparison window |
| Zoom in / out / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| Word wrap | `Alt+Z` |
| Dark / light palette | `F11` |

## Project layout

```
src/app/      wxApp, command line, profiling marks
src/ui/       MainFrame, TabBook, FindPanel, CompareFrame, DiffPane, SaveChangesDialog
src/editor/   EditorWidget: wxStyledTextCtrl adapter, lexers, palette
src/core/     pure C++20, no wxWidgets: encoding, EOL, language, diff, session, file I/O
tests/        framework-free core tests
scripts/      bootstrap, build, test, package, benchmark
patches/      wxWidgets patches applied by scripts/build-wx.sh
docs/         architecture, build, performance
.cline/rules/ project rules for AI-assisted development
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — design, layering, trade-offs
- [docs/BUILD.md](docs/BUILD.md) — toolchain, packaging, troubleshooting
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — budgets, manual checklists, measured numbers

## Licence

MIT.
