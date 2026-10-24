#include "crash.h"
#include "gamepads.h"

#include <animal/core/log.h>
#include <animal/platform/platform.h>

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <format>

namespace animal::platform {

namespace {

constexpr size_t kCursorCount = static_cast<size_t>(Cursor::Hidden);
std::array<SDL_Cursor*, kCursorCount> gCursors{};
Cursor gCurrentCursor = Cursor::Arrow;

SDL_SystemCursor systemCursor(Cursor cursor) {
    switch (cursor) {
    case Cursor::Arrow:
    case Cursor::Hidden:
        break;
    case Cursor::TextInput:
        return SDL_SYSTEM_CURSOR_TEXT;
    case Cursor::Move:
        return SDL_SYSTEM_CURSOR_MOVE;
    case Cursor::ResizeNS:
        return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case Cursor::ResizeEW:
        return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case Cursor::ResizeNESW:
        return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case Cursor::ResizeNWSE:
        return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case Cursor::Hand:
        return SDL_SYSTEM_CURSOR_POINTER;
    case Cursor::NotAllowed:
        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    }
    return SDL_SYSTEM_CURSOR_DEFAULT;
}

MouseButton mouseButtonOf(uint8_t sdlButton, bool& known) {
    known = true;
    switch (sdlButton) {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    default:
        known = false;
        return MouseButton::Left;
    }
}

void translateInputEvent(const SDL_Event& event, std::vector<InputEvent>& out) {
    InputEvent e;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        e.type = InputEvent::Type::Key;
        e.key = detail::keyFromScancode(event.key.scancode);
        e.down = event.type == SDL_EVENT_KEY_DOWN;
        e.modifiers = detail::modifiersFrom(event.key.mod);
        break;
    case SDL_EVENT_TEXT_INPUT:
        e.type = InputEvent::Type::Text;
        e.text = event.text.text ? event.text.text : "";
        break;
    case SDL_EVENT_MOUSE_MOTION:
        e.type = InputEvent::Type::MouseMove;
        e.x = event.motion.x;
        e.y = event.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        bool known = false;
        e.type = InputEvent::Type::MouseButton;
        e.button = mouseButtonOf(event.button.button, known);
        e.down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (!known) {
            return;
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        e.type = InputEvent::Type::MouseWheel;
        e.x = event.wheel.x;
        e.y = event.wheel.y;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        e.type = InputEvent::Type::Focus;
        e.down = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        e.type = InputEvent::Type::MouseMove;
        e.x = e.y = -1.0e30f; // no mouse over the window
        break;
    default:
        return;
    }
    out.push_back(std::move(e));
}

} // namespace

bool initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log::error("SDL_Init failed: {}", SDL_GetError());
        return false;
    }
    log::info("SDL {}.{}.{} ({})", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
              SDL_VERSIONNUM_MICRO(SDL_GetVersion()), SDL_GetPlatform());
    return true;
}

void shutdown() {
    detail::closeAllGamepads();
    for (SDL_Cursor*& cursor : gCursors) {
        if (cursor) {
            SDL_DestroyCursor(cursor);
            cursor = nullptr;
        }
    }
    SDL_Quit();
}

FrameEvents pollEvents() {
    FrameEvents events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            events.quitRequested = true;
        }
        detail::handleGamepadEvent(event);
        translateInputEvent(event, events.input);
    }
    return events;
}

uint64_t nowNanoseconds() {
    return SDL_GetTicksNS();
}

void sleepNanoseconds(uint64_t duration) {
    SDL_DelayPrecise(duration);
}

std::string userDataDirectory(const char* organization, const char* application) {
    char* path = SDL_GetPrefPath(organization, application);
    if (!path) {
        log::error("No user data directory: {}", SDL_GetError());
        return {};
    }
    std::string result(path);
    SDL_free(path);
    return result;
}

std::string executableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::string(base) : std::string("./");
}

std::string clipboardText() {
    char* text = SDL_GetClipboardText();
    std::string result = text ? text : "";
    SDL_free(text);
    return result;
}

void setClipboardText(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

ProcessResult runProcess(const std::vector<std::string>& arguments) {
    ProcessResult result;
    if (arguments.empty()) {
        return result;
    }
    std::vector<const char*> argv;
    for (const std::string& a : arguments) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) {
        result.output = SDL_GetError();
        return result;
    }
    size_t size = 0;
    int exitCode = -1;
    if (void* data = SDL_ReadProcess(process, &size, &exitCode)) {
        result.output.assign(static_cast<const char*>(data), size);
        SDL_free(data);
    }
    result.exitCode = exitCode;
    SDL_DestroyProcess(process);
    return result;
}

std::string installCrashHandler(const std::string& directory, const std::string& logPath) {
    std::string dir = directory;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += '/';
    }
    SDL_CreateDirectory(dir.c_str());
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char stamp[32] = {};
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    const std::string path =
        std::format("{}crash-{}-{}.txt", dir, std::string_view(stamp), SDL_GetTicksNS() % 100000);
    detail::installCrashHandler(path, logPath);
    std::set_terminate([] {
        std::string reason = "std::terminate";
        if (const std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& e) {
                reason += std::string(": uncaught exception: ") + e.what();
            } catch (...) {
                reason += ": uncaught exception of unknown type";
            }
        }
        log::error("{}", reason);
        log::flushFileLog();
        detail::writeCrashReport(reason.c_str());
        std::_Exit(3);
    });
    return path;
}

void setCursor(Cursor cursor) {
    if (cursor == gCurrentCursor) {
        return;
    }
    gCurrentCursor = cursor;
    if (cursor == Cursor::Hidden) {
        SDL_HideCursor();
        return;
    }
    SDL_Cursor*& slot = gCursors[static_cast<size_t>(cursor)];
    if (!slot) {
        slot = SDL_CreateSystemCursor(systemCursor(cursor));
    }
    SDL_SetCursor(slot);
    SDL_ShowCursor();
}

} // namespace animal::platform
