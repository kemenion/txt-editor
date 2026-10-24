#pragma once

#include <string>
#include <string_view>

#include "core/TextTypes.h"

namespace txt {

// Detects UTF-8 BOM, UTF-16 BOM, valid UTF-8, then falls back to Latin-1.
Encoding DetectEncoding(std::string_view bytes);

// Counts CRLF, lone LF and lone CR; returns the dominant form (LF on tie).
Eol DetectEol(std::string_view text);

// Converts bytes in `encoding` to UTF-8. `ok` is set false for malformed UTF-16
// (odd byte count) or invalid UTF-8 that had to be treated as Latin-1.
std::string ToUtf8(std::string_view bytes, Encoding encoding, bool* ok);

// Encodes UTF-8 into `encoding`. Latin-1 is lossy: code points > U+00FF become '?'.
std::string FromUtf8(std::string_view utf8, Encoding encoding);

bool IsValidUtf8(std::string_view text);

}  // namespace txt
