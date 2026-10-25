// The front end (ADR-0036): title screen, pause menu, settings (audio, display, controls with
// rebinding). Menus are navigated with fixed inputs (arrows / WASD / D-pad / stick, Enter / Space /
// South to confirm, Escape / Backspace / East to go back) so rebinding can never lock the player out.

#include "sandbox_game.h"

#include <animal/core/file.h>
#include <animal/platform/input.h>
#include <animal/renderer/renderer2d.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace game {

namespace {

using animal::platform::GamepadAxis;
using animal::platform::GamepadButton;
using animal::platform::Key;

constexpr animal::Color kTitleShade = animal::Color::rgba(4, 4, 10, 120);
constexpr animal::Color kMenuShade = animal::Color::rgba(4, 4, 10, 200);
constexpr animal::Color kTitleText = animal::Color::rgba(150, 240, 215);
constexpr animal::Color kItem = animal::Color::rgba(190, 196, 206);
constexpr animal::Color kSelected = animal::Color::rgba(255, 236, 170);
constexpr animal::Color kDisabled = animal::Color::rgba(90, 96, 106);
constexpr animal::Color kHint = animal::Color::rgba(120, 130, 140);
constexpr float kStickThreshold = 0.5f;
constexpr float kRebindTimeout = 5.0f; // seconds of waiting before a rebind is given up

const char* pageName(SandboxGame::Page page) {
    switch (page) {
    case SandboxGame::Page::None: return "playing";
    case SandboxGame::Page::Title: return "title";
    case SandboxGame::Page::ConfirmNewGame: return "confirm";
    case SandboxGame::Page::Pause: return "paused";
    case SandboxGame::Page::Settings: return "settings";
    case SandboxGame::Page::Audio: return "audio";
    case SandboxGame::Page::Display: return "display";
    case SandboxGame::Page::Graphics: return "graphics";
    case SandboxGame::Page::Controls: return "controls";
    case SandboxGame::Page::Rebind: return "rebind";
    }
    return "?";
}

SandboxGame::MenuItem entry(std::string label, std::function<void()> activate) {
    return {std::move(label), {}, std::move(activate), {}, false};
}

SandboxGame::MenuItem setting(std::string label, std::string value, std::function<void()> activate,
                              std::function<void(int)> adjust) {
    return {std::move(label), std::move(value), std::move(activate), std::move(adjust), false};
}

std::string percent(float v) {
    return std::format("{}%", static_cast<int>(std::lround(v * 100.0f)));
}


} // namespace

std::string SandboxGame::screenName() const {
    if (gearOpen_) {
        return "gear";
    }
    return pageName(menu_.page);
}

void SandboxGame::openPage(Page page) {
    if (menu_.page != Page::None) {
        menu_.stack.push_back({menu_.page, menu_.selected});
    }
    menu_.page = page;
    menu_.selected = 0;
}

void SandboxGame::backPage() {
    if (menu_.page == Page::Audio || menu_.page == Page::Display || menu_.page == Page::Graphics ||
        menu_.page == Page::Controls) {
        settingsChanged_ = true; // written when the menus close (and on exit)
    }
    if (menu_.stack.empty()) {
        closeMenus();
        return;
    }
    menu_.page = menu_.stack.back().page;
    menu_.selected = menu_.stack.back().selected;
    menu_.stack.pop_back();
}

void SandboxGame::closeMenus() {
    menu_.page = Page::None;
    menu_.stack.clear();
    menu_.selected = 0;
    menu_.fromTitle = false;
    actions_.endFixedStep(); // the press that closed the menu must not reach the game
    if (settingsChanged_ && context_) {
        settingsChanged_ = false;
        saveSettings(*context_);
    }
}

void SandboxGame::openTitle() {
    menu_.stack.clear();
    menu_.page = Page::Title;
    menu_.selected = 0;
    menu_.fromTitle = true;
}

bool SandboxGame::worldPaused() const {
    return menu_.page != Page::None && !menu_.fromTitle;
}

// Every item of the page shown, built each frame so labels and values are always current.
std::vector<SandboxGame::MenuItem> SandboxGame::menuItems() {
    std::vector<MenuItem> items;
    animal::EngineContext& context = *context_;
    switch (menu_.page) {
    case Page::Title: {
        const bool hasSave = !savePath_.empty() && animal::fileExists(savePath_);
        if (hasSave) {
            items.push_back(entry(text("menu.continue"), [this] { closeMenus(); }));
        }
        items.push_back(entry(text("menu.new_game"), [this, hasSave, &context] {
                             if (hasSave) {
                                 openPage(Page::ConfirmNewGame);
                             } else {
                                 resetProgress(*context.renderer);
                                 closeMenus();
                             }
                         }));
        items.push_back(entry(text("menu.settings"), [this] { openPage(Page::Settings); }));
        items.push_back(entry(text("menu.quit"), [&context] { context.quitRequested = true; }));
        break;
    }
    case Page::ConfirmNewGame:
        items.push_back(entry(text("menu.start_over"), [this, &context] {
                             resetProgress(*context.renderer);
                             closeMenus();
                         }));
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    case Page::Pause:
        items.push_back(entry(text("menu.resume"), [this] { closeMenus(); }));
        items.push_back(entry(text("menu.settings"), [this] { openPage(Page::Settings); }));
        items.push_back(entry(text("menu.quit_to_title"), [this] {
                             saveProgress();
                             openTitle();
                         }));
        break;
    case Page::Settings: {
        items.push_back(entry(text("menu.audio"), [this] { openPage(Page::Audio); }));
        items.push_back(entry(text("menu.display"), [this] { openPage(Page::Display); }));
        items.push_back(entry(text("menu.graphics"), [this] { openPage(Page::Graphics); }));
        items.push_back(entry(text("menu.controls"), [this] { openPage(Page::Controls); }));
        const auto changeLanguage = [this](int step) {
            cycleLanguage(step);
            settingsChanged_ = true;
        };
        items.push_back(setting(text("menu.language"), strings_.name(), [changeLanguage] { changeLanguage(1); },
                                changeLanguage));
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    }
    case Page::Audio: {
        const auto volume = [&](const char* label, float& value) {
            items.push_back(setting(text(label), percent(value), nullptr, [&value](int step) {
                value = std::clamp(std::round((value + 0.1f * static_cast<float>(step)) * 10.0f) / 10.0f, 0.0f, 1.0f);
            }));
        };
        volume("audio.master", settings_.masterVolume);
        volume("audio.effects", settings_.effectsVolume);
        volume("audio.ambience", settings_.ambienceVolume);
        volume("audio.music", settings_.musicVolume);
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    }
    case Page::Display: {
        const auto toggle = [this, &context](bool& value) {
            return [this, &context, &value](int) {
                value = !value;
                applyDisplaySettings(context);
            };
        };
        items.push_back(setting(text("display.fullscreen"), text(settings_.fullscreen ? "value.on" : "value.off"), [f = toggle(settings_.fullscreen)] { f(1); },
                                toggle(settings_.fullscreen)));
        items.push_back(setting(text("display.vsync"), text(settings_.vsync ? "value.on" : "value.off"), [f = toggle(settings_.vsync)] { f(1); },
                                toggle(settings_.vsync)));
        const auto cycleCap = [this, &context](int step) {
            const auto& caps = animal::Settings::kFrameCaps;
            const auto it = std::ranges::find(caps, settings_.frameCap);
            const auto count = static_cast<int>(std::size(caps));
            const int index = it == std::end(caps) ? 0 : static_cast<int>(it - std::begin(caps));
            settings_.frameCap = caps[((index + step) % count + count) % count];
            applyDisplaySettings(context);
        };
        MenuItem cap = setting(text("display.frame_cap"), settings_.frameCap == 0 ? text("value.off") : std::to_string(settings_.frameCap),
                               [cycleCap] { cycleCap(1); }, cycleCap);
        cap.disabled = settings_.vsync; // the display's refresh paces the frames
        items.push_back(cap);
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    }
    case Page::Graphics: {
        items.push_back(setting(text("graphics.brightness"), percent(settings_.brightness), nullptr, [this](int step) {
            const float next = std::round((settings_.brightness + 0.1f * static_cast<float>(step)) * 10.0f) / 10.0f;
            settings_.brightness = std::clamp(next, animal::Settings::kMinBrightness, animal::Settings::kMaxBrightness);
        }));
        const auto toggle = [&](const char* label, bool& value, std::string on, std::string off,
                                std::function<void()> changed = nullptr) {
            const auto flip = [&value, changed](int) {
                value = !value;
                if (changed) {
                    changed();
                }
            };
            items.push_back(setting(text(label), value ? std::move(on) : std::move(off), [flip] { flip(1); }, flip));
        };
        toggle("graphics.shadows", settings_.shadows, text("value.on"), text("value.off"));
        toggle("graphics.fill_light", settings_.fillLight, text("value.on"), text("value.off"));
        toggle("graphics.bloom", settings_.bloom, text("value.on"), text("value.off"));
        toggle("graphics.particles", settings_.particles, text("value.on"), text("value.off"), [this, &context] { applyParticles(*context.renderer); });
        toggle("graphics.light_bands", settings_.lightBands, text("value.scene"), text("value.smooth"));
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    }
    case Page::Controls: {
        const auto& infos = actionInfos();
        for (size_t i = 0; i < infos.size(); ++i) {
            const animal::ActionBinding& b = actions_.binding(infos[i].id);
            std::string value = b.keys.empty() ? "-" : inputLabel(b.keys.front());
            value += " / ";
            value += b.buttons.empty() ? "-" : animal::platform::gamepadButtonLabel(b.buttons.front());
            items.push_back(setting(text(infos[i].label), value, [this, i] {
                menu_.rebindAction = i;
                menu_.rebindWait = kRebindTimeout;
                openPage(Page::Rebind);
            }, nullptr));
        }
        items.push_back(entry(text("menu.reset_controls"), [this] {
                             settings_.bindings.clear();
                             bindActions();
                         }));
        items.push_back(entry(text("menu.back"), [this] { backPage(); }));
        break;
    }
    case Page::Rebind:
    case Page::None:
        break;
    }
    return items;
}

// Gives the action the key (or gamepad button) pressed, taking it from any other action that had it.
void SandboxGame::rebind(size_t actionIndex, std::optional<Key> key, std::optional<GamepadButton> button) {
    const auto& infos = actionInfos();
    for (size_t i = 0; i < infos.size(); ++i) {
        animal::ActionBinding binding = bindingFor(infos[i]);
        if (i == actionIndex) {
            if (key) {
                binding.keys = {*key};
            } else {
                binding.buttons = {*button};
            }
        } else {
            const auto before = binding;
            if (key) {
                std::erase(binding.keys, *key);
            } else {
                std::erase(binding.buttons, *button);
            }
            if (binding.keys == before.keys && binding.buttons == before.buttons) {
                continue;
            }
        }
        settings_.bindings[infos[i].name] = {binding.keys, binding.buttons};
    }
    bindActions();
}

void SandboxGame::updateFrontEnd(animal::EngineContext& context, float dt) {
    const animal::platform::Input& input = *context.input;
    if (menu_.page == Page::None) {
        if (frontEndEnabled_ && !mapOpen_ && !endingOpen_ && !gearOpen_ && actions_.pressedThisFrame(Pause, input)) {
            openPage(Page::Pause);
        }
        return;
    }

    if (menu_.page == Page::Rebind) {
        menu_.rebindWait -= dt;
        if (input.pressed(Key::Escape) || menu_.rebindWait <= 0.0f) {
            backPage();
            return;
        }
        for (int k = 0; k < static_cast<int>(Key::Count); ++k) {
            if (input.pressed(static_cast<Key>(k))) {
                rebind(menu_.rebindAction, static_cast<Key>(k), std::nullopt);
                backPage();
                return;
            }
        }
        for (int b = 0; b < static_cast<int>(GamepadButton::Count); ++b) {
            if (input.pressed(static_cast<GamepadButton>(b))) {
                rebind(menu_.rebindAction, std::nullopt, static_cast<GamepadButton>(b));
                backPage();
                return;
            }
        }
        return;
    }

    // Stick directions count once each time they cross the threshold.
    const float stickX = input.axis(GamepadAxis::LeftX);
    const float stickY = input.axis(GamepadAxis::LeftY);
    const auto crossed = [](float now, float before, float sign) {
        return now * sign > kStickThreshold && before * sign <= kStickThreshold;
    };
    const bool up = input.pressed(Key::Up) || input.pressed(Key::W) || input.pressed(GamepadButton::DpadUp) ||
                    crossed(stickY, menu_.stickY, -1.0f);
    const bool down = input.pressed(Key::Down) || input.pressed(Key::S) || input.pressed(GamepadButton::DpadDown) ||
                      crossed(stickY, menu_.stickY, 1.0f);
    const bool left = input.pressed(Key::Left) || input.pressed(Key::A) || input.pressed(GamepadButton::DpadLeft) ||
                      crossed(stickX, menu_.stickX, -1.0f);
    const bool right = input.pressed(Key::Right) || input.pressed(Key::D) ||
                       input.pressed(GamepadButton::DpadRight) || crossed(stickX, menu_.stickX, 1.0f);
    const bool confirm = input.pressed(Key::Enter) || input.pressed(Key::KeypadEnter) || input.pressed(Key::Space) ||
                         input.pressed(GamepadButton::South);
    const bool back = input.pressed(Key::Escape) || input.pressed(Key::Backspace) || input.pressed(GamepadButton::East) ||
                      (menu_.page == Page::Pause && input.pressed(GamepadButton::Start));
    menu_.stickX = stickX;
    menu_.stickY = stickY;

    std::vector<MenuItem> items = menuItems();
    if (items.empty()) {
        return;
    }
    const int count = static_cast<int>(items.size());
    menu_.selected = std::clamp(menu_.selected, 0, count - 1);
    if (up || down) {
        do { // skip disabled items
            menu_.selected = (menu_.selected + (down ? 1 : -1) + count) % count;
        } while (items[static_cast<size_t>(menu_.selected)].disabled);
    }
    MenuItem& item = items[static_cast<size_t>(menu_.selected)];
    if ((left || right) && item.adjust && !item.disabled) {
        item.adjust(right ? 1 : -1);
    } else if (confirm && item.activate && !item.disabled) {
        item.activate();
    } else if (back && menu_.page != Page::Title) {
        backPage();
    }
}

void SandboxGame::drawFrontEnd(animal::Renderer2D& renderer) {
    const animal::PresentLayout& screen = renderer.presentLayout();
    const animal::RectF canvas{static_cast<float>(screen.x), static_cast<float>(screen.y),
                               static_cast<float>(screen.width), static_cast<float>(screen.height)};
    const bool title = menu_.page == Page::Title || menu_.page == Page::ConfirmNewGame;
    renderer.fillRect(canvas, title ? kTitleShade : kMenuShade);
    const float line = renderer.textLineHeight(font_);
    const auto width = [&](std::string_view s, uint32_t size = 1) { return renderer.textWidth(font_, s, size); };
    const auto centred = [&](const std::string& s, float y, animal::Color color, uint32_t size = 1) {
        renderer.drawText(font_, {canvas.x + (canvas.width - width(s, size)) * 0.5f, y}, s, color, size);
    };

    float y = canvas.y + canvas.height * 0.15f;
    if (title) {
        centred(text("title.name"), y, kTitleText, 3);
        y += line * 4.5f;
        const std::string version = "v" ANIMAL_GAME_VERSION;
        renderer.drawText(font_, {canvas.x + canvas.width - width(version) - line, canvas.y + canvas.height - line * 1.5f},
                          version, kHint);
    } else {
        static constexpr const char* kHeadings[] = {"", "", "", "menu.paused", "menu.settings", "menu.audio",
                                                    "menu.display", "menu.graphics", "menu.controls", ""};
        if (const char* heading = kHeadings[static_cast<size_t>(menu_.page)]; *heading) {
            centred(text(heading), y, kTitleText, 2);
        }
        y += line * 3.0f;
    }

    const std::string confirm = usingGamepad() ? animal::platform::gamepadButtonLabel(GamepadButton::South) : "ENTER";
    const std::string cancel = usingGamepad() ? animal::platform::gamepadButtonLabel(GamepadButton::East) : "ESC";
    if (menu_.page == Page::Rebind) {
        const auto& info = actionInfos()[menu_.rebindAction];
        centred(text("menu.rebind", text(info.label)), y, kSelected);
        centred(text("menu.rebind_cancel", "ESC"), y + line * 2.0f, kHint);
        return;
    }

    // Labels in one column and values in another, the pair centred; plain entries centred alone.
    const std::vector<MenuItem> items = menuItems();
    float labels = 0.0f;
    float values = 0.0f;
    for (const MenuItem& item : items) {
        if (!item.value.empty()) {
            labels = std::max(labels, width("> " + item.label));
            values = std::max(values, width("< " + item.value + " >"));
        }
    }
    const float gap = width("    ");
    const float left = canvas.x + (canvas.width - labels - gap - values) * 0.5f;
    for (size_t i = 0; i < items.size(); ++i) {
        const MenuItem& item = items[i];
        const bool selected = static_cast<int>(i) == menu_.selected;
        const animal::Color color = item.disabled ? kDisabled : (selected ? kSelected : kItem);
        if (item.value.empty()) {
            centred(selected ? "> " + item.label + " <" : item.label, y, color);
        } else {
            renderer.drawText(font_, {left, y}, (selected ? "> " : "  ") + item.label, color);
            const std::string value = item.adjust ? "< " + item.value + " >" : item.value;
            renderer.drawText(font_, {left + labels + gap, y}, value, color);
        }
        y += line * 1.25f;
    }
    const bool canGoBack = menu_.page != Page::Title;
    const bool adjustable = std::ranges::any_of(items, [](const MenuItem& item) { return item.adjust != nullptr; });
    std::string hint = text("menu.hint_select", confirm);
    if (canGoBack) {
        hint += "    " + text("menu.hint_back", cancel);
    }
    if (adjustable) {
        hint += "    " + text("menu.hint_change", text(usingGamepad() ? "menu.dpad" : "menu.arrows"));
    }
    centred(hint, canvas.y + canvas.height - line * 1.5f, kHint);
}

} // namespace game
