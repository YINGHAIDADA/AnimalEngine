// Abilities and carried items (the loadout). Every gained ability stays active. Items stack in the
// order they were first picked up, and one of them is in hand: a consumable shows its count in the
// corner of the icon under the hearts, a reusable item shows the icon alone. B opens a menu of icons
// only, inside a corner-cut frame: items in a 2×5 grid, abilities in a 1×5 row, split by a 1px
// line. The highlight moves through every item slot; confirming an empty one does nothing. A spent
// consumable stays, with a 0.

#include "sandbox_game.h"

#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>

#include <cmath>
#include <string>

namespace game {

namespace {

using animal::platform::Key;

struct ItemDef {
    const char* id;
    animal::ItemUse use;
    int maxCount;
    float iconCell; // props sheet
    const char* nameKey;
};

struct AbilityDef {
    const char* id;
    float iconCell;
    const char* nameKey;
};

constexpr ItemDef kItems[] = {
    {"dynamite", animal::ItemUse::Consumable, 9, 12.0f, "item.dynamite"},
    {"grapple", animal::ItemUse::Reusable, 1, 13.0f, "item.grapple"},
};

constexpr AbilityDef kAbilities[] = {
    {"dive", 8.0f, "ability.dive"},
};

constexpr float kGenericIcon = 8.0f; // the ability orb, for a name without its own picture
constexpr animal::Vec2 kSlotCanvas{3.0f, 13.0f}; // under the hearts (those occupy y = 3..11)
constexpr float kSlotSize = 12.0f;
constexpr float kIconInset = 2.0f;

constexpr animal::Color kSlotFill = animal::Color::rgba(8, 10, 16, 210);
constexpr animal::Color kSlotEdge = animal::Color::rgba(232, 214, 176);
constexpr animal::Color kCount = animal::Color::rgba(255, 248, 230);
constexpr animal::Color kShade = animal::Color::rgba(6, 6, 12, 170);
constexpr animal::Color kSelectEdge = animal::Color::rgba(255, 236, 170);
constexpr animal::Color kFrameFill = animal::Color::rgba(12, 14, 22, 230);
constexpr animal::Color kFrameEdge = animal::Color::rgba(186, 168, 140);
constexpr animal::Color kPlaceholder = animal::Color::rgba(78, 74, 66);
constexpr animal::Color kDivider = animal::Color::rgba(186, 168, 140, 160);

constexpr int kItemCols = 5;
constexpr int kItemRows = 2;
constexpr int kItemSlots = kItemCols * kItemRows;
constexpr int kAbilitySlots = 5;
constexpr int kKnownItems = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));
constexpr int kKnownAbilities = static_cast<int>(sizeof(kAbilities) / sizeof(kAbilities[0]));
constexpr float kSlotGap = 2.0f;
constexpr float kFramePad = 5.0f;
constexpr float kSplitGap = 3.0f;

const ItemDef* itemDef(std::string_view id) {
    for (const ItemDef& item : kItems) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

const AbilityDef* abilityDef(std::string_view id) {
    for (const AbilityDef& ability : kAbilities) {
        if (ability.id == id) {
            return &ability;
        }
    }
    return nullptr;
}

float iconCell(std::string_view id, bool ability) {
    if (ability) {
        const AbilityDef* def = abilityDef(id);
        return def ? def->iconCell : kGenericIcon;
    }
    const ItemDef* def = itemDef(id);
    return def ? def->iconCell : kGenericIcon;
}

// A 1px box with the four corner pixels left empty.
void drawOpenCornerBox(animal::Renderer2D& renderer, animal::RectF box, animal::Color fill, animal::Color edge) {
    if (fill.a != 0) {
        renderer.fillRect({box.x + 1.0f, box.y + 1.0f, box.width - 2.0f, box.height - 2.0f}, fill);
    }
    renderer.fillRect({box.x + 1.0f, box.y, box.width - 2.0f, 1.0f}, edge);
    renderer.fillRect({box.x + 1.0f, box.y + box.height - 1.0f, box.width - 2.0f, 1.0f}, edge);
    renderer.fillRect({box.x, box.y + 1.0f, 1.0f, box.height - 2.0f}, edge);
    renderer.fillRect({box.x + box.width - 1.0f, box.y + 1.0f, 1.0f, box.height - 2.0f}, edge);
}

struct GearFrame {
    animal::RectF outer;
    float gridX = 0.0f;
    float gridW = 0.0f;
    float itemsTop = 0.0f;
    float dividerY = 0.0f;
    float abilitiesTop = 0.0f;
};

GearFrame gearFrame() {
    const float gridW = kItemCols * kSlotSize + (kItemCols - 1) * kSlotGap;
    const float itemsH = kItemRows * kSlotSize + (kItemRows - 1) * kSlotGap;
    const float width = kFramePad * 2.0f + gridW;
    const float height = kFramePad * 2.0f + itemsH + kSplitGap + 1.0f + kSplitGap + kSlotSize;
    const float canvasW = static_cast<float>(animal::Renderer2D::kCanvasExtent.width);
    const float canvasH = static_cast<float>(animal::Renderer2D::kCanvasExtent.height);
    GearFrame frame;
    frame.outer = {std::round((canvasW - width) * 0.5f), std::round((canvasH - height) * 0.5f), width, height};
    frame.gridX = frame.outer.x + kFramePad;
    frame.gridW = gridW;
    frame.itemsTop = frame.outer.y + kFramePad;
    frame.dividerY = frame.itemsTop + itemsH + kSplitGap;
    frame.abilitiesTop = frame.dividerY + 1.0f + kSplitGap;
    return frame;
}

animal::Vec2 itemSlotPos(const GearFrame& frame, int index) {
    const int col = index % kItemCols;
    const int row = index / kItemCols;
    return {frame.gridX + static_cast<float>(col) * (kSlotSize + kSlotGap),
            frame.itemsTop + static_cast<float>(row) * (kSlotSize + kSlotGap)};
}

animal::Vec2 abilitySlotPos(const GearFrame& frame, int index) {
    return {frame.gridX + static_cast<float>(index) * (kSlotSize + kSlotGap), frame.abilitiesTop};
}

// The carried stack for a catalog slot, or null when that item has never been picked up.
const animal::CarriedItem* itemInSlot(const animal::Loadout& loadout, int slot) {
    if (slot < 0 || slot >= kKnownItems) {
        return nullptr;
    }
    return loadout.findItem(kItems[slot].id);
}

animal::Vec2 canvasToScreen(const animal::Renderer2D& renderer, float x, float y) {
    const animal::PresentLayout& layout = renderer.presentLayout();
    const float scale = static_cast<float>(layout.scale);
    return {static_cast<float>(layout.x) + x * scale, static_cast<float>(layout.y) + y * scale};
}

void drawIcon(animal::Renderer2D& renderer, const animal::SpriteMaterial& props, animal::Vec2 topLeft, float cell) {
    const float size = static_cast<float>(kTileSize);
    renderer.drawSprite(props, topLeft, {cell * size, 0.0f, size, size});
}

// The count's baseline sits on the icon's bottom edge, and its advance on the right edge, with a
// 1px black outline. The cell is taller than the digit, so aligning the cell itself lands too high.
void drawCount(animal::Renderer2D& renderer, const animal::BitmapFont& font, animal::Vec2 iconCanvas, int count) {
    const std::string text = std::to_string(count);
    const float fontPx = static_cast<float>(renderer.overlayScale());
    const float canvasScale = static_cast<float>(renderer.presentLayout().scale);
    const animal::Vec2 screen = canvasToScreen(renderer, iconCanvas.x, iconCanvas.y);
    const float iconSize = static_cast<float>(kTileSize) * canvasScale;
    const float x = std::round(screen.x + iconSize - renderer.textWidth(font, text));
    const float y = std::round(screen.y + iconSize - static_cast<float>(font.baseline()) * fontPx);
    const animal::Color edge = animal::Color::rgba(0, 0, 0);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            renderer.drawText(font, {x + static_cast<float>(dx) * fontPx, y + static_cast<float>(dy) * fontPx}, text, edge);
        }
    }
    renderer.drawText(font, {x, y}, text, kCount);
}

} // namespace

void SandboxGame::syncAbilities() {
    constexpr std::string_view kPrefix = "ability:";
    for (const std::string& flag : save_.flags) {
        if (flag.starts_with(kPrefix)) {
            loadout_.grantAbility(flag.substr(kPrefix.size()));
        }
    }
}

std::string SandboxGame::gearLabel(std::string_view id, bool ability) const {
    const char* key = nullptr;
    if (ability) {
        if (const AbilityDef* def = abilityDef(id)) {
            key = def->nameKey;
        }
    } else if (const ItemDef* def = itemDef(id)) {
        key = def->nameKey;
    }
    if (key && strings_.has(key)) {
        return text(key);
    }
    return std::string(id);
}

void SandboxGame::collectCarriedItems(const animal::IntRect& player) {
    const ItemDef* dynamite = itemDef("dynamite");
    if (!dynamite) {
        return;
    }
    const int room = dynamite->maxCount - loadout_.count(dynamite->id);
    const float x = static_cast<float>(player.x) + static_cast<float>(player.width) * 0.5f;
    const int picked = cave_.collectDynamite(player, room);
    if (picked > 0) {
        loadout_.gainItem(dynamite->id, dynamite->use, picked, dynamite->maxCount);
        playSound(sounds_.pickup, x, 0.8f);
    }
    const ItemDef* grapple = itemDef("grapple");
    if (grapple && !loadout_.findItem(grapple->id) && cave_.collectGrapple(player)) {
        loadout_.gainItem(grapple->id, grapple->use, 1, grapple->maxCount);
        playSound(sounds_.pickup, x, 0.8f);
    }
}

void SandboxGame::useHeldItem(animal::Entity& player) {
    const animal::CarriedItem* held = loadout_.equipped();
    if (!held) {
        return;
    }
    if (held->id == "dynamite") {
        throwDynamite(player);
    } else if (held->id == "grapple") {
        useGrapple(player);
    }
}

void SandboxGame::updateGearMenu(const animal::platform::Input& input) {
    if (dev_.editing() || endingOpen_ || menu_.page != Page::None || mapOpen_) {
        gearOpen_ = false;
        return;
    }
    if (actions_.pressedThisFrame(Items, input)) {
        gearOpen_ = !gearOpen_;
        if (gearOpen_) {
            gearIndex_ = 0;
            if (const animal::CarriedItem* held = loadout_.equipped()) {
                for (int i = 0; i < kItemSlots && i < kKnownItems; ++i) {
                    if (kItems[i].id == held->id) {
                        gearIndex_ = i;
                        break;
                    }
                }
            }
        }
        return;
    }
    if (!gearOpen_) {
        return;
    }
    if (input.pressed(Key::Escape) || input.pressed(Key::Backspace)) {
        gearOpen_ = false;
        return;
    }
    const bool right = actions_.pressed(MoveRight);
    const bool left = actions_.pressed(MoveLeft);
    const bool down = actions_.pressed(MoveDown);
    const bool up = actions_.pressed(MoveUp);
    int col = gearIndex_ % kItemCols;
    int row = gearIndex_ / kItemCols;
    if (left != right) {
        col = (col + (right ? 1 : kItemCols - 1)) % kItemCols;
    }
    if (up != down) {
        row ^= 1;
    }
    const int next = row * kItemCols + col;
    if (next != gearIndex_) {
        gearIndex_ = next;
        if (const animal::Entity* player = entities_.get(player_)) {
            playSound(sounds_.click, player->body.exactPosition().x, 0.35f);
        }
    }
    const bool confirm = actions_.pressed(Jump) || input.pressed(Key::Enter) || input.pressed(Key::KeypadEnter);
    if (!confirm) {
        return;
    }
    const animal::CarriedItem* slot = itemInSlot(loadout_, gearIndex_);
    if (!slot || (loadout_.equipped() && loadout_.equipped()->id == slot->id)) {
        return; // an empty placeholder, or the item already in hand
    }
    loadout_.equip(slot->id);
    if (const animal::Entity* player = entities_.get(player_)) {
        playSound(sounds_.click, player->body.exactPosition().x, 0.6f);
    }
}

void SandboxGame::drawHeldItem(animal::Renderer2D& renderer) const {
    const animal::CarriedItem* held = loadout_.equipped();
    if (!held) {
        return;
    }
    const animal::Vec2 origin = camera_.topLeft(animal::Renderer2D::kCanvasExtent) + kSlotCanvas;
    drawOpenCornerBox(renderer, {origin.x, origin.y, kSlotSize, kSlotSize}, kSlotFill, kSlotEdge);
    drawIcon(renderer, assets_.props, origin + animal::Vec2{kIconInset, kIconInset}, iconCell(held->id, false));
}

void SandboxGame::drawHeldItemCount(animal::Renderer2D& renderer) const {
    const animal::CarriedItem* held = loadout_.equipped();
    if (!held || held->use != animal::ItemUse::Consumable) {
        return;
    }
    drawCount(renderer, font_, kSlotCanvas + animal::Vec2{kIconInset, kIconInset}, held->count);
}

void SandboxGame::drawGearMenuWorld(animal::Renderer2D& renderer) const {
    const animal::Vec2 origin = camera_.topLeft(animal::Renderer2D::kCanvasExtent);
    const float canvasW = static_cast<float>(animal::Renderer2D::kCanvasExtent.width);
    const float canvasH = static_cast<float>(animal::Renderer2D::kCanvasExtent.height);
    renderer.fillRect({origin.x, origin.y, canvasW, canvasH}, kShade);

    const GearFrame frame = gearFrame();
    const animal::Vec2 panel = origin + animal::Vec2{frame.outer.x, frame.outer.y};
    drawOpenCornerBox(renderer, {panel.x, panel.y, frame.outer.width, frame.outer.height}, kFrameFill, kFrameEdge);
    const animal::Vec2 line = origin + animal::Vec2{frame.gridX, frame.dividerY};
    renderer.fillRect({line.x, line.y, frame.gridW, 1.0f}, kDivider);

    const int selected = gearIndex_ >= 0 && gearIndex_ < kItemSlots ? gearIndex_ : 0;
    const auto slotBox = [&](animal::Vec2 canvas) {
        const animal::Vec2 at = origin + canvas;
        return animal::RectF{at.x, at.y, kSlotSize, kSlotSize};
    };
    for (int i = 0; i < kItemSlots; ++i) {
        if (i == selected) {
            continue;
        }
        drawOpenCornerBox(renderer, slotBox(itemSlotPos(frame, i)), animal::Color::rgba(0, 0, 0, 0), kPlaceholder);
    }
    drawOpenCornerBox(renderer, slotBox(itemSlotPos(frame, selected)), animal::Color::rgba(0, 0, 0, 0), kSelectEdge);
    for (int i = 0; i < kAbilitySlots; ++i) {
        drawOpenCornerBox(renderer, slotBox(abilitySlotPos(frame, i)), animal::Color::rgba(0, 0, 0, 0), kPlaceholder);
    }
    for (int i = 0; i < kItemSlots && i < kKnownItems; ++i) {
        if (const animal::CarriedItem* item = itemInSlot(loadout_, i)) {
            drawIcon(renderer, assets_.props, origin + itemSlotPos(frame, i) + animal::Vec2{kIconInset, kIconInset},
                     iconCell(item->id, false));
        }
    }
    for (int i = 0; i < kAbilitySlots && i < kKnownAbilities; ++i) {
        if (loadout_.hasAbility(kAbilities[i].id)) {
            drawIcon(renderer, assets_.props,
                     origin + abilitySlotPos(frame, i) + animal::Vec2{kIconInset, kIconInset},
                     iconCell(kAbilities[i].id, true));
        }
    }
}

void SandboxGame::drawGearMenu(animal::Renderer2D& renderer) const {
    const GearFrame frame = gearFrame();
    for (int i = 0; i < kItemSlots && i < kKnownItems; ++i) {
        const animal::CarriedItem* item = itemInSlot(loadout_, i);
        if (!item || item->use != animal::ItemUse::Consumable) {
            continue;
        }
        drawCount(renderer, font_, itemSlotPos(frame, i) + animal::Vec2{kIconInset, kIconInset}, item->count);
    }
}

} // namespace game
