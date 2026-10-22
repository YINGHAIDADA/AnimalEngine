# ADR-0002: 平台层采用 SDL3

- 状态：Accepted
- 日期：2026-10-22
- 背景与问题：v1.0 计划使用 SDL2。SDL3 已发布稳定版本，在高 DPI、窗口像素尺寸、输入与 Vulkan Surface 支持上更完善。
- 备选方案：SDL2；SDL3；GLFW + 独立的音频/输入库。
- 决策：平台层采用 SDL3，固定到 `release-3.4.18`，以静态库形式随项目从源码构建。
- 主要理由：SDL3 对 OpenGL 与 Vulkan 的 Surface 创建都有一等支持；`SDL_GetWindowSizeInPixels` 等接口简化高 DPI 处理；同时覆盖窗口、输入、控制器与音频基础。静态链接避免在 Windows 上分发 DLL。
- 代价与风险：SDL3 的 API 与 SDL2 不兼容，部分网上资料不适用；升级 SDL3 版本需要验证两个平台。
- 影响的模块：engine/platform、engine/rhi 的后端 Surface 创建、构建脚本。
- 验证方式：macOS 与 Windows 干净构建并运行 M0 演示。
- 重新评估条件：SDL3 出现阻塞性平台问题，或需要 SDL3 不支持的发行平台。
