#include "app/Profile.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace txt::profile {
namespace {

using Clock = std::chrono::steady_clock;

const Clock::time_point& Origin() {
    static const Clock::time_point origin = Clock::now();
    return origin;
}

bool Enabled() {
    static const bool enabled = std::getenv("TXT_EDITOR_PROFILE") != nullptr;
    return enabled;
}

double Milliseconds() {
    return std::chrono::duration<double, std::milli>(Clock::now() - Origin()).count();
}

}  // namespace

void Mark(const char* name) {
    if (!Enabled()) return;
    std::fprintf(stderr, "[txt] t=%.1fms %s\n", Milliseconds(), name);
    std::fflush(stderr);
}

void MarkBytes(const char* name, std::size_t bytes) {
    if (!Enabled()) return;
    std::fprintf(stderr, "[txt] t=%.1fms %s (%zu bytes)\n", Milliseconds(), name, bytes);
    std::fflush(stderr);
}

}  // namespace txt::profile
