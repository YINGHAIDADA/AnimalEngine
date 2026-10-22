#include <animal/framework/triggers.h>

namespace animal {

std::vector<const TriggerSet::Trigger*> TriggerSet::update(const IntRect& body) {
    std::vector<const Trigger*> entered;
    for (Trigger& trigger : triggers_) {
        const bool inside = trigger.area.overlaps(body);
        if (inside && !trigger.inside && !(trigger.once && trigger.fired)) {
            trigger.fired = true;
            entered.push_back(&trigger);
        }
        trigger.inside = inside;
    }
    return entered;
}

} // namespace animal
