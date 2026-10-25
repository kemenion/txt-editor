#include "core/Diff.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace txt::diff {
namespace {

constexpr std::size_t kMaxDiffLines = 50'000;
constexpr int kMaxEditDistance = 2'000;

enum class OpKind {
    Equal,
    Delete,
    Insert,
};

struct Op {
    OpKind kind;
    std::size_t base = 0;
    std::size_t pane = 0;
};

void AppendDeletes(std::vector<Op>& ops, std::size_t baseStart, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        ops.push_back({OpKind::Delete, baseStart + i, 0});
    }
}

void AppendInserts(std::vector<Op>& ops, std::size_t paneStart, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        ops.push_back({OpKind::Insert, 0, paneStart + i});
    }
}

// Bounded Myers diff for the middle part of two line arrays.  The V snapshots
// are kept only for the relevant k range, so memory use is O(D^2) rather than
// O(D * (N + M)).  The edit-distance budget keeps pathological inputs from
// allocating without bound; callers fall back to proportional mapping.
std::vector<Op> DiffRange(const std::vector<std::string_view>& base, std::size_t baseStart,
                          std::size_t baseCount, const std::vector<std::string_view>& pane,
                          std::size_t paneStart, std::size_t paneCount, bool* limited) {
    std::vector<Op> ops;
    if (baseCount == 0) {
        AppendInserts(ops, paneStart, paneCount);
        return ops;
    }
    if (paneCount == 0) {
        AppendDeletes(ops, baseStart, baseCount);
        return ops;
    }

    const int n = static_cast<int>(baseCount);
    const int m = static_cast<int>(paneCount);
    const int budget = std::min(n + m, kMaxEditDistance);
    const int offset = budget;
    std::vector<int> v(static_cast<std::size_t>(2 * budget + 1), 0);
    std::vector<std::vector<int>> trace;
    trace.reserve(static_cast<std::size_t>(budget + 1));

    int found = -1;
    for (int d = 0; d <= budget; ++d) {
        std::vector<int> snapshot;
        if (d > 0) {
            snapshot.resize(static_cast<std::size_t>(2 * d - 1));
            for (int k = -(d - 1); k <= d - 1; ++k) {
                snapshot[static_cast<std::size_t>(k + d - 1)] =
                    v[static_cast<std::size_t>(offset + k)];
            }
        }
        trace.push_back(std::move(snapshot));

        for (int k = -d; k <= d; k += 2) {
            int x = 0;
            if (k == -d ||
                (k != d &&
                 v[static_cast<std::size_t>(offset + k - 1)] <
                     v[static_cast<std::size_t>(offset + k + 1)])) {
                x = v[static_cast<std::size_t>(offset + k + 1)];
            } else {
                x = v[static_cast<std::size_t>(offset + k - 1)] + 1;
            }
            int y = x - k;
            while (x < n && y < m &&
                   base[baseStart + static_cast<std::size_t>(x)] ==
                       pane[paneStart + static_cast<std::size_t>(y)]) {
                ++x;
                ++y;
            }
            v[static_cast<std::size_t>(offset + k)] = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
        if (found >= 0) break;
    }

    if (found < 0) {
        *limited = true;
        return {};
    }

    std::vector<Op> reversed;
    reversed.reserve(static_cast<std::size_t>(found + baseCount + paneCount));
    int x = n;
    int y = m;

    for (int d = found; d > 0; --d) {
        const std::vector<int>& snapshot = trace[static_cast<std::size_t>(d)];
        const int k = x - y;
        int previousK = 0;
        if (k == -d ||
            (k != d &&
             snapshot[static_cast<std::size_t>(k - 1 + d - 1)] <
                 snapshot[static_cast<std::size_t>(k + 1 + d - 1)])) {
            previousK = k + 1;
        } else {
            previousK = k - 1;
        }

        const int previousX = snapshot[static_cast<std::size_t>(previousK + d - 1)];
        const int previousY = previousX - previousK;

        while (x > previousX && y > previousY) {
            reversed.push_back({OpKind::Equal,
                                baseStart + static_cast<std::size_t>(x - 1),
                                paneStart + static_cast<std::size_t>(y - 1)});
            --x;
            --y;
        }

        if (previousK == k + 1) {
            reversed.push_back(
                {OpKind::Insert, 0, paneStart + static_cast<std::size_t>(y - 1)});
            --y;
        } else {
            reversed.push_back(
                {OpKind::Delete, baseStart + static_cast<std::size_t>(x - 1), 0});
            --x;
        }
        x = previousX;
        y = previousY;
    }

    while (x > 0 && y > 0) {
        reversed.push_back({OpKind::Equal,
                            baseStart + static_cast<std::size_t>(x - 1),
                            paneStart + static_cast<std::size_t>(y - 1)});
        --x;
        --y;
    }
    while (x > 0) {
        reversed.push_back(
            {OpKind::Delete, baseStart + static_cast<std::size_t>(x - 1), 0});
        --x;
    }
    while (y > 0) {
        reversed.push_back(
            {OpKind::Insert, 0, paneStart + static_cast<std::size_t>(y - 1)});
        --y;
    }

    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

std::vector<Op> DiffFull(const std::vector<std::string_view>& base,
                         const std::vector<std::string_view>& pane, bool* limited) {
    std::vector<Op> ops;
    const std::size_t baseCount = base.size();
    const std::size_t paneCount = pane.size();

    std::size_t prefix = 0;
    while (prefix < baseCount && prefix < paneCount && base[prefix] == pane[prefix]) {
        ops.push_back({OpKind::Equal, prefix, prefix});
        ++prefix;
    }

    std::size_t suffix = 0;
    while (suffix + prefix < baseCount && suffix + prefix < paneCount &&
           base[baseCount - 1 - suffix] == pane[paneCount - 1 - suffix]) {
        ++suffix;
    }

    const std::size_t baseMiddle = baseCount - prefix - suffix;
    const std::size_t paneMiddle = paneCount - prefix - suffix;
    if (baseMiddle > kMaxDiffLines || paneMiddle > kMaxDiffLines) {
        *limited = true;
        return {};
    }

    std::vector<Op> middle =
        DiffRange(base, prefix, baseMiddle, pane, prefix, paneMiddle, limited);
    if (*limited) return {};
    ops.insert(ops.end(), middle.begin(), middle.end());

    for (std::size_t i = 0; i < suffix; ++i) {
        ops.push_back({OpKind::Equal, baseCount - suffix + i, paneCount - suffix + i});
    }
    return ops;
}

void FillProportional(std::vector<int>& mapping, std::size_t fromCount,
                      std::size_t toCount) {
    mapping.resize(fromCount);
    if (fromCount == 0) return;
    if (fromCount == 1 || toCount <= 1) {
        std::fill(mapping.begin(), mapping.end(), 0);
        return;
    }

    const std::size_t last = toCount - 1;
    for (std::size_t i = 0; i < fromCount; ++i) {
        mapping[i] = static_cast<int>((i * last) / (fromCount - 1));
    }
}

void FillMappings(Alignment& alignment, std::size_t baseCount, std::size_t paneCount,
                  const std::vector<std::pair<std::size_t, std::size_t>>& anchors) {
    if (anchors.empty()) {
        FillProportional(alignment.baseToPane, baseCount, paneCount);
        FillProportional(alignment.paneToBase, paneCount, baseCount);
        return;
    }

    alignment.baseToPane.assign(baseCount, -1);
    alignment.paneToBase.assign(paneCount, -1);
    for (const auto& [baseLine, paneLine] : anchors) {
        alignment.baseToPane[baseLine] = static_cast<int>(paneLine);
        alignment.paneToBase[paneLine] = static_cast<int>(baseLine);
    }

    int previousPane = static_cast<int>(anchors.front().second);
    for (std::size_t i = 0; i < baseCount; ++i) {
        if (alignment.baseToPane[i] < 0) {
            alignment.baseToPane[i] = previousPane;
        } else {
            previousPane = alignment.baseToPane[i];
        }
    }

    int previousBase = static_cast<int>(anchors.front().first);
    for (std::size_t i = 0; i < paneCount; ++i) {
        if (alignment.paneToBase[i] < 0) {
            alignment.paneToBase[i] = previousBase;
        } else {
            previousBase = alignment.paneToBase[i];
        }
    }
}

void CountStats(Alignment& alignment) {
    for (LineKind kind : alignment.baseKinds) {
        if (kind == LineKind::Removed) {
            ++alignment.stats.removed;
        } else if (kind == LineKind::Modified) {
            ++alignment.stats.modified;
        }
    }
    for (LineKind kind : alignment.paneKinds) {
        if (kind == LineKind::Added) ++alignment.stats.added;
    }
}

}  // namespace

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r') continue;

        lines.push_back(text.substr(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
        start = i + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
}

Alignment AlignToBase(const std::vector<std::string_view>& baseLines,
                      const std::vector<std::string_view>& paneLines) {
    Alignment alignment;
    alignment.baseKinds.assign(baseLines.size(), LineKind::Same);
    alignment.paneKinds.assign(paneLines.size(), LineKind::Same);

    if (baseLines.size() == 1 && paneLines.size() == 1 &&
        baseLines.front() == paneLines.front()) {
        alignment.baseToPane = {0};
        alignment.paneToBase = {0};
        return alignment;
    }

    bool limited = false;
    const std::vector<Op> ops = DiffFull(baseLines, paneLines, &limited);
    if (limited) {
        alignment.limited = true;
        FillMappings(alignment, baseLines.size(), paneLines.size(), {});
        return alignment;
    }

    std::vector<std::pair<std::size_t, std::size_t>> anchors;
    std::size_t index = 0;
    while (index < ops.size()) {
        if (ops[index].kind == OpKind::Equal) {
            anchors.emplace_back(ops[index].base, ops[index].pane);
            ++index;
            continue;
        }

        std::vector<std::size_t> removed;
        std::vector<std::size_t> added;
        while (index < ops.size() && ops[index].kind != OpKind::Equal) {
            if (ops[index].kind == OpKind::Delete) {
                removed.push_back(ops[index].base);
            } else {
                added.push_back(ops[index].pane);
            }
            ++index;
        }

        const std::size_t paired = std::min(removed.size(), added.size());
        for (std::size_t i = 0; i < paired; ++i) {
            alignment.baseKinds[removed[i]] = LineKind::Modified;
            alignment.paneKinds[added[i]] = LineKind::Modified;
            anchors.emplace_back(removed[i], added[i]);
        }
        for (std::size_t i = paired; i < removed.size(); ++i) {
            alignment.baseKinds[removed[i]] = LineKind::Removed;
        }
        for (std::size_t i = paired; i < added.size(); ++i) {
            alignment.paneKinds[added[i]] = LineKind::Added;
        }
    }

    CountStats(alignment);
    FillMappings(alignment, baseLines.size(), paneLines.size(), anchors);
    return alignment;
}

}  // namespace txt::diff

