#pragma once

// CPU timing zones (ADR-0032): ANIMAL_PROFILE_ZONE("name") times the rest of the scope. Zones nest;
// a zone entered several times in one frame (e.g. once per fixed step) is summed. The application
// marks frames; the zones of the last finished frame can be read until the next one ends.
// Main thread only. `name` must outlive the results (use a literal).

#include <cstdint>
#include <vector>

namespace animal::profiler {

struct ZoneTiming {
    const char* name;
    uint32_t depth; // 0 = outermost
    uint32_t calls; // entries this frame
    double milliseconds;
};

void beginFrame();
void endFrame();
const std::vector<ZoneTiming>& lastFrame();

class Zone {
public:
    explicit Zone(const char* name);
    ~Zone();
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

private:
    uint32_t index_;
    uint64_t start_;
};

} // namespace animal::profiler

#define ANIMAL_PROFILE_CONCAT_INNER(a, b) a##b
#define ANIMAL_PROFILE_CONCAT(a, b) ANIMAL_PROFILE_CONCAT_INNER(a, b)
#define ANIMAL_PROFILE_ZONE(name) const ::animal::profiler::Zone ANIMAL_PROFILE_CONCAT(animalProfileZone, __LINE__)(name)
