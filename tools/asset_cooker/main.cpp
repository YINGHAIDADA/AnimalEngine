// AnimalCooker (ADR-0031): cooks the source assets into a resource pack.
//
//   AnimalCooker --manifest=assets/cook.json --source=assets/source --shaders=<data/shaders>
//                --out=data.pak [--report=report.json] [--level=19]
//   AnimalCooker --verify=data.pak --manifest=assets/cook.json
//
// Exit code 0 on success, 1 when the cook (or the verification) found errors, 2 for bad arguments.

#include "cooker.h"

#include <animal/core/file.h>
#include <animal/core/log.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    animal::cooker::Options options;
    std::string out;
    std::string report;
    std::string verify;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&](std::string_view prefix) { return std::string(arg.substr(prefix.size())); };
        if (arg.starts_with("--manifest=")) {
            options.manifest = value("--manifest=");
        } else if (arg.starts_with("--source=")) {
            options.sourceRoot = value("--source=");
        } else if (arg.starts_with("--shaders=")) {
            options.shaderDirectory = value("--shaders=");
        } else if (arg.starts_with("--out=")) {
            out = value("--out=");
        } else if (arg.starts_with("--report=")) {
            report = value("--report=");
        } else if (arg.starts_with("--level=")) {
            options.level = std::atoi(value("--level=").c_str());
        } else if (arg.starts_with("--verify=")) {
            verify = value("--verify=");
        } else {
            std::fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    animal::log::setMinimumLevel(animal::log::Level::Warn);

    if (!verify.empty()) {
        if (options.manifest.empty()) {
            std::fprintf(stderr, "--verify needs --manifest\n");
            return 2;
        }
        const auto problems = animal::cooker::verifyPack(verify, options.manifest);
        for (const std::string& p : problems) {
            std::fprintf(stderr, "verify: %s\n", p.c_str());
        }
        std::printf("%s: %s\n", verify.c_str(), problems.empty() ? "complete and valid" : "PROBLEMS FOUND");
        return problems.empty() ? 0 : 1;
    }
    if (options.manifest.empty() || options.sourceRoot.empty() || options.shaderDirectory.empty() || out.empty()) {
        std::fprintf(stderr, "usage: AnimalCooker --manifest=... --source=... --shaders=... --out=... [--report=...]\n");
        return 2;
    }
    for (const std::string& file : {out, report}) {
        if (!file.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(
                std::filesystem::path(std::u8string(file.begin(), file.end())).parent_path(), ec);
        }
    }
    const animal::cooker::Result result = animal::cooker::cook(options);
    std::fputs(animal::cooker::reportText(result).c_str(), stdout);
    if (!report.empty()) {
        const std::string json = animal::cooker::reportJson(result);
        animal::writeFileAtomic(report, json.data(), json.size());
    }
    if (!result.ok) {
        return 1;
    }
    if (!animal::writeFileAtomic(out, result.pack.data(), result.pack.size())) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    return 0;
}
