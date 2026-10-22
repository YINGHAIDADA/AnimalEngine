#pragma once

#include <animal/framework/geometry.h>

#include <string>
#include <vector>

namespace animal {

// Rectangular areas that report the step on which a body enters them.
class TriggerSet {
public:
    struct Trigger {
        int id = 0;
        IntRect area;
        bool once = false;
        std::string message; // game-defined payload
        bool inside = false;
        bool fired = false;
    };

    void add(Trigger trigger) { triggers_.push_back(std::move(trigger)); }
    void clear() { triggers_.clear(); }

    // Returns the triggers entered this step (outside last step, overlapping now). Triggers
    // marked `once` fire only the first time.
    std::vector<const Trigger*> update(const IntRect& body);

    const std::vector<Trigger>& triggers() const { return triggers_; }

private:
    std::vector<Trigger> triggers_;
};

} // namespace animal
