# ADR-0001: 图形架构——按 Vulkan 语义设计的 RHI，OpenGL 首发、Vulkan 后续

- 状态：Accepted
- 日期：2026-10-22
- 背景与问题：项目后期确定要迁移到 Vulkan，但早期需要尽快验证像素渲染、光照和合成。如果渲染代码直接写在 OpenGL 之上，迁移时需要重写 Renderer 与上层。
- 备选方案：
  1. 直接使用 OpenGL，后期重写为 Vulkan。
  2. 直接从 Vulkan 开始。
  3. 自研 RHI，按 Vulkan 显式模型设计，OpenGL 作为第一个后端。
  4. 使用 SDL3 自带的 GPU API（SDL_GPU）。
- 决策：采用方案 3。RHI 公共接口以 Vulkan 语义为准（不可变管线、显式渲染通道 load/store、命令录制后提交、set/binding 资源绑定、frames-in-flight），OpenGL 4.1 Core 为首个后端，Vulkan 后端在 M2.5 实现。
- 主要理由：OpenGL 能最快跑通 M0–M2；按 Vulkan 语义设计接口可让迁移只发生在后端目录内；OpenGL 4.1 是 macOS 上限，可保证两平台行为一致。
- 代价与风险：OpenGL 后端需要模拟 Vulkan 约定（坐标原点、深度范围、绑定槽、正面朝向）；OpenGL 4.1 无计算着色器；抽象可能在没有第二个后端时悄悄泄漏 OpenGL 语义。
- 影响的模块：engine/rhi、engine/renderer、shaders、着色器工具链。
- 验证方式：M0 后的限时 Vulkan 技术验证；M2.5 时 M0–M2 全部测试场景在两后端截图一致且上层代码无需修改。
- 重新评估条件：Vulkan 技术验证发现 RHI 需要结构性修改；或 SDL_GPU 的评估结论表明其更适合作为后端。
