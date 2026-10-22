// Engine logic that runs without a window or GPU.

#include <animal/core/handle.h>
#include <animal/core/math.h>
#include <animal/renderer/camera2d.h>
#include <animal/renderer/pixel_canvas.h>
#include <animal/rhi/command_list.h>
#include <animal/rhi/gpu_timers.h>
#include <animal/rhi/shader_package.h>

#include "test_harness.h"

#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace {

using animal::test::near;
using namespace animal;

// --- HandlePool ----------------------------------------------------------------

struct TestTag;

TEST(handle_pool_resolves_and_invalidates) {
    HandlePool<TestTag, int> pool;
    const auto a = pool.insert(10);
    const auto b = pool.insert(20);
    CHECK(a.valid() && b.valid());
    CHECK(pool.get(a) && *pool.get(a) == 10);
    CHECK(pool.size() == 2);

    CHECK(pool.take(a).value_or(0) == 10);
    CHECK(pool.get(a) == nullptr);
    CHECK(!pool.take(a).has_value());

    const auto c = pool.insert(30); // reuses a's slot with a new generation
    CHECK(c.index == a.index && c.generation != a.generation);
    CHECK(pool.get(a) == nullptr);
    CHECK(*pool.get(c) == 30);
    CHECK(pool.get(Handle<TestTag>{}) == nullptr);
}

// --- Shader packages -------------------------------------------------------------

TEST(shader_package_round_trip) {
    rhi::ShaderPackage package;
    package.stage = rhi::ShaderStage::Fragment;
    package.bindings = {{0, 0, rhi::BindingKind::UniformBuffer}, {1, 2, rhi::BindingKind::CombinedImageSampler}};
    package.spirv = {0x07230203u, 1u, 2u, 3u};
    package.glsl = "#version 410\nvoid main() {}\n";

    const auto bytes = rhi::serializeShaderPackage(package);
    std::string error;
    const auto parsed = rhi::parseShaderPackage(bytes, &error);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->stage == rhi::ShaderStage::Fragment);
        CHECK(parsed->bindings.size() == 2);
        CHECK(parsed->bindings[1].set == 1 && parsed->bindings[1].binding == 2);
        CHECK(parsed->bindings[1].kind == rhi::BindingKind::CombinedImageSampler);
        CHECK(parsed->spirv == package.spirv);
        CHECK(parsed->glsl == package.glsl);
    }
}

TEST(shader_package_rejects_corruption) {
    rhi::ShaderPackage package;
    package.glsl = "void main() {}";
    auto bytes = rhi::serializeShaderPackage(package);

    std::string error;
    auto truncated = bytes;
    truncated.pop_back();
    CHECK(!rhi::parseShaderPackage(truncated, &error).has_value());
    CHECK(!error.empty());

    auto badMagic = bytes;
    badMagic[0] = std::byte{'X'};
    CHECK(!rhi::parseShaderPackage(badMagic).has_value());

    CHECK(!rhi::parseShaderPackage({}).has_value());
}

TEST(glsl_resource_names_follow_convention) {
    CHECK(rhi::glslResourceName(rhi::BindingKind::UniformBuffer, 0, 0) == "ubo_s0_b0");
    CHECK(rhi::glslResourceName(rhi::BindingKind::CombinedImageSampler, 1, 3) == "tex_s1_b3");
}

// --- Pixel canvas presentation ------------------------------------------------------

TEST(present_layout_uses_largest_integer_scale) {
    const rhi::Extent2D canvas{320, 180};

    const PresentLayout exact = computePresentLayout(canvas, {1280, 720});
    CHECK(exact.scale == 4 && exact.x == 0 && exact.y == 0 && exact.width == 1280 && exact.height == 720);

    const PresentLayout boxed = computePresentLayout(canvas, {1300, 800});
    CHECK(boxed.scale == 4 && boxed.x == 10 && boxed.y == 40);

    const PresentLayout retina = computePresentLayout(canvas, {2560, 1600});
    CHECK(retina.scale == 8 && retina.y == 80);

    const PresentLayout tiny = computePresentLayout(canvas, {200, 100});
    CHECK(tiny.scale == 1 && tiny.x == -60 && tiny.y == -40);
}

TEST(window_to_canvas_inverts_present_layout) {
    const rhi::Extent2D canvas{320, 180};
    const PresentLayout layout = computePresentLayout(canvas, {1300, 800}); // scale 4, offset (10, 40)

    const auto origin = windowToCanvas(layout, canvas, {10.0f, 40.0f});
    CHECK(origin && near(origin->x, 0.0f) && near(origin->y, 0.0f));

    const auto inside = windowToCanvas(layout, canvas, {10.0f + 4.0f * 100.5f, 40.0f + 4.0f * 20.0f});
    CHECK(inside && near(inside->x, 100.5f) && near(inside->y, 20.0f));

    CHECK(!windowToCanvas(layout, canvas, {5.0f, 100.0f}).has_value());
    CHECK(!windowToCanvas(layout, canvas, {10.0f + 1280.0f, 100.0f}).has_value());
}

// --- Math / camera -------------------------------------------------------------------

TEST(orthographic_uses_vulkan_conventions) {
    const Mat4 m = Mat4::orthographic(0.0f, 320.0f, 0.0f, 180.0f);
    const Vec2 topLeft = m.transformPoint({0.0f, 0.0f});
    const Vec2 bottomRight = m.transformPoint({320.0f, 180.0f});
    CHECK(near(topLeft.x, -1.0f) && near(topLeft.y, -1.0f));
    CHECK(near(bottomRight.x, 1.0f) && near(bottomRight.y, 1.0f));
}

TEST(camera_snaps_to_whole_pixels) {
    Camera2D camera;
    camera.center = {200.4f, 100.6f};
    const Vec2 origin = camera.topLeft({320, 180});
    CHECK(near(origin.x, 40.0f) && near(origin.y, 11.0f));

    const Mat4 vp = camera.viewProjection({320, 180});
    const Vec2 ndc = vp.transformPoint(origin);
    CHECK(near(ndc.x, -1.0f) && near(ndc.y, -1.0f));
}

TEST(color_premultiplies) {
    CHECK((Color::rgba(255, 128, 0, 255).premultiplied() == Color::rgba(255, 128, 0, 255)));
    CHECK((Color::rgba(255, 255, 255, 0).premultiplied() == Color::rgba(0, 0, 0, 0)));
    CHECK((Color::rgba(200, 100, 50, 128).premultiplied() == Color::rgba(100, 50, 25, 128)));
}

// --- Command list -------------------------------------------------------------------

TEST(command_list_records_in_order) {
    rhi::CommandList list;
    rhi::ColorAttachment attachment;
    attachment.texture = rhi::TextureHandle{0, 1};
    list.beginRenderPass(rhi::RenderPassDesc::single(attachment));
    list.setPipeline(rhi::PipelineHandle{0, 1});
    list.draw(3);
    list.endRenderPass();

    CHECK(list.commands().size() == 4);
    CHECK(std::holds_alternative<rhi::cmd::BeginRenderPass>(list.commands()[0]));
    CHECK(std::holds_alternative<rhi::cmd::Draw>(list.commands()[2]));
    CHECK(!list.insideRenderPass());

    list.reset();
    CHECK(list.commands().empty());
}

TEST(timer_scopes_nest_and_convert_ticks) {
    rhi::TimerScopes scopes;
    const uint32_t frameBegin = scopes.begin("frame");
    const uint32_t passBegin = scopes.begin("pass");
    const uint32_t passEnd = scopes.end();
    const uint32_t frameEnd = scopes.end();
    CHECK(scopes.used() == 4);
    CHECK(scopes.end() == rhi::TimerScopes::kNone); // unbalanced end is ignored

    std::vector<uint64_t> ticks(4);
    ticks[frameBegin] = 1000;
    ticks[passBegin] = 1500;
    ticks[passEnd] = 3500;
    ticks[frameEnd] = 5000;
    const auto timings = scopes.timings(ticks, 1000.0); // 1 tick = 1 µs
    CHECK(timings.size() == 2);
    CHECK(std::string(timings[0].name) == "frame" && timings[0].depth == 0);
    CHECK(std::fabs(timings[0].milliseconds - 4.0) < 1e-9);
    CHECK(std::string(timings[1].name) == "pass" && timings[1].depth == 1);
    CHECK(std::fabs(timings[1].milliseconds - 2.0) < 1e-9);

    scopes.reset();
    for (uint32_t i = 0; i < rhi::TimerScopes::kMaxTimestamps / 2 + 3; ++i) {
        scopes.begin("many");
        scopes.end();
    }
    CHECK(scopes.used() == rhi::TimerScopes::kMaxTimestamps); // extra scopes get no slots
}

TEST(command_list_records_timer_scopes) {
    rhi::CommandList list;
    list.beginTimer("outer");
    list.beginTimer("inner");
    CHECK(list.openTimers() == 2);
    list.endTimer();
    list.endTimer();
    CHECK(list.openTimers() == 0);
    CHECK(list.commands().size() == 4);
    CHECK(std::holds_alternative<rhi::cmd::BeginTimer>(list.commands()[0]));
    CHECK(std::holds_alternative<rhi::cmd::EndTimer>(list.commands()[3]));
}

} // namespace
