#include "core/Encoding.h"

#include <cstdint>

namespace txt {
namespace {

constexpr bool IsContinuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

void AppendUtf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

bool DecodeOneUtf8(std::string_view text, std::size_t& i, std::uint32_t& cp) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c < 0x80u) {
        cp = c;
        ++i;
        return true;
    }

    int extra = 0;
    if ((c & 0xE0u) == 0xC0u) {
        if (c < 0xC2u) return false;
        cp = c & 0x1Fu;
        extra = 1;
    } else if ((c & 0xF0u) == 0xE0u) {
        cp = c & 0x0Fu;
        extra = 2;
    } else if ((c & 0xF8u) == 0xF0u) {
        cp = c & 0x07u;
        extra = 3;
    } else {
        return false;
    }

    if (i + static_cast<std::size_t>(extra) >= text.size()) return false;
    for (int k = 1; k <= extra; ++k) {
        const auto cc = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
        if (!IsContinuation(cc)) return false;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if ((extra == 1 && cp < 0x80u) || (extra == 2 && cp < 0x800u) ||
        (extra == 3 && cp < 0x10000u) || cp > 0x10FFFFu ||
        (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return false;
    }
    i += static_cast<std::size_t>(extra) + 1;
    return true;
}

std::string Latin1ToUtf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 2);
    for (const unsigned char c : bytes) AppendUtf8(c, out);
    return out;
}

std::string Utf16ToUtf8(std::string_view bytes, bool littleEndian) {
    std::string out;
    out.reserve(bytes.size());
    std::size_t start = 0;
    if (bytes.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(bytes[0]);
        const auto b1 = static_cast<unsigned char>(bytes[1]);
        const bool hasBom = littleEndian ? (b0 == 0xFFu && b1 == 0xFEu)
                                         : (b0 == 0xFEu && b1 == 0xFFu);
        if (hasBom) start = 2;
    }
    const auto unitAt = [&](std::size_t i) -> std::uint16_t {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        return littleEndian ? static_cast<std::uint16_t>(a | (b << 8))
                            : static_cast<std::uint16_t>((a << 8) | b);
    };

    for (std::size_t i = start; i + 1 < bytes.size(); i += 2) {
        const std::uint16_t unit = unitAt(i);
        std::uint32_t cp = unit;
        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (i + 3 < bytes.size()) {
                const std::uint16_t low = unitAt(i + 2);
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    cp = 0x10000u + ((static_cast<std::uint32_t>(unit - 0xD800u) << 10) |
                                     (low - 0xDC00u));
                    i += 2;
                } else {
                    cp = 0xFFFDu;
                }
            } else {
                cp = 0xFFFDu;
            }
        } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            cp = 0xFFFDu;
        }
        AppendUtf8(cp, out);
    }
    if (bytes.size() % 2 != 0) out.append("\xEF\xBF\xBD");
    return out;
}

std::string Utf8ToUtf16(std::string_view utf8, bool littleEndian) {
    std::string out;
    out.reserve(utf8.size() * 2);
    std::size_t i = 0;
    while (i < utf8.size()) {
        std::uint32_t cp = 0;
        if (!DecodeOneUtf8(utf8, i, cp)) cp = 0xFFFDu;
        const auto appendUnit = [&](std::uint16_t unit) {
            if (littleEndian) {
                out.push_back(static_cast<char>(unit & 0xFFu));
                out.push_back(static_cast<char>(unit >> 8));
            } else {
                out.push_back(static_cast<char>(unit >> 8));
                out.push_back(static_cast<char>(unit & 0xFFu));
            }
        };
        if (cp <= 0xFFFFu) {
            appendUnit(static_cast<std::uint16_t>(cp));
        } else {
            cp -= 0x10000u;
            appendUnit(static_cast<std::uint16_t>(0xD800u + (cp >> 10)));
            appendUnit(static_cast<std::uint16_t>(0xDC00u + (cp & 0x3FFu)));
        }
    }
    return out;
}

std::string Utf8ToLatin1(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        std::uint32_t cp = 0;
        if (!DecodeOneUtf8(utf8, i, cp)) cp = 0xFFFDu;
        out.push_back(cp <= 0xFFu ? static_cast<char>(cp) : '?');
    }
    return out;
}

}  // namespace

bool IsValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t cp = 0;
        if (!DecodeOneUtf8(text, i, cp)) return false;
    }
    return true;
}

Encoding DetectEncoding(std::string_view bytes) {
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEFu &&
        static_cast<unsigned char>(bytes[1]) == 0xBBu &&
        static_cast<unsigned char>(bytes[2]) == 0xBFu) {
        return Encoding::Utf8Bom;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFFu &&
        static_cast<unsigned char>(bytes[1]) == 0xFEu) {
        return Encoding::Utf16LE;
    }
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFEu &&
        static_cast<unsigned char>(bytes[1]) == 0xFFu) {
        return Encoding::Utf16BE;
    }
    return IsValidUtf8(bytes) ? Encoding::Utf8 : Encoding::Latin1;
}

Eol DetectEol(std::string_view text) {
    std::size_t crlf = 0;
    std::size_t lf = 0;
    std::size_t cr = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (text[i] == '\n') {
            ++lf;
        }
    }
    if (lf >= crlf && lf >= cr) return Eol::Lf;
    if (crlf >= cr) return Eol::CrLf;
    return Eol::Cr;
}

std::string ToUtf8(std::string_view bytes, Encoding encoding, bool* ok) {
    if (ok) *ok = true;
    switch (encoding) {
        case Encoding::Utf8:
            if (IsValidUtf8(bytes)) return std::string(bytes);
            if (ok) *ok = false;
            return Latin1ToUtf8(bytes);
        case Encoding::Utf8Bom: {
            const std::string_view body =
                bytes.size() >= 3 ? bytes.substr(3) : std::string_view{};
            if (IsValidUtf8(body)) return std::string(body);
            if (ok) *ok = false;
            return Latin1ToUtf8(body);
        }
        case Encoding::Utf16LE:
            if (ok && bytes.size() % 2 != 0) *ok = false;
            return Utf16ToUtf8(bytes, true);
        case Encoding::Utf16BE:
            if (ok && bytes.size() % 2 != 0) *ok = false;
            return Utf16ToUtf8(bytes, false);
        case Encoding::Latin1:
            return Latin1ToUtf8(bytes);
    }
    return {};
}

std::string FromUtf8(std::string_view utf8, Encoding encoding) {
    switch (encoding) {
        case Encoding::Utf8:
            return std::string(utf8);
        case Encoding::Utf8Bom:
            return std::string("\xEF\xBB\xBF") + std::string(utf8);
        case Encoding::Utf16LE:
            return std::string("\xFF\xFE") + Utf8ToUtf16(utf8, true);
        case Encoding::Utf16BE:
            return std::string("\xFE\xFF") + Utf8ToUtf16(utf8, false);
        case Encoding::Latin1:
            return Utf8ToLatin1(utf8);
    }
    return {};
}

std::string_view EncodingName(Encoding encoding) {
    switch (encoding) {
        case Encoding::Utf8: return "UTF-8";
        case Encoding::Utf8Bom: return "UTF-8 BOM";
        case Encoding::Utf16LE: return "UTF-16 LE";
        case Encoding::Utf16BE: return "UTF-16 BE";
        case Encoding::Latin1: return "ISO-8859-1";
    }
    return "unknown";
}

std::string_view EolName(Eol eol) {
    switch (eol) {
        case Eol::Lf: return "LF";
        case Eol::CrLf: return "CRLF";
        case Eol::Cr: return "CR";
    }
    return "unknown";
}

}  // namespace txt

