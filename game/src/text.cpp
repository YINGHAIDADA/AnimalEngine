// The player's language (ADR-0041): string tables by key, English for keys a table lacks.

#include "sandbox_game.h"

#include <animal/core/log.h>

#include <algorithm>
#include <iterator>

namespace game {

namespace {

// The order of the language menu.
constexpr const char* kLanguages[] = {"zh-Hans", "en"};
constexpr const char* kFallbackLanguage = "en";

} // namespace

bool SandboxGame::loadLanguage(const std::string& language) {
    const std::string folder = context_->dataDirectory + "strings/";
    std::optional<animal::StringTable> table = animal::loadStringTable(folder + language + ".strings.json");
    if (!table) {
        return false;
    }
    std::optional<animal::StringTable> fallback;
    if (language != kFallbackLanguage) {
        fallback = animal::loadStringTable(folder + kFallbackLanguage + ".strings.json");
    }
    strings_.set(std::move(*table), std::move(fallback));
    return true;
}

void SandboxGame::cycleLanguage(int step) {
    const auto count = static_cast<int>(std::size(kLanguages));
    const auto it = std::ranges::find(kLanguages, strings_.language());
    const int index = it == std::end(kLanguages) ? 0 : static_cast<int>(it - std::begin(kLanguages));
    for (int tried = 1; tried <= count; ++tried) {
        const char* next = kLanguages[((index + step * tried) % count + count) % count];
        if (loadLanguage(next)) {
            settings_.language = next;
            return;
        }
    }
}

std::string SandboxGame::levelText(const std::string& message) const {
    return strings_.has(message) ? strings_.get(message) : message;
}

} // namespace game
