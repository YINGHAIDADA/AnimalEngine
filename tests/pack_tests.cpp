// Resource packs and mounting (ADR-0031).

#include <animal/core/file.h>
#include <animal/core/pack.h>

#include "test_harness.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace {

// Cleanup must not throw (Windows refuses to delete files that are still open).
std::error_code cleanupError;

using namespace animal;
namespace fs = std::filesystem;

std::vector<std::byte> bytesOf(const std::string& text) {
    std::vector<std::byte> out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

std::string pathString(const fs::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::vector<PackInput> sampleInputs() {
    std::string json(4000, ' ');
    for (size_t i = 0; i < json.size(); i += 40) {
        json.replace(i, 10, "\"tile\": 4,");
    }
    std::vector<std::byte> noise(3000);
    uint32_t x = 12345;
    for (std::byte& b : noise) {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::byte>(x >> 24);
    }
    return {{"world/rooms/a.tmj", bytesOf(json), true},
            {"world/rooms/copy.tmj", bytesOf(json), true}, // same contents: stored once
            {"textures/noise.bin", noise, true},           // incompressible: stored as is
            {"shaders/x.ashd", bytesOf("ASHD....."), false},
            {"empty.txt", {}, true}};
}

TEST(pack_round_trips_dedupes_and_is_deterministic) {
    PackStats stats;
    const std::vector<std::byte> pack = writePack(sampleInputs(), "test-1", 19, &stats);
    CHECK(writePack(sampleInputs(), "test-1", 19) == pack); // same input, same bytes
    CHECK(stats.sharedEntries == 1);
    CHECK(stats.storedBytes < stats.inputBytes);

    const fs::path dir = fs::temp_directory_path() / "animal_pack_test";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir);
    const std::string file = pathString(dir / "data.pak");
    CHECK(writeFileAtomic(file, pack.data(), pack.size()));
    std::string error;
    std::unique_ptr<Pack> opened = Pack::open(file, &error);
    CHECK(opened != nullptr);
    if (!opened) {
        return;
    }
    CHECK(opened->dataVersion() == "test-1" && opened->entries().size() == 5);
    for (const PackInput& input : sampleInputs()) {
        const PackEntry* entry = opened->find(input.path);
        CHECK(entry != nullptr);
        if (entry) {
            const auto data = opened->read(*entry, &error);
            CHECK(data && *data == input.data);
        }
    }
    const PackEntry* a = opened->find("world/rooms/a.tmj");
    const PackEntry* copy = opened->find("world/rooms/copy.tmj");
    CHECK(a && copy && a->offset == copy->offset && a->compression == PackCompression::Zstd);
    CHECK(opened->find("textures/noise.bin")->compression == PackCompression::None);
    CHECK(!opened->find("world/rooms/missing.tmj"));
    opened.reset(); // close the file before deleting it
    fs::remove_all(dir, cleanupError);
}

TEST(pack_detects_damage_and_other_versions) {
    const std::vector<std::byte> pack = writePack(sampleInputs(), "test-1");
    const fs::path dir = fs::temp_directory_path() / "animal_pack_damage";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir);
    const std::string file = pathString(dir / "data.pak");
    std::string error;

    std::vector<std::byte> damaged = pack;
    damaged[damaged.size() - 3] ^= std::byte{0x40}; // in the table of contents
    CHECK(writeFileAtomic(file, damaged.data(), damaged.size()));
    CHECK(!Pack::open(file, &error) && error.find("damaged") != std::string::npos);

    std::unique_ptr<Pack> clean;
    CHECK(writeFileAtomic(file, pack.data(), pack.size()));
    clean = Pack::open(file);
    const PackEntry noise = *clean->find("textures/noise.bin");
    clean.reset();
    damaged = pack;
    damaged[noise.offset + 10] ^= std::byte{0x01}; // in an entry's contents
    CHECK(writeFileAtomic(file, damaged.data(), damaged.size()));
    clean = Pack::open(file);
    CHECK(clean && !clean->read(*clean->find("textures/noise.bin"), &error) &&
          error.find("damaged") != std::string::npos);
    clean.reset();

    damaged = pack;
    damaged[4] = std::byte{99}; // format version
    CHECK(writeFileAtomic(file, damaged.data(), damaged.size()));
    CHECK(!Pack::open(file, &error) && error.find("version") != std::string::npos);
    CHECK(!Pack::open(pathString(dir / "missing.pak"), &error));
    fs::remove_all(dir, cleanupError);
}

TEST(mounted_pack_serves_its_directory_only) {
    const fs::path dir = fs::temp_directory_path() / "animal_pack_mount";
    fs::remove_all(dir, cleanupError);
    fs::create_directories(dir / "data");
    const std::vector<std::byte> pack = writePack(sampleInputs(), "test-1");
    const std::string file = pathString(dir / "data.pak");
    CHECK(writeFileAtomic(file, pack.data(), pack.size()));
    const std::string loose = pathString(dir / "data" / "loose.txt"); // under the mount: hidden
    const std::string outside = pathString(dir / "outside.txt");
    CHECK(writeFileAtomic(loose, "x", 1));
    CHECK(writeFileAtomic(outside, "y", 1));

    CHECK(mountPack(file, pathString(dir / "data")));
    const auto room = readFile(pathString(dir / "data" / "world" / "rooms" / ".." / "rooms" / "a.tmj"));
    CHECK(room && room->size() == 4000);
    CHECK(fileExists(pathString(dir / "data" / "shaders" / "x.ashd")));
    std::string error;
    CHECK(!readFile(loose, &error) && error.find("not in the resource pack") != std::string::npos);
    CHECK(!fileExists(loose));
    CHECK(readFile(outside).has_value() && fileExists(outside));
    CHECK(packMountedAt(pathString(dir / "data" / "x")) != nullptr && packMountedAt(outside) == nullptr);
    unmountPacks();
    CHECK(readFile(loose).has_value());
    fs::remove_all(dir, cleanupError);
}

} // namespace
