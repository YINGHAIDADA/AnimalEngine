#pragma once

#include <animal/platform/input.h>

#include <cstdint>
#include <string>
#include <vector>

namespace animal::platform {

bool initialize();
void shutdown();

// Pumps the OS event queue; call once per frame before reading input.
FrameEvents pollEvents();

uint64_t nowNanoseconds();
void sleepNanoseconds(uint64_t duration); // precise: sleeps most of it, spins the last part

// Directory containing the executable, with a trailing separator.
std::string executableDirectory();

// Per-user writable directory for saves and settings (created if missing), with a trailing
// separator; empty on failure.
std::string userDataDirectory(const char* organization, const char* application);

// System clipboard (UTF-8 text); empty when it holds no text.
std::string clipboardText();
void setClipboardText(const std::string& text);

struct ProcessResult {
    int exitCode = -1;  // -1: the program could not be started
    std::string output; // standard output and standard error, interleaved
};
// Runs a program to completion (blocking) and captures what it prints. arguments[0] is the program.
ProcessResult runProcess(const std::vector<std::string>& arguments);

// Crash reports (ADR-0032): fatal signals and unhandled exceptions (and std::terminate) write
// <directory>/crash-<date>-<time>.txt with the reason and the call stack (Windows: also a .dmp
// minidump) and name the log file. Call once at start-up; returns the report path.
std::string installCrashHandler(const std::string& directory, const std::string& logPath);

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
