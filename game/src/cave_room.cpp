#include "cave_room.h"

#include <animal/core/log.h>
#include <animal/renderer/fluid.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>

namespace game {

namespace {

constexpr float kEmbersPerSecond = 2.2f; // per candle
constexpr int kVineSegment = 3;          // px, matches VineSystem::Params::segmentLength
constexpr int kVineClearance = 3;        // px left between a vine tip and the ground
constexpr int kMaxVineSegments = 50;

constexpr animal::Vec3 kCandleColor{1.0f, 0.62f, 0.28f};
constexpr animal::Vec3 kFireflyLight{0.75f, 1.0f, 0.45f};
constexpr animal::Vec3 kSmokeColor{0.85f, 0.85f, 0.9f};
constexpr animal::Color kDustColor = animal::Color::rgba(205, 195, 175, 170);
constexpr animal::Color kFireflyColor = animal::Color::rgba(210, 255, 130, 255);
constexpr animal::Color kVineStem = animal::Color::rgba(70, 140, 72);
constexpr animal::Color kVineLeaf = animal::Color::rgba(120, 196, 96);
// Grass colors per [shade][level] (base → tip).
constexpr animal::Color kGrassColors[3][3] = {
    {animal::Color::rgba(46, 104, 52), animal::Color::rgba(76, 150, 70), animal::Color::rgba(136, 198, 92)},
    {animal::Color::rgba(40, 92, 58), animal::Color::rgba(64, 136, 76), animal::Color::rgba(110, 182, 96)},
    {animal::Color::rgba(56, 112, 46), animal::Color::rgba(96, 160, 64), animal::Color::rgba(160, 208, 96)},
};

animal::Vec3 toVec3(animal::Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
}

float flicker(double time, float phase) {
    const auto t = static_cast<float>(time);
    return 1.0f + 0.08f * std::sin(t * 13.0f + phase) + 0.05f * std::sin(t * 29.0f + phase * 2.3f);
}

// Point objects stand on a surface: the point is the bottom-centre of the 8×8 prop.
animal::Vec2 propTopLeft(const animal::LevelObject& object) {
    return {std::round(object.position.x) - 4.0f, std::round(object.position.y) - 8.0f};
}

} // namespace

bool CaveRoom::build(const animal::Room& room, uint32_t seed) {
    tiles_ = room.tiles;
    rng_ = animal::Random(seed);
    spawn_.reset();
    candles_.clear();
    vents_.clear();
    tileLights_.clear();
    pointLights_.clear();
    particles_.clear();
    vines_ = animal::VineSystem();

    ambient_ = toVec3(room.properties.getColor("ambient", animal::Color::rgba(18, 18, 28)));
    dustCount_ = static_cast<size_t>(std::max(0, room.properties.getInt("dust", 70)));
    fireflyCount_ = static_cast<size_t>(std::max(0, room.properties.getInt("fireflies", 14)));

    grass_.generate(tiles_, [&room](animal::TileId id) { return room.propertiesOf(id).getBool("grass"); }, seed);

    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            const animal::Properties& p = room.propertiesOf(tiles_.tile(tx, ty));
            if (p.getBool("light")) {
                TileLight light;
                light.position = {tx * 8.0f + 4.0f, ty * 8.0f + 4.0f};
                light.color = toVec3(p.getColor("lightColor"));
                light.radius = p.getFloat("lightRadius", 44.0f);
                light.intensity = p.getFloat("lightIntensity", 1.0f);
                light.height = p.getFloat("lightHeight", 12.0f);
                light.phase = static_cast<float>(tx);
                tileLights_.push_back(light);
            }
        }
    }

    float phase = 0.0f;
    for (const animal::LevelObject& object : room.objects) {
        if (object.type == "spawn") {
            spawn_ = object.position;
        } else if (object.type == "candle") {
            candles_.push_back({propTopLeft(object), phase, 0.0f});
            phase += 1.7f;
        } else if (object.type == "vent") {
            vents_.push_back({propTopLeft(object), object.properties.getFloat("rate", 6.0f),
                              object.properties.getFloat("force", 420.0f)});
        } else if (object.type == "vine") {
            // Hang down to just above the first solid tile below the anchor.
            const int tx = static_cast<int>(std::floor(object.position.x / kTileSize));
            int row = static_cast<int>(std::floor(object.position.y / kTileSize));
            const int anchorRow = row;
            while (row < tiles_.height() && !tiles_.isSolid(tx, row)) {
                ++row;
            }
            const int drop = (row - anchorRow) * kTileSize - kVineClearance;
            const int segments = object.properties.getInt("segments", std::clamp(drop / kVineSegment, 2, kMaxVineSegments));
            vines_.addVine(object.position, segments);
        } else if (object.type == "light") {
            TileLight light;
            light.position = object.position;
            light.color = toVec3(object.properties.getColor("color"));
            light.radius = object.properties.getFloat("radius", 48.0f);
            light.intensity = object.properties.getFloat("intensity", 1.0f);
            light.height = object.properties.getFloat("height", 16.0f);
            pointLights_.push_back(light);
        } else {
            animal::log::warn("{}: unknown object type '{}' (id {}) ignored", room.path, object.type, object.id);
        }
    }

    for (size_t i = 0; i < dustCount_; ++i) {
        spawnDust(true);
    }
    for (size_t i = 0; i < fireflyCount_; ++i) {
        spawnFirefly();
    }
    return true;
}

animal::Vec2 CaveRoom::randomOpenPosition() {
    const animal::IntRect bounds = tiles_.pixelBounds();
    animal::Vec2 p;
    for (int attempt = 0; attempt < 20; ++attempt) {
        p = {rng_.range(0.0f, static_cast<float>(bounds.width)), rng_.range(0.0f, static_cast<float>(bounds.height))};
        if (!tiles_.isSolid(static_cast<int>(p.x) / kTileSize, static_cast<int>(p.y) / kTileSize)) {
            break;
        }
    }
    return p;
}

// Dust only shows where light falls (drawn in the lit pass): motes floating in candle light.
void CaveRoom::spawnDust(bool midLife) {
    animal::Particle p;
    p.kind = Dust;
    p.position = randomOpenPosition();
    p.velocity = {rng_.range(-3.0f, 3.0f), rng_.range(-2.0f, 2.0f)};
    p.acceleration = {0.0f, rng_.range(-1.5f, 1.5f)};
    p.drag = 0.6f;
    p.windResponse = 6.0f;
    p.wander = 4.0f;
    p.lifetime = rng_.range(8.0f, 16.0f);
    p.age = midLife ? rng_.range(0.0f, p.lifetime * 0.8f) : 0.0f;
    p.fade = 2.0f;
    p.startColor = p.endColor = kDustColor;
    p.seed = rng_.next();
    particles_.emit(p);
}

void CaveRoom::spawnFirefly() {
    animal::Particle p;
    p.kind = Firefly;
    p.position = randomOpenPosition();
    p.drag = 1.6f;
    p.wander = 45.0f;
    p.pushResponse = 1.5f; // scatter when the player comes close
    p.flicker = rng_.range(0.4f, 0.9f);
    p.lifetime = 1.0e6f;   // until it drifts out of the room
    p.fade = 1.0f;
    p.startColor = p.endColor = kFireflyColor;
    p.seed = rng_.next();
    particles_.emit(p);
}

void CaveRoom::spawnEmber(const Candle& candle) {
    animal::Particle p;
    p.kind = Ember;
    p.position = candle.position + animal::Vec2{3.5f + rng_.range(-0.5f, 0.5f), 1.0f};
    p.velocity = {rng_.range(-8.0f, 8.0f), rng_.range(-28.0f, -16.0f)};
    p.acceleration = {0.0f, -14.0f}; // buoyancy
    p.drag = 0.9f;
    p.windResponse = 18.0f;
    p.wander = 10.0f;
    p.pushResponse = 0.6f;
    p.lifetime = rng_.range(0.7f, 1.5f);
    p.fade = 0.1f;
    p.startColor = animal::Color::rgba(255, 225, 140, 255);
    p.endColor = animal::Color::rgba(210, 60, 20, 0); // cools and fades
    p.dieOnSolid = true;
    p.seed = rng_.next();
    particles_.emit(p);
}

void CaveRoom::step(float dt, double time, std::span<const animal::Influence> influences) {
    grass_.step(dt, time, influences, wind_);
    vines_.step(dt, time, influences, wind_);
    for (Candle& candle : candles_) {
        candle.emberBudget += kEmbersPerSecond * dt;
        while (candle.emberBudget >= 1.0f) {
            spawnEmber(candle);
            candle.emberBudget -= 1.0f;
        }
    }
    particles_.step(dt, time, &tiles_, influences, wind_);
    for (size_t n = particles_.countOf(Dust); n < dustCount_; ++n) {
        spawnDust(false);
    }
    for (size_t n = particles_.countOf(Firefly); n < fireflyCount_; ++n) {
        spawnFirefly();
    }
}

void CaveRoom::drawBehindPlayer(animal::Renderer2D& renderer, const CaveAssets& assets) const {
    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            const animal::TileId tile = tiles_.tile(tx, ty);
            if (tile != 0) {
                renderer.drawSprite(assets.tileset, {tx * size, ty * size}, {tile * size, 0.0f, size, size});
            }
        }
    }
    for (const Candle& candle : candles_) {
        renderer.drawSprite(assets.props, candle.position, {0.0f, 0.0f, size, size});
    }
    for (const Vent& vent : vents_) {
        renderer.drawSprite(assets.props, vent.position, {size, 0.0f, size, size});
    }
    vines_.forEachPixel([&](int x, int y, bool leaf) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, leaf ? kVineLeaf : kVineStem);
    });
}

void CaveRoom::drawInFrontOfPlayer(animal::Renderer2D& renderer) const {
    grass_.forEachPixel([&](int x, int y, int level, uint8_t shade) {
        renderer.fillRect({static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f}, kGrassColors[shade][level]);
    });
    for (const animal::Particle& p : particles_.particles()) {
        const animal::Color c = particles_.colorOf(p);
        if (p.kind == Dust) {
            renderer.fillRect({std::round(p.position.x), std::round(p.position.y), 1.0f, 1.0f}, c);
        } else if (p.kind == Firefly) {
            renderer.drawGlow(p.position, c);
            const animal::Color halo{c.r, c.g, c.b, static_cast<uint8_t>(c.a * 0.3f)};
            for (const animal::Vec2 d : {animal::Vec2{1, 0}, animal::Vec2{-1, 0}, animal::Vec2{0, 1}, animal::Vec2{0, -1}}) {
                renderer.drawGlow(p.position + d, halo);
            }
        } else if (p.kind == Ember) {
            renderer.drawGlow(p.position, c);
        }
    }
}

void CaveRoom::drawCollision(animal::Renderer2D& renderer) const {
    const float size = static_cast<float>(kTileSize);
    for (int ty = 0; ty < tiles_.height(); ++ty) {
        for (int tx = 0; tx < tiles_.width(); ++tx) {
            if (tiles_.isSolid(tx, ty)) {
                renderer.fillRect({tx * size, ty * size, size, size}, animal::Color::rgba(255, 60, 60, 50));
            }
        }
    }
}

void CaveRoom::addLights(animal::Renderer2D& renderer, double time) const {
    for (const Candle& candle : candles_) {
        animal::PointLight light;
        light.position = candle.position + animal::Vec2{3.5f, 1.5f}; // the flame
        light.radius = 60.0f;
        light.color = kCandleColor;
        light.intensity = 1.5f * flicker(time, candle.phase);
        light.height = 10.0f;
        renderer.addLight(light);
    }
    for (const TileLight& t : tileLights_) {
        animal::PointLight light;
        light.position = t.position;
        light.radius = t.radius;
        light.color = t.color;
        light.intensity = t.intensity + 0.15f * static_cast<float>(std::sin(time * 1.3 + t.phase));
        light.height = t.height;
        renderer.addLight(light);
    }
    for (const TileLight& t : pointLights_) {
        renderer.addLight({t.position, t.radius, t.color, t.intensity, t.height});
    }
    for (const animal::Particle& p : particles_.particles()) {
        if (p.kind == Firefly) {
            animal::PointLight light;
            light.position = p.position;
            light.radius = 18.0f;
            light.color = kFireflyLight;
            light.intensity = 0.7f * static_cast<float>(particles_.colorOf(p).a) / 255.0f;
            light.height = 6.0f;
            renderer.addLight(light);
        }
    }
}

void CaveRoom::addFluidSources(animal::Renderer2D& renderer, double time) const {
    float phase = 0.0f;
    for (const Vent& vent : vents_) {
        const auto t = static_cast<float>(time) + phase;
        const float puff = 0.75f + 0.25f * std::sin(t * 1.3f);
        animal::FluidSplat splat;
        splat.position = vent.position + animal::Vec2{4.0f, 5.0f};
        splat.radius = 5.0f;
        // A slight sideways wobble keeps the column from rising perfectly straight.
        splat.force = {90.0f * (std::sin(t * 2.1f) + 0.5f * std::sin(t * 5.3f)), -vent.force * puff};
        splat.color = kSmokeColor;
        splat.smoke = vent.rate * puff;
        renderer.addFluidSplat(splat);
        phase += 2.0f;
    }
}

std::vector<uint8_t> CaveRoom::fluidObstacles(animal::Vec2 canvasOrigin) const {
    const float cellSize = static_cast<float>(animal::Renderer2D::kCanvasExtent.width) / animal::kFluidGrid.width;
    std::vector<uint8_t> mask(size_t{animal::kFluidGrid.width} * animal::kFluidGrid.height);
    for (uint32_t cy = 0; cy < animal::kFluidGrid.height; ++cy) {
        for (uint32_t cx = 0; cx < animal::kFluidGrid.width; ++cx) {
            const float wx = canvasOrigin.x + (static_cast<float>(cx) + 0.5f) * cellSize;
            const float wy = canvasOrigin.y + (static_cast<float>(cy) + 0.5f) * cellSize;
            const auto tx = static_cast<int>(std::floor(wx / kTileSize));
            const auto ty = static_cast<int>(std::floor(wy / kTileSize));
            mask[cy * animal::kFluidGrid.width + cx] = (!tiles_.inBounds(tx, ty) || tiles_.isSolid(tx, ty)) ? 1 : 0;
        }
    }
    return mask;
}

} // namespace game
