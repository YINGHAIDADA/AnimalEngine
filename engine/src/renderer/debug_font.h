#pragma once

#include <animal/core/math.h>
#include <animal/rhi/rhi.h>

namespace animal {

// Built-in 3x5 pixel font baked into a texture at startup, so diagnostics work even when
// no asset files can be loaded.
class DebugFont {
public:
    static constexpr int kGlyphWidth = 3;
    static constexpr int kGlyphHeight = 5;
    static constexpr int kAdvance = 4;
    static constexpr int kLineHeight = 7;

    bool init(rhi::Device& device);
    void shutdown(rhi::Device& device);

    rhi::TextureHandle texture() const { return texture_; }
    RectF glyphUV(char c) const;
    RectF solidUV() const;

private:
    rhi::TextureHandle texture_;
};

} // namespace animal
