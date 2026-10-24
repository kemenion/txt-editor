#include "core/Language.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace txt {
namespace {

std::string LowerExtension(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    const auto dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    std::string ext(path.substr(dot));
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool HasShebang(std::string_view text, std::string_view needle) {
    if (text.rfind("#!", 0) != 0) return false;
    const auto lineEnd = text.find('\n');
    const auto firstLine = text.substr(0, lineEnd == std::string_view::npos
                                             ? text.size()
                                             : lineEnd);
    return firstLine.find(needle) != std::string_view::npos;
}

}  // namespace

LanguageId DetectLanguage(std::string_view path, std::string_view text) {
    const std::string ext = LowerExtension(path);
    if (ext == ".py" || ext == ".pyw" || ext == ".pyi") return LanguageId::Python;
    if (ext == ".c") return LanguageId::C;
    if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
        ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx" ||
        ext == ".h++" || ext == ".ipp" || ext == ".inl") {
        return LanguageId::Cpp;
    }
    if (ext == ".json" || ext == ".jsonc" || ext == ".geojson") return LanguageId::Json;
    if (ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd") {
        return LanguageId::Markdown;
    }
    if (ext == ".sh" || ext == ".bash" || ext == ".zsh" || ext == ".ksh" ||
        ext == ".profile" || ext == ".bashrc" || ext == ".zshrc") {
        return LanguageId::Shell;
    }
    if (ext == ".xml" || ext == ".xhtml" || ext == ".html" || ext == ".htm" ||
        ext == ".svg" || ext == ".xsd" || ext == ".xsl" || ext == ".xslt") {
        return LanguageId::Xml;
    }
    if (ext == ".yaml" || ext == ".yml") return LanguageId::Yaml;

    if (HasShebang(text, "python")) return LanguageId::Python;
    if (HasShebang(text, "bash") || HasShebang(text, "/sh") || HasShebang(text, "zsh")) {
        return LanguageId::Shell;
    }
    return LanguageId::Plain;
}

std::string_view LanguageName(LanguageId language) {
    switch (language) {
        case LanguageId::Plain: return "Plain Text";
        case LanguageId::Python: return "Python";
        case LanguageId::C: return "C";
        case LanguageId::Cpp: return "C++";
        case LanguageId::Json: return "JSON";
        case LanguageId::Markdown: return "Markdown";
        case LanguageId::Shell: return "Shell";
        case LanguageId::Xml: return "XML";
        case LanguageId::Yaml: return "YAML";
    }
    return "Plain Text";
}

}  // namespace txt
