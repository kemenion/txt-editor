#include <algorithm>
#include <chrono>
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


void TestSessionInstanceList() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "txt-editor-session-instances";
    std::error_code ec;
    fs::remove_all(root, ec);

    std::string error;
    txt::SessionOwner owner;
    owner.pid = 4242;
    owner.host = "host-a";
    owner.sequence = 100;
    std::string first;
    CHECK(txt::CreateSessionInstance(root.string(), owner, &first, &error));
    CHECK(fs::is_directory(first));
    CHECK(fs::path(first).parent_path() == root / "instances");
    const fs::perms rootPermissions = fs::status(root).permissions();
    CHECK((rootPermissions & fs::perms::group_read) == fs::perms::none);
    CHECK((rootPermissions & fs::perms::others_read) == fs::perms::none);
    const fs::perms instancePermissions = fs::status(first).permissions();
    CHECK((instancePermissions & fs::perms::others_read) == fs::perms::none);

    owner.pid = 4343;
    owner.host = "host-b";
    owner.sequence = 200;
    std::string second;
    CHECK(txt::CreateSessionInstance(root.string(), owner, &second, &error));
    CHECK(first != second);

    std::vector<txt::SessionInstance> instances =
        txt::ListSessionInstances(root.string());
    CHECK(instances.size() == 2);
    CHECK(instances[0].directory == first);
    CHECK(instances[1].directory == second);
    CHECK(instances[0].owner.pid == 4242);
    CHECK(instances[0].owner.host == "host-a");
    CHECK(instances[0].owner.sequence == 100);
    CHECK(!instances[0].legacy);
    CHECK(!instances[0].hasManifest);

    txt::SessionInfo empty;
    CHECK(txt::SaveSession(first, empty, &error));
    CHECK(txt::ListSessionInstances(root.string())[0].hasManifest);

    // Names that are not session ids are never listed, so a stray directory
    // cannot be restored or removed as a session.
    fs::create_directories(root / "instances" / "Not-A-Session");
    CHECK(txt::ListSessionInstances(root.string()).size() == 2);

    // An instance without an owner file is listed but has no pid, which the UI
    // treats as dead.
    fs::create_directories(root / "instances" / "abcdef");
    instances = txt::ListSessionInstances(root.string());
    CHECK(instances.size() == 3);
    CHECK(instances[0].directory == (root / "instances" / "abcdef").string());
    CHECK(instances[0].owner.pid == 0);

    // The pre-instance layout is listed first so its tabs keep their order.
    {
        std::ofstream out(root / "manifest", std::ios::binary | std::ios::trunc);
        out << "txt-editor-session 1\nactive 0\n";
    }
    instances = txt::ListSessionInstances(root.string());
    CHECK(instances.size() == 4);
    CHECK(instances[0].legacy);
    CHECK(instances[0].hasManifest);
    CHECK(instances[0].directory == root.string());

    CHECK(txt::RemoveSessionInstance(first, &error));
    CHECK(!fs::exists(first));
    CHECK(fs::exists(second));

    fs::remove_all(root, ec);
}

void TestSessionInstancesAreIsolated() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "txt-editor-session-isolation";
    std::error_code ec;
    fs::remove_all(root, ec);

    std::string error;
    txt::SessionOwner owner;
    owner.pid = 1111;
    owner.sequence = 10;
    std::string first;
    std::string second;
    CHECK(txt::CreateSessionInstance(root.string(), owner, &first, &error));
    owner.pid = 2222;
    owner.sequence = 20;
    CHECK(txt::CreateSessionInstance(root.string(), owner, &second, &error));

    txt::SessionInfo a;
    txt::SessionTabInfo tab;
    tab.id = "aaaaaa";
    tab.path = "/tmp/a.txt";
    tab.modified = true;
    CHECK(txt::WriteSessionContent(first, tab.id, "A buffer", &tab.contentFile, &error));
    a.tabs.push_back(tab);
    CHECK(txt::SaveSession(first, a, &error));

    txt::SessionInfo b;
    txt::SessionTabInfo other;
    other.id = "bbbbbb";
    other.path.clear();
    CHECK(txt::WriteSessionContent(second, other.id, "B buffer", &other.contentFile,
                                   &error));
    b.tabs.push_back(other);
    CHECK(txt::SaveSession(second, b, &error));

    // Rewriting one instance must not prune the other instance's snapshots:
    // this is the bug that made a second window delete the first one's tabs.
    CHECK(txt::SaveSession(first, a, &error));
    CHECK(fs::exists(fs::path(second) / "tab-bbbbbb.utf8"));
    CHECK(fs::exists(fs::path(second) / "manifest"));
    CHECK(txt::LoadSession(second).ok);
    CHECK(txt::LoadSession(first).ok);

    CHECK(txt::ClearSession(first, &error));
    CHECK(fs::exists(fs::path(second) / "manifest"));
    CHECK(fs::exists(fs::path(second) / "tab-bbbbbb.utf8"));

    // Removing the legacy layout must leave instance directories alone too.
    CHECK(txt::RemoveLegacySession(root.string(), &error));
    CHECK(fs::exists(fs::path(second) / "manifest"));
    CHECK(fs::exists(fs::path(first) / "owner"));

    fs::remove_all(root, ec);
}

void TestSessionMerge() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "txt-editor-session-merge";
    std::error_code ec;
    fs::remove_all(root, ec);

    std::string error;
    const fs::path oldDir = root / "instances" / "aaaaaaaa";
    const fs::path newDir = root / "instances" / "bbbbbbbb";
    const fs::path badDir = root / "instances" / "cccccccc";
    fs::create_directories(oldDir);
    fs::create_directories(newDir);
    fs::create_directories(badDir);

    txt::SessionInfo older;
    txt::SessionTabInfo untitled;
    untitled.id = "oldaaa";
    untitled.path.clear();
    untitled.modified = true;
    CHECK(txt::WriteSessionContent(oldDir.string(), untitled.id, "old untitled",
                                   &untitled.contentFile, &error));
    older.tabs.push_back(untitled);

    txt::SessionTabInfo sharedClean;
    sharedClean.id = "oldbbb";
    sharedClean.path = "/tmp/shared.txt";
    sharedClean.modified = false;
    older.tabs.push_back(sharedClean);

    txt::SessionTabInfo dirty;
    dirty.id = "oldccc";
    dirty.path = "/tmp/dirty.txt";
    dirty.modified = true;
    CHECK(txt::WriteSessionContent(oldDir.string(), dirty.id, "old dirty",
                                   &dirty.contentFile, &error));
    older.tabs.push_back(dirty);
    older.active = 1;
    CHECK(txt::SaveSession(oldDir.string(), older, &error));

    txt::SessionInfo newer;
    txt::SessionTabInfo sharedDirty;
    sharedDirty.id = "newaaa";
    sharedDirty.path = "/tmp/shared.txt";
    sharedDirty.modified = true;
    CHECK(txt::WriteSessionContent(newDir.string(), sharedDirty.id, "new shared",
                                   &sharedDirty.contentFile, &error));
    newer.tabs.push_back(sharedDirty);

    txt::SessionTabInfo dirtyAgain;
    dirtyAgain.id = "newbbb";
    dirtyAgain.path = "/tmp/dirty.txt";
    dirtyAgain.modified = true;
    CHECK(txt::WriteSessionContent(newDir.string(), dirtyAgain.id, "new dirty",
                                   &dirtyAgain.contentFile, &error));
    newer.tabs.push_back(dirtyAgain);

    txt::SessionTabInfo fresh;
    fresh.id = "newccc";
    fresh.path = "/tmp/only-new.txt";
    fresh.modified = false;
    newer.tabs.push_back(fresh);
    newer.active = 1;
    CHECK(txt::SaveSession(newDir.string(), newer, &error));

    {
        std::ofstream out(badDir / "manifest", std::ios::binary | std::ios::trunc);
        out << "txt-editor-session 1\nthis is not a session\n";
    }

    const txt::MergedSession merged =
        txt::MergeSessions({badDir.string(), oldDir.string(), newDir.string()});
    CHECK(merged.errors.size() == 1);
    CHECK(merged.mergedDirectories.size() == 2);
    CHECK(merged.session.tabs.size() == 5);
    CHECK(merged.session.tabs[0].id == "oldaaa");  // untitled tab is kept
    CHECK(merged.session.tabs[1].id == "newaaa");  // modified beats clean
    CHECK(merged.session.tabs[1].path == "/tmp/shared.txt");
    CHECK(merged.session.tabs[2].id == "oldccc");  // both modified: kept twice
    CHECK(merged.session.tabs[3].id == "newbbb");
    CHECK(merged.session.tabs[4].id == "newccc");
    CHECK(merged.sourceDirectory[0] == oldDir.string());
    CHECK(merged.sourceDirectory[1] == newDir.string());
    CHECK(merged.sourceDirectory[2] == oldDir.string());
    CHECK(merged.sourceDirectory[3] == newDir.string());
    CHECK(merged.sourceDirectory[4] == newDir.string());
    CHECK(merged.session.active == 3);  // the newest session's active tab

    // Snapshots can move into the surviving instance before it is written.
    std::string copied;
    CHECK(txt::CopySessionContent(merged.sourceDirectory[2],
                                  merged.session.tabs[2].contentFile, newDir.string(),
                                  merged.session.tabs[2].id, &copied, &error));
    CHECK(copied == "tab-oldccc.utf8");
    std::string text;
    CHECK(txt::ReadSessionContent(newDir.string(), copied, &text, &error));
    CHECK(text == "old dirty");
    const fs::perms permissions = fs::status(newDir / copied).permissions();
    CHECK((permissions & fs::perms::owner_read) != fs::perms::none);
    CHECK((permissions & fs::perms::owner_write) != fs::perms::none);
    CHECK((permissions & fs::perms::group_read) == fs::perms::none);
    CHECK((permissions & fs::perms::others_read) == fs::perms::none);

    // A missing snapshot is a failure, not an empty buffer.
    CHECK(!txt::CopySessionContent(badDir.string(), "tab-oldccc.utf8",
                                   newDir.string(), "oldccc", &copied, &error));
    CHECK(!error.empty());

    fs::remove_all(root, ec);
}

void TestLegacySessionMigration() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "txt-editor-session-legacy";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    std::string error;
    txt::SessionInfo legacy;
    txt::SessionTabInfo tab;
    tab.id = "legacy1";
    tab.path.clear();
    tab.modified = true;
    CHECK(txt::WriteSessionContent(root.string(), tab.id, "legacy text",
                                   &tab.contentFile, &error));
    legacy.tabs.push_back(tab);
    CHECK(txt::SaveSession(root.string(), legacy, &error));

    txt::SessionOwner owner;
    owner.pid = 777;
    owner.sequence = 50;
    std::string instance;
    CHECK(txt::CreateSessionInstance(root.string(), owner, &instance, &error));

    const std::vector<txt::SessionInstance> instances =
        txt::ListSessionInstances(root.string());
    CHECK(instances.size() == 2);
    CHECK(instances[0].legacy);
    CHECK(instances[1].directory == instance);

    const txt::MergedSession merged =
        txt::MergeSessions({root.string(), instance});
    CHECK(merged.errors.empty());
    CHECK(merged.session.tabs.size() == 1);
    CHECK(merged.sourceDirectory[0] == root.string());

    std::string copied;
    CHECK(txt::CopySessionContent(merged.sourceDirectory[0],
                                  merged.session.tabs[0].contentFile, instance,
                                  merged.session.tabs[0].id, &copied, &error));
    std::string text;
    CHECK(txt::ReadSessionContent(instance, copied, &text, &error));
    CHECK(text == "legacy text");

    CHECK(txt::RemoveLegacySession(root.string(), &error));
    CHECK(!fs::exists(root / "manifest"));
    CHECK(!fs::exists(root / "tab-legacy1.utf8"));
    CHECK(fs::is_directory(instance));

    fs::remove_all(root, ec);
}

void TestSessionTemporaries() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "txt-editor-session-tmp";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "instances" / "aaaaaaaa");

    const fs::path staleRoot = root / "tab-legacy1.utf8.tmp-1-1";
    const fs::path freshRoot = root / "manifest.tmp-2-2";
    const fs::path keepRoot = root / "manifest";
    const fs::path staleInstance = root / "instances" / "aaaaaaaa" / "owner.tmp-3-3";
    for (const fs::path& path : {staleRoot, freshRoot, keepRoot, staleInstance}) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "x";
    }
    const fs::file_time_type old =
        fs::file_time_type::clock::now() - std::chrono::hours(48);
    fs::last_write_time(staleRoot, old, ec);
    fs::last_write_time(staleInstance, old, ec);

    std::string error;
    CHECK(txt::CleanSessionTemporaries(root.string(), &error));
    CHECK(!fs::exists(staleRoot));
    CHECK(!fs::exists(staleInstance));
    // Young temporaries may belong to a running instance.
    CHECK(fs::exists(freshRoot));
    CHECK(fs::exists(keepRoot));

    fs::remove_all(root, ec);
}

void TestFileStamp() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "txt-editor-tests";
    fs::create_directories(dir);
    const fs::path path = dir / "stamp.txt";
    std::error_code ec;
    fs::remove(path, ec);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "first\n";
    }

    const txt::LoadResult first = txt::LoadDocument(path.string());
    CHECK(first.ok);
    CHECK(first.stamp.valid);

    txt::FileStamp read;
    CHECK(txt::ReadFileStamp(path.string(), &read));
    CHECK(read.valid);
    CHECK(txt::SameFileStamp(first.stamp, read));

    std::string error;
    CHECK(txt::SaveDocument(path.string(), "second, longer\n", first.meta, &error));
    txt::FileStamp changed;
    CHECK(txt::ReadFileStamp(path.string(), &changed));
    CHECK(changed.valid);
    CHECK(!txt::SameFileStamp(first.stamp, changed));

    // Unknown stamps never look like a conflict on their own.
    CHECK(!txt::ReadFileStamp((dir / "missing-stamp.txt").string(), &changed));
    CHECK(!changed.valid);
    CHECK(!txt::SameFileStamp(first.stamp, changed));
    CHECK(!txt::SameFileStamp(txt::FileStamp{}, txt::FileStamp{}));

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
    TestSessionInstanceList();
    TestSessionInstancesAreIsolated();
    TestSessionMerge();
    TestLegacySessionMigration();
    TestSessionTemporaries();
    TestFileStamp();
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
