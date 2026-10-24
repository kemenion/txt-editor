#pragma once

#include <string>
#include <string_view>

#include "core/TextTypes.h"

namespace txt {

struct LoadResult {
    bool ok = false;
    std::string utf8;
    DocumentMeta meta;
    std::string error;
};

// Reads a file as bytes, detects encoding/EOL/language and returns UTF-8.
// Never throws; check `ok`.
LoadResult LoadDocument(const std::string& path);

// Atomically writes UTF-8 text using the encoding/EOL settings in `meta`.
// Returns false and fills `error` on failure; the original file is untouched.
bool SaveDocument(const std::string& path, std::string_view utf8,
                  const DocumentMeta& meta, std::string* error);

}  // namespace txt
