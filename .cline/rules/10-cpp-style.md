# C++ and wxWidgets style

Toolchain: g++ ≥13, **C++20** (`CMAKE_CXX_STANDARD 20`, extensions off), warnings
`-Wall -Wextra -Wpedantic` on every target. There is no `.clang-format`; match the surrounding
code by hand.

## Layout

- 4 spaces, no tabs, **≤100 columns** (the current maximum in `src/` is exactly 100).
- One class per header, `#pragma once` (never include guards), no `using namespace` anywhere.
- Everything lives in `namespace txt` (sub-namespaces like `txt::diff` are fine).
- Include order, separated by blank lines:
  1. own header (`.cpp` only),
  2. standard library, alphabetically (`<algorithm>`, `<cstdint>`, `<string>`…),
  3. wx headers, alphabetically (`<wx/stc/stc.h>`, `<wx/sizer.h>`…),
  4. project headers, alphabetically, always `"core/Foo.h"` / `"ui/Foo.h"` (include root is `src`).

## Naming

| Kind | Style | Example |
|---|---|---|
| classes / structs / enums | `PascalCase` | `EditorWidget`, `DocumentMeta`, `DiffPane` |
| enum class values | `PascalCase` | `LanguageId::Cpp`, `Eol::CrLf`, `Encoding::Utf8Bom` |
| functions / methods | `PascalCase` | `LoadText`, `RestoreViewState`, `AlignToBase` |
| locals / parameters | `lowerCamelCase` | `needleBytes`, `largeFile`, `firstVisibleLine` |
| members | `m_` + `lowerCamelCase` | `m_path`, `m_forceModified` |
| constants / `constexpr` | `k` + `PascalCase` tail | `kLargeFileThreshold`, `kMaxEditDistance` |
| files | match the primary type | `EditorWidget.h`, `Session.cpp`; shared types `TextTypes.h` |

Use `final` on leaf UI classes, `override` on every virtual override, `explicit` on
single-argument constructors.

## Modern C++ rules

- `nullptr`, never `NULL`/`0` for pointers. `static_cast`/`reinterpret_cast` only — no C casts.
- `enum class` for every enum that crosses a file boundary.
- `constexpr` / `inline constexpr` instead of `#define`; `static constexpr` inside functions.
- Prefer `std::string_view` for read-only text parameters in `core/`; return `std::string` by value.
- `std::optional<T>` for "maybe a value"; a result struct (`LoadResult{ok, found, error}`) for
  operations with more context; `bool f(..., std::string* error)` when there is one failure reason.
  Follow the convention already used by the module you are touching.
- **No exceptions** (`throw`/`try`/`catch`) outside tests. No RTTI, no `dynamic_cast`.
- Avoid `auto` except for iterators, structured bindings and verbose template types. Prefer
  range-`for`. Take `const&` for non-trivial parameters.
- No raw `new`/`delete`: widgets and sizers are owned by their wx parent; everything else is on
  the stack or in a standard container.

## wxWidgets rules

- Wire events with `Bind(...)` next to the code that owns the handler; call `event.Skip()` unless
  the event is fully handled (UI update events must stay skippable).
- **Never consume or filter printable key input.** Do not bind `wxEVT_CHAR`/`wxEVT_KEY_DOWN` to
  insert text, and never swallow keys while an IME composition is active — that breaks fcitx5.
  Use standard `wxID_*` menu accelerators for shortcuts.
- Use wxSTC high-level methods (`SetTextRaw`, `GetTextRaw`, `SearchInTarget`, `BeginUndoAction`),
  not raw `SendMsg` with magic numbers, when a wrapper exists.
- `wxString` ↔ UTF-8 only through `wxString::FromUTF8` / `.utf8_string()`. Paths are `std::string`
  UTF-8 in the model, converted at the wx call.
- Custom-drawn chrome: express metric constants in DIP (`constexpr int kBarHeightDip = 38;`) and
  convert with `FromDIP(...)`. Never hard-code device pixels for new UI.
- `wxUSE_INTL=OFF`: no `_()`, no `wxT()`; pass plain `"..."` literals or `wxString`.
- User-visible errors go through `wxMessageBox(...)` with a title and `wxICON_ERROR|WARNING`;
  `std::fprintf(stderr, ...)` is reserved for `src/app/Profile.cpp` profiling output.

## Comments and structure

- Comments in English, sentence case, explaining **why** (invariants, GTK/wx quirks, trade-offs),
  not what the next line does. No changelog comments, no commented-out code, no decorative banners.
- Keep functions small and single-purpose. Helpers that are not part of an interface go into an
  anonymous namespace at the top of the `.cpp`.
- `// namespace` / `// namespace txt` closing comment on namespaces.
- Public header changes are API changes: check every caller (and `tests/`) before editing.
