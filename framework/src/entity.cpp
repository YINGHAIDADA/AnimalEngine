#include <animal/framework/entity.h>

namespace animal {

EntityId EntityWorld::create(EntityKind kind) {
    const EntityId id = entities_.insert(Entity{});
    Entity* entity = entities_.get(id);
    entity->id = id;
    entity->kind = kind;
    return id;
}

void EntityWorld::flushDestroyed() {
    for (EntityId id : pendingDestroy_) {
        entities_.take(id);
    }
    pendingDestroy_.clear();
}

} // namespace animal
