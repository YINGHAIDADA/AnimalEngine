#pragma once

// Sprite sheets with animations, imported from Aseprite's sprite sheet export (ADR-0030): the
// .json Aseprite writes next to the .png ("Export Sprite Sheet", JSON data, array or hash frames,
// with tags). Each tag becomes an AnimationClip over the sheet's frames.

#include <animal/framework/animation.h>
#include <animal/framework/geometry.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace animal {

struct SpriteFrame {
    IntRect source;    // pixels in the sheet image
    Vec2i offset;      // where the (trimmed) source sits inside the untrimmed sprite
    Vec2i sourceSize;  // the untrimmed sprite
    double duration = 0.1; // seconds (double: 1000/3 ms frames must give exactly 3 per second)
};

struct SpriteSheet {
    std::string image; // the sheet image, resolved relative to the .json
    Vec2i size;        // image size
    std::vector<SpriteFrame> frames;

    struct Tag {
        std::string name;
        AnimationClip clip; // frame indices into `frames`
    };
    std::vector<Tag> tags;

    const AnimationClip* clip(std::string_view name) const;
};

// Tags: "direction" forward / reverse / pingpong / pingpong_reverse; "repeat" "1" plays once, absent
// or "0" loops. A tag whose frames all last the same time becomes a fixed-rate clip. Rotated frames,
// frames outside the image and tags outside the frames are errors.
std::optional<SpriteSheet> parseAsepriteSheet(std::string_view json, const std::string& path, std::string* error);
std::optional<SpriteSheet> loadSpriteSheet(const std::string& path); // logs errors

} // namespace animal
