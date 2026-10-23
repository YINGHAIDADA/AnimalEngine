#pragma once

// Tuning (ADR-0027): named game parameters (player physics, climbing, smoke dynamics...) that a data
// file can override and the developer tools can edit live. Code registers each parameter with its
// built-in value; the file only lists values that differ from it.

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace animal {

class Tuning {
public:
    using Value = std::variant<float, int, bool>;

    struct Entry {
        std::string name; // "group.parameter", e.g. "player.runSpeed"
        std::variant<float*, int*, bool*> target;
        float min = 0.0f; // slider range (numbers)
        float max = 0.0f;
        Value builtIn; // the value at registration
    };

    // The parameter must outlive the table. Names are unique; registering a name again replaces it.
    void add(std::string name, float* value, float min, float max);
    void add(std::string name, int* value, int min, int max);
    void add(std::string name, bool* value);

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view name) const;
    static Value current(const Entry& entry);
    static bool atBuiltIn(const Entry& entry);
    void resetToBuiltIn();

    struct ApplyResult {
        bool valid = false;                // a readable tuning file (otherwise nothing was applied)
        std::vector<std::string> problems; // unknown names, wrong types, or why the file is invalid
    };
    // File format: {"format": "animal-tuning", "version": 1, "values": {"player.runSpeed": 90, ...}}.
    // Values of the right type are applied (out of slider range is accepted); `dryRun` only checks.
    ApplyResult apply(std::string_view json, const std::string& path, bool dryRun = false);
    // Every parameter that differs from its built-in value, in registration order.
    std::string toJson() const;

private:
    std::vector<Entry> entries_;
};

} // namespace animal
