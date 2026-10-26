# Performance and acceptance

## Budgets

| Scenario | Budget | Method |
|---|---|---|
| Linux cold start | <= 200 ms | `hyperfine` or 10x `/usr/bin/time` |
| Warm start | <= 80 ms | page cache warm, 10 runs |
| Open 10 MB UTF-8 | <= 300 ms | in-process `file.load` marks |
| Open 100 MB UTF-8 | <= 1500 ms | in-process marks; large-file mode |
| Input latency p95 | <= 16 ms | manual typing + frame timing; log if needed |
| Idle RSS, one 10 MB file | <= 60 MB | `ps -o rss=` after 10 s idle |
| Cursor / IME preedit | correct | manual checklist below |

Cold start is measured to the first painted frame, not to process exit. Use
`TXT_EDITOR_PROFILE=1` for in-process marks; they print to stderr. `window shown` is the
first-paint mark; `scripts/bench.sh` parses these names, so a renamed mark must be updated in the
script too.

## Profiling marks

```
TXT_EDITOR_PROFILE=1 ./build/txt-editor README.md
```

Expected output on a warm page cache (values are examples, not promises):

```
[txt] t=0.0ms process main
[txt] t=41.3ms OnInit begin
[txt] t=79.4ms window shown
[txt] t=79.9ms session restore begin
[txt] t=87.1ms session restore done
[txt] t=87.1ms load begin
[txt] t=87.1ms load done (3680 bytes)
```

`load begin` and `load done` bracket the decode and the buffer fill of one document.

If `load done - load begin` exceeds the budget for a 10 MB file, check that the file is
not being decoded twice and that lexer/undo are disabled above 32 MiB.

## Benchmark procedure

1. Build Release:
   ```bash
   ./scripts/build.sh
   ```
2. Generate fixtures (not committed):
   ```bash
   ./scripts/bench.sh --generate
   ls -lh /tmp/txt-editor-bench
   ```
3. Measure:
   ```bash
   ./scripts/bench.sh
   ```
   Results are written to `docs/benchmark-$(date +%F).txt`. The script prints one markdown row
   that can be pasted into the table below. `RUNS` (startups, default 10), `OPENS` (file opens,
   default 3) and `IDLE_SECONDS` (default 10) shorten a smoke run.
4. Record the machine, kernel, session (`echo $XDG_SESSION_TYPE`), compiler, and wx version.
5. Do not compare Debug builds or `-march=native` builds against portable artifacts.

## Manual functional checklist (M0)

- [ ] Start with no arguments; window appears; one untitled tab.
- [ ] Open a UTF-8 file with LF; content, line numbers and syntax colours correct.
- [ ] Open a CRLF file; status shows CRLF; save; `file`/hexdump still shows CRLF.
- [ ] Open a UTF-8 BOM file; BOM is not displayed; save preserves BOM.
- [ ] Open a UTF-16LE file with CJK; text displays; save preserves UTF-16LE.
- [ ] Open a 10 MB file; status shows large file? (only >32 MiB); editing is responsive.
- [ ] Open a 100 MB file; no lexer/fold/undo; open within 1.5 s; save works.
- [ ] Type ASCII, Chinese, emoji; no duplicate characters; undo works.
- [ ] Find next/previous wraps; match case and whole word behave.
- [ ] Replace one/all; undo restores the original.
- [ ] Close a modified tab; Save/Discard/Cancel all behave.
- [ ] Open a file, change it in another editor, then `Ctrl+S`: the Reload / Overwrite / Cancel
      prompt appears; Reload shows the other editor's text, Overwrite keeps the tab's text.
- [ ] Run two windows at once (`XDG_STATE_HOME=/tmp/txt-state-1 ./build/txt-editor`), leave
      unsaved text in both, close both, start once more: both sets of tabs are restored, no
      duplicates and no lost buffer.
- [ ] Drag a file onto the window (if implemented) or open via dialog and CLI.
- [ ] `Ctrl+Q` with modified tabs prompts.

## IME acceptance (hard gate)

Environment:

```bash
# Wayland
GTK_IM_MODULE=fcitx GDK_BACKEND=wayland ./build/txt-editor /tmp/ime-test.txt

# X11
GTK_IM_MODULE=fcitx GDK_BACKEND=x11 ./build/txt-editor /tmp/ime-test.txt
```

With fcitx5 + a Chinese input method (e.g. Pinyin):

- [ ] Pre-edit (underline) text appears **inside** the editor at the caret.
- [ ] Candidate window follows the caret, including when the caret is near the window edge.
- [ ] Committing text inserts exactly one copy (no duplicated or dropped characters).
- [ ] `Esc` cancels pre-edit without inserting text.
- [ ] Arrow keys move within pre-edit, not the document, while composing.
- [ ] Backspace edits pre-edit, not the document, while composing.
- [ ] Switching focus between tabs commits/cancels pre-edit cleanly.
- [ ] No crash when closing a tab during composition.

If pre-edit is not visible on Wayland, first confirm `fcitx5-frontend-gtk3` is installed and
`GTK_IM_MODULE=fcitx`. If it still fails, wxGTK is not viable for this product and the
Qt6 + QScintilla fallback should be evaluated.

The candidate window is positioned from the rectangle wx hands to GTK. To check that plumbing
without typing, break on the hand-off and inspect the rectangle:

```bash
# The symbol name is wx-version specific; find it with:
nm -C build/txt-editor | grep GTKUpdateIMCursorRect
# 000000000040b2d0 t wxWindow::GTKUpdateIMCursorRect(_GtkIMContext*) const

gdb -q -batch \
  -ex 'set breakpoint pending on' \
  -ex 'break _ZNK8wxWindow21GTKUpdateIMCursorRectEP13_GtkIMContext' \
  -ex 'run' \
  -ex 'tbreak gtk_im_context_set_cursor_location' -ex 'continue' \
  -ex 'x/4dw $rsi' -ex 'bt 3' -ex 'kill' \
  --args ./build/txt-editor /tmp/ime-test.txt
```

`x/4dw $rsi` prints `x y width height`. Width must be at least 1; the backtrace should reach
`ScintillaWX::UpdateSystemCaret`. With width 0 wxGTK returned before the call and fcitx5 never
learned the caret position, which is what `patches/wx-3.3.4-ime-caret-rect.patch` fixes. This
proves the geometry only; the visual checks above still decide acceptance.

## Measured results

Fill this table from `docs/benchmark-$(date +%F).txt` after running `scripts/bench.sh` on the
target machine. Do not invent numbers.

| Date | Machine/session | Build | Cold start p50 | Cold start p95 | Warm p50 | 10 MB open | 100 MB open | Idle RSS | IME Wayland | IME X11 |
|---|---|---|---|---|---|---|---|---|---|---|
| 2026-10-26 | Debian 13, Wayland | Release | 66.8 ms | 86.3 ms | 66.8 ms | 17.6 ms | 164.2 ms | 112 MB | not run | not run |

Startup, open and memory values were taken with a warm page cache (dropping it needs root); the
fixtures are generated by `scripts/bench.sh --generate`. Idle RSS is above the 60 MB budget: an
empty tab already measures ~73 MB, so the budget needs a memory pass or a revision before it is
used as a gate.

## Known limits

- GTK3 is dynamically linked and not bundled; AppImage portability depends on the host GTK3.
- UTF-16 without BOM is not guessed.
- Latin-1 save replaces characters above U+00FF with `?`.
- Large-file mode disables undo; this is intentional and shown in the status bar.
- M0 has no background loading; a 1 GB file may still block the UI. Do not claim 1 GB support.
