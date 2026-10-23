#pragma once

#include <animal/rhi/rhi.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace animal {
struct EngineContext;
}

namespace animal::devtools {

// Frame time and GPU pass timings with a short history (the F1 overlay's numbers as graphs).
class PerformancePanel {
public:
    static constexpr size_t kHistory = 240;

    // Once per frame.
    void sample(const EngineContext& context);
    // Draws the panel's contents into the current window.
    void draw(const EngineContext& context);

private:
    struct Series {
        std::string name;
        uint32_t depth = 0;
        std::array<float, kHistory> values{};
        float last = 0.0f;
    };

    static Series& series(std::vector<Series>& list, const char* name, uint32_t depth);

    uint64_t lastTime_ = 0;
    size_t cursor_ = 0;
    Series frame_{"frame (CPU)", 0, {}, 0.0f};
    std::vector<Series> gpu_;
    std::vector<Series> cpu_; // CPU zones (ANIMAL_PROFILE_ZONE)
    bool paused_ = false;
};

} // namespace animal::devtools
