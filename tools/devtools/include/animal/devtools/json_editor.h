#pragma once

#include <json.hpp>

#include <string>
#include <vector>

namespace animal::devtools {

using JsonDocument = nlohmann::ordered_json; // keeps the file's member order

struct JsonEdit {
    bool changed = false;   // a value changed this frame (e.g. while dragging)
    bool committed = false; // an edit finished this frame (released, toggled, added, removed)
};

struct JsonEditorOptions {
    std::vector<std::string> lockedKeys{"format", "version"}; // shown, not editable
    // Members that must stay whole numbers (dragged in steps of 1). Other numbers drag freely; one
    // written as an integer stays an integer while its value is whole.
    std::vector<std::string> integerKeys;
    bool allowAddRemove = true;
};

// Edits the members of a JSON object in place: numbers drag, booleans toggle, "#rrggbb" strings and
// [r, g, b(, a)] byte arrays named "...color" get colour pickers, other strings a text field, small
// numeric arrays drag together, nested values open as trees. Members can be removed and added
// (name + JSON value). The caller validates the result (e.g. with the format's parser).
JsonEdit editJsonObject(const char* id, JsonDocument& object, const JsonEditorOptions& options = {});

// Pretty prints like the hand-written data files: `indent` spaces per level, arrays of scalars on one
// line, numbers rounded to 4 decimals (floating-point values keep a ".0" when whole). Ends with a
// newline.
std::string formatJson(const JsonDocument& value, int indent = 2);

} // namespace animal::devtools
