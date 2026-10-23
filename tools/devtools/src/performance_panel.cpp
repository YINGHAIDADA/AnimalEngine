#include <animal/devtools/performance_panel.h>

#include <animal/app/application.h>
#include <animal/audio/audio.h>
#include <animal/core/profiler.h>
#include <animal/platform/platform.h>
#include <animal/renderer/renderer2d.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace animal::devtools {

PerformancePanel::Series& PerformancePanel::series(std::vector<Series>& list, const char* name, uint32_t depth) {
    const auto it =
        std::ranges::find_if(list, [&](const Series& s) { return s.name == name && s.depth == depth; });
    if (it != list.end()) {
        return *it;
    }
    list.push_back({name, depth, {}, 0.0f});
    return list.back();
}

void PerformancePanel::sample(const EngineContext& context) {
    const uint64_t now = platform::nowNanoseconds();
    const float frameMs = lastTime_ ? static_cast<float>(now - lastTime_) * 1.0e-6f : 0.0f;
    lastTime_ = now;
    if (paused_) {
        return;
    }
    cursor_ = (cursor_ + 1) % kHistory;
    frame_.values[cursor_] = frameMs;
    frame_.last = frameMs;
    for (std::vector<Series>* list : {&gpu_, &cpu_}) {
        for (Series& s : *list) {
            s.values[cursor_] = 0.0f;
            s.last = 0.0f;
        }
    }
    for (const rhi::GpuTiming& t : context.device->gpuTimings()) {
        Series& s = series(gpu_, t.name, t.depth);
        s.values[cursor_] = static_cast<float>(t.milliseconds);
        s.last = static_cast<float>(t.milliseconds);
    }
    for (const profiler::ZoneTiming& z : profiler::lastFrame()) {
        Series& s = series(cpu_, z.name, z.depth);
        s.values[cursor_] = static_cast<float>(z.milliseconds);
        s.last = static_cast<float>(z.milliseconds);
    }
}

void PerformancePanel::draw(const EngineContext& context) {
    const Renderer2D& renderer = *context.renderer;
    ImGui::Text("%.1f FPS   frame %.2f ms (avg over 0.5 s)", context.stats.fps, context.stats.frameMilliseconds);
    ImGui::Text("%s | %s", rhi::backendName(context.device->backend()), context.device->caps().deviceName.c_str());
    ImGui::Text("sprites %u  draws %u  lights %u", renderer.stats().quads, renderer.stats().drawCalls,
                renderer.lightCount());
    if (context.audio) {
        const audio::AudioSystem::Stats a = context.audio->stats();
        ImGui::Text("audio: %u voices  %u sounds  peak %.2f", a.voices, a.sounds, a.peak);
    }
    ImGui::Checkbox("Pause graphs", &paused_);

    const auto plot = [this](const Series& s, float height) {
        float peak = 1.0f;
        for (const float v : s.values) {
            peak = std::max(peak, v);
        }
        const std::string overlay = std::format("{:.2f} ms", s.last);
        ImGui::PlotLines(("##" + s.name).c_str(), s.values.data(), static_cast<int>(kHistory),
                         static_cast<int>((cursor_ + 1) % kHistory), overlay.c_str(), 0.0f, peak * 1.2f,
                         ImVec2(-1.0f, height));
    };
    ImGui::SeparatorText("CPU frame (wall clock)");
    plot(frame_, 60.0f);
    if (ImGui::BeginTable("cpu", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const Series& s : cpu_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%*s%s", static_cast<int>(s.depth * 2), "", s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", s.last);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("GPU passes");
    if (gpu_.empty()) {
        ImGui::TextDisabled("GPU timing unavailable");
        return;
    }
    if (const auto total = std::ranges::find_if(gpu_, [](const Series& s) { return s.depth == 0; });
        total != gpu_.end()) {
        plot(*total, 50.0f);
    }
    if (ImGui::BeginTable("gpu", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const Series& s : gpu_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%*s%s", static_cast<int>(s.depth * 2), "", s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f ms", s.last);
        }
        ImGui::EndTable();
    }
}

} // namespace animal::devtools
