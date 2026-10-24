#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/TextTypes.h"

namespace txt {

// Metadata for one tab in the persisted session. Content is kept in a
// separate file so unmodified files are never copied and tabs can be loaded
// lazily on the next start.
struct SessionTabInfo {
    std::string id;
    std::string path;  // empty => untitled buffer
    DocumentMeta meta;
    bool modified = false;
    int anchor = 0;
    int caret = 0;
    int firstVisibleLine = 0;
    int zoom = 0;
    std::string contentFile;  // relative file name, empty for clean files
};

struct SessionInfo {
    std::vector<SessionTabInfo> tabs;
    int active = 0;
};

struct SessionLoadResult {
    bool ok = false;
    bool found = false;
    SessionInfo session;
    std::string error;
};

// Returns the XDG state directory used for the session; empty if HOME is
// unavailable. No filesystem changes are made.
std::string DefaultSessionDirectory();

// Generates a safe identifier for a tab. The value only contains [a-z0-9].
std::string MakeSessionId();

// Reads and validates the manifest. A missing manifest is reported as
// `found=false, ok=true`; malformed or incomplete state as `ok=false`.
SessionLoadResult LoadSession(const std::string& directory);

// Reads the UTF-8 snapshot referenced by a tab. The tab must come from a
// successful LoadSession() or from a preceding WriteSessionContent().
bool ReadSessionContent(const std::string& directory, const std::string& contentFile,
                        std::string* utf8, std::string* error);

// Atomically writes one tab snapshot and returns its manifest-relative name.
bool WriteSessionContent(const std::string& directory, const std::string& id,
                         std::string_view utf8, std::string* contentFile,
                         std::string* error);

// Atomically replaces the manifest and removes snapshots no longer referenced.
// Content files referenced by tabs must already exist.
bool SaveSession(const std::string& directory, const SessionInfo& session,
                 std::string* error);

// Removes the manifest and all tab snapshots. Missing files are not an error.
bool ClearSession(const std::string& directory, std::string* error);

}  // namespace txt
