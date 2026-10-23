#pragma once

namespace animal {
class Tuning;
}

namespace animal::devtools {

// Sliders for every tuning parameter, grouped by the name before the first '.'; parameters that
// differ from their built-in value are highlighted and can be reset one by one.
// Returns true when a value changed this frame.
bool drawTuning(Tuning& tuning);

} // namespace animal::devtools
