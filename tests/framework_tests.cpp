// Framework: tile collision, pixel-stepped movement, platformer feel, action latching.

#include <animal/framework/action_map.h>
#include <animal/framework/entity.h>
#include <animal/framework/foliage.h>
#include <animal/framework/physics.h>
#include <animal/framework/platformer.h>
#include <animal/framework/tilemap.h>

#include "test_harness.h"

#include <algorithm>
#include <climits>

namespace {

using namespace animal;

constexpr TileId kSolid = 1;
constexpr float kDt = 1.0f / 60.0f;

TileMap emptyMap(int32_t w = 20, int32_t h = 12) {
    TileMap map(w, h, 8);
    map.setSolid(kSolid, true);
    return map;
}

void fillRow(TileMap& map, int32_t ty, int32_t x0, int32_t x1) {
    for (int32_t tx = x0; tx <= x1; ++tx) {
        map.setTile(tx, ty, kSolid);
    }
}

Body playerBody(int32_t x, int32_t y) {
    Body body;
    body.position = {x, y};
    body.collider = {0, 0, 6, 7};
    return body;
}

// Places a player standing on the floor row `floorTy` at pixel column x.
Body standingOn(const TileMap& map, int32_t floorTy, int32_t x) {
    return playerBody(x, floorTy * map.tileSize() - 7);
}

// --- Tile map ------------------------------------------------------------------------

TEST(tilemap_overlap_and_bounds) {
    TileMap map = emptyMap();
    map.setTile(3, 2, kSolid);
    CHECK(map.overlapsSolid({24, 16, 8, 8}));
    CHECK(map.overlapsSolid({31, 23, 1, 1}));
    CHECK(!map.overlapsSolid({32, 16, 8, 8})); // touching the right edge is not overlapping
    CHECK(!map.overlapsSolid({16, 16, 8, 8}));
    CHECK(!map.overlapsSolid({-16, -16, 8, 8})); // outside the map is open
    CHECK(!map.isSolid(-1, 0));
}

// --- Pixel-stepped movement ----------------------------------------------------------

TEST(move_stops_flush_against_wall) {
    TileMap map = emptyMap();
    for (int32_t ty = 0; ty < map.height(); ++ty) {
        map.setTile(5, ty, kSolid); // wall spans x = 40..47
    }
    Body body = playerBody(20, 8);
    CHECK(moveX(body, 100.0f, map));
    CHECK(body.position.x == 34); // right edge 40, flush with the wall
    CHECK(body.remainder.x == 0.0f);
    CHECK(!moveX(body, -3.0f, map));
    CHECK(body.position.x == 31);
}

TEST(move_accumulates_subpixel_remainder) {
    TileMap map = emptyMap();
    Body body = playerBody(10, 10);
    moveX(body, 0.4f, map);
    CHECK(body.position.x == 10);
    moveX(body, 0.4f, map);
    moveX(body, 0.4f, map);
    CHECK(body.position.x == 11);
    CHECK(test::near(body.exactPosition().x, 11.2f));
}

TEST(move_never_tunnels_at_high_speed) {
    TileMap map = emptyMap(200, 4);
    map.setTile(100, 1, kSolid); // single-tile post at x = 800..807
    Body body = playerBody(0, 8);
    CHECK(moveX(body, 5000.0f, map));
    CHECK(body.position.x == 794);
}

TEST(body_fits_one_tile_tunnel) {
    TileMap map = emptyMap();
    fillRow(map, 4, 0, 19); // ceiling y = 32..39
    fillRow(map, 6, 0, 19); // floor   y = 48..55, gap y = 40..47
    Body body = playerBody(0, 41);
    CHECK(!moveX(body, 100.0f, map));
    CHECK(body.position.x == 100);
}

// --- Platformer ------------------------------------------------------------------------

TEST(platformer_lands_exactly_on_floor) {
    TileMap map = emptyMap();
    fillRow(map, 10, 0, 19); // floor top at y = 80
    Body body = playerBody(40, 10);
    PlatformerState state;
    PlatformerParams params;
    bool landed = false;
    for (int i = 0; i < 120; ++i) {
        stepPlatformer(body, state, {}, params, map, kDt);
        landed = landed || state.landedThisStep;
    }
    CHECK(landed);
    CHECK(state.landingSpeed > 100.0f);
    CHECK(state.grounded);
    CHECK(body.worldCollider().bottom() == 80);
    CHECK(state.velocity.y == 0.0f);
}

TEST(platformer_variable_jump_height) {
    PlatformerParams params;
    auto peakHeight = [&](int holdFrames) {
        TileMap map = emptyMap();
        fillRow(map, 10, 0, 19);
        Body body = standingOn(map, 10, 40);
        const int32_t startY = body.position.y;
        PlatformerState state;
        int32_t minY = startY;
        for (int i = 0; i < 120; ++i) {
            PlatformerInput input;
            input.jumpPressed = i == 0;
            input.jumpHeld = i < holdFrames;
            stepPlatformer(body, state, input, params, map, kDt);
            minY = std::min(minY, body.position.y);
        }
        return startY - minY;
    };
    const int32_t full = peakHeight(120);
    const int32_t tap = peakHeight(2);
    CHECK(full >= 24 && full <= 36); // about 3–4 tiles
    CHECK(tap > 0 && tap * 2 < full);
}

TEST(platformer_coyote_time) {
    PlatformerParams params;
    // Walks right off a ledge; jumps `delay` steps after the first airborne step.
    auto jumpsAfterLeaving = [&](int delay) {
        TileMap map = emptyMap();
        fillRow(map, 10, 0, 5); // ledge ends at x = 48
        Body body = standingOn(map, 10, 30);
        PlatformerState state;
        int airborneSteps = -1;
        for (int i = 0; i < 200; ++i) {
            PlatformerInput input;
            input.moveX = 1.0f;
            input.jumpPressed = airborneSteps == delay;
            input.jumpHeld = input.jumpPressed;
            stepPlatformer(body, state, input, params, map, kDt);
            if (state.jumpedThisStep) {
                return true;
            }
            if (!state.grounded && airborneSteps < 0) {
                airborneSteps = 0;
            } else if (airborneSteps >= 0) {
                ++airborneSteps;
            }
            if (airborneSteps > delay) {
                return false;
            }
        }
        return false;
    };
    CHECK(jumpsAfterLeaving(2));
    CHECK(!jumpsAfterLeaving(params.coyoteFrames + 2));
}

TEST(platformer_jump_buffer) {
    PlatformerParams params;
    auto simulate = [&](int pressStep, int& landStep, int& jumpStep) {
        TileMap map = emptyMap();
        fillRow(map, 10, 0, 19);
        Body body = playerBody(40, 20);
        PlatformerState state;
        landStep = jumpStep = -1;
        for (int i = 0; i < 120; ++i) {
            PlatformerInput input;
            input.jumpPressed = i == pressStep;
            input.jumpHeld = input.jumpPressed;
            stepPlatformer(body, state, input, params, map, kDt);
            if (state.landedThisStep && landStep < 0) {
                landStep = i;
            }
            if (state.jumpedThisStep && jumpStep < 0) {
                jumpStep = i;
            }
        }
    };
    int land = 0;
    int jump = 0;
    simulate(INT_MAX, land, jump);
    CHECK(land > 0 && jump < 0);

    int land2 = 0;
    simulate(land - 3, land2, jump);
    CHECK(jump == land + 1); // buffered press fires on the first grounded step

    simulate(land - params.jumpBufferFrames - 3, land2, jump);
    CHECK(jump < 0); // pressed too early: buffer expired
}

TEST(platformer_ceiling_corner_correction) {
    TileMap map = emptyMap();
    fillRow(map, 9, 0, 19);       // floor top at y = 72
    map.setTile(3, 5, kSolid);    // ceiling block x = 24..31, y = 40..47
    Body body = standingOn(map, 9, 19); // spans x = 19..24: overlaps the block by 1 px
    PlatformerState state;
    PlatformerParams params;
    int32_t minY = body.position.y;
    for (int i = 0; i < 60; ++i) {
        PlatformerInput input;
        input.jumpPressed = i == 0;
        input.jumpHeld = true;
        stepPlatformer(body, state, input, params, map, kDt);
        minY = std::min(minY, body.position.y);
    }
    CHECK(minY < 40);               // rose past the block instead of bonking
    CHECK(body.position.x == 18);   // nudged 1 px left

    params.cornerCorrection = 0;
    Body blocked = standingOn(map, 9, 19);
    PlatformerState blockedState;
    int32_t blockedMinY = blocked.position.y;
    for (int i = 0; i < 60; ++i) {
        PlatformerInput input;
        input.jumpPressed = i == 0;
        input.jumpHeld = true;
        stepPlatformer(blocked, blockedState, input, params, map, kDt);
        blockedMinY = std::min(blockedMinY, blocked.position.y);
    }
    CHECK(blockedMinY == 48); // head stops at the block's bottom edge
}

// --- Foliage -------------------------------------------------------------------------------

constexpr TileId kGrassy = 2;

TileMap grassMap() {
    TileMap map = emptyMap();
    map.setSolid(kGrassy, true);
    for (int32_t tx = 0; tx < 10; ++tx) {
        map.setTile(tx, 10, kGrassy); // exposed grass top at y = 80
    }
    map.setTile(4, 9, kSolid);        // covers tile (4, 10): no grass there
    return map;
}

GrassField makeGrass() {
    GrassField grass;
    grass.generate(grassMap(), [](TileId id) { return id == kGrassy; }, 1234u);
    return grass;
}

FoliageInfluence playerAt(int32_t x, float vx = 0.0f) {
    return {{x, 80 - 7, 6, 7}, {vx, 0.0f}, false};
}

TEST(grass_grows_only_on_exposed_grass_tops) {
    const GrassField grass = makeGrass();
    CHECK(!grass.blades().empty());
    for (const auto& blade : grass.blades()) {
        CHECK(blade.baseY == 79.0f);
        CHECK(blade.x < 32.0f || blade.x >= 40.0f); // tile 4 is covered
        CHECK(blade.height >= 2 && blade.height <= 5);
    }
    const GrassField again = makeGrass();
    CHECK(again.blades().size() == grass.blades().size()); // deterministic placement
}

TEST(grass_settles_back_upright_without_wind) {
    GrassField grass = makeGrass();
    for (auto& blade : grass.blades()) {
        blade.angle = 0.9f;
    }
    const WindParams calm{0.0f, 0.8f};
    for (int i = 0; i < 240; ++i) {
        grass.step(kDt, i * kDt, {}, calm);
    }
    for (const auto& blade : grass.blades()) {
        CHECK(std::fabs(blade.angle) < 0.01f);
    }
}

TEST(grass_bends_away_from_the_player) {
    GrassField grass = makeGrass();
    const WindParams calm{0.0f, 0.8f};
    const FoliageInfluence player = playerAt(60); // center x = 63
    for (int i = 0; i < 30; ++i) {
        grass.step(kDt, i * kDt, std::span(&player, 1), calm);
    }
    int bentRight = 0;
    int bentLeft = 0;
    for (const auto& blade : grass.blades()) {
        if (blade.x >= 64.0f && blade.x <= 69.0f) {
            bentRight += blade.angle > 0.3f;
        } else if (blade.x >= 57.0f && blade.x <= 62.0f) {
            bentLeft += blade.angle < -0.3f;
        } else if (blade.x < 50.0f) {
            CHECK(std::fabs(blade.angle) < 0.01f); // far away: untouched
        }
    }
    CHECK(bentRight > 0 && bentLeft > 0);
}

TEST(grass_is_flattened_by_landing) {
    GrassField grass = makeGrass();
    FoliageInfluence landing = playerAt(60);
    landing.velocity.y = 200.0f;
    landing.landed = true;
    landing.bounds.x = 40; // center 43; push overlap ends at 49
    grass.step(kDt, 0.0, std::span(&landing, 1), WindParams{0.0f, 0.8f});
    bool pushed = false;
    for (const auto& blade : grass.blades()) {
        if (blade.x > 50.0f && blade.x < 56.0f) { // inside the impact radius, outside the overlap
            pushed = pushed || blade.angularVelocity > 1.0f;
        }
    }
    CHECK(pushed);
}

TEST(vine_hangs_from_its_anchor) {
    VineSystem vines;
    vines.addVine({50.0f, 8.0f}, 10);
    for (int i = 0; i < 300; ++i) {
        vines.step(kDt, i * kDt, {}, WindParams{0.0f, 0.8f});
    }
    const auto& points = vines.vines()[0].points;
    CHECK(points[0] == (Vec2{50.0f, 8.0f}));
    for (size_t i = 1; i < points.size(); ++i) {
        CHECK(test::near(length(points[i] - points[i - 1]), 3.0f, 0.05f));
        CHECK(std::fabs(points[i].x - 50.0f) < 0.5f); // straight down when calm
    }
    CHECK(points.back().y > 37.0f);
}

TEST(vine_is_pushed_by_the_player) {
    VineSystem vines;
    vines.addVine({50.0f, 8.0f}, 10); // tip around y = 38
    FoliageInfluence player{{44, 26, 6, 7}, {80.0f, 0.0f}, false}; // walking right through it
    for (int i = 0; i < 20; ++i) {
        player.bounds.x = 44 + i;
        vines.step(kDt, i * kDt, std::span(&player, 1), WindParams{0.0f, 0.8f});
    }
    const auto& points = vines.vines()[0].points;
    CHECK(points[0] == (Vec2{50.0f, 8.0f}));
    CHECK(points.back().x > 52.0f); // tip swung away in the walking direction
}

// A chain under a steady sideways force should lean as a whole (ideally a straight slanted
// line, midpoint offset ≈ half the tip offset), not just whip at the bottom.
TEST(vine_leans_as_a_whole_in_steady_wind) {
    VineSystem vines;
    vines.params().windForce = 80.0f;
    vines.addVine({50.0f, 8.0f}, 20);
    const WindParams steady{1.0f, 0.0f}; // speed 0 freezes the wind field in time
    const float wind = windAt(steady, 50.0f, 0.0);
    CHECK(std::fabs(wind) > 0.05f);
    for (int i = 0; i < 600; ++i) {
        vines.step(kDt, 0.0, {}, steady);
    }
    const auto& points = vines.vines()[0].points;
    const float tip = points.back().x - 50.0f;
    const float mid = points[points.size() / 2].x - 50.0f;
    CHECK(std::fabs(tip) > 2.0f);
    CHECK((tip > 0.0f) == (wind > 0.0f));
    CHECK(mid / tip > 0.4f);
}

TEST(vine_push_at_the_tip_moves_the_upper_chain) {
    VineSystem vines;
    vines.addVine({50.0f, 8.0f}, 20); // tip around y = 68
    FoliageInfluence player{{44, 58, 6, 7}, {90.0f, 0.0f}, false};
    float maxUpper = 0.0f;
    float maxTip = 0.0f;
    for (int i = 0; i < 40; ++i) {
        player.bounds.x = 44 + i;
        vines.step(kDt, i * kDt, std::span(&player, 1), WindParams{0.0f, 0.8f});
        const auto& points = vines.vines()[0].points;
        maxUpper = std::max(maxUpper, points[points.size() / 3].x - 50.0f);
        maxTip = std::max(maxTip, points.back().x - 50.0f);
    }
    CHECK(maxTip > 4.0f);
    CHECK(maxUpper > maxTip * 0.2f); // the push travels up the chain
}

TEST(foliage_simulation_is_deterministic) {
    auto run = [] {
        GrassField grass = makeGrass();
        VineSystem vines;
        vines.addVine({30.0f, 8.0f}, 8);
        for (int i = 0; i < 120; ++i) {
            const FoliageInfluence player = playerAt(20 + i / 4, 40.0f);
            grass.step(kDt, i * kDt, std::span(&player, 1), WindParams{});
            vines.step(kDt, i * kDt, std::span(&player, 1), WindParams{});
        }
        return std::pair{grass.blades()[3].angle, vines.vines()[0].points.back().x};
    };
    CHECK(run() == run());
}

// --- Entities & input ---------------------------------------------------------------------

TEST(entity_world_defers_destruction) {
    EntityWorld world;
    const EntityId a = world.create(EntityKind::Player);
    const EntityId b = world.create(EntityKind::None);
    CHECK(world.get(a) && world.get(a)->id == a && world.get(a)->kind == EntityKind::Player);
    world.destroy(a);
    CHECK(world.get(a) != nullptr); // still alive until flushed
    world.flushDestroyed();
    CHECK(world.get(a) == nullptr);
    CHECK(world.get(b) != nullptr);
    CHECK(world.size() == 1);
}

TEST(action_press_is_latched_until_fixed_step) {
    ActionMap actions;
    constexpr ActionId kJump = 0;
    actions.applyStrength(kJump, 1.0f); // frame 1: pressed, but no fixed step ran
    actions.applyStrength(kJump, 1.0f); // frame 2: still held
    CHECK(actions.pressed(kJump));       // the press survives until a step consumes it
    actions.endFixedStep();
    CHECK(!actions.pressed(kJump));
    CHECK(actions.down(kJump));

    actions.applyStrength(kJump, 0.0f);
    actions.applyStrength(kJump, 0.3f); // analog below threshold: not a press
    CHECK(!actions.down(kJump) && !actions.pressed(kJump));
    CHECK(test::near(actions.strength(kJump), 0.3f));
}

} // namespace
