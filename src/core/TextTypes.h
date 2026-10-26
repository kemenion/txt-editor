#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace txt {

enum class Encoding {
    Utf8,
    Utf8Bom,
    Utf16LE,
    Utf16BE,
    Latin1,
};

enum class Eol {
    Lf,
    CrLf,
    Cr,
};

enum class LanguageId {
    Plain,
    Python,
    C,
    Cpp,
    Json,
    Markdown,
    Shell,
    Xml,
    Yaml,
};

struct DocumentMeta {
    Encoding encoding = Encoding::Utf8;
    Eol eol = Eol::Lf;
    LanguageId language = LanguageId::Plain;
    bool largeFile = false;
};

// Identity of a file on disk at load or save time. `valid` is false when the
// file could not be inspected, in which case no conflict detection is possible.
struct FileStamp {
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    bool valid = false;
};

inline constexpr std::size_t kLargeFileThreshold = 32u * 1024u * 1024u;

std::string_view EncodingName(Encoding encoding);
std::string_view EolName(Eol eol);

}  // namespace txt
