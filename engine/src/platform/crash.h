#pragma once

#include <string>

namespace animal::platform::detail {

// Per-OS crash handling (crash_posix.cpp / crash_windows.cpp).
void installCrashHandler(const std::string& reportPath, const std::string& logPath);
void writeCrashReport(const char* reason);

} // namespace animal::platform::detail
