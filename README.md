# txt-editor

A small, fast, native plain-text editor for Linux/KDE, built with **wxWidgets 3.3.4 + wxSTC**
(bundled Scintilla). Linux + Wayland is the primary target; X11 works; Windows is a planned
second target from the same codebase.

> Status: M0 skeleton. The focus is correctness, startup latency, large-file behaviour and
> native CJK input, not feature count.

## Features (M0)

- Tabs with per-tab close buttons, new/open/save/save-as, and a three-way
  Save / Don't Save / Cancel prompt for unsaved changes.
- Session restore across restarts: tab order, active tab, view state, and unsaved recovery buffers.
- Line numbers, current-line highlight, brace matching, folding (normal files only).
- UTF-8, UTF-8 BOM, UTF-16LE/BE and Latin-1 detection; original encoding preserved on save.
- LF / CRLF / CR detection and preservation.
- Syntax highlighting for Python, C/C++, JSON, Markdown, shell, XML, YAML and plain text.
- Find / replace with match-case and whole-word options.
- Large-file mode above 32 MiB: no lexer, no folding, no undo collection.
- Dark/light editor palette, zoom.
- AppImage and `.deb` packaging scripts.

Not in M0: plugins, LSP, split view, minimap, telemetry, multi-cursor.

Session recovery data is local only, stored with owner-only permissions under
`$XDG_STATE_HOME/txt-editor/session` (falling back to
`~/.local/state/txt-editor/session`). It is never sent anywhere. Delete that directory to
discard the saved session; the editor opens a blank tab when no session exists.

## Quick start (no root)

The host only needs `g++`, `git`, `curl`, `unzip`, `dpkg-deb`. Everything else is bootstrapped
into `third_party/`:

```bash
./scripts/bootstrap-local-deps.sh   # CMake, Ninja, pkgconf, GTK3 dev + runtime (user space)
./scripts/build-wx.sh               # wxWidgets 3.3.4 static with wxSTC
./scripts/build.sh                  # app -> build/txt-editor
./scripts/test.sh
./build/txt-editor                  # or: ./build/txt-editor some-file.txt
```

Packaging:

```bash
./scripts/package.sh                # AppImage + .deb + tarball in dist/
```

The AppImage contains the application only. **GTK3 is a host runtime dependency** (present on
KDE Plasma desktops that use GTK-based apps). See [docs/BUILD.md](docs/BUILD.md).

## Keyboard shortcuts

| Action | Shortcut |
|---|---|
| New tab | `Ctrl+N` |
| Open | `Ctrl+O` |
| Save | `Ctrl+S` |
| Save as | `Ctrl+Shift+S` |
| Close tab | `Ctrl+W`, tab `×`, middle-click, or `Ctrl`+click |
| Quit | `Ctrl+Q` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Find | `Ctrl+F` |
| Replace | `Ctrl+H` |
| Find next / previous | `F3` / `Shift+F3` |
| Zoom in / out / reset | `Ctrl++` / `Ctrl+-` / `Ctrl+0` |
| Toggle editor palette | `F11` |

## Layout

```
src/app/      wxApp, command line, profiling marks
src/ui/       MainFrame, TabBook, SaveChangesDialog, FindPanel, menus
src/editor/   EditorWidget: wxStyledTextCtrl adapter, lexer/theme
src/core/     pure C++20: encoding, EOL, language detection, file I/O
tests/        framework-free core tests
scripts/      bootstrap, build, test, package, benchmark
docs/         architecture, build, performance
.cline/rules/ project rules for AI-assisted development
```

Design details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
Build details: [docs/BUILD.md](docs/BUILD.md).
Performance and IME checklist: [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Licence

MIT.
