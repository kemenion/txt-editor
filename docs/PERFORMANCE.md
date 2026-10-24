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
`TXT_EDITOR_PROFILE=1` for in-process marks; they print to stderr.

## Profiling marks

```
TXT_EDITOR_PROFILE=1 ./build/txt-editor README.md
```

Expected output (values are examples, not promises):

```
[txt] t=0.0ms app.init.begin
[txt] t=42.1ms frame.created
[txt] t=58.7ms frame.shown
[txt] t=62.0ms file.load.begin
[txt] t=64.3ms file.load.end (1.2 KiB)
[txt] t=70.0ms editor.first_idle
```

If `file.load.end - file.load.begin` exceeds the budget for a 10 MB file, check that the file is
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
   Results are written to `docs/benchmark-$(date +%F).txt`.
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

## Measured results

Fill this table after running `scripts/bench.sh` on the target machine. Do not invent numbers.

| Date | Machine/session | Build | Cold start p50 | Cold start p95 | Warm p50 | 10 MB open | 100 MB open | Idle RSS | IME Wayland | IME X11 |
|---|---|---|---|---|---|---|---|---|---|---|
| TBD | Debian 13, Wayland | Release | — | — | — | — | — | — | — | — |

## Known limits

- GTK3 is dynamically linked and not bundled; AppImage portability depends on the host GTK3.
- UTF-16 without BOM is not guessed.
- Latin-1 save replaces characters above U+00FF with `?`.
- Large-file mode disables undo; this is intentional and shown in the status bar.
- M0 has no background loading; a 1 GB file may still block the UI. Do not claim 1 GB support.
