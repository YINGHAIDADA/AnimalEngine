#pragma once

#include <cstdint>
#include <string>

namespace animal::platform {

bool initialize();
void shutdown();

struct FrameEvents {
    bool quitRequested = false;
};

// Pumps the OS event queue; call once per frame before reading input.
FrameEvents pollEvents();

uint64_t nowNanoseconds();

// Directory containing the executable, with a trailing separator.
std::string executableDirectory();

// Per-user writable directory for saves and settings (created if missing), with a trailing
// separator; empty on failure.
std::string userDataDirectory(const char* organization, const char* application);

} // namespace animal::platform
