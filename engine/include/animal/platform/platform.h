#pragma once

#include <animal/platform/input.h>

#include <cstdint>
#include <string>

namespace animal::platform {

bool initialize();
void shutdown();

// Pumps the OS event queue; call once per frame before reading input.
FrameEvents pollEvents();

uint64_t nowNanoseconds();

// Directory containing the executable, with a trailing separator.
std::string executableDirectory();

// Per-user writable directory for saves and settings (created if missing), with a trailing
// separator; empty on failure.
std::string userDataDirectory(const char* organization, const char* application);

// System clipboard (UTF-8 text); empty when it holds no text.
std::string clipboardText();
void setClipboardText(const std::string& text);

enum class Cursor : uint8_t {
    Arrow,
    TextInput,
    Move,
    ResizeNS,
    ResizeEW,
    ResizeNESW,
    ResizeNWSE,
    Hand,
    NotAllowed,
    Hidden,
};
void setCursor(Cursor cursor);

} // namespace animal::platform
