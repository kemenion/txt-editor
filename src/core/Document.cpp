#include "core/Document.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "core/Encoding.h"
#include "core/Language.h"

namespace txt {
namespace {

std::string TempSuffix() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return ".tmp-" + std::to_string(stamp);
}

}  // namespace

LoadResult LoadDocument(const std::string& path) {
    LoadResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        result.error = "Cannot open file: " + path;
        return result;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        result.error = "Cannot determine file size: " + path;
        return result;
    }
    in.seekg(0, std::ios::beg);

    std::string bytes;
    bytes.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(bytes.data(), size);
        if (!in) {
            result.error = "Cannot read file: " + path;
            return result;
        }
    }

    result.meta.encoding = DetectEncoding(bytes);
    bool conversionOk = true;
    result.utf8 = ToUtf8(bytes, result.meta.encoding, &conversionOk);
    result.meta.eol = DetectEol(result.utf8);
    result.meta.language = DetectLanguage(path, result.utf8);
    result.meta.largeFile = bytes.size() > kLargeFileThreshold;
    // The stamp describes the file this buffer came from; a failure to read it
    // only disables the external-modification check for this tab.
    ReadFileStamp(path, &result.stamp);
    result.ok = true;
    if (!conversionOk) {
        result.error = "Malformed input; some bytes were replaced.";
    }
    return result;
}

bool ReadFileStamp(const std::string& path, FileStamp* stamp) {
    if (stamp == nullptr) return false;
    *stamp = FileStamp{};

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(path, ec);
    if (ec) return false;

    stamp->size = size;
    stamp->modified = modified;
    stamp->valid = true;
    return true;
}

bool SameFileStamp(const FileStamp& left, const FileStamp& right) {
    if (!left.valid || !right.valid) return false;
    return left.size == right.size && left.modified == right.modified;
}

bool SaveDocument(const std::string& path, std::string_view utf8,
                  const DocumentMeta& meta, std::string* error) {
    const std::string encoded = FromUtf8(utf8, meta.encoding);
    const std::string temp = path + TempSuffix();

    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "Cannot create temporary file next to: " + path;
        return false;
    }
    out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    out.flush();
    if (!out) {
        out.close();
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        if (error) *error = "Cannot write file: " + path;
        return false;
    }
    out.close();

    std::error_code ec;
    const auto targetStatus = std::filesystem::status(path, ec);
    if (!ec && std::filesystem::exists(targetStatus)) {
        std::error_code permEc;
        std::filesystem::permissions(temp, targetStatus.permissions(), permEc);
    }

    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        if (error) *error = "Cannot replace file: " + path + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

}  // namespace txt
