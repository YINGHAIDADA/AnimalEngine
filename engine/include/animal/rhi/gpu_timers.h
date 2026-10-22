#pragma once

#include <animal/rhi/rhi.h>

#include <span>
#include <vector>

namespace animal::rhi {

// Backend bookkeeping for CommandList timer scopes within one frame: hands out timestamp slots
// (two per scope) and turns the timestamps read back later into durations.
class TimerScopes {
public:
    static constexpr uint32_t kMaxTimestamps = 64;
    static constexpr uint32_t kNone = UINT32_MAX;

    // Slot for the scope's begin timestamp, or kNone when the frame has run out of slots.
    uint32_t begin(const char* name) {
        Scope scope{name, static_cast<uint32_t>(stack_.size()), kNone, kNone};
        if (next_ + 2 <= kMaxTimestamps) {
            scope.begin = next_;
            scope.end = next_ + 1;
            next_ += 2;
        }
        stack_.push_back(scopes_.size());
        scopes_.push_back(scope);
        return scope.begin;
    }

    // Slot for the end timestamp of the innermost open scope, or kNone.
    uint32_t end() {
        if (stack_.empty()) {
            return kNone;
        }
        const Scope& scope = scopes_[stack_.back()];
        stack_.pop_back();
        return scope.end;
    }

    uint32_t used() const { return next_; }
    bool empty() const { return next_ == 0; }

    void reset() {
        scopes_.clear();
        stack_.clear();
        next_ = 0;
    }

    // `ticks[i]` is the timestamp written to slot i.
    std::vector<GpuTiming> timings(std::span<const uint64_t> ticks, double nanosecondsPerTick) const {
        std::vector<GpuTiming> result;
        for (const Scope& scope : scopes_) {
            if (scope.begin == kNone || scope.end >= ticks.size()) {
                continue;
            }
            const uint64_t a = ticks[scope.begin];
            const uint64_t b = ticks[scope.end];
            const double ms = b > a ? static_cast<double>(b - a) * nanosecondsPerTick * 1e-6 : 0.0;
            result.push_back({scope.name, scope.depth, ms});
        }
        return result;
    }

private:
    struct Scope {
        const char* name;
        uint32_t depth;
        uint32_t begin;
        uint32_t end;
    };

    std::vector<Scope> scopes_;
    std::vector<size_t> stack_;
    uint32_t next_ = 0;
};

} // namespace animal::rhi
