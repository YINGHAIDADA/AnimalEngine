#include <animal/core/profiler.h>

#include <chrono>

namespace animal::profiler {

namespace {

struct Entry {
    const char* name;
    uint32_t parent; // index into the frame's entries, or kNone
    uint32_t depth;
    uint32_t calls;
    uint64_t nanoseconds;
};

constexpr uint32_t kNone = UINT32_MAX;

struct State {
    std::vector<Entry> current;
    std::vector<uint32_t> stack;
    std::vector<ZoneTiming> last;
};

State& state() {
    static State s;
    return s;
}

uint64_t now() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

void beginFrame() {
    State& s = state();
    s.current.clear();
    s.stack.clear();
}

void endFrame() {
    State& s = state();
    s.last.clear();
    for (const Entry& e : s.current) {
        s.last.push_back({e.name, e.depth, e.calls, static_cast<double>(e.nanoseconds) * 1e-6});
    }
    s.current.clear();
    s.stack.clear();
}

const std::vector<ZoneTiming>& lastFrame() {
    return state().last;
}

Zone::Zone(const char* name) : start_(now()) {
    State& s = state();
    const uint32_t parent = s.stack.empty() ? kNone : s.stack.back();
    index_ = kNone;
    for (uint32_t i = 0; i < s.current.size(); ++i) {
        if (s.current[i].parent == parent && s.current[i].name == name) {
            index_ = i;
            break;
        }
    }
    if (index_ == kNone) {
        index_ = static_cast<uint32_t>(s.current.size());
        s.current.push_back({name, parent, static_cast<uint32_t>(s.stack.size()), 0, 0});
    }
    ++s.current[index_].calls;
    s.stack.push_back(index_);
}

Zone::~Zone() {
    State& s = state();
    if (index_ < s.current.size()) { // a frame boundary inside the zone drops it
        s.current[index_].nanoseconds += now() - start_;
    }
    if (!s.stack.empty()) {
        s.stack.pop_back();
    }
}

} // namespace animal::profiler
