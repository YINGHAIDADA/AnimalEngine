#include <animal/devtools/console.h>

#include <imgui.h>

namespace animal::devtools {

Console::~Console() {
    detach();
}

void Console::attach() {
    if (sink_ == 0) {
        sink_ = log::addSink([this](log::Level level, std::string_view text) { add(level, text); });
    }
}

void Console::detach() {
    if (sink_ != 0) {
        log::removeSink(sink_);
        sink_ = 0;
    }
}

void Console::add(log::Level level, std::string_view text) {
    const std::lock_guard lock(mutex_);
    lines_.push_back({level, std::string(text)});
    if (lines_.size() > kMaxLines) {
        lines_.pop_front();
    }
    if (level == log::Level::Warn) {
        ++unseenWarnings_;
    } else if (level == log::Level::Error) {
        ++unseenErrors_;
    }
}

void Console::draw(bool* open) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(10, display.y - 270), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Console", open)) {
        ImGui::End();
        return;
    }
    unseenWarnings_ = unseenErrors_ = 0;
    if (ImGui::Button("Clear")) {
        const std::lock_guard lock(mutex_);
        lines_.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##filter", "filter", filter_, sizeof(filter_));
    ImGui::Separator();

    ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    {
        const std::lock_guard lock(mutex_);
        const std::string_view filter = filter_;
        for (const Line& line : lines_) {
            if ((line.level == log::Level::Debug && !showDebug_) ||
                (!filter.empty() && line.text.find(filter) == std::string::npos)) {
                continue;
            }
            ImVec4 color{0.85f, 0.85f, 0.85f, 1.0f};
            if (line.level == log::Level::Debug) {
                color = {0.55f, 0.55f, 0.6f, 1.0f};
            } else if (line.level == log::Level::Warn) {
                color = {1.0f, 0.8f, 0.35f, 1.0f};
            } else if (line.level == log::Level::Error) {
                color = {1.0f, 0.42f, 0.38f, 1.0f};
            }
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (scrollToBottom_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    scrollToBottom_ = false;
    ImGui::EndChild();
    ImGui::End();
}

} // namespace animal::devtools
