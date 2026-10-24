#pragma once

#include <cstddef>

namespace txt::profile {

// Prints `[txt] t=<ms> <name>` to stderr when TXT_EDITOR_PROFILE is set.
void Mark(const char* name);

// Same, with a byte count for file loads.
void MarkBytes(const char* name, std::size_t bytes);

}  // namespace txt::profile
