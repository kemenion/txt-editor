# Architecture

## 1. Goals and constraints

`txt-editor` is a native Linux/KDE text editor with a deliberately small feature surface.
The architecture optimises for:

1. **Cold-start latency** — no config scan, no plugin loading, no file read before first paint.
2. **Large-file survivability** — one buffer, no lexer/fold/undo above 32 MiB.
3. **Native input** — CJK IME (fcitx5) works on Wayland and X11; no custom key filtering.
4. **Portability** — the same core compiles on Windows; wxSTC isolates Scintilla.

wxWidgets 3.3.4 is pinned because it vendors Scintilla 5.0.0 and Lexilla 5.5.4, and includes
the October 2026 wxGTK IME pre-edit fix. wx 3.2.x is explicitly unsupported. 3.3.4 still hands
wxSTC's caret rectangle to GTK with a zero width, which wxGTK discards, so `patches/` carries
`wx-3.3.4-ime-caret-rect.patch`; `scripts/build-wx.sh` applies it.

## 2. Layers

```
┌──────────────────────────────────────────────────────────────┐
│ app/       wxApp, command line, process lifetime             │
├──────────────────────────────────────────────────────────────┤
│ ui/        MainFrame · FindPanel · CompareFrame · menus      │
├──────────────────────────────────────────────────────────────┤
│ editor/    EditorWidget (wxStyledTextCtrl adapter)           │
├──────────────────────────────────────────────────────────────┤
│ core/      encoding · EOL · language · diff · file I/O       │
└──────────────────────────────────────────────────────────────┘
```

Dependencies point downward. `core/` has **zero wxWidgets includes** and is tested without a GUI.
`editor/` knows Scintilla but not menus or paths. `ui/` owns document lifecycle and prompts.
`app/` is wiring only.

This split matters because the Windows port will keep `core/` and swap `editor/` for a different
Scintilla host if needed; the wxSTC adapter can remain identical.

## 3. Core model

### 3.1 Encoding

`core/Encoding` exposes:

```cpp
Encoding DetectEncoding(std::string_view bytes);
std::string ToUtf8(std::string_view bytes, Encoding encoding, bool* ok);
std::string FromUtf8(std::string_view utf8, Encoding encoding);
```

Detection order:

1. UTF-8 BOM `EF BB BF`.
2. UTF-16 BOM `FF FE` / `FE FF`.
3. Valid UTF-8 (including ASCII).
4. Latin-1 fallback.

UTF-16 conversion handles surrogate pairs. Latin-1 save replaces code points above U+00FF with
`?`; this is documented and surfaced only for legacy files. The MVP does not guess UTF-16 without
a BOM.

### 3.2 EOL

`DetectEol` counts CRLF, lone LF and lone CR, and picks the dominant form (LF on a tie).
The original mode is stored in `DocumentMeta` and selected in Scintilla with `SetEOLMode`.
Save writes the buffer back without rewriting line endings. New lines follow the selected mode.

### 3.3 Language

`DetectLanguage(path, text)` maps extensions and shebangs to a small enum:
`Plain, Python, C, Cpp, Json, Markdown, Shell, Xml, Yaml`.
`EditorWidget::ApplyLanguage` maps the enum to a wxSTC lexer and keyword set. The core never sees
Scintilla constants.

### 3.4 Document I/O

```cpp
struct LoadResult {
    bool ok = false;
    std::string utf8;
    DocumentMeta meta;
    std::string error;
};

LoadResult Load(const std::string& path);
bool Save(const std::string& path, std::string_view utf8,
          const DocumentMeta& meta, std::string* error);
```

- Read once with `std::ifstream` into a `std::string`.
- Decode once to UTF-8; detect EOL on the decoded text; detect language.
- `largeFile` is `bytes.size() > 32 MiB`.
- Save encodes once; writes a sibling `*.tmp-<pid>` file; flushes; copies permissions; renames
  over the target. A failed save leaves the original untouched.
- No exceptions cross the core boundary.

### 3.5 Session persistence

`core/Session.*` is wx-free and owns the on-disk recovery format. The UI only supplies a
`SessionInfo` of `SessionTabInfo` entries.

Location: `$XDG_STATE_HOME/txt-editor/session`, or
`~/.local/state/txt-editor/session`; directory mode `0700`, snapshot files `0600`.

Files:

- `manifest` — a small text file: `txt-editor-session 1`, `active <index>`, then one
  `tab ...` line per tab. Paths are hex-encoded UTF-8 so spaces, newlines and non-ASCII names
  round-trip without escaping rules.
- `tab-<id>.utf8` — UTF-8 recovery text. Written only when a tab is untitled or modified.
  Clean named tabs reference the real file and keep no snapshot.

Each tab line records id, path, modified flag, encoding, EOL, language, large-file flag,
selection anchor/caret, first visible line, zoom and content-file name. Ids are random,
lowercase hex and are also used as a filename component; the parser rejects unsafe ids and
content names that do not match their tab.

Write and read rules:

- `WriteSessionContent` writes the snapshot atomically (temp file, flush, rename).
- `SaveSession` validates every reference first, writes the manifest atomically, and only then
  removes snapshot files that the new manifest does not reference.
- `LoadSession` validates the header, every numeric field, id uniqueness and the existence of
  every referenced snapshot. A malformed session is reported as a whole and is not partially
  restored.
- The UI restores tabs lazily: only the active tab is read during restore. Selecting another
  tab loads it through the same `LoadText` path and re-applies the stored view state.
- On clean close the session is persisted before the window is allowed to close. If persistence
  fails, the UI falls back to the normal Save/Discard/Cancel prompt and removes the stale
  session so an older buffer is never restored over newer files.

## 4. Editor adapter

`EditorWidget` derives from `wxStyledTextCtrl` and is the only place that calls Scintilla APIs.

Load path:

```
ui: Document::Load(path)
      -> LoadResult{utf8, meta}
      -> EditorWidget::LoadText(utf8, meta)
           -> SetTextRaw(utf8.data(), utf8.size())   // no wxString copy
           -> SetEOLMode(meta.eol)
           -> ApplyLanguage(meta.language)
           -> largeFile ? disable lexer/fold/undo : enable
```

Save path:

```
EditorWidget::GetUtf8()  -> GetTextRaw() (UTF-8 bytes)
ui: Document::Save(path, utf8, meta, &error)
```

Configuration:

- `SetCodePage(wxSTC_CP_UTF8)`.
- Margin 0 = line numbers, margin 1 = fold markers.
- `SetWrapMode(wxSTC_WRAP_NONE)` by default.
- 4-space indentation, tabs replaced by spaces.
- `SetCaretLineVisible(true)`, selection/caret colours from the active palette.
- Font: monospace family; on Linux wx resolves `wxFONTFAMILY_TELETYPE`.
- Large file: `SetLexer(wxSTC_LEX_NULL)`, fold margin width 0, `SetUndoCollection(false)`.

Find/replace uses Scintilla's `SearchInTarget`/`ReplaceTarget` APIs so the document is not copied
into a C++ string merely to search it.



## 5. UI

`MainFrame` owns:

- one `TabBook` (a `wxSimplebook` for pages plus an owner-drawn wx tab strip);
  no AUI or docking is involved;
- one `FindPanel` at the bottom, hidden until `Ctrl+F`/`Ctrl+H`;
- one `wxStatusBar` with fields: message, `Ln x, Col y`, encoding/EOL, language;
- menus and accelerators.

`TabBook` exists because GTK's `wxNotebook` has no per-tab close button and AUI is
deliberately not built into this static wx configuration. It paints rounded tabs, a
selected-tab accent, hover states and a right-side `×`; the strip scrolls horizontally
with the wheel when many tabs are open. Clicking `×`, middle-clicking a tab, or
`Ctrl`+clicking emits a close request. The close request, `Ctrl+W` and the File menu all
use the same `MainFrame::CloseTab(index)` path, so an inactive tab is selected before
the save prompt is shown.

Each tab is an `EditorWidget` carrying:

```cpp
std::string m_path;        // empty => untitled
DocumentMeta m_meta;
bool m_forceModified;      // recovered-but-unsaved snapshot
bool m_loading;            // suppress modified events during SetTextRaw
```

`MainFrame` keeps a parallel `TabState` vector (session id, snapshot name, load error,
recovered-modified flag, and view state). Restored tabs are deliberately not loaded until they
are selected, so a twenty-tab session still paints immediately.

Events:

- `wxEVT_STC_UPDATEUI` → status line/column.
- `wxEVT_STC_MODIFIED` → mark modified, update tab title.
- `EVT_TAB_CHANGED` → load an unloaded tab, then refresh title/status.
- `EVT_TAB_CLOSE` → select the tab and run the normal close flow.
- `wxEVT_STC_SAVEPOINTREACHED/LEFT` is avoided: explicit modified tracking is simpler.

Close flow: persist the session; if that fails, ask Save / Don't Save / Cancel per modified tab.
The prompt is a custom dialog (`ui/SaveChangesDialog.*`) with the file name, full path, warning
icon and standard button placement; Cancel is the escape path and Save is the default for a
named file. Save failures keep the tab open. A failed snapshot load is shown in the status bar
and blocks saving that tab rather than writing an empty buffer over the file; closing that tab
offers an explicit Discard Changes / Cancel warning instead.

### Compare tabs

`Compare` → `Compare Tabs...` (`Ctrl+Shift+C`) opens a separate top-level `CompareFrame`.
`MainFrame` collects a snapshot of each selected tab: document id, title/path, UTF-8 text,
`DocumentMeta`, zoom and first visible line. The first snapshot is the base; every other pane is
compared against it. Snapshots are read-only, so the editor tabs remain editable while the
comparison window is open.

The comparison is deliberately not live. Edits, saves, new tabs and tab closes call
`CompareFrame::MarkStale()`; the status bar asks for Refresh (`F5`), which re-collects the same
document ids and rebuilds the panes. Tabs that have disappeared are dropped; if fewer than two
remain, the comparison window closes with an explanation.

Line alignment lives in `core/Diff.*` and has no wxWidgets dependency. `SplitLines` accepts LF,
CRLF and lone CR. `AlignToBase` uses Myers diff with prefix/suffix trimming, bounded to 2,000 edit
steps and 50,000 lines per side. Exceeding either bound produces a proportional line mapping with
`Alignment::limited = true`, which suppresses highlight markers rather than showing misleading
ones. Otherwise a change block pairs removed and added lines as `Modified` and leaves the rest as
`Added`/`Removed`; `baseToPane`/`paneToBase` stay monotonic for scrolling.

Each `DiffPane` is an `EditorWidget` in read-only mode with a title/meta/stats header.
`wxSTC_MARK_BACKGROUND` markers in a zero-width symbol margin colour added, removed and modified
lines without disturbing the line-number margin. `CompareFrame` creates a `wxSplitterWindow` for
two panes and a flex grid for more. Scroll sync listens to `wxEVT_STC_UPDATEUI`: "Diff anchors"
maps the top line through the active pane's alignment, while "Line numbers" keeps identical line
indices. Programmatic scrolls are recorded in `m_expectedTop` so they are not echoed back as user
scrolls. `F11` dark-mode changes are propagated with `SetDarkMode`.

## 6. Startup and threading

`App::OnInit`:

1. Set app name/display name.
2. Create `MainFrame` (one empty tab, no session file read).
3. `Show()` — first paint.
4. `CallAfter`: `RestoreSession()` then open command-line files. Restore reads only the manifest
   and the active tab; other tabs stay lazy.

Profiling marks (`TXT_EDITOR_PROFILE=1`) print `t=…ms` to stderr at:

- `process main`, `OnInit begin`, `window shown`;
- `session restore begin/none/failed/done`, `session tab load begin/loaded`;
- `load begin`, `load done` (byte count).

M0 loads files on the UI thread. The 100 MB budget is met by avoiding lexer/undo and by using
raw UTF-8 transfer; if a profile shows a stall, move `Document::Load` to a worker and deliver the
buffer via `CallAfter`. The core API is already thread-agnostic.

## 7. Linux/desktop integration

- Wayland and X11 are both supported through wxGTK3; no direct Wayland protocol code.
- fcitx5 via GTK IM module. The editor does not consume `wxEVT_CHAR`/key events for printable
  input; only standard accelerators are bound. The input method is told where the caret is through
  `wxWindow::UpdateInputMethodCursorRect()`; the local wx patch (§8) keeps that rectangle
  non-empty, which is what positions the candidate window.
- Dark title bar on Windows is a future `DwmSetWindowAttribute` call; Linux uses the system theme.
- `StartupWMClass=txt-editor` in the desktop file keeps the KDE task manager association.
- AppImage is a single-file distribution; GTK3 remains a host dependency.
- `.deb` uses `libgtk-3-0t64 | libgtk-3-0` so it works on Debian 13 and older Ubuntu.

## 8. Deliberate trade-offs

| Decision | Why |
|---|---|
| Static wx, dynamic GTK3 | One binary, no wx runtime install; GTK3 is ubiquitous on KDE. |
| wx 3.3.4 development release | Needed for Scintilla 5.0.0 + Lexilla 5.5.4 + recent IME fix. Pinned, not rolling. |
| Owner-drawn `TabBook` | GTK `wxNotebook` has no per-tab close button; AUI would add a module and docking complexity. |
| No background file I/O in M0 | Simpler; budgets are achievable with large-file mode. Revisit after measurements. |
| Session snapshots in user state dir | Restores unsaved work without scanning or touching source files; owner-only files and lazy loading keep startup and privacy bounded. |
| UTF-16 without BOM treated as UTF-8/Latin-1 | Avoid false positives; documented limitation. |
| Latin-1 lossy save | Rare legacy path; visible warning could be added later. |
| Local patch for the IME caret rectangle | wx 3.3.4 reports a zero-width caret rect and wxGTK drops it. Two patched call sites beat an `OnUpdateUI` workaround, which wxSTC overwrites on every caret move. Drop the patch when upstream fixes it. |

## 9. Future Windows port

Keep `core/` unchanged. Build wx 3.3.4 with `-DwxBUILD_USE_STATIC_RUNTIME=ON`, link
`wx::core wx::base wx::stc`, and add:

- a manifest for DPI awareness and common controls;
- optional `SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE)`;
- `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` for the title bar;
- MSI/portable ZIP packaging.

No core file should need `#ifdef _WIN32`.

