#pragma once

#include <animal/core/handle.h>
#include <animal/core/math.h>
#include <animal/framework/physics.h>

#include <cstdint>
#include <vector>

namespace animal {

struct EntityTag;
using EntityId = Handle<EntityTag>;

enum class EntityKind : uint16_t {
    None,
    Player,
};

// Flat entity record (ADR-0006): common fields live here; add fields when a system needs
// them rather than introducing a generic component system.
struct Entity {
    EntityId id;
    EntityKind kind = EntityKind::None;
    Body body;
};

// Owns all entities of a room. Destruction is deferred to flushDestroyed() so systems can
// destroy entities while iterating.
class EntityWorld {
public:
    EntityId create(EntityKind kind);
    void destroy(EntityId id) { pendingDestroy_.push_back(id); }
    void flushDestroyed();

    Entity* get(EntityId id) { return entities_.get(id); }
    const Entity* get(EntityId id) const { return entities_.get(id); }
    size_t size() const { return entities_.size(); }

    template <typename F>
    void forEach(F&& fn) {
        entities_.forEach(fn);
    }

private:
    HandlePool<EntityTag, Entity> entities_;
    std::vector<EntityId> pendingDestroy_;
};

} // namespace animal
