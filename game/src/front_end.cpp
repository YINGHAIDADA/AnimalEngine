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

std::string onOff(bool v) {
    return v ? "ON" : "OFF";
}

} // namespace

std::string SandboxGame::screenName() const {
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
            items.push_back(entry("CONTINUE", [this] { closeMenus(); }));
        }
        items.push_back(entry("NEW GAME", [this, hasSave, &context] {
                             if (hasSave) {
                                 openPage(Page::ConfirmNewGame);
                             } else {
                                 resetProgress(*context.renderer);
                                 closeMenus();
                             }
                         }));
        items.push_back(entry("SETTINGS", [this] { openPage(Page::Settings); }));
        items.push_back(entry("QUIT", [&context] { context.quitRequested = true; }));
        break;
    }
    case Page::ConfirmNewGame:
        items.push_back(entry("START OVER (PROGRESS IS LOST)", [this, &context] {
                             resetProgress(*context.renderer);
                             closeMenus();
                         }));
        items.push_back(entry("BACK", [this] { backPage(); }));
        break;
    case Page::Pause:
        items.push_back(entry("RESUME", [this] { closeMenus(); }));
        items.push_back(entry("SETTINGS", [this] { openPage(Page::Settings); }));
        items.push_back(entry("QUIT TO TITLE", [this] {
                             saveProgress();
                             openTitle();
                         }));
        break;
    case Page::Settings:
        items.push_back(entry("AUDIO", [this] { openPage(Page::Audio); }));
        items.push_back(entry("DISPLAY", [this] { openPage(Page::Display); }));
        items.push_back(entry("GRAPHICS", [this] { openPage(Page::Graphics); }));
        items.push_back(entry("CONTROLS", [this] { openPage(Page::Controls); }));
        items.push_back(entry("BACK", [this] { backPage(); }));
        break;
    case Page::Audio: {
        const auto volume = [&](const char* label, float& value) {
            items.push_back(setting(label, percent(value), nullptr, [&value](int step) {
                value = std::clamp(std::round((value + 0.1f * static_cast<float>(step)) * 10.0f) / 10.0f, 0.0f, 1.0f);
            }));
        };
        volume("MASTER", settings_.masterVolume);
        volume("EFFECTS", settings_.effectsVolume);
        volume("AMBIENCE", settings_.ambienceVolume);
        volume("MUSIC", settings_.musicVolume);
        items.push_back(entry("BACK", [this] { backPage(); }));
        break;
    }
    case Page::Display: {
        const auto toggle = [this, &context](bool& value) {
            return [this, &context, &value](int) {
                value = !value;
                applyDisplaySettings(context);
            };
        };
        items.push_back(setting("FULLSCREEN", onOff(settings_.fullscreen), [f = toggle(settings_.fullscreen)] { f(1); },
                                toggle(settings_.fullscreen)));
        items.push_back(setting("VSYNC", onOff(settings_.vsync), [f = toggle(settings_.vsync)] { f(1); },
                                toggle(settings_.vsync)));
        const auto cycleCap = [this, &context](int step) {
            const auto& caps = animal::Settings::kFrameCaps;
            const auto it = std::ranges::find(caps, settings_.frameCap);
            const auto count = static_cast<int>(std::size(caps));
            const int index = it == std::end(caps) ? 0 : static_cast<int>(it - std::begin(caps));
            settings_.frameCap = caps[((index + step) % count + count) % count];
            applyDisplaySettings(context);
        };
        MenuItem cap = setting("FRAME CAP", settings_.frameCap == 0 ? "OFF" : std::to_string(settings_.frameCap),
                               [cycleCap] { cycleCap(1); }, cycleCap);
        cap.disabled = settings_.vsync; // the display's refresh paces the frames
        items.push_back(cap);
        items.push_back(entry("BACK", [this] { backPage(); }));
        break;
    }
    case Page::Graphics: {
        items.push_back(setting("BRIGHTNESS", percent(settings_.brightness), nullptr, [this](int step) {
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
            items.push_back(setting(label, value ? std::move(on) : std::move(off), [flip] { flip(1); }, flip));
        };
        toggle("SHADOWS", settings_.shadows, "ON", "OFF");
        toggle("INDIRECT + AO", settings_.fillLight, "ON", "OFF");
        toggle("BLOOM", settings_.bloom, "ON", "OFF");
        toggle("PARTICLES", settings_.particles, "ON", "OFF", [this, &context] { applyParticles(*context.renderer); });
        toggle("LIGHT BANDS", settings_.lightBands, "SCENE", "SMOOTH");
        items.push_back(entry("BACK", [this] { backPage(); }));
        break;
    }
    case Page::Controls: {
        const auto& infos = actionInfos();
        for (size_t i = 0; i < infos.size(); ++i) {
            const animal::ActionBinding& b = actions_.binding(infos[i].id);
            std::string value = b.keys.empty() ? "-" : inputLabel(b.keys.front());
            value += " / ";
            value += b.buttons.empty() ? "-" : animal::platform::gamepadButtonLabel(b.buttons.front());
            items.push_back(setting(infos[i].label, value, [this, i] {
                menu_.rebindAction = i;
                menu_.rebindWait = kRebindTimeout;
                openPage(Page::Rebind);
            }, nullptr));
        }
        items.push_back(entry("RESET TO DEFAULTS", [this] {
                             settings_.bindings.clear();
                             bindActions();
                         }));
        items.push_back(entry("BACK", [this] { backPage(); }));
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
        if (frontEndEnabled_ && !mapOpen_ && !endingOpen_ && actions_.pressedThisFrame(Pause, input)) {
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
    const float line = renderer.textLineHeight();
    const auto centred = [&](const std::string& text, float y, animal::Color color, uint32_t size = 1) {
        renderer.drawText({canvas.x + (canvas.width - renderer.textWidth(text, size)) * 0.5f, y}, text, color, size);
    };

    float y = canvas.y + canvas.height * 0.2f;
    if (title) {
        centred("ANIMAL ENGINE", y, kTitleText, 3);
        y += line * 5.0f;
        const std::string version = "V" ANIMAL_GAME_VERSION;
        renderer.drawText({canvas.x + canvas.width - renderer.textWidth(version) - line,
                           canvas.y + canvas.height - line * 2.5f},
                          version, kHint);
    } else {
        static constexpr const char* kHeadings[] = {"", "", "", "PAUSED", "SETTINGS", "AUDIO", "DISPLAY", "GRAPHICS", "CONTROLS", ""};
        centred(kHeadings[static_cast<size_t>(menu_.page)], y, kTitleText, 2);
        y += line * 4.0f;
    }

    if (menu_.page == Page::Rebind) {
        const auto& info = actionInfos()[menu_.rebindAction];
        centred(std::format("PRESS A KEY OR BUTTON FOR {}", info.label), y, kSelected);
        centred("ESC CANCELS", y + line * 2.0f, kHint);
        return;
    }

    const std::vector<MenuItem> items = menuItems();
    const float column = renderer.textWidth("RESET TO DEFAULTS") + renderer.textWidth("  ");
    for (size_t i = 0; i < items.size(); ++i) {
        const MenuItem& item = items[i];
        const bool selected = static_cast<int>(i) == menu_.selected;
        const animal::Color color = item.disabled ? kDisabled : (selected ? kSelected : kItem);
        if (item.value.empty()) {
            centred(selected ? "> " + item.label + " <" : item.label, y, color);
        } else {
            const float width = column + renderer.textWidth("XXXXXXXXXXXXX");
            const float left = canvas.x + (canvas.width - width) * 0.5f;
            renderer.drawText({left, y}, (selected ? "> " : "  ") + item.label, color);
            const std::string value = item.adjust ? "< " + item.value + " >" : item.value;
            renderer.drawText({left + column, y}, value, color);
        }
        y += line * 1.6f;
    }
    const bool canGoBack = menu_.page != Page::Title;
    const bool adjustable = std::ranges::any_of(items, [](const MenuItem& item) { return item.adjust != nullptr; });
    std::string hint = usingGamepad() ? animal::platform::gamepadButtonLabel(GamepadButton::South) + " SELECT"
                                      : std::string("ENTER SELECT");
    if (canGoBack) {
        hint += usingGamepad() ? "  " + animal::platform::gamepadButtonLabel(GamepadButton::East) + " BACK"
                               : std::string("  ESC BACK");
    }
    if (adjustable) {
        hint += usingGamepad() ? "  D-PAD CHANGE" : "  ARROWS CHANGE";
    }
    centred(hint, canvas.y + canvas.height - line * 2.5f, kHint);
}

} // namespace game
