#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

}  // namespace

int main() {
    TestUtf8Validation();
    TestBomAndUtf16();
    TestEol();
    TestLanguage();
    TestDocumentRoundTrip();
    TestSessionRoundTrip();
    TestSessionCorruption();
    if (g_failures == 0) {
        std::cout << "all core tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
}
