// animal_shaderc: GLSL 450 (Vulkan style) -> SPIR-V + GLSL 410 + binding table (.ashd).
//
// Usage: animal_shaderc --stage vert|frag -o <output.ashd> <input.glsl>

#include <animal/rhi/shader_package.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace {

using animal::rhi::BindingKind;
using animal::rhi::ShaderBinding;
using animal::rhi::ShaderPackage;
using animal::rhi::ShaderStage;

struct Options {
    ShaderStage stage = ShaderStage::Vertex;
    std::string input;
    std::string output;
};

void printUsage() {
    std::fprintf(stderr, "usage: animal_shaderc --stage vert|frag -o <output.ashd> <input.glsl>\n");
}

std::optional<Options> parseArgs(int argc, char** argv) {
    Options options;
    bool hasStage = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--stage" && i + 1 < argc) {
            const std::string stage = argv[++i];
            if (stage == "vert") {
                options.stage = ShaderStage::Vertex;
            } else if (stage == "frag") {
                options.stage = ShaderStage::Fragment;
            } else {
                std::fprintf(stderr, "animal_shaderc: unknown stage '%s'\n", stage.c_str());
                return std::nullopt;
            }
            hasStage = true;
        } else if (arg == "-o" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (!arg.empty() && arg[0] != '-' && options.input.empty()) {
            options.input = arg;
        } else {
            std::fprintf(stderr, "animal_shaderc: unexpected argument '%s'\n", arg.c_str());
            return std::nullopt;
        }
    }
    if (!hasStage || options.input.empty() || options.output.empty()) {
        return std::nullopt;
    }
    return options;
}

std::filesystem::path utf8Path(const std::string& path) {
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::optional<std::string> readTextFile(const std::string& path) {
    std::ifstream file(utf8Path(path), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

bool compileToSpirv(const Options& options, const std::string& source, std::vector<uint32_t>& spirv) {
    const EShLanguage language = options.stage == ShaderStage::Vertex ? EShLangVertex : EShLangFragment;

    glslang::TShader shader(language);
    const char* sources[] = {source.c_str()};
    const char* names[] = {options.input.c_str()};
    shader.setStringsWithLengthsAndNames(sources, nullptr, names, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);

    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        std::fprintf(stderr, "%s%s", shader.getInfoLog(), shader.getInfoDebugLog());
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        std::fprintf(stderr, "%s%s", program.getInfoLog(), program.getInfoDebugLog());
        return false;
    }

    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(language), spirv, &spvOptions);
    return true;
}

bool fail(const Options& options, const std::string& message) {
    std::fprintf(stderr, "%s: error: %s\n", options.input.c_str(), message.c_str());
    return false;
}

bool crossCompile(const Options& options, ShaderPackage& package) {
    spirv_cross::CompilerGLSL compiler(package.spirv);
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    if (!resources.push_constant_buffers.empty()) {
        return fail(options, "push constants are not supported yet; use a uniform buffer");
    }
    if (!resources.separate_images.empty() || !resources.separate_samplers.empty()) {
        return fail(options, "separate images/samplers are not supported; use combined sampler2D");
    }
    if (!resources.storage_buffers.empty() || !resources.storage_images.empty()) {
        return fail(options, "storage buffers/images are not available on the OpenGL 4.1 baseline");
    }

    std::set<std::pair<uint32_t, uint32_t>> used;
    auto addBinding = [&](const spirv_cross::Resource& resource, BindingKind kind) -> bool {
        const uint32_t set = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
        const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
        if (set >= animal::rhi::kMaxBindGroups || binding >= animal::rhi::kMaxBindingsPerSet) {
            return fail(options, "resource '" + resource.name + "' uses set " + std::to_string(set) +
                                     ", binding " + std::to_string(binding) + "; limits are set < " +
                                     std::to_string(animal::rhi::kMaxBindGroups) + ", binding < " +
                                     std::to_string(animal::rhi::kMaxBindingsPerSet));
        }
        if (!used.insert({set, binding}).second) {
            return fail(options, "duplicate resource at set " + std::to_string(set) + ", binding " +
                                     std::to_string(binding));
        }
        const std::string glName = animal::rhi::glslResourceName(kind, set, binding);
        if (kind == BindingKind::UniformBuffer) {
            compiler.set_name(resource.base_type_id, glName);
        } else {
            compiler.set_name(resource.id, glName);
        }
        package.bindings.push_back({set, binding, kind});
        return true;
    };

    for (const auto& resource : resources.uniform_buffers) {
        if (!addBinding(resource, BindingKind::UniformBuffer)) {
            return false;
        }
    }
    for (const auto& resource : resources.sampled_images) {
        if (!addBinding(resource, BindingKind::CombinedImageSampler)) {
            return false;
        }
    }

    spirv_cross::CompilerGLSL::Options glslOptions;
    glslOptions.version = 410;
    glslOptions.es = false;
    glslOptions.enable_420pack_extension = false;
    glslOptions.vertex.fixup_clipspace = options.stage == ShaderStage::Vertex;
    glslOptions.vertex.flip_vert_y = false;
    compiler.set_common_options(glslOptions);

    package.glsl = compiler.compile();
    return true;
}

bool writeOutput(const Options& options, const ShaderPackage& package) {
    const std::filesystem::path outPath = utf8Path(options.output);
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    const std::vector<std::byte> bytes = animal::rhi::serializeShaderPackage(package);
    std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(options, "cannot open output file '" + options.output + "'");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char** argv) {
    const std::optional<Options> options = parseArgs(argc, argv);
    if (!options) {
        printUsage();
        return 2;
    }

    const std::optional<std::string> source = readTextFile(options->input);
    if (!source) {
        std::fprintf(stderr, "%s: error: cannot read file\n", options->input.c_str());
        return 1;
    }

    ShaderPackage package;
    package.stage = options->stage;

    glslang::InitializeProcess();
    const bool compiled = compileToSpirv(*options, *source, package.spirv);
    glslang::FinalizeProcess();
    if (!compiled) {
        return 1;
    }

    try {
        if (!crossCompile(*options, package)) {
            return 1;
        }
    } catch (const std::exception& e) {
        fail(*options, std::string("SPIRV-Cross: ") + e.what());
        return 1;
    }

    return writeOutput(*options, package) ? 0 : 1;
}
