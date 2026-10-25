#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/Diff.h"
#include "core/Document.h"
#include "core/Encoding.h"
#include "core/Language.h"
#include "core/Session.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "      \
                      << #cond << "\n";                                      \
            ++g_failures;                                                    \
        }                                                                    \
    } while (false)

void TestUtf8Validation() {
    CHECK(txt::IsValidUtf8(""));
    CHECK(txt::IsValidUtf8("hello"));
    CHECK(txt::IsValidUtf8("中文🙂"));
    CHECK(!txt::IsValidUtf8("\xC0\x80"));
    CHECK(!txt::IsValidUtf8("\xE4\xB8"));
}

void TestBomAndUtf16() {
    CHECK(txt::DetectEncoding("plain") == txt::Encoding::Utf8);
    CHECK(txt::DetectEncoding("\xEF\xBB\xBFhi") == txt::Encoding::Utf8Bom);
    CHECK(txt::DetectEncoding("\xFF\xFE") == txt::Encoding::Utf16LE);
    CHECK(txt::DetectEncoding("\xFE\xFF") == txt::Encoding::Utf16BE);

    const std::string utf16le("\xFF\xFE" "A\x00\x2D\x4E", 6);
    bool ok = false;
    CHECK(txt::ToUtf8(utf16le, txt::Encoding::Utf16LE, &ok) == "A中");
    CHECK(ok);

    const std::string utf16be("\xFE\xFF\x00" "A\x4E\x2D", 6);
    CHECK(txt::ToUtf8(utf16be, txt::Encoding::Utf16BE, &ok) == "A中");
    CHECK(ok);

    const std::string emoji = "🙂";
    const std::string encoded = txt::FromUtf8(emoji, txt::Encoding::Utf16LE);
    CHECK(txt::ToUtf8(encoded, txt::Encoding::Utf16LE, &ok) == emoji);
    CHECK(ok);
}

void TestEol() {
    CHECK(txt::DetectEol("a\nb\n") == txt::Eol::Lf);
    CHECK(txt::DetectEol("a\r\nb\r\n") == txt::Eol::CrLf);
    CHECK(txt::DetectEol("a\rb\r") == txt::Eol::Cr);
    CHECK(txt::DetectEol("a\r\nb\n") == txt::Eol::Lf);  // tie -> LF
}

void TestLanguage() {
    CHECK(txt::DetectLanguage("a.py", "") == txt::LanguageId::Python);
    CHECK(txt::DetectLanguage("a.cpp", "") == txt::LanguageId::Cpp);
    CHECK(txt::DetectLanguage("a.h", "") == txt::LanguageId::Cpp);
    CHECK(txt::DetectLanguage("a.json", "") == txt::LanguageId::Json);
    CHECK(txt::DetectLanguage("README", "#!/usr/bin/env python3\n") ==
          txt::LanguageId::Python);
    CHECK(txt::DetectLanguage("README", "#!/bin/bash\n") == txt::LanguageId::Shell);
    CHECK(txt::DetectLanguage("README.unknown", "") == txt::LanguageId::Plain);
    CHECK(txt::LanguageName(txt::LanguageId::Cpp) == "C++");
}

void TestDocumentRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "txt-editor-tests";
    fs::create_directories(dir);
    const fs::path path = dir / "utf16le.txt";
    const std::string original = "line1\r\n中文🙂\r\n";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << txt::FromUtf8(original, txt::Encoding::Utf16LE);
    }

    const txt::LoadResult loaded = txt::LoadDocument(path.string());
    CHECK(loaded.ok);
    CHECK(loaded.utf8 == original);
    CHECK(loaded.meta.encoding == txt::Encoding::Utf16LE);
    CHECK(loaded.meta.eol == txt::Eol::CrLf);
    CHECK(!loaded.meta.largeFile);

    std::string error;
    CHECK(txt::SaveDocument(path.string(), loaded.utf8, loaded.meta, &error));
    const txt::LoadResult reloaded = txt::LoadDocument(path.string());
    CHECK(reloaded.ok);
    CHECK(reloaded.utf8 == original);
    CHECK(reloaded.meta.encoding == txt::Encoding::Utf16LE);

    std::error_code ec;
    fs::remove_all(dir, ec);
}


void TestNonAsciiPath() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "txt-editor-tests";
    fs::create_directories(dir);
    const fs::path path = dir / "中文 测试.txt";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "non-ascii path\n";
    }

    const txt::LoadResult loaded = txt::LoadDocument(path.string());
    CHECK(loaded.ok);
    CHECK(loaded.utf8 == "non-ascii path\n");
    CHECK(loaded.meta.language == txt::LanguageId::Plain);

    std::string error;
    CHECK(txt::SaveDocument(path.string(), loaded.utf8, loaded.meta, &error));
    CHECK(txt::LoadDocument(path.string()).ok);

    std::error_code ec;
    fs::remove_all(dir, ec);
}


void TestSessionRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "txt-editor-session-tests";
    std::error_code ec;
    fs::remove_all(dir, ec);

    txt::SessionInfo session;
    txt::SessionTabInfo clean;
    clean.id = "clean1";
    clean.path = "/tmp/space 中文\nfile.txt";
    clean.meta.encoding = txt::Encoding::Utf16LE;
    clean.meta.eol = txt::Eol::CrLf;
    clean.meta.language = txt::LanguageId::Python;
    clean.meta.largeFile = true;
    clean.modified = false;
    clean.anchor = 41;
    clean.caret = 43;
    clean.firstVisibleLine = 7;
    clean.zoom = 3;
    session.tabs.push_back(clean);

    txt::SessionTabInfo dirty;
    dirty.id = "dirty1";
    dirty.path = "/tmp/dirty.txt";
    dirty.meta.encoding = txt::Encoding::Latin1;
    dirty.meta.eol = txt::Eol::Cr;
    dirty.meta.language = txt::LanguageId::Cpp;
    dirty.modified = true;
    dirty.anchor = 2;
    dirty.caret = 9;
    dirty.firstVisibleLine = 1;
    dirty.zoom = -2;
    std::string error;
    CHECK(txt::WriteSessionContent(dir.string(), dirty.id, "alpha\r\n中文", &dirty.contentFile,
                                   &error));
    CHECK(dirty.contentFile == "tab-dirty1.utf8");
    session.tabs.push_back(dirty);

    txt::SessionTabInfo blank;
    blank.id = "blank1";
    blank.path.clear();
    blank.modified = false;
    CHECK(txt::WriteSessionContent(dir.string(), blank.id, "", &blank.contentFile, &error));
    session.tabs.push_back(blank);
    session.active = 1;

    CHECK(txt::SaveSession(dir.string(), session, &error));
    CHECK(fs::is_regular_file(dir / "manifest"));
    CHECK(!fs::exists(dir / "tab-clean1.utf8"));

    const txt::SessionLoadResult loaded = txt::LoadSession(dir.string());
    CHECK(loaded.found);
    CHECK(loaded.ok);
    CHECK(loaded.session.tabs.size() == 3);
    CHECK(loaded.session.active == 1);
    CHECK(loaded.session.tabs[0].id == "clean1");
    CHECK(loaded.session.tabs[0].path == "/tmp/space 中文\nfile.txt");
    CHECK(loaded.session.tabs[0].meta.encoding == txt::Encoding::Utf16LE);
    CHECK(loaded.session.tabs[0].meta.eol == txt::Eol::CrLf);
    CHECK(loaded.session.tabs[0].meta.language == txt::LanguageId::Python);
    CHECK(loaded.session.tabs[0].meta.largeFile);
    CHECK(!loaded.session.tabs[0].modified);
    CHECK(loaded.session.tabs[0].contentFile.empty());
    CHECK(loaded.session.tabs[0].anchor == 41);
    CHECK(loaded.session.tabs[0].caret == 43);
    CHECK(loaded.session.tabs[0].firstVisibleLine == 7);
    CHECK(loaded.session.tabs[0].zoom == 3);

    std::string recovered;
    CHECK(txt::ReadSessionContent(dir.string(), loaded.session.tabs[1].contentFile, &recovered,
                                   &error));
    CHECK(recovered == "alpha\r\n中文");
    CHECK(loaded.session.tabs[2].path.empty());
    CHECK(loaded.session.tabs[2].contentFile == "tab-blank1.utf8");

    // Overwriting must keep referenced snapshots and remove orphaned ones.
    txt::SessionInfo rewritten;
    rewritten.tabs.push_back(loaded.session.tabs[1]);
    rewritten.active = 0;
    CHECK(txt::SaveSession(dir.string(), rewritten, &error));
    CHECK(fs::exists(dir / "tab-dirty1.utf8"));
    CHECK(!fs::exists(dir / "tab-blank1.utf8"));
    CHECK(txt::ReadSessionContent(dir.string(), "tab-dirty1.utf8", &recovered, &error));
    CHECK(recovered == "alpha\r\n中文");

    const txt::SessionLoadResult smaller = txt::LoadSession(dir.string());
    CHECK(smaller.ok);
    CHECK(smaller.session.tabs.size() == 1);

    // Snapshot references are validated before the manifest is accepted.
    txt::SessionInfo missing;
    txt::SessionTabInfo missingTab;
    missingTab.id = "missing1";
    missingTab.path.clear();
    missingTab.modified = true;
    missingTab.contentFile = "tab-missing1.utf8";
    missing.tabs.push_back(missingTab);
    CHECK(!txt::SaveSession(dir.string(), missing, &error));

    CHECK(txt::ClearSession(dir.string(), &error));
    CHECK(!fs::exists(dir / "manifest"));
    CHECK(!fs::exists(dir / "tab-dirty1.utf8"));
}

void TestSessionCorruption() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "txt-editor-session-corrupt";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const txt::SessionLoadResult none = txt::LoadSession(dir.string());
    CHECK(none.ok);
    CHECK(!none.found);

    {
        std::ofstream out(dir / "manifest", std::ios::binary | std::ios::trunc);
        out << "not a session\n";
    }
    txt::SessionLoadResult bad = txt::LoadSession(dir.string());
    CHECK(bad.found);
    CHECK(!bad.ok);

    {
        std::ofstream out(dir / "manifest", std::ios::binary | std::ios::trunc);
        out << "txt-editor-session 1\n"
               "active 0\n"
               "tab id=abc path= modified=1 encoding=0 eol=0 language=0 large=0 "
               "anchor=0 caret=0 scroll=0 zoom=0 content=tab-abc.utf8\n";
    }
    bad = txt::LoadSession(dir.string());
    CHECK(bad.found);
    CHECK(!bad.ok);

    {
        std::ofstream out(dir / "manifest", std::ios::binary | std::ios::trunc);
        out << "txt-editor-session 1\nactive 4\n";
    }
    bad = txt::LoadSession(dir.string());
    CHECK(bad.found);
    CHECK(!bad.ok);

    const std::string id = txt::MakeSessionId();
    CHECK(id.size() == 32);
    CHECK(std::all_of(id.begin(), id.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    }));

    fs::remove_all(dir, ec);
}


std::vector<std::string_view> ViewsOf(const std::vector<std::string>& lines) {
    std::vector<std::string_view> views;
    views.reserve(lines.size());
    for (const std::string& line : lines) views.push_back(line);
    return views;
}

bool IsMonotonic(const std::vector<int>& mapping) {
    return std::is_sorted(mapping.begin(), mapping.end());
}

void TestSplitLines() {
    using txt::diff::SplitLines;

    const std::string empty;
    const auto emptyLines = SplitLines(empty);
    CHECK(emptyLines.size() == 1);
    CHECK(emptyLines[0].empty());

    const std::string trailing = "a\n";
    const auto trailingLines = SplitLines(trailing);
    CHECK(trailingLines.size() == 2);
    CHECK(trailingLines[0] == "a");
    CHECK(trailingLines[1].empty());

    const std::string crlf = "a\r\nb\r\n";
    const auto crlfLines = SplitLines(crlf);
    CHECK(crlfLines.size() == 3);
    CHECK(crlfLines[0] == "a");
    CHECK(crlfLines[1] == "b");
    CHECK(crlfLines[2].empty());

    const std::string cr = "a\rb\rc";
    const auto crLines = SplitLines(cr);
    CHECK(crLines.size() == 3);
    CHECK(crLines[0] == "a");
    CHECK(crLines[1] == "b");
    CHECK(crLines[2] == "c");

    const std::string mixed = "a\r\n\nb";
    const auto mixedLines = SplitLines(mixed);
    CHECK(mixedLines.size() == 3);
    CHECK(mixedLines[0] == "a");
    CHECK(mixedLines[1].empty());
    CHECK(mixedLines[2] == "b");
}

void TestDiffIdentical() {
    const std::vector<std::string> lines = {"alpha", "beta", "中文", ""};
    const auto views = ViewsOf(lines);
    const txt::diff::Alignment alignment = txt::diff::AlignToBase(views, views);
    CHECK(!alignment.limited);
    CHECK(alignment.baseKinds ==
          std::vector<txt::diff::LineKind>(4, txt::diff::LineKind::Same));
    CHECK(alignment.paneKinds ==
          std::vector<txt::diff::LineKind>(4, txt::diff::LineKind::Same));
    CHECK(alignment.baseToPane == std::vector<int>({0, 1, 2, 3}));
    CHECK(alignment.paneToBase == std::vector<int>({0, 1, 2, 3}));
    CHECK(alignment.stats.added == 0);
    CHECK(alignment.stats.removed == 0);
    CHECK(alignment.stats.modified == 0);
}

void TestDiffReplace() {
    const std::vector<std::string> base = {"a", "b", "c", "d", "e"};
    const std::vector<std::string> pane = {"a", "b", "x", "d", "e"};
    const txt::diff::Alignment alignment =
        txt::diff::AlignToBase(ViewsOf(base), ViewsOf(pane));
    using txt::diff::LineKind;
    CHECK(alignment.baseKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Same, LineKind::Modified,
                                 LineKind::Same, LineKind::Same}));
    CHECK(alignment.paneKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Same, LineKind::Modified,
                                 LineKind::Same, LineKind::Same}));
    CHECK(alignment.baseToPane == std::vector<int>({0, 1, 2, 3, 4}));
    CHECK(alignment.stats.modified == 1);
    CHECK(alignment.stats.added == 0);
    CHECK(alignment.stats.removed == 0);
}

void TestDiffInsertDelete() {
    using txt::diff::LineKind;

    const std::vector<std::string> base = {"a", "b", "c"};
    const std::vector<std::string> inserted = {"a", "x", "b", "c"};
    const txt::diff::Alignment add =
        txt::diff::AlignToBase(ViewsOf(base), ViewsOf(inserted));
    CHECK(add.paneKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Added, LineKind::Same,
                                 LineKind::Same}));
    CHECK(add.baseToPane == std::vector<int>({0, 2, 3}));
    CHECK(add.paneToBase == std::vector<int>({0, 0, 1, 2}));
    CHECK(add.stats.added == 1);
    CHECK(add.stats.removed == 0);
    CHECK(add.stats.modified == 0);

    const std::vector<std::string> removedBase = {"a", "x", "b", "c"};
    const txt::diff::Alignment remove =
        txt::diff::AlignToBase(ViewsOf(removedBase), ViewsOf(base));
    CHECK(remove.baseKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Removed, LineKind::Same,
                                 LineKind::Same}));
    CHECK(remove.baseToPane == std::vector<int>({0, 0, 1, 2}));
    CHECK(remove.paneToBase == std::vector<int>({0, 2, 3}));
    CHECK(remove.stats.removed == 1);
    CHECK(remove.stats.added == 0);
    CHECK(remove.stats.modified == 0);
}


void TestDiffBlockAndEdges() {
    using txt::diff::LineKind;

    const std::vector<std::string> base = {"a", "b", "c", "d"};
    const std::vector<std::string> pane = {"a", "x", "d"};
    const txt::diff::Alignment block =
        txt::diff::AlignToBase(ViewsOf(base), ViewsOf(pane));
    CHECK(block.baseKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Modified,
                                 LineKind::Removed, LineKind::Same}));
    CHECK(block.paneKinds ==
          std::vector<LineKind>({LineKind::Same, LineKind::Modified, LineKind::Same}));
    CHECK(block.stats.modified == 1);
    CHECK(block.stats.removed == 1);
    CHECK(block.stats.added == 0);

    const std::vector<std::string> emptyBase = {""};
    const std::vector<std::string> oneLine = {"a"};
    const txt::diff::Alignment empty =
        txt::diff::AlignToBase(ViewsOf(emptyBase), ViewsOf(oneLine));
    CHECK(empty.stats.modified == 1);
    CHECK(empty.baseToPane == std::vector<int>({0}));
    CHECK(empty.paneToBase == std::vector<int>({0}));

    const std::vector<std::string> differentBase = {"a", "b"};
    const std::vector<std::string> differentPane = {"c", "d"};
    const txt::diff::Alignment different =
        txt::diff::AlignToBase(ViewsOf(differentBase), ViewsOf(differentPane));
    CHECK(different.stats.modified == 2);
    CHECK(different.baseToPane == std::vector<int>({0, 1}));
    CHECK(different.paneToBase == std::vector<int>({0, 1}));
}

void TestDiffMappings() {
    const std::vector<std::string> base = {"one", "two", "three", "four",
                                            "five", "six", "seven"};
    const std::vector<std::string> pane = {"zero", "one", "inserted", "two",
                                            "four", "five", "six", "seven"};
    const txt::diff::Alignment alignment =
        txt::diff::AlignToBase(ViewsOf(base), ViewsOf(pane));
    CHECK(alignment.baseToPane.size() == base.size());
    CHECK(alignment.paneToBase.size() == pane.size());
    CHECK(IsMonotonic(alignment.baseToPane));


    CHECK(IsMonotonic(alignment.paneToBase));
    CHECK(alignment.baseToPane.back() <= static_cast<int>(pane.size()) - 1);
    CHECK(alignment.paneToBase.back() <= static_cast<int>(base.size()) - 1);
    for (std::size_t i = 0; i < alignment.baseToPane.size(); ++i) {
        const int mapped = alignment.baseToPane[i];
        CHECK(mapped >= 0);
        CHECK(mapped < static_cast<int>(pane.size()));
        CHECK(alignment.paneToBase[static_cast<std::size_t>(mapped)] >= 0);
    }
}


void TestDiffRandomInvariants() {
    using txt::diff::LineKind;

    std::uint32_t state = 0x12345678u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };

    for (int iteration = 0; iteration < 600; ++iteration) {
        std::vector<std::string> base;
        std::vector<std::string> pane;
        const std::size_t baseSize = 1u + next() % 23u;
        const std::size_t paneSize = 1u + next() % 23u;
        for (std::size_t i = 0; i < baseSize; ++i) {
            base.push_back("l" + std::to_string(next() % 6u));
        }
        for (std::size_t i = 0; i < paneSize; ++i) {
            pane.push_back("l" + std::to_string(next() % 6u));
        }

        const txt::diff::Alignment alignment =
            txt::diff::AlignToBase(ViewsOf(base), ViewsOf(pane));
        CHECK(!alignment.limited);
        CHECK(alignment.baseKinds.size() == base.size());
        CHECK(alignment.paneKinds.size() == pane.size());
        CHECK(alignment.baseToPane.size() == base.size());
        CHECK(alignment.paneToBase.size() == pane.size());
        CHECK(IsMonotonic(alignment.baseToPane));
        CHECK(IsMonotonic(alignment.paneToBase));

        for (std::size_t i = 0; i < alignment.baseToPane.size(); ++i) {
            CHECK(alignment.baseToPane[i] >= 0);
            CHECK(alignment.baseToPane[i] < static_cast<int>(pane.size()));
        }
        for (std::size_t i = 0; i < alignment.paneToBase.size(); ++i) {
            CHECK(alignment.paneToBase[i] >= 0);
            CHECK(alignment.paneToBase[i] < static_cast<int>(base.size()));
        }

        for (std::size_t i = 0; i < base.size(); ++i) {
            if (alignment.baseKinds[i] != LineKind::Same) continue;
            const std::size_t mapped = static_cast<std::size_t>(alignment.baseToPane[i]);
            CHECK(base[i] == pane[mapped]);
        }
        for (std::size_t i = 0; i < pane.size(); ++i) {
            if (alignment.paneKinds[i] != LineKind::Same) continue;
            const std::size_t mapped = static_cast<std::size_t>(alignment.paneToBase[i]);
            CHECK(pane[i] == base[mapped]);
        }
    }
}

void TestDiffLargeFallback() {
    std::vector<std::string> base;
    std::vector<std::string> pane;
    base.reserve(60000);
    pane.reserve(60000);
    for (int i = 0; i < 60000; ++i) {
        base.push_back("base-" + std::to_string(i));
        pane.push_back("pane-" + std::to_string(i));
    }

    const txt::diff::Alignment alignment =
        txt::diff::AlignToBase(ViewsOf(base), ViewsOf(pane));
    CHECK(alignment.limited);
    CHECK(alignment.baseToPane.size() == base.size());
    CHECK(alignment.paneToBase.size() == pane.size());
    CHECK(IsMonotonic(alignment.baseToPane));
    CHECK(IsMonotonic(alignment.paneToBase));
    CHECK(alignment.stats.added == 0);
    CHECK(alignment.stats.removed == 0);
    CHECK(alignment.stats.modified == 0);
}

}  // namespace

int main() {
    TestUtf8Validation();
    TestBomAndUtf16();
    TestEol();
    TestLanguage();
    TestDocumentRoundTrip();
    TestNonAsciiPath();
    TestSessionRoundTrip();
    TestSessionCorruption();
    TestSplitLines();
    TestDiffIdentical();
    TestDiffReplace();
    TestDiffInsertDelete();
    TestDiffBlockAndEdges();
    TestDiffMappings();
    TestDiffRandomInvariants();
    TestDiffLargeFallback();
    if (g_failures == 0) {
        std::cout << "all core tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
}
