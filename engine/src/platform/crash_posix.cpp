// Crash reports on macOS (and other POSIX systems), ADR-0032: fatal signals write the signal, the
// address and the call stack to a text file prepared at start-up, then the default action runs.
#include "crash.h"

#include <animal/core/log.h>

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>
#include <ctime>
#include <string>

namespace animal::platform::detail {

namespace {

char gReportPath[1024] = {};
char gLogPath[1024] = {};
alignas(16) char gAltStack[64 * 1024];

void put(int fd, const char* text) {
    (void)!write(fd, text, std::strlen(text));
}

void putHex(int fd, uintptr_t value) {
    char buffer[2 + sizeof(uintptr_t) * 2 + 1];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (size_t i = 0; i < sizeof(uintptr_t) * 2; ++i) {
        const unsigned nibble = (value >> ((sizeof(uintptr_t) * 2 - 1 - i) * 4)) & 0xF;
        buffer[2 + i] = static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    buffer[sizeof(buffer) - 1] = '\0';
    put(fd, buffer);
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap / failed assertion)";
    default: return "signal";
    }
}

// Async-signal-safe apart from backtrace_symbols_fd, which is the usual compromise.
void writeReport(const char* reason, const void* address, bool hasAddress) {
    const int fd = open(gReportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    for (const int out : {fd, 2}) {
        if (out < 0) {
            continue;
        }
        put(out, "Animal Engine crash\nreason: ");
        put(out, reason);
        if (hasAddress) {
            put(out, "\naddress: ");
            putHex(out, reinterpret_cast<uintptr_t>(address));
        }
        put(out, "\nlog: ");
        put(out, gLogPath[0] ? gLogPath : "(none)");
        put(out, "\n\ncall stack:\n");
        void* frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, out);
        put(out, "\n");
    }
    if (fd >= 0) {
        close(fd);
        put(2, "Crash report written to ");
        put(2, gReportPath);
        put(2, "\n");
    }
}

void onSignal(int sig, siginfo_t* info, void*) {
    writeReport(signalName(sig), info ? info->si_addr : nullptr, info != nullptr);
    // SA_RESETHAND restored the default action: re-raise to end the process the usual way.
    raise(sig);
}

} // namespace

void installCrashHandler(const std::string& reportPath, const std::string& logPath) {
    std::strncpy(gReportPath, reportPath.c_str(), sizeof(gReportPath) - 1);
    std::strncpy(gLogPath, logPath.c_str(), sizeof(gLogPath) - 1);
    stack_t stack{};
    stack.ss_sp = gAltStack;
    stack.ss_size = sizeof(gAltStack);
    sigaltstack(&stack, nullptr); // a stack overflow still gets a report
    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) {
        sigaction(sig, &action, nullptr);
    }
}

void writeCrashReport(const char* reason) {
    writeReport(reason, nullptr, false);
}

} // namespace animal::platform::detail
