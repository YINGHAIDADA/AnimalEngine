// Aseprite sprite sheet import and per-frame timing (ADR-0030).

#include <animal/framework/animation.h>
#include <animal/framework/sprite_sheet.h>

#include "test_harness.h"

#include <format>
#include <string>

namespace {

using namespace animal;

std::string frame(int x, int duration) {
    return std::format(R"({{"frame": {{"x": {}, "y": 0, "w": 8, "h": 8}}, "rotated": false, "trimmed": false,
        "spriteSourceSize": {{"x": 0, "y": 0, "w": 8, "h": 8}}, "sourceSize": {{"w": 8, "h": 8}}, "duration": {}}})",
                       x, duration);
}

std::optional<SpriteSheet> parse(const std::string& frames, const std::string& tags, std::string* error = nullptr) {
    const std::string json = R"({"frames": )" + frames + R"(, "meta": {"image": "hero.png", "size": {"w": 32, "h": 8},
        "frameTags": )" + tags + "}}";
    return parseAsepriteSheet(json, "sprites/hero.json", error);
}

TEST(aseprite_sheet_reads_frames_and_tags) {
    const auto sheet = parse("[" + frame(0, 100) + "," + frame(8, 100) + "," + frame(16, 50) + "," + frame(24, 150) + "]",
                             R"([{"name": "walk", "from": 0, "to": 1, "direction": "forward"},
                                 {"name": "hit", "from": 2, "to": 3, "direction": "forward", "repeat": "1"},
                                 {"name": "back", "from": 0, "to": 2, "direction": "reverse"},
                                 {"name": "bob", "from": 0, "to": 3, "direction": "pingpong"},
                                 {"name": "bobr", "from": 0, "to": 2, "direction": "pingpong_reverse"}])");
    CHECK(sheet.has_value());
    if (!sheet) {
        return;
    }
    CHECK(sheet->image == "sprites/hero.png");
    CHECK(sheet->frames.size() == 4 && sheet->frames[3].source.x == 24 && sheet->frames[3].source.width == 8);
    const AnimationClip* walk = sheet->clip("walk");
    CHECK(walk && walk->frames == std::vector<int>({0, 1}) && walk->loop && walk->durations.empty() &&
          test::near(walk->framesPerSecond, 10.0f));
    const AnimationClip* hit = sheet->clip("hit");
    CHECK(hit && !hit->loop && hit->durations.size() == 2 && test::near(hit->durations[0], 0.05f) &&
          test::near(hit->durations[1], 0.15f));
    CHECK(sheet->clip("back")->frames == std::vector<int>({2, 1, 0}));
    CHECK(sheet->clip("bob")->frames == std::vector<int>({0, 1, 2, 3, 2, 1}));
    CHECK(sheet->clip("bobr")->frames == std::vector<int>({1, 2, 1, 0}));
    CHECK(!sheet->clip("missing"));
}

TEST(aseprite_sheet_accepts_hash_frames_in_file_order) {
    const auto sheet = parse(R"({"b 0.aseprite": )" + frame(16, 100) + R"(, "a 1.aseprite": )" + frame(0, 100) + "}",
                             R"([{"name": "x", "from": 0, "to": 1}])");
    CHECK(sheet && sheet->frames.size() == 2 && sheet->frames[0].source.x == 16 && sheet->frames[1].source.x == 0);
}

TEST(aseprite_sheet_rejects_bad_data) {
    std::string error;
    CHECK(!parse("[" + frame(0, 100) + "]", R"([{"name": "x", "from": 0, "to": 3}])", &error));
    CHECK(error.find("not in the sheet") != std::string::npos);
    CHECK(!parse("[" + frame(32, 100) + "]", "[]", &error)); // outside the 32-px-wide image
    CHECK(!parse(R"([{"frame": {"x": 0, "y": 0, "w": 8, "h": 8}, "rotated": true, "duration": 100}])", "[]", &error));
    CHECK(error.find("rotated") != std::string::npos);
    CHECK(!parse("[" + frame(0, 0) + "]", "[]", &error));
    CHECK(!parse("[" + frame(0, 100) + "]", R"([{"name": "x", "from": 0, "to": 0, "direction": "sideways"}])", &error));
    CHECK(!parse("[" + frame(0, 100) + "]", R"([{"name": "x", "from": 0, "to": 0}, {"name": "x", "from": 0, "to": 0}])", &error));
    CHECK(!parseAsepriteSheet("{\"frames\": []}", "a.json", &error));
    CHECK(!parseAsepriteSheet("not json", "a.json", &error));
}

TEST(animator_plays_per_frame_durations) {
    Animator animator;
    const auto clip = animator.add({{4, 5, 6}, 8.0f, false, {0.1f, 0.3f, 0.1f}});
    animator.play(clip);
    CHECK(animator.frame() == 4);
    animator.update(0.15f);
    CHECK(animator.frame() == 5);
    animator.update(0.3f);
    CHECK(animator.frame() == 6 && !animator.finished());
    animator.update(0.1f);
    CHECK(animator.frame() == 6 && animator.finished());

    const auto loop = animator.add({{1, 2}, 8.0f, true, {0.2f, 0.1f}});
    animator.play(loop);
    animator.update(0.65f); // 0.65 - 0.3 - 0.3 = 0.05 into the third cycle
    CHECK(animator.frame() == 1);
}

// The creature's sheet must give exactly the clips the game used to hard-code (same frame rates,
// so recorded runs and the render goldens do not change).
TEST(creature_sheet_matches_the_original_clips) {
    const auto sheet = loadSpriteSheet(std::string(ANIMAL_TEST_ASSETS_DIR) + "/textures/creature.json");
    CHECK(sheet.has_value());
    if (!sheet) {
        return;
    }
    struct Expected {
        const char* tag;
        std::vector<int> cells;
        float fps;
        bool loop;
    };
    const Expected expected[] = {{"idle", {0, 0, 0, 0, 0, 1}, 3.0f, true}, {"run", {2, 3, 4, 5}, 10.0f, true},
                                 {"jump", {6}, 1.0f, true},                {"fall", {7}, 1.0f, true},
                                 {"climb", {8, 9}, 6.0f, true},            {"land", {10}, 12.0f, false}};
    for (const Expected& e : expected) {
        const AnimationClip* clip = sheet->clip(e.tag);
        CHECK(clip != nullptr);
        if (!clip) {
            continue;
        }
        std::vector<int> cells;
        for (const int f : clip->frames) {
            cells.push_back(sheet->frames[static_cast<size_t>(f)].source.x / 8);
        }
        CHECK(cells == e.cells);
        CHECK(clip->framesPerSecond == e.fps); // exactly
        CHECK(clip->durations.empty() && clip->loop == e.loop);
    }
}

} // namespace
