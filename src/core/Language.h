#pragma once

#include <string>
#include <string_view>

#include "core/TextTypes.h"

namespace txt {

// Maps an extension or shebang to a lexer-independent language id.
LanguageId DetectLanguage(std::string_view path, std::string_view text);

// Human-readable name for the status bar.
std::string_view LanguageName(LanguageId language);

}  // namespace txt
