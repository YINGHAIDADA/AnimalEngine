# ADR-0004: 依赖管理与着色器工具链

- 状态：Accepted
- 日期：2026-10-22
- 背景与问题：项目要求依赖固定版本、两个平台可重复构建，不依赖开发机上未记录的全局库。着色器需要单一源码同时服务 OpenGL 与 Vulkan 后端。
- 备选方案：
  - 依赖：系统包 / Homebrew；CMake FetchContent；git 子模块；直接复制源码。
  - 着色器工具：Vulkan SDK 自带工具；从源码构建 glslang 与 SPIRV-Cross；运行时交叉编译。
- 决策：
  - 大型依赖使用 git 浅子模块，固定到发布 tag：SDL（release-3.4.18）、glslang 与 SPIRV-Cross（vulkan-sdk-1.4.363.0）。
  - 单头文件依赖直接复制到 `external/`，在 `external/README.md` 记录来源 commit 与许可证：stb_image、stb_image_write、OpenGL `glcorearb.h` / `khrplatform.h`。
  - 着色器工具 `animal_shaderc` 从源码构建并链接 glslang 与 SPIRV-Cross。构建期把 GLSL 450 编译为 SPIR-V，再生成 GLSL 410，连同资源绑定表一起写入 `.ashd` 着色器包。运行时不编译着色器源码。
- 主要理由：子模块可离线构建、版本在仓库中可见；从源码构建工具使 OpenGL 阶段不依赖 Vulkan SDK，且两个平台使用同一版本的编译器。
- 代价与风险：首次构建需要编译 glslang 与 SPIRV-Cross（约一到两分钟）；克隆时需要 `git submodule update --init --depth 1`。
- 影响的模块：external、cmake、tools/shaderc、shaders、engine/rhi。
- 验证方式：干净克隆后在两个平台构建成功；着色器包可被 OpenGL 后端加载。
- 重新评估条件：依赖数量增长到子模块难以管理时，评估 vcpkg / CPM；Vulkan 后端阶段评估是否改用 Vulkan SDK 提供的验证层与工具。
