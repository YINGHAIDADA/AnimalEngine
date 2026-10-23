// DevTools when the build has no developer tools (ANIMAL_DEV_TOOLS=OFF).
#include "dev/dev_tools.h"

namespace game {

struct DevTools::State {};

DevTools::DevTools() = default;
DevTools::~DevTools() = default;

bool DevTools::handleArgument(std::string_view) {
    return false;
}

bool DevTools::init(animal::EngineContext&, SandboxGame&) {
    return true;
}

void DevTools::frame(animal::EngineContext&) {}

void DevTools::render(animal::EngineContext&, animal::rhi::CommandList&) {}

void DevTools::shutdown(animal::EngineContext&) {}

bool DevTools::editing() const {
    return false;
}

} // namespace game
