#include "core/Session.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
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
constexpr std::string_view kInstancesDirectory = "instances";
constexpr std::string_view kOwnerName = "owner";
constexpr std::string_view kOwnerHeader = "txt-editor-instance 1";
constexpr std::size_t kMaxManifestBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaxOwnerBytes = 4096;
constexpr std::size_t kMaxMergedTabs = 200;
constexpr std::size_t kCopyBufferBytes = 64u * 1024u;
constexpr std::chrono::hours kTemporaryMaxAge{24};

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

// Temporary files are left behind when a process is killed between writing and
// renaming; they are never referenced by a manifest.
bool IsTemporaryName(std::string_view name) {
    return name.find(".tmp-") != std::string_view::npos;
}

bool ParseLongLong(std::string_view text, long long* value) {
    if (text.empty()) return false;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, *value);
    return result.ec == std::errc() && result.ptr == end;
}

bool ParseOwner(std::string_view text, SessionOwner* owner) {
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kOwnerHeader) return false;

    std::optional<long long> pid;
    std::optional<std::string> hostHex;
    std::optional<long long> sequence;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        const std::string_view view(line);
        if (view.rfind("pid ", 0) == 0) {
            long long parsed = 0;
            if (!ParseLongLong(view.substr(4), &parsed)) return false;
            pid = parsed;
        } else if (view.rfind("host ", 0) == 0) {
            hostHex = std::string(view.substr(5));
        } else if (view.rfind("sequence ", 0) == 0) {
            long long parsed = 0;
            if (!ParseLongLong(view.substr(9), &parsed)) return false;
            sequence = parsed;
        } else {
            return false;
        }
    }
    if (!pid || !hostHex || !sequence || *pid < 0) return false;

    std::string host;
    if (!HexDecode(*hostHex, &host)) return false;
    owner->pid = static_cast<long>(*pid);
    owner->host = std::move(host);
    owner->sequence = *sequence;
    return true;
}

// A missing or malformed owner file is not an error: the instance is then
// simply treated as dead, which is the safe direction for restore and pruning.
bool LoadOwner(const fs::path& directory, SessionOwner* owner) {
    *owner = SessionOwner{};
    std::string text;
    std::string error;
    if (!ReadWholeFile(directory / std::string(kOwnerName), kMaxOwnerBytes, &text,
                       &error)) {
        return false;
    }
    return ParseOwner(text, owner);
}

void CleanTemporariesIn(const fs::path& directory, std::string* error) {
    std::error_code ec;
    fs::directory_iterator iterator(directory, ec);
    if (ec) return;

    const fs::file_time_type cutoff =
        fs::file_time_type::clock::now() - kTemporaryMaxAge;
    for (const fs::directory_entry& entry : iterator) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) continue;
        if (!IsTemporaryName(entry.path().filename().string())) continue;
        // Young files may belong to a process that is writing right now.
        const fs::file_time_type modified = entry.last_write_time(entryEc);
        if (entryEc || modified > cutoff) continue;

        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (removeEc && error) {
            *error = "Cannot remove a temporary session file: " + removeEc.message();
        }
    }
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

std::vector<SessionInstance> ListSessionInstances(const std::string& root) {
    std::vector<SessionInstance> instances;
    if (root.empty()) return instances;

    const fs::path rootPath(root);
    std::error_code ec;
    if (fs::is_regular_file(rootPath / std::string(kManifestName), ec) && !ec) {
        SessionInstance legacy;
        legacy.directory = root;
        legacy.legacy = true;
        legacy.hasManifest = true;
        instances.push_back(std::move(legacy));
    }

    std::vector<SessionInstance> found;
    fs::directory_iterator iterator(rootPath / std::string(kInstancesDirectory), ec);
    if (!ec) {
        for (const fs::directory_entry& entry : iterator) {
            std::error_code entryEc;
            // A symlink would make the snapshot paths below point outside the
            // session tree; only real directories are considered ours.
            if (entry.is_symlink(entryEc) || entryEc) continue;
            if (!entry.is_directory(entryEc) || entryEc) continue;
            const std::string name = entry.path().filename().string();
            if (!IsSafeId(name)) continue;

            SessionInstance instance;
            instance.directory = entry.path().string();
            LoadOwner(entry.path(), &instance.owner);
            std::error_code manifestEc;
            instance.hasManifest = fs::is_regular_file(
                                       entry.path() / std::string(kManifestName),
                                       manifestEc) &&
                                   !manifestEc;
            found.push_back(std::move(instance));
        }
    }

    std::sort(found.begin(), found.end(),
              [](const SessionInstance& left, const SessionInstance& right) {
                  if (left.owner.sequence != right.owner.sequence) {
                      return left.owner.sequence < right.owner.sequence;
                  }
                  return left.directory < right.directory;
              });
    instances.insert(instances.end(), std::make_move_iterator(found.begin()),
                     std::make_move_iterator(found.end()));
    return instances;
}

bool CreateSessionInstance(const std::string& root, const SessionOwner& owner,
                           std::string* directory, std::string* error) {
    if (root.empty()) {
        if (error) *error = "Session directory is not available.";
        return false;
    }

    const fs::path instances = fs::path(root) / std::string(kInstancesDirectory);
    // The root is restricted as well: instance names are harmless, but the
    // documented invariant is an owner-only tree from the root down.
    if (!EnsureDirectory(root, error)) return false;
    if (!EnsureDirectory(instances.string(), error)) return false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        const fs::path candidate = instances / MakeSessionId();
        std::error_code ec;
        if (!fs::create_directory(candidate, ec) || ec) continue;
        if (!EnsureDirectory(candidate.string(), error)) return false;
        if (!WriteSessionOwner(candidate.string(), owner, error)) return false;
        if (directory) *directory = candidate.string();
        return true;
    }

    if (error) *error = "Cannot create a session instance directory.";
    return false;
}

bool WriteSessionOwner(const std::string& directory, const SessionOwner& owner,
                       std::string* error) {
    if (!EnsureDirectory(directory, error)) return false;

    std::ostringstream out;
    out << kOwnerHeader << '\n';
    out << "pid " << owner.pid << '\n';
    // The host name is hex encoded so an unexpected character cannot turn the
    // owner file into an unparsable one.
    out << "host " << HexEncode(owner.host) << '\n';
    out << "sequence " << owner.sequence << '\n';
    return WriteFileAtomic(fs::path(directory) / std::string(kOwnerName), out.str(),
                           error);
}

MergedSession MergeSessions(const std::vector<std::string>& directories) {
    MergedSession merged;
    std::set<std::string> ids;
    bool capped = false;

    for (const std::string& directory : directories) {
        if (capped) break;

        const SessionLoadResult result = LoadSession(directory);
        if (!result.found) continue;
        if (!result.ok) {
            merged.errors.push_back(directory + ": " + result.error);
            continue;
        }
        merged.mergedDirectories.push_back(directory);

        std::vector<int> slotOfTab(result.session.tabs.size(), -1);
        for (std::size_t i = 0; i < result.session.tabs.size(); ++i) {
            const SessionTabInfo& tab = result.session.tabs[i];
            int slot = -1;
            if (!tab.path.empty()) {
                for (std::size_t existing = 0;
                     existing < merged.session.tabs.size(); ++existing) {
                    if (merged.session.tabs[existing].path == tab.path) {
                        slot = static_cast<int>(existing);
                        break;
                    }
                }
            }
            if (slot >= 0) {
                SessionTabInfo& kept =
                    merged.session.tabs[static_cast<std::size_t>(slot)];
                slotOfTab[i] = slot;
                if (tab.modified && !kept.modified) {
                    // The newer session holds unsaved work for a file the older
                    // one only had open; keep the position but take the content.
                    kept = tab;
                    merged.sourceDirectory[static_cast<std::size_t>(slot)] =
                        directory;
                    continue;
                }
                if (!tab.modified) continue;
                // Both versions are modified: keep both instead of dropping
                // unsaved work from either window.
            }

            if (merged.session.tabs.size() >= kMaxMergedTabs) {
                capped = true;
                break;
            }
            if (tab.id.empty() || !ids.insert(tab.id).second) {
                merged.errors.push_back(directory + ": duplicate tab id " +
                                        tab.id);
                continue;
            }
            merged.session.tabs.push_back(tab);
            merged.sourceDirectory.push_back(directory);
            slotOfTab[i] = static_cast<int>(merged.session.tabs.size()) - 1;
        }

        // The most recent source decides which merged tab starts active.
        const int active = result.session.active;
        if (active >= 0 && active < static_cast<int>(slotOfTab.size()) &&
            slotOfTab[static_cast<std::size_t>(active)] >= 0) {
            merged.session.active = slotOfTab[static_cast<std::size_t>(active)];
        }
    }

    if (capped) {
        merged.errors.push_back("Too many tabs to restore; the rest was skipped.");
    }
    if (merged.session.tabs.empty()) merged.session.active = 0;
    return merged;
}

bool CopySessionContent(const std::string& sourceDirectory,
                        const std::string& contentFile,
                        const std::string& targetDirectory, const std::string& id,
                        std::string* newContentFile, std::string* error) {
    if (sourceDirectory.empty() || targetDirectory.empty() || contentFile.empty()) {
        if (error) *error = "Session content file is not available.";
        return false;
    }
    if (!IsSafeId(id) || !IsContentFileFor(contentFile, id)) {
        if (error) *error = "Session content file does not match its tab id.";
        return false;
    }
    if (!EnsureDirectory(targetDirectory, error)) return false;

    const std::string name = ContentName(id);
    const fs::path source = fs::path(sourceDirectory) / contentFile;
    const fs::path target = fs::path(targetDirectory) / name;
    if (source == target) {
        if (newContentFile) *newContentFile = name;
        return true;
    }

    std::ifstream in(source, std::ios::binary);
    if (!in) {
        if (error) *error = "Cannot open session content: " + source.string();
        return false;
    }

    const std::string temp = target.string() + TempSuffix();
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "Cannot create temporary session file: " + temp;
        return false;
    }

    // Recovery buffers can be large, so the copy never holds a second full
    // copy of the text in memory.
    std::array<char, kCopyBufferBytes> buffer{};
    bool failed = false;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = in.gcount();
        if (read <= 0) break;
        out.write(buffer.data(), read);
        if (!out) {
            failed = true;
            break;
        }
    }
    if (!failed && !in.eof()) failed = true;

    out.flush();
    if (!out) failed = true;
    out.close();
    if (failed && error) *error = "Cannot copy session content: " + source.string();

    if (!failed) {
        std::error_code ec;
        fs::permissions(temp, kFilePermissions, fs::perm_options::replace, ec);
        fs::rename(temp, target, ec);
        if (ec) {
            if (error) {
                *error = "Cannot replace session file: " + target.string() + " (" +
                         ec.message() + ")";
            }
            failed = true;
        }
    }

    if (failed) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return false;
    }

    if (newContentFile) *newContentFile = name;
    return true;
}

bool RemoveSessionInstance(const std::string& directory, std::string* error) {
    if (directory.empty()) return true;

    std::error_code ec;
    if (!fs::exists(directory, ec)) return !ec;

    bool ok = true;
    fs::directory_iterator iterator(directory, ec);
    if (ec) {
        if (error) *error = "Cannot list session directory: " + ec.message();
        return false;
    }
    for (const fs::directory_entry& entry : iterator) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) {
            entryEc.clear();
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name != kOwnerName && name != kManifestName && !IsSnapshotName(name) &&
            !IsTemporaryName(name)) {
            continue;
        }
        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (removeEc) {
            if (error) *error = "Cannot remove session file: " + removeEc.message();
            ok = false;
        }
    }

    fs::remove(directory, ec);
    if (ec) {
        // Unknown files may keep the directory alive; that is harmless.
        ec.clear();
    }
    return ok;
}

bool RemoveLegacySession(const std::string& root, std::string* error) {
    if (root.empty()) return true;

    const fs::path rootPath(root);
    std::error_code ec;
    fs::remove(rootPath / std::string(kManifestName), ec);
    if (ec) {
        if (error) *error = "Cannot remove session manifest: " + ec.message();
        return false;
    }

    fs::directory_iterator iterator(rootPath, ec);
    if (ec) {
        if (error) *error = "Cannot list session directory: " + ec.message();
        return false;
    }
    bool ok = true;
    for (const fs::directory_entry& entry : iterator) {
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc) || entryEc) {
            entryEc.clear();
            continue;
        }
        const std::string name = entry.path().filename().string();
        // Only the layout that lived directly in the root is removed; the
        // instances/ subdirectory and unrelated files are never touched.
        if (!IsSnapshotName(name) && !IsTemporaryName(name)) continue;
        std::error_code removeEc;
        fs::remove(entry.path(), removeEc);
        if (removeEc) {
            if (error) *error = "Cannot remove session content: " + removeEc.message();
            ok = false;
        }
    }
    return ok;
}

bool CleanSessionTemporaries(const std::string& root, std::string* error) {
    if (root.empty()) return true;

    const fs::path rootPath(root);
    std::error_code ec;
    if (!fs::is_directory(rootPath, ec)) return !ec;

    CleanTemporariesIn(rootPath, error);

    fs::directory_iterator iterator(rootPath / std::string(kInstancesDirectory), ec);
    if (!ec) {
        for (const fs::directory_entry& entry : iterator) {
            std::error_code entryEc;
            if (entry.is_symlink(entryEc) || entryEc) continue;
            if (!entry.is_directory(entryEc) || entryEc) continue;
            CleanTemporariesIn(entry.path(), error);
        }
    }
    return true;
}

}  // namespace txt

