#pragma once

#include <string>
#include <string_view>

#include "core/TextTypes.h"

namespace txt {

struct LoadResult {
    bool ok = false;
    std::string utf8;
    DocumentMeta meta;
    FileStamp stamp;
    std::string error;
};

// Reads a file as bytes, detects encoding/EOL/language and returns UTF-8.
// Never throws; check `ok`.
LoadResult LoadDocument(const std::string& path);

// Inspects size and modification time without reading the content. Returns
// false and leaves `stamp` invalid when the file is missing or unreadable.
bool ReadFileStamp(const std::string& path, FileStamp* stamp);

// Compares two stamps. Unknown stamps are never considered equal: callers must
// check `valid` before treating "not same" as an external modification.
bool SameFileStamp(const FileStamp& left, const FileStamp& right);

// Atomically writes UTF-8 text using the encoding/EOL settings in `meta`.
// Returns false and fills `error` on failure; the original file is untouched.
bool SaveDocument(const std::string& path, std::string_view utf8,
                  const DocumentMeta& meta, std::string* error);

}  // namespace txt
