# Workflow, docs and definition of done

## Before writing code

1. Read `README.md`, `docs/ARCHITECTURE.md` and the gate you are about to touch
   (`docs/PERFORMANCE.md` for startup/large files/IME, `docs/BUILD.md` for toolchain/packaging).
2. Read the actual module, not just its header, and check `tests/` for the existing expectations.
3. For anything larger than a few lines — and always for wx/IME, session format, save path or
   public core APIs — state a short plan first (files, approach, risks, how it will be verified).
4. If the request is ambiguous (e.g. "the IME popup is in the wrong place": pre-edit vs candidate
   window, Wayland vs X11, fcitx5 vs ibus), ask instead of guessing. One focused question is
   cheaper than a wrong fix.

## While writing code

- Smallest correct diff. Do not reformat, reorder, rename or "clean up" code you are not changing;
  the repo has no formatter, so unrelated churn is permanent noise.
- Follow neighbouring code (`10-cpp-style.md`) and the existing error strategy of the module.
- No new dependencies, no version bumps, no new build steps without explicit approval.
- Keep the user's data safe: session files stay owner-only, saves stay atomic, nothing is deleted
  that the user did not ask to delete.
- Never edit `third_party/` as a "quick fix" — use a patch file (`20-wx-ime-and-scintilla.md`).

## Docs are part of the change

| You changed | Update |
|---|---|
| user-visible feature, shortcut, scope | `README.md` (features, shortcut table) |
| layering, data flow, a trade-off | `docs/ARCHITECTURE.md` (§3–§9 as appropriate) |
| build flags, wx patches, packaging | `docs/BUILD.md` (wx build, packaging, troubleshooting) |
| startup/large-file/IME behaviour | `docs/PERFORMANCE.md` checklists (numbers only if measured) |
| on-disk session format | `docs/ARCHITECTURE.md` §3.5 plus a compatibility note |

Docs are English, present tense, no marketing. Add a line only if it is true and verifiable.

## Definition of done

- [ ] `./scripts/build.sh` succeeds with no new warnings.
- [ ] `./scripts/test.sh` passes; new `core/` behaviour has a `TestXxx()`.
- [ ] GUI/IME changes were exercised manually and the exact commands are in the report.
- [ ] Docs updated per the table above.
- [ ] No edits inside `third_party/` (or they are captured as a patch applied by `build-wx.sh`).
- [ ] No build artifacts, no `build/`, no `dist/`, no session files added to git.
- [ ] The diff contains nothing unrelated to the request.

## Commits and reporting

- One logical change per commit; messages are short, imperative and capitalized, matching history
  (`Add compare tab feature`, `Fix chinese name file issue`). No `feat:`/`fix:` prefixes.
- Report what changed, why, the commands you ran and their result, and anything left unverified.
  Do not claim a GUI or IME fix works without having run it.
- If something is blocked (missing toolchain, no Wayland session, no fcitx5), say so plainly and
  list the manual steps the user should run instead of pretending it passed.

## Scope guard

M0 is deliberately small. Improvements outside the current request are suggestions in the reply,
not commits: no plugins, LSP, split view, minimap, multi-cursor, telemetry, theme systems, new
languages, extra packaging formats.
