// Crash reports on Windows, ADR-0032: unhandled exceptions write a minidump and a text report with the
// exception and the call stack; abort() writes the text report. No error dialogs are shown.
#include "crash.h"

#include <windows.h>
#include <dbghelp.h>

#include <process.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace animal::platform::detail {

namespace {

wchar_t gReportPath[1024] = {};
wchar_t gDumpPath[1024] = {};
char gLogPath[1024] = {};

std::wstring widen(const std::string& utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    out.resize(std::wcslen(out.c_str()));
    return out;
}

void writeStack(FILE* out, CONTEXT* context) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    void* frames[64];
    USHORT count = 0;
    if (context) {
        // Walk from the faulting context.
        STACKFRAME64 frame{};
        CONTEXT copy = *context;
#if defined(_M_X64)
        frame.AddrPC.Offset = copy.Rip;
        frame.AddrFrame.Offset = copy.Rbp;
        frame.AddrStack.Offset = copy.Rsp;
        const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
        frame.AddrPC.Offset = copy.Pc;
        frame.AddrFrame.Offset = copy.Fp;
        frame.AddrStack.Offset = copy.Sp;
        const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
        const DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif
        frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
        while (count < 64 && StackWalk64(machine, process, GetCurrentThread(), &frame, &copy, nullptr,
                                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
               frame.AddrPC.Offset != 0) {
            frames[count++] = reinterpret_cast<void*>(frame.AddrPC.Offset);
        }
    } else {
        count = CaptureStackBackTrace(0, 64, frames, nullptr);
    }
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
                std::fprintf(out, "  %2u %s (%s:%lu)\n", i, symbol->Name, line.FileName, line.LineNumber);
            } else {
                std::fprintf(out, "  %2u %s + 0x%llx\n", i, symbol->Name, static_cast<unsigned long long>(displacement));
            }
        } else {
            std::fprintf(out, "  %2u 0x%llx\n", i, static_cast<unsigned long long>(address));
        }
    }
    SymCleanup(process);
}

void writeReport(const char* reason, EXCEPTION_POINTERS* exception) {
    FILE* out = _wfopen(gReportPath, L"w");
    if (out) {
        std::fprintf(out, "Animal Engine crash\nreason: %s\n", reason);
        if (exception) {
            std::fprintf(out, "exception: 0x%08lx at %p\nminidump: %ls\n", exception->ExceptionRecord->ExceptionCode,
                         exception->ExceptionRecord->ExceptionAddress, gDumpPath);
        }
        std::fprintf(out, "log: %s\n\ncall stack:\n", gLogPath[0] ? gLogPath : "(none)");
        writeStack(out, exception ? exception->ContextRecord : nullptr);
        std::fclose(out);
        std::fprintf(stderr, "Crash report written to %ls\n", gReportPath);
    }
}

LONG WINAPI onException(EXCEPTION_POINTERS* exception) {
    HANDLE file = CreateFileW(gDumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    writeReport("unhandled exception", exception);
    return EXCEPTION_EXECUTE_HANDLER; // ends the process without the Windows error dialog
}

void onAbort(int) {
    writeReport("abort()", nullptr);
    _exit(3);
}

} // namespace

void installCrashHandler(const std::string& reportPath, const std::string& logPath) {
    wcsncpy_s(gReportPath, widen(reportPath).c_str(), _TRUNCATE);
    std::wstring dump = widen(reportPath);
    if (dump.size() > 4) {
        dump.replace(dump.size() - 4, 4, L".dmp");
    }
    wcsncpy_s(gDumpPath, dump.c_str(), _TRUNCATE);
    strncpy_s(gLogPath, logPath.c_str(), _TRUNCATE);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // no abort() dialog
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onAbort);
}

void writeCrashReport(const char* reason) {
    writeReport(reason, nullptr);
}

} // namespace animal::platform::detail
