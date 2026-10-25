#include <animal/framework/loadout.h>

#include <algorithm>

namespace animal {

namespace {

bool sameId(const std::string& id, std::string_view want) {
    return id == want;
}

} // namespace

bool Loadout::hasAbility(std::string_view id) const {
    const auto it = std::ranges::find_if(abilities_, [&](const std::string& name) { return sameId(name, id); });
    return it != abilities_.end();
}

bool Loadout::grantAbility(std::string_view id) {
    if (id.empty() || hasAbility(id)) {
        return false;
    }
    abilities_.emplace_back(id);
    return true;
}

const CarriedItem* Loadout::findItem(std::string_view id) const {
    const auto it = std::ranges::find_if(items_, [&](const CarriedItem& item) { return sameId(item.id, id); });
    return it == items_.end() ? nullptr : &*it;
}

CarriedItem* Loadout::findMutable(std::string_view id) {
    return const_cast<CarriedItem*>(static_cast<const Loadout*>(this)->findItem(id));
}

int Loadout::count(std::string_view id) const {
    const CarriedItem* item = findItem(id);
    return item ? item->count : 0;
}

int Loadout::gainItem(std::string_view id, ItemUse use, int amount, int maxCount) {
    if (id.empty() || amount <= 0) {
        return 0;
    }
    if (CarriedItem* existing = findMutable(id)) {
        if (existing->use != use || use == ItemUse::Reusable) {
            return 0;
        }
        const int room = std::max(0, maxCount - existing->count);
        const int added = std::min(amount, room);
        existing->count += added;
        return added;
    }
    CarriedItem item;
    item.id = std::string(id);
    item.use = use;
    if (use == ItemUse::Reusable) {
        item.count = 1;
        amount = 1;
    } else {
        item.count = std::min(amount, std::max(0, maxCount));
        amount = item.count;
    }
    if (item.count <= 0) {
        return 0;
    }
    const bool takeHand = equipped_.empty();
    items_.push_back(std::move(item));
    if (takeHand) {
        equipped_ = items_.back().id;
    }
    return amount;
}

bool Loadout::equip(std::string_view id) {
    if (!findItem(id)) {
        return false;
    }
    equipped_ = std::string(id);
    return true;
}

const CarriedItem* Loadout::equipped() const {
    return equipped_.empty() ? nullptr : findItem(equipped_);
}

int Loadout::equippedIndex() const {
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (sameId(items_[static_cast<size_t>(i)].id, equipped_)) {
            return i;
        }
    }
    return -1;
}

bool Loadout::consumeEquipped() {
    const auto it = std::ranges::find_if(items_, [&](const CarriedItem& item) { return sameId(item.id, equipped_); });
    if (it == items_.end() || it->use != ItemUse::Consumable || it->count <= 0) {
        return false;
    }
    --it->count;
    return true;
}

void Loadout::clear() {
    abilities_.clear();
    items_.clear();
    equipped_.clear();
}

} // namespace animal
