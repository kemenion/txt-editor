#pragma once

#include <string>

#include "core/TextTypes.h"

namespace txt {

// A snapshot of one tab taken when the comparison window is opened or
// refreshed.  The source tab remains fully editable; the comparison is
// deliberately read-only until the user presses Refresh.
struct CompareDocument {
    std::string id;
    std::string title;
    std::string path;
    std::string text;
    DocumentMeta meta;
    int zoom = 0;
    int firstVisibleLine = 0;
    bool modified = false;
};

}  // namespace txt
