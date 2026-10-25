#include <animal/framework/water.h>
#include <animal/framework/tilemap.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace animal {

namespace {

// A pixel column of the pool: where the water starts and the first solid pixel below it.
struct Column {
    int top = 0;
    int bottom = 0;
};

bool solidAt(const TileMap& map, int x, int y) {
    const int tile = map.tileSize();
    if (tile <= 0 || x < 0 || y < 0 || x >= map.width() * tile || y >= map.height() * tile) {
        return true; // the map edge holds the water in
    }
    return map.isSolid(x / tile, y / tile);
}

// Next tile boundary strictly below y.
int nextTileRow(const TileMap& map, int y) {
    const int tile = map.tileSize();
    const int row = y >= 0 ? y / tile : -1;
    return (row + 1) * tile;
}

// Air under the waterline, down to the first solid tile. A ceiling on the waterline is skipped,
// so the column starts at its underside. No air: nullopt.
std::optional<Column> columnUnder(const TileMap& map, int x, int surface) {
    const int tile = map.tileSize();
    if (tile <= 0) {
        return std::nullopt;
    }
    const int limit = map.height() * tile;
    int y = surface;
    if (solidAt(map, x, y)) {
        while (y < limit && solidAt(map, x, y)) {
            y = nextTileRow(map, y);
        }
        if (y >= limit) {
            return std::nullopt;
        }
    }
    const int top = y;
    while (y < limit && !solidAt(map, x, y)) {
        y = nextTileRow(map, y);
    }
    if (y <= top) {
        return std::nullopt;
    }
    return Column{top, y};
}

bool surfaceOpen(const TileMap& map, int x, int surface) {
    const int tile = map.tileSize();
    return tile > 0 && x >= 0 && x < map.width() * tile && surface >= 0 && surface < map.height() * tile &&
           !solidAt(map, x, surface);
}

// The back wall: open air drawn behind the playfield. Hazards and the void outside the map are not.
bool backWall(const TileMap& map, int x, int y) {
    const int tile = map.tileSize();
    if (tile <= 0 || x < 0 || y < 0 || x >= map.width() * tile || y >= map.height() * tile) {
        return false;
    }
    const int tx = x / tile;
    const int ty = y / tile;
    return !map.isSolid(tx, ty) && map.hazardOf(map.tile(tx, ty)) == Hazard::None;
}

bool alreadyFalling(const std::vector<WaterBody::Fall>& falls, int x0, int top) {
    for (const WaterBody::Fall& fall : falls) {
        if (fall.x0 == x0 && fall.top == top) {
            return true;
        }
    }
    return false;
}

// Still water in this pool covers the pixel. A hollow under a floor is the same column but a
// lower span, so it is not covered.
bool covered(int left, int width, const std::vector<int>& tops, const std::vector<int>& floors, int x, int y) {
    if (x < left || x >= left + width) {
        return false;
    }
    const size_t i = static_cast<size_t>(x - left);
    return y >= tops[i] && y < floors[i];
}

// Every place still water touches back-wall air, including a hollow cut out of the pool, pours
// down that tile column to the first solid tile.
void addFalls(std::vector<WaterBody::Fall>& falls, int left, int width, const std::vector<int>& tops,
              const std::vector<int>& floors, const TileMap& map) {
    const int tile = map.tileSize();
    if (tile <= 0 || width <= 0 || tops.empty()) {
        return;
    }
    const int limit = map.height() * tile;
    const int mapWidth = map.width() * tile;
    for (int i = 0; i < width; ++i) {
        const int top = tops[static_cast<size_t>(i)];
        const int floor = floors[static_cast<size_t>(i)];
        for (const int direction : {-1, 1}) {
            const int beside = left + i + direction;
            if (beside < 0 || beside >= mapWidth) {
                continue;
            }
            int opening = -1;
            for (int y = top; y < floor; y = nextTileRow(map, y)) {
                if (!covered(left, width, tops, floors, beside, y) && backWall(map, beside, y)) {
                    opening = std::max(y, (y / tile) * tile);
                    break;
                }
            }
            if (opening < 0) {
                continue;
            }
            int ground = opening;
            while (ground < limit && backWall(map, beside, ground) &&
                   !covered(left, width, tops, floors, beside, ground)) {
                ground = nextTileRow(map, ground);
            }
            if (ground - opening < tile) {
                continue;
            }
            if (!alreadyFalling(falls, beside, opening)) {
                falls.push_back({beside, beside + 1, opening, ground});
            }
        }
    }
}

} // namespace

WaterBody::WaterBody(const IntRect& area, const WaterParams& params)
    : area_(area),
      params_(params),
      height_(static_cast<size_t>(std::max(area.width, 1)), 0.0f),
      velocity_(height_.size(), 0.0f) {}

std::vector<WaterBody> WaterBody::fit(const IntRect& area, const TileMap& map, const WaterParams& params) {
    std::vector<WaterBody> pools;
    const int tile = map.tileSize();
    if (tile <= 0 || area.width <= 0 || area.height <= 0 || map.width() <= 0 || map.height() <= 0) {
        return pools;
    }
    const int mapWidth = map.width() * tile;
    int left = std::clamp(area.x, 0, mapWidth);
    int right = std::clamp(area.right(), 0, mapWidth);
    // An open waterline grows out to the walls. A solid waterline (a ceiling) stays put.
    while (left > 0 && surfaceOpen(map, left - 1, area.y)) {
        --left;
    }
    while (right < mapWidth && surfaceOpen(map, right, area.y)) {
        ++right;
    }
    for (int x = left; x < right;) {
        while (x < right && !columnUnder(map, x, area.y)) {
            ++x;
        }
        const int start = x;
        while (x < right && columnUnder(map, x, area.y)) {
            ++x;
        }
        if (start == x || x <= area.x || start >= area.right()) {
            continue; // a gap, or a run that does not meet the rectangle
        }
        const int width = x - start;
        WaterBody body({start, area.y, width, 1}, params);
        body.tops_.assign(static_cast<size_t>(width), area.y);
        body.floors_.assign(static_cast<size_t>(width), area.y);
        int deepest = area.y;
        for (int i = 0; i < width; ++i) {
            const Column column = *columnUnder(map, start + i, area.y);
            body.tops_[static_cast<size_t>(i)] = column.top;
            body.floors_[static_cast<size_t>(i)] = column.bottom;
            deepest = std::max(deepest, column.bottom);
        }
        body.area_.height = std::max(1, deepest - area.y);
        addFalls(body.falls_, body.area_.x, body.area_.width, body.tops_, body.floors_, map);
        pools.push_back(std::move(body));
    }
    return pools;
}

void WaterBody::step(float dt) {
    // Semi-implicit wave equation; substeps keep waveSpeed · dt below one column (stability).
    const auto substeps = static_cast<int>(std::ceil(params_.waveSpeed * dt / 0.5f));
    const float h = dt / static_cast<float>(std::max(substeps, 1));
    const float c2 = params_.waveSpeed * params_.waveSpeed;
    const size_t n = height_.size();
    for (int s = 0; s < substeps; ++s) {
        for (size_t i = 0; i < n; ++i) {
            const float left = height_[i > 0 ? i - 1 : i];
            const float right = height_[i + 1 < n ? i + 1 : i];
            const float laplacian = left + right - 2.0f * height_[i];
            const float a = c2 * laplacian - params_.stiffness * height_[i] - params_.damping * velocity_[i];
            velocity_[i] += a * h;
        }
        for (size_t i = 0; i < n; ++i) {
            height_[i] = std::clamp(height_[i] + velocity_[i] * h, -params_.maxOffset, params_.maxOffset);
        }
    }
}

void WaterBody::disturb(float x, float halfWidth, float velocity) {
    const float local = x - static_cast<float>(area_.x);
    const int first = std::max(0, static_cast<int>(std::floor(local - halfWidth)));
    const int last = std::min(static_cast<int>(height_.size()) - 1, static_cast<int>(std::ceil(local + halfWidth)));
    for (int i = first; i <= last; ++i) {
        // Strongest at x, fading to the edges of the disturbed span.
        const float falloff = 1.0f - std::min(1.0f, std::fabs(static_cast<float>(i) + 0.5f - local) / (halfWidth + 1.0f));
        velocity_[static_cast<size_t>(i)] += velocity * falloff;
    }
}

float WaterBody::surfaceY(float x) const {
    const int column = std::clamp(static_cast<int>(std::floor(x)) - area_.x, 0, static_cast<int>(height_.size()) - 1);
    return static_cast<float>(area_.y) + height_[static_cast<size_t>(column)];
}

float WaterBody::submersion(const IntRect& box) const {
    const float centerX = static_cast<float>(box.x) + static_cast<float>(box.width) * 0.5f;
    if (centerX < static_cast<float>(area_.x) || centerX >= static_cast<float>(area_.right()) || box.height <= 0 ||
        box.bottom() <= area_.y - static_cast<int>(params_.maxOffset)) {
        return 0.0f;
    }
    const int column = std::clamp(static_cast<int>(std::floor(centerX)) - area_.x, 0, area_.width - 1);
    const int floorY = floors_.empty() ? area_.bottom() : floors_[static_cast<size_t>(column)];
    if (box.y >= floorY) {
        return 0.0f; // on or below this column's floor
    }
    const float below = static_cast<float>(box.bottom()) - surfaceY(centerX);
    return std::clamp(below / static_cast<float>(box.height), 0.0f, 1.0f);
}

PlatformerParams swimmingParams(const PlatformerParams& base, float submersion, const SwimParams& swim) {
    if (submersion <= 0.0f) {
        return base;
    }
    const float s = std::clamp(submersion, 0.0f, 1.0f);
    auto blend = [s](float dry, float wet) { return dry + (wet - dry) * s; };
    PlatformerParams p = base;
    p.gravity = base.gravity * (1.0f - s / swim.floatDepth);
    p.fallGravityMultiplier = 1.0f; // no snappy descent in water
    p.runSpeed = blend(base.runSpeed, base.runSpeed * swim.runScale);
    p.groundAcceleration = blend(base.groundAcceleration, base.groundAcceleration * swim.runScale);
    p.airAcceleration = blend(base.airAcceleration, base.airAcceleration * swim.runScale);
    p.maxFallSpeed = blend(base.maxFallSpeed, base.maxFallSpeed * swim.fallSpeedScale);
    return p;
}

void applyWater(PlatformerState& state, const PlatformerParams& base, float submersion, float dt,
                const SwimParams& swim) {
    if (submersion <= 0.0f) {
        return;
    }
    const float keep = std::max(0.0f, 1.0f - swim.drag * submersion * dt);
    state.velocity = state.velocity * keep;
    if (submersion >= swim.jumpDepth) {
        state.coyoteTimer = base.coyoteFrames;
    }
}

void stepDive(Body& body, PlatformerState& state, Vec2 move, const DiveParams& params, const TileMap& map, float dt) {
    const float length = std::hypot(move.x, move.y);
    if (length > 1.0f) {
        move = move * (1.0f / length);
    }
    Vec2 target = move * params.speed;
    if (move.y == 0.0f) {
        target.y = -params.drift;
    }
    const float maxChange = params.acceleration * dt;
    const auto approach = [maxChange](float value, float goal) {
        return value + std::clamp(goal - value, -maxChange, maxChange);
    };
    state.velocity = {approach(state.velocity.x, target.x), approach(state.velocity.y, target.y)};
    if (moveX(body, state.velocity.x * dt, map)) {
        state.velocity.x = 0.0f;
    }
    if (moveY(body, state.velocity.y * dt, map)) {
        state.velocity.y = 0.0f;
    }
    state.grounded = isGrounded(body, map);
    state.rising = false;
    state.jumpedThisStep = false;
    state.landedThisStep = false;
}

void applyCurrent(Body& body, Vec2 current, float submersion, const TileMap& map, float dt) {
    if (submersion <= 0.0f) {
        return;
    }
    const float share = std::min(submersion, 1.0f);
    moveX(body, current.x * share * dt, map);
    moveY(body, current.y * share * dt, map);
}

} // namespace animal
