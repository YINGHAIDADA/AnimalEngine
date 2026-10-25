#pragma once

// What the player has learned and is carrying. Every gained ability is active at once. Items are
// kept in the order they were first picked up, and at most one of them is in hand.

#include <string>
#include <string_view>
#include <vector>

namespace animal {

enum class ItemUse {
    Consumable, // a stack; using it spends one, and a spent stack stays at zero
    Reusable,   // owned or not; using it does not spend it
};

struct CarriedItem {
    std::string id;
    ItemUse use = ItemUse::Consumable;
    int count = 0; // consumables; a reusable item stays at 1 while owned
};

class Loadout {
public:
    bool hasAbility(std::string_view id) const;
    // False when `id` is empty or already gained.
    bool grantAbility(std::string_view id);
    const std::vector<std::string>& abilities() const { return abilities_; }

    const CarriedItem* findItem(std::string_view id) const;
    int count(std::string_view id) const;
    // Adds `amount` of a consumable, capped at `maxCount`, or grants a reusable item once.
    // The first item gained is put in hand; later ones wait until equipped. Returns how many
    // were actually added (1 or 0 for a reusable item).
    int gainItem(std::string_view id, ItemUse use, int amount, int maxCount);
    bool equip(std::string_view id); // must already be carried
    const CarriedItem* equipped() const;
    int equippedIndex() const; // -1 when the hand is empty
    // Spends one of the consumable in hand. At zero the stack stays, still in hand, so it can be
    // shown empty. False when the hand is empty, the item is reusable, or its count is already zero.
    bool consumeEquipped();
    const std::vector<CarriedItem>& items() const { return items_; }
    void clear();

private:
    CarriedItem* findMutable(std::string_view id);

    std::vector<std::string> abilities_; // gained order
    std::vector<CarriedItem> items_;     // first-gained order
    std::string equipped_;               // id in hand; empty when none
};

} // namespace animal
