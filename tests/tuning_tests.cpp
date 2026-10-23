// Tuning parameters and their data file (ADR-0027).

#include <animal/framework/tuning.h>

#include "test_harness.h"

#include <string>

namespace {

using namespace animal;

struct Params {
    float speed = 84.0f;
    int frames = 6;
    bool flag = false;
};

Tuning tuningFor(Params& p) {
    Tuning t;
    t.add("player.speed", &p.speed, 0.0f, 200.0f);
    t.add("player.frames", &p.frames, 0, 20);
    t.add("debug.flag", &p.flag);
    return t;
}

TEST(tuning_applies_values_and_reports_problems) {
    Params p;
    Tuning t = tuningFor(p);
    const auto problems = t.apply(R"({"format": "animal-tuning", "version": 1,
        "values": {"player.speed": 90.5, "player.frames": 3, "debug.flag": true,
                   "player.unknown": 1, "player.frames2": 2}})",
                                  "tuning.json");
    CHECK(test::near(p.speed, 90.5f));
    CHECK(p.frames == 3);
    CHECK(p.flag);
    CHECK(problems.size() == 2);
}

TEST(tuning_rejects_wrong_types_and_bad_files) {
    Params p;
    Tuning t = tuningFor(p);
    auto problems = t.apply(R"({"format": "animal-tuning", "version": 1,
        "values": {"player.frames": 2.5, "debug.flag": 1, "player.speed": "fast"}})",
                            "tuning.json");
    CHECK(problems.size() == 3);
    CHECK(p.frames == 6 && !p.flag && test::near(p.speed, 84.0f));

    CHECK(t.apply("{not json", "tuning.json").size() == 1);
    CHECK(t.apply(R"({"format": "animal-look", "version": 1})", "tuning.json").size() == 1);
    CHECK(t.apply(R"({"format": "animal-tuning", "version": 2})", "tuning.json").size() == 1);
}

TEST(tuning_file_keeps_only_changed_values_and_round_trips) {
    Params p;
    Tuning t = tuningFor(p);
    CHECK(t.toJson() == "{\n  \"format\": \"animal-tuning\",\n  \"version\": 1,\n  \"values\": {}\n}\n");
    p.speed = 100.25f;
    p.flag = true;
    const std::string json = t.toJson();
    CHECK(json.find("player.speed\": 100.25") != std::string::npos);
    CHECK(json.find("player.frames") == std::string::npos);
    CHECK(json.find("debug.flag\": true") != std::string::npos);

    Params q;
    Tuning u = tuningFor(q);
    CHECK(u.apply(json, "tuning.json").empty());
    CHECK(test::near(q.speed, 100.25f) && q.flag && q.frames == 6);

    t.resetToBuiltIn();
    CHECK(test::near(p.speed, 84.0f) && !p.flag);
    CHECK(Tuning::atBuiltIn(*t.find("player.speed")));
}

} // namespace
