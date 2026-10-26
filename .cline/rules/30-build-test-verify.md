# Build, test and verification

The host has no system wx, CMake, Ninja or GTK3 headers. Everything is bootstrapped into
`third_party/` (gitignored) and used through the scripts. **Do not call `cmake`/`ninja`/`ctest`
directly** unless you have sourced `third_party/env.sh` and are doing an incremental rebuild; the
scripts set `PKG_CONFIG_*`, `CMAKE_PREFIX_PATH`, `LIBRARY_PATH` and the sysroot flags for you.

## Commands

| Task | Command |
|---|---|
| Bootstrap CMake/Ninja/pkgconf/GTK3 sysroot (once) | `./scripts/bootstrap-local-deps.sh` |
| Build static wx 3.3.4 (once, minutes) | `./scripts/build-wx.sh` |
| Build the app | `./scripts/build.sh` (or `BUILD_TYPE=Debug ./scripts/build.sh`) |
| Tests | `./scripts/test.sh` (CTest + `build/txt_core_tests`) |
| Benchmark | `./scripts/bench.sh [--generate]` → `docs/benchmark-<date>.txt` (needs a display) |
| Packaging | `./scripts/package.sh` → `dist/` |
| Incremental wx rebuild | `source third_party/env.sh && "$NINJA" -C third_party/wx-build install` |
| Run | `./build/txt-editor [file]`; `TXT_EDITOR_PROFILE=1` prints `t=…ms` marks to stderr |

`TXT_EDITOR_NATIVE=ON` (`-march=native`) is **local benchmarking only** — never for packages,
never committed as a default. Build output goes to `build/` (override with `BUILD_DIR`).

## Tests

`tests/test_core.cpp` is framework-free: a `CHECK(cond)` macro increments `g_failures`, each test is
a `TestXxx()` function called from `main()`, and the process prints `all core tests passed` or
`N test(s) failed` (exit code 1). Follow that pattern exactly; do not add a test framework.

- Core only: no wxWidgets, no window, no network, no `sleep`, no reliance on the user's home.
  Temp files go under `fs::temp_directory_path() / "txt-editor-tests"`.
- Every new `core/` behaviour gets a `TestXxx()`; keep the suite fast (well under a second) and
  deterministic, including the randomized diff invariants (fixed seed).
- Never weaken or delete an existing check to make a change pass.

## What cannot be unit-tested

GUI, IME, theming, session restore across restarts, packaging. These are verified manually with
`docs/PERFORMANCE.md` (startup, 100 MB open, IME checklist) and are part of the definition of done
for anything touching `src/ui/`, `src/editor/`, `src/app/` or wx itself. Report exactly which
commands you ran and what you observed; never say "verified" for something you did not run.

## Performance discipline

- Quote only measured numbers. `docs/PERFORMANCE.md` says plainly: "Do not invent numbers." Do not
  invent latency, RSS or timing figures, and do not silently edit the budgets to match a regression.
- Keep the startup path free of file I/O, config scans and widget construction beyond the first
  paint; restored tabs stay lazy.
- No per-keystroke O(file) work (no re-init of lexer, no full-text copy, no `GetTextRaw` on every
  edit). Large-file mode must stay a single `DocumentMeta::largeFile` switch.

## Hygiene

- `build/`, `dist/`, `third_party/` are gitignored: never commit them, never place sources there,
  never point an include path into `build/`.
- Warnings are errors in spirit: `-Wall -Wextra -Wpedantic` must stay clean. A new warning is a bug.
- After every source change: `./scripts/build.sh` and `./scripts/test.sh` before you report done.
  For wx changes also rebuild wx first (see `20-wx-ime-and-scintilla.md`), then relink the app.
