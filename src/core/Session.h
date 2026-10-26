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

// Identity of one running editor process. Every process writes its recovery
// state into its own directory, so two windows can never overwrite or prune
// each other's snapshots. The owner file is written before the first manifest
// and lets the next start tell crashed instances from live ones.
struct SessionOwner {
    long pid = 0;
    std::string host;        // UTF-8 host name; empty when unknown
    long long sequence = 0;  // start time in seconds since the epoch
};

struct SessionInstance {
    std::string directory;  // absolute path of the instance directory
    SessionOwner owner;
    bool legacy = false;     // old layout: manifest directly in the root
    bool hasManifest = false;
};

// Result of merging the sessions of dead instances. `sourceDirectory` is
// parallel to `session.tabs` and names the directory that holds the snapshot;
// it is not necessarily the directory the merged session is saved to later.
struct MergedSession {
    SessionInfo session;
    std::vector<std::string> sourceDirectory;
    std::vector<std::string> mergedDirectories;  // sources read successfully
    std::vector<std::string> errors;
};

// Returns the XDG state directory used for the session; empty if HOME is
// unavailable. No filesystem changes are made.
std::string DefaultSessionDirectory();

// Lists the legacy root session plus every instance below `<root>/instances`.
// Entries are ordered oldest first; directories whose name is not a safe id
// are ignored so a stray entry can never be restored or deleted.
std::vector<SessionInstance> ListSessionInstances(const std::string& root);

// Creates `<root>/instances/<id>`, restricts it to the owner and writes the
// owner file. Returns the new directory.
bool CreateSessionInstance(const std::string& root, const SessionOwner& owner,
                           std::string* directory, std::string* error);

// Rewrites the owner file atomically (0600).
bool WriteSessionOwner(const std::string& directory, const SessionOwner& owner,
                       std::string* error);

// Reads the given sessions in order and merges their tabs by path: a modified
// tab wins over a clean one, two modified tabs are both kept because either
// may hold unsaved work. Malformed sessions are skipped and reported in
// `errors`.
MergedSession MergeSessions(const std::vector<std::string>& directories);

// Copies a snapshot into another instance directory, rewriting the file name
// for `id`. Used when an unloaded recovered tab changes owner on save.
bool CopySessionContent(const std::string& sourceDirectory,
                        const std::string& contentFile,
                        const std::string& targetDirectory, const std::string& id,
                        std::string* newContentFile, std::string* error);

// Deletes one dead instance: snapshots, manifest, owner file and directory.
// Callers must verify liveness first; live sessions must never be passed here.
bool RemoveSessionInstance(const std::string& directory, std::string* error);

// Deletes the pre-instance layout (root manifest and root snapshots) after a
// successful migration. `instances/` is left untouched.
bool RemoveLegacySession(const std::string& root, std::string* error);

// Removes `*.tmp-*` leftovers of killed processes older than one day, both in
// the root and in every instance directory. Best effort.
bool CleanSessionTemporaries(const std::string& root, std::string* error);

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
