#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace txt::diff {

enum class LineKind {
    Same,
    Added,
    Removed,
    Modified,
};

struct Stats {
    std::size_t added = 0;
    std::size_t removed = 0;
    std::size_t modified = 0;
};

// Per-line diff information for one pane compared with the base document.
// baseToPane and paneToBase are monotonic (non-decreasing) line mappings used
// for synchronized scrolling when lines have been inserted or removed.
struct Alignment {
    std::vector<LineKind> baseKinds;
    std::vector<LineKind> paneKinds;
    std::vector<int> baseToPane;
    std::vector<int> paneToBase;
    Stats stats;
    bool limited = false;
};

// Splits UTF-8 text into lines without their line terminators.  LF, CRLF and
// lone CR are all accepted.  The result always contains at least one line,
// matching Scintilla's line count for empty text and trailing newlines.
std::vector<std::string_view> SplitLines(std::string_view text);

// Compares paneLines with baseLines.  The returned vectors have exactly the
// same sizes as their inputs.  Files that are too large, or whose edit
// distance exceeds the bounded Myers budget, are marked limited and receive a
// proportional fallback mapping without line highlighting.
Alignment AlignToBase(const std::vector<std::string_view>& baseLines,
                      const std::vector<std::string_view>& paneLines);

}  // namespace txt::diff
