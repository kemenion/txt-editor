#include "core/Session.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <system_error>

namespace txt {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kManifestName = "manifest";
constexpr std::string_view kManifestHeader = "txt-editor-session 1";
constexpr std::size_t kMaxManifestBytes = 16u * 1024u * 1024u;

constexpr fs::perms kDirectoryPermissions = fs::perms::owner_all;
constexpr fs::perms kFilePermissions =
    fs::perms::owner_read | fs::perms::owner_write;

std::string TempSuffix() {
    static std::atomic<unsigned long long> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return ".tmp-" + std::to_string(stamp) + "-" +
           std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

bool EnsureDirectory(const std::string& directory, std::string* error) {
    if (directory.empty()) {
        if (error) *error = "Session directory is not available.";
        return false;
    }

    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec && !fs::is_directory(directory)) {
        if (error) {
            *error = "Cannot create session directory: " + directory + " (" +
                     ec.message() + ")";
        }
        return false;
    }
    if (!fs::is_directory(directory, ec) || ec) {
        if (error) *error = "Session path is not a directory: " + directory;
        return false;
    }

    fs::permissions(directory, kDirectoryPermissions, fs::perm_options::replace, ec);
    if (ec) {
        if (error) {
            *error = "Cannot restrict session directory permissions: " + directory +
                     " (" + ec.message() + ")";
        }
        return false;
    }
    return true;
}

bool ReadWholeFile(const fs::path& path, std::size_t maxBytes, std::string* bytes,
                   std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "Cannot open session file: " + path.string();
        return false;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        if (error) *error = "Cannot determine session file size: " + path.string();
        return false;
    }
    if (maxBytes > 0 && static_cast<std::size_t>(size) > maxBytes) {
        if (error) *error = "Session file is too large: " + path.string();
        return false;
    }
    in.seekg(0, std::ios::beg);

    bytes->resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(bytes->data(), size);
        if (!in) {
            if (error) *error = "Cannot read session file: " + path.string();
            return false;
        }
    }
    return true;
}

bool WriteFileAtomic(const fs::path& path, std::string_view bytes, std::string* error) {
    const std::string temp = path.string() + TempSuffix();
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "Cannot create temporary session file: " + temp;
        return false;
    }

    std::error_code ec;
    fs::permissions(temp, kFilePermissions, fs::perm_options::replace, ec);

    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        out.close();
        std::error_code ignored;
        fs::remove(temp, ignored);
        if (error) *error = "Cannot write session file: " + path.string();
        return false;
    }
    out.close();

    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        if (error) {
            *error = "Cannot replace session file: " + path.string() + " (" +
                     ec.message() + ")";
        }
        return false;
    }
    return true;
}

std::string HexEncode(std::string_view bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        encoded.push_back(kHex[value >> 4]);
        encoded.push_back(kHex[value & 0x0f]);
    }
    return encoded;
}

int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool HexDecode(std::string_view encoded, std::string* bytes) {
    if (encoded.size() % 2 != 0) return false;
    bytes->clear();
    bytes->reserve(encoded.size() / 2);
    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        const int high = HexValue(encoded[i]);
        const int low = HexValue(encoded[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes->push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool IsSafeId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    return std::all_of(id.begin(), id.end(), [](char value) {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
    });
}

std::string ContentName(std::string_view id) {
    return "tab-" + std::string(id) + ".utf8";
}

bool IsContentFileFor(std::string_view file, std::string_view id) {
    return file == ContentName(id);
}

bool IsSnapshotName(std::string_view name) {
    return name.rfind("tab-", 0) == 0 && name.size() >= 5 &&
           name.substr(name.size() - 5) == ".utf8";
}

bool ParseInt(std::string_view text, int* value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, *value);
    return result.ec == std::errc() && result.ptr == end;
}

bool SplitValue(std::string_view token, std::string_view key, std::string_view* value) {
    if (token.size() <= key.size() || token.substr(0, key.size()) != key ||
        token[key.size()] != '=') {
        return false;
    }
    *value = token.substr(key.size() + 1);
    return true;
}

bool ValidEncoding(int value) {
    return value >= static_cast<int>(Encoding::Utf8) &&
           value <= static_cast<int>(Encoding::Latin1);
}

bool ValidEol(int value) {
    return value >= static_cast<int>(Eol::Lf) &&
           value <= static_cast<int>(Eol::Cr);
}

bool ValidLanguage(int value) {
    return value >= static_cast<int>(LanguageId::Plain) &&
           value <= static_cast<int>(LanguageId::Yaml);
}

bool ParseTabLine(std::string_view line, SessionTabInfo* tab, std::string* error) {
    std::istringstream input{std::string(line.substr(4))};
    std::string token;

    std::optional<std::string> id;
    std::optional<std::string> pathHex;
    std::optional<std::string> content;
    std::optional<int> modified;
    std::optional<int> encoding;
    std::optional<int> eol;
    std::optional<int> language;
    std::optional<int> large;
    std::optional<int> anchor;
    std::optional<int> caret;
    std::optional<int> scroll;
    std::optional<int> zoom;

    while (input >> token) {
        std::string_view value;
        if (SplitValue(token, "id", &value)) {
            id = std::string(value);
        } else if (SplitValue(token, "path", &value)) {
            pathHex = std::string(value);
        } else if (SplitValue(token, "modified", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            modified = parsed;
        } else if (SplitValue(token, "encoding", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            encoding = parsed;
        } else if (SplitValue(token, "eol", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            eol = parsed;
        } else if (SplitValue(token, "language", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            language = parsed;
        } else if (SplitValue(token, "large", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            large = parsed;
        } else if (SplitValue(token, "anchor", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            anchor = parsed;
        } else if (SplitValue(token, "caret", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            caret = parsed;
        } else if (SplitValue(token, "scroll", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            scroll = parsed;
        } else if (SplitValue(token, "zoom", &value)) {
            int parsed = 0;
            if (!ParseInt(value, &parsed)) return false;
            zoom = parsed;
        } else if (SplitValue(token, "content", &value)) {
            content = std::string(value);
        } else {
            return false;
        }
    }


    if (!id || !pathHex || !content || !modified || !encoding || !eol || !language ||
        !large || !anchor || !caret || !scroll || !zoom) {
        return false;
    }
    if (!IsSafeId(*id)) return false;
    if (*modified != 0 && *modified != 1) return false;
    if (*large != 0 && *large != 1) return false;
    if (!ValidEncoding(*encoding) || !ValidEol(*eol) || !ValidLanguage(*language)) {
        return false;
    }
    if (*anchor < 0 || *caret < 0 || *scroll < 0) return false;
    if (*zoom < -100 || *zoom > 100) return false;

    std::string path;
    if (!HexDecode(*pathHex, &path)) return false;

    const bool needsContent = path.empty() || *modified != 0;
    if (needsContent && *content == "no") {
        if (error) *error = "Session tab is missing its content file.";
        return false;
    }
    if (!needsContent && *content != "no") {
        if (error) *error = "Session tab unexpectedly has a content file.";
        return false;
    }
    if (*content != "no" && !IsContentFileFor(*content, *id)) {
        if (error) *error = "Session content file does not match its tab id.";
        return false;
    }

    tab->id = *id;
    tab->path = std::move(path);
    tab->meta.encoding = static_cast<Encoding>(*encoding);
    tab->meta.eol = static_cast<Eol>(*eol);
    tab->meta.language = static_cast<LanguageId>(*language);
    tab->meta.largeFile = *large != 0;
    tab->modified = *modified != 0;
    tab->anchor = *anchor;
    tab->caret = *caret;
    tab->firstVisibleLine = *scroll;
    tab->zoom = *zoom;
    tab->contentFile = *content == "no" ? std::string() : *content;
    return true;
}

}  // namespace

std::string DefaultSessionDirectory() {
    const char* stateHome = std::getenv("XDG_STATE_HOME");
    if (stateHome != nullptr && *stateHome != '\0') {
        return (fs::path(stateHome) / "txt-editor" / "session").string();
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return (fs::path(home) / ".local" / "state" / "txt-editor" / "session").string();
    }
    return {};
}

std::string MakeSessionId() {
    static std::atomic<unsigned long long> counter{0};
    unsigned long long value = 0;
    try {
        std::random_device device;
        value = (static_cast<unsigned long long>(device()) << 32) ^ device();
    } catch (...) {
        value = 0;
    }
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const unsigned long long serial =
        counter.fetch_add(1, std::memory_order_relaxed);
    value ^= stamp;
    value ^= serial * 0x9e3779b97f4a7c15ULL;

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value
        << std::setw(16) << (serial + 1);
    return out.str();
}


SessionLoadResult LoadSession(const std::string& directory) {
    SessionLoadResult result;
    if (directory.empty()) {
        result.ok = true;
        return result;
    }

    const fs::path manifest = fs::path(directory) / std::string(kManifestName);
    std::error_code ec;
    const bool exists = fs::exists(manifest, ec);
    if (ec) {
        result.found = true;
        result.error = "Cannot access session manifest: " + manifest.string();
        return result;
    }
    if (!exists) {
        result.ok = true;
        return result;
    }
    result.found = true;

    std::string text;
    std::string readError;
    if (!ReadWholeFile(manifest, kMaxManifestBytes, &text, &readError)) {
        result.error = readError;
        return result;
    }

    std::istringstream input(text);
    std::string line;
    bool haveHeader = false;
    bool haveActive = false;
    int active = 0;
    std::set<std::string> ids;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (!haveHeader) {
            if (line != kManifestHeader) {
                result.error = "Unsupported or malformed session manifest.";
                return result;
            }
            haveHeader = true;
            continue;
        }

        if (line.rfind("active ", 0) == 0) {
            if (haveActive || !ParseInt(std::string_view(line).substr(7), &active)) {
                result.error = "Malformed active-tab entry in session manifest.";
                return result;
            }
            haveActive = true;
            continue;
        }

        if (line.rfind("tab ", 0) == 0) {
            SessionTabInfo tab;
            std::string parseError;
            if (!ParseTabLine(line, &tab, &parseError)) {
                result.error = parseError.empty() ? "Malformed tab entry in session manifest."
                                                  : parseError;
                return result;
            }
            if (!ids.insert(tab.id).second) {
                result.error = "Duplicate tab id in session manifest.";
                return result;
            }
            if (!tab.contentFile.empty()) {
                const fs::path content = fs::path(directory) / tab.contentFile;
                std::error_code contentEc;
                if (!fs::is_regular_file(content, contentEc) || contentEc) {
                    result.error = "Session content file is missing: " + content.string();
                    return result;
                }
            }
            result.session.tabs.push_back(std::move(tab));
            continue;
        }

        result.error = "Unknown entry in session manifest.";
        return result;
    }

    if (!haveHeader || !haveActive) {
        result.error = "Truncated session manifest.";
        return result;
    }
    if (result.session.tabs.empty()) {
        if (active != 0) {
            result.error = "Active tab index is invalid.";
            return result;
        }
        result.session.active = 0;
    } else {
        if (active < 0 || active >= static_cast<int>(result.session.tabs.size())) {
            result.error = "Active tab index is out of range.";
            return result;
        }
        result.session.active = active;
    }

    result.ok = true;
    return result;
}

bool ReadSessionContent(const std::string& directory, const std::string& contentFile,
                        std::string* utf8, std::string* error) {
    if (directory.empty() || contentFile.empty()) {
        if (error) *error = "Session content file is not available.";
        return false;
    }
    const fs::path path = fs::path(directory) / contentFile;
    return ReadWholeFile(path, 0, utf8, error);
}

bool WriteSessionContent(const std::string& directory, const std::string& id,
                         std::string_view utf8, std::string* contentFile,
                         std::string* error) {
    if (!IsSafeId(id)) {
        if (error) *error = "Invalid session tab id.";
        return false;
    }
    if (!EnsureDirectory(directory, error)) return false;

    const std::string name = ContentName(id);
    if (!WriteFileAtomic(fs::path(directory) / name, utf8, error)) return false;
    if (contentFile) *contentFile = name;
    return true;
}


bool SaveSession(const std::string& directory, const SessionInfo& session,
                 std::string* error) {
    if (!EnsureDirectory(directory, error)) return false;

    std::set<std::string> ids;
    std::set<std::string> referencedFiles;
    for (const SessionTabInfo& tab : session.tabs) {
        if (!IsSafeId(tab.id)) {
            if (error) *error = "Invalid session tab id.";
            return false;
        }
        if (!ids.insert(tab.id).second) {
            if (error) *error = "Duplicate session tab id.";
            return false;
        }

        const bool needsContent = tab.path.empty() || tab.modified;
        if (needsContent) {
            if (!IsContentFileFor(tab.contentFile, tab.id)) {
                if (error) *error = "Session tab is missing its content file.";
                return false;
            }
        } else if (!tab.contentFile.empty()) {
            if (error) *error = "Clean session tab unexpectedly has content.";
            return false;
        }

        if (!tab.contentFile.empty()) {
            const fs::path content = fs::path(directory) / tab.contentFile;
            std::error_code ec;
            if (!fs::is_regular_file(content, ec) || ec) {
                if (error) {
                    *error = "Session content file is missing: " + content.string();
                }
                return false;
            }
            referencedFiles.insert(tab.contentFile);
        }
    }

    if (session.tabs.empty()) {
        if (session.active != 0) {
            if (error) *error = "Active tab index is invalid.";
            return false;
        }
    } else if (session.active < 0 ||
               session.active >= static_cast<int>(session.tabs.size())) {
        if (error) *error = "Active tab index is out of range.";
        return false;
    }

    std::ostringstream out;
    out << kManifestHeader << '\n';
    out << "active " << session.active << '\n';
    for (const SessionTabInfo& tab : session.tabs) {
        out << "tab id=" << tab.id << " path=" << HexEncode(tab.path)
            << " modified=" << (tab.modified ? 1 : 0)
            << " encoding=" << static_cast<int>(tab.meta.encoding)
            << " eol=" << static_cast<int>(tab.meta.eol)
            << " language=" << static_cast<int>(tab.meta.language)
            << " large=" << (tab.meta.largeFile ? 1 : 0)
            << " anchor=" << tab.anchor << " caret=" << tab.caret
            << " scroll=" << tab.firstVisibleLine << " zoom=" << tab.zoom
            << " content=" << (tab.contentFile.empty() ? "no" : tab.contentFile)
            << '\n';
    }

    if (!WriteFileAtomic(fs::path(directory) / std::string(kManifestName),
                         out.str(), error)) {
        return false;
    }

    // The manifest is now the source of truth. Snapshots from removed tabs can
    // be deleted without risking the current session.
    std::error_code ec;
    fs::directory_iterator iterator(directory, ec);
    if (!ec) {
        for (const fs::directory_entry& entry : iterator) {
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (IsSnapshotName(name) &&
                referencedFiles.find(name) == referencedFiles.end()) {
                std::error_code removeEc;
                fs::remove(entry.path(), removeEc);
            }
        }
    }
    return true;
}


bool ClearSession(const std::string& directory, std::string* error) {
    if (directory.empty()) return true;

    std::error_code ec;
    if (!fs::exists(directory, ec)) {
        if (ec) {
            if (error) *error = "Cannot access session directory: " + directory;
            return false;
        }
        return true;
    }

    fs::remove(fs::path(directory) / std::string(kManifestName), ec);
    if (ec) {
        if (error) *error = "Cannot remove session manifest: " + ec.message();
        return false;
    }

    fs::directory_iterator iterator(directory, ec);
    if (ec) {
        if (error) *error = "Cannot list session directory: " + ec.message();
        return false;
    }
    bool ok = true;
    for (const fs::directory_entry& entry : iterator) {
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (IsSnapshotName(name)) {
            std::error_code removeEc;
            fs::remove(entry.path(), removeEc);
            if (removeEc) {
                if (error) *error = "Cannot remove session content: " + removeEc.message();
                ok = false;
            }
        }
    }

    fs::remove(directory, ec);
    if (ec) {
        // Unknown files may keep the directory alive; that is harmless.
        ec.clear();
    }
    return ok;
}

}  // namespace txt

