#pragma once

#include <animal/renderer/post.h>
#include <animal/rhi/rhi.h>

#include <array>
#include <string>

namespace animal::rhi {
class CommandList;
}

namespace animal {

// Bloom (bright extract at half resolution, two separable Gaussian blur iterations) and the
// grading pass that writes the final canvas. See ADR-0009.
class PostPass {
public:
    bool init(rhi::Device& device, const std::string& shaderDirectory, rhi::TextureHandle canvas,
              rhi::TextureHandle output, rhi::Extent2D canvasExtent);
    void shutdown();

    // Reads the canvas, writes the output texture. Once per frame.
    void render(rhi::CommandList& commands, const PostSettings& settings);

private:
    enum Program : size_t {
        Bright,
        Blur,
        Grade,
        ProgramCount,
    };

    struct GradeUniforms {
        float bloom[4];
        float grade[4];
        float shadowTint[4];
        float highlightTint[4];
        float canvasSize[4];
    };

    rhi::BindGroupHandle makeGroup(Program program, uint32_t set, std::array<rhi::TextureHandle, 2> textures,
                                   std::array<rhi::SamplerHandle, 2> samplers, uint32_t textureCount);
    rhi::BindGroupHandle makeUniformGroup(Program program, rhi::BufferHandle buffer);
    void pass(rhi::CommandList& commands, Program program, rhi::BindGroupHandle uniforms, rhi::BindGroupHandle inputs,
              rhi::TextureHandle target);

    rhi::Device* device_ = nullptr;
    rhi::Extent2D canvasExtent_;
    rhi::ShaderHandle vertex_;
    std::array<rhi::ShaderHandle, ProgramCount> fragments_{};
    std::array<rhi::PipelineHandle, ProgramCount> pipelines_{};
    rhi::BufferHandle triangle_;
    rhi::SamplerHandle linear_;
    rhi::SamplerHandle nearest_;
    rhi::TextureHandle canvas_;
    rhi::TextureHandle output_;
    std::array<rhi::TextureHandle, 2> bloom_{};

    rhi::BufferHandle brightUniforms_;
    rhi::BufferHandle blurHorizontalUniforms_;
    rhi::BufferHandle blurVerticalUniforms_;
    rhi::BufferHandle gradeUniforms_;

    rhi::BindGroupHandle brightSettings_;
    rhi::BindGroupHandle brightInputs_;
    rhi::BindGroupHandle blurHorizontalSettings_;
    rhi::BindGroupHandle blurVerticalSettings_;
    rhi::BindGroupHandle blurFromA_;
    rhi::BindGroupHandle blurFromB_;
    rhi::BindGroupHandle gradeSettings_;
    rhi::BindGroupHandle gradeInputs_;
    bool needsClear_ = true;
};

} // namespace animal
