**ANIMAL-INSPIRED 2D ENGINE**

# 核心项目计划与工程基准

C++ 专用 2D 探索游戏引擎 · Project Charter & Technical Roadmap

版本：v1.3    |    日期：2026-10-22    |    状态：项目基线

**本文件是项目的单一事实参考（Single Source of Truth）。后续实施、技术取舍、里程碑验收和范围变更均以此为基准；若决定变更，必须更新本文件的版本与决策记录**。

## 0. 文档使用规则

- 用途：作为持续开发的项目基准，覆盖目标、范围、架构、实现顺序、里程碑、验收条件、工程规则和风险。
- 基线原则：先保证一个可运行、可测试、可迭代的游戏，再扩展引擎；不以“模块数量”或“代码量”作为进度指标。
- 完成定义：每个里程碑必须有可运行演示、明确验收条件、可重复构建方式和记录过的已知问题。
- 变更控制：改变核心技术选型、模块边界、逻辑分辨率、资源格式或里程碑顺序时，更新版本号、变更原因、影响面和迁移方案。
- 资料边界：目标是实现同类型游戏所需的能力，不是复制 Animal Well 的私有源码。公开信息可用于启发，但未公开的内部细节一律视为未知。

## 1. 项目愿景与成功标准

构建一套面向像素风 2D 探索 / 解谜 / 平台游戏的 C++ 专用引擎，能够支持具有高质量像素渲染、动态光照、粒子与低分辨率流体效果、复杂实体行为、房间式关卡、紧凑资源管线和高效内容创作流程的完整游戏。

### 1.1 产品目标

- 以 320×180 作为默认逻辑画布；在高分辨率窗口中保持清晰、可预测的像素边缘和整数倍缩放优先策略。
- 渲染通过自研 RHI（Render Hardware Interface，渲染硬件接口）隔离图形 API。第一个后端为 OpenGL，Vulkan 是确定的后续目标后端；RHI 从第一天起按 Vulkan 的显式模型设计，保证迁移时上层代码无需重写。
- 跨平台开发与分发：macOS 与 Windows 均为一等平台。同一套 CMake 工程可生成 Xcode 工程、Visual Studio 工程和 Ninja 构建，两个平台都能开发、调试和发行。
- 支持从单房间原型成长为多个互连房间的完整探索游戏，包括角色、敌人、机关、道具、存档与地图探索。
- 支持专用的像素视觉管线：精灵/Tile、图层、法线、动态光照、遮罩、粒子、流体和最终合成。
- 支持从开发期源资源到发行资源包的离线转换流程，并能分析最终发行包的组成。
- 工具优先服务于内容生产：调试覆盖层、参数热调、资源热重载、关卡编辑和资源烘焙逐步建设。

### 1.2 非目标（早期明确不做）

- 不做通用 2D/3D 引擎，不追求 Unity/Unreal 的功能覆盖。
- OpenGL 是主开发后端。Vulkan 后端（ADR-0005 验证版）已在主线中，但在 M2.5 之前只维护“可编译 + 与 OpenGL 输出一致”，不做优化或 Vulkan 独有功能。
- RHI 只抽象本项目实际用到的能力，不追求覆盖所有图形 API 特性。
- 不一开始构建大型通用 ECS、复杂反射系统、通用渲染图或插件框架。RHI 是资源、管线与命令录制的薄抽象，不是渲染图。
- 不先做独立、完整的编辑器再做游戏；先做运行时调试工具和内嵌编辑模式。
- 不在没有体积测量数据前，过早设计复杂二进制格式或极端压缩方案。
- 不把“达到约 33 MB”当成成功的唯一标准；游戏内容、画面品质、可维护性与迭代效率同等重要。

## 2. 约定与初始技术决策

| 领域 | 基线决策 | 说明 / 重新评估条件 |
|---|---|---|
| 语言标准 | C++20（可回退 C++17） | 优先使用清晰、可移植的语言特性；只使用 Apple Clang 与 MSVC 均稳定支持的特性。 |
| 构建系统 | CMake + CMakePresets.json | CMake 是唯一的工程真相来源；Xcode 与 Visual Studio 工程都由 CMake 生成，生成的工程文件不提交。 |
| 生成器 | macOS：Ninja、Xcode；Windows：Visual Studio 2022、Ninja（MSVC） | Ninja 用于快速增量构建与 CI；Xcode / Visual Studio 用于 IDE 开发与调试。 |
| 编译器 | macOS：Apple Clang（Xcode 工具链）；Windows：MSVC（v143） | 预设中显式指定编译器，不依赖 PATH 中碰巧找到的 clang；clang-cl 可后续评估。 |
| 目标平台 | macOS（arm64；x86_64 待定）、Windows x64 | 两个平台都是一等平台，主分支必须同时可编译、可运行。 |
| 平台层 | SDL3 | 窗口、输入、控制器、音频基础、文件路径与图形 Surface 创建。 |
| 渲染抽象 | 自研 RHI，按 Vulkan 显式模型设计 | Device / Buffer / Texture / Sampler / Shader / Pipeline / CommandList / RenderPass / BindGroup / Swapchain；Renderer 与上层只依赖 RHI。 |
| 图形后端 | 主开发：OpenGL 4.1 Core；同步维护：Vulkan 1.2 | OpenGL 4.1 是 macOS 的上限，保证两平台行为一致；Vulkan 基线为 1.2 + dynamic rendering + synchronization2，macOS 上经 MoltenVK 运行（ADR-0005）。Vulkan 完善见 §4（M2.5）。 |
| 着色器 | 单一源码：GLSL 450（Vulkan 风格）→ SPIR-V → SPIRV-Cross 生成 GLSL 410 | 构建期离线编译，运行时不解析着色器源码；后续可经 SPIRV-Cross 输出 MSL / HLSL。 |
| 依赖管理 | 源码方式固定版本（`external/` 子模块或 CMake FetchContent 固定 tag） | 不依赖 Homebrew 或系统包；具体机制在首次提交时通过 ADR 确定。 |
| 逻辑画布 | 320×180 | 作为默认基线；窗口缩放优先整数倍，剩余区域留黑边或按明确策略处理。 |
| 房间与镜头 | 每个房间正好一屏，8 px Tile，出屏切换房间 | 房间 40×22 Tile（320×176），画布余下的 4 像素行用作边框 / HUD；具体尺寸在 M1 的 ADR 中确认。 |
| 关卡数据 | Tiled 编辑器（`.tmj` JSON） | M4 内嵌编辑器完成前用 Tiled 制作关卡；运行时只读取约定的图层与对象属性，M5 起烘焙为紧凑格式。 |
| 实体架构 | EntityId + 扁平实体存储，组件按需引入 | 先解决生命周期、碰撞与行为；通过测量决定是否采用更完整 ECS。 |
| 开发期资源 | 可读源文件 + 清晰元数据 | PNG/动画源/关卡数据；让美术与调试流程先跑通。 |
| 发行资源 | 后期 Asset Cooker 输出紧凑包 | 需要版本号、校验、资源目录、压缩策略和错误诊断。 |
| 代码组织 | Platform → RHI → Runtime → Framework → Game；Tools 横向支撑 | Game 可依赖 Engine；Engine 不得依赖具体 Game。 |

## 3. 总体架构与依赖规则

| 层 / 模块 | 职责 | 边界规则 |
|---|---|---|
| Platform | 窗口、输入、计时、文件系统基础、控制器、图形 Surface 创建（基于 SDL3） | 封装平台差异，不包含游戏规则；平台条件编译只允许出现在这一层和构建脚本中。 |
| RHI | 图形 API 抽象：资源、管线、命令录制、呈现；OpenGL / Vulkan 后端实现 | 只有 RHI 后端目录可以包含图形 API 头文件；后端类型不得出现在 RHI 公共接口中。 |
| Engine Runtime | Renderer（精灵、Tile、光照、合成）、Resource Manager、Audio、Clock、Logging、Memory/Diagnostics | Renderer 只调用 RHI，不直接调用任何图形 API；不知道具体敌人或关卡语义。 |
| Game Framework | Entity、Collision、Animation、Room、Trigger、Save、Event | 提供 2D 探索游戏常用机制；保持轻量且可测试。 |
| Game | 玩家、敌人、道具、谜题、关卡规则与内容 | 游戏逻辑依赖框架和引擎；禁止把专属逻辑塞入 Renderer/Platform。 |
| Tools | Debug UI、Profiler、Asset Cooker、Level Editor、Hot Reload | 工具可调用引擎服务；发行构建可以关闭或剔除开发工具。 |

### 3.1 建议的仓库目录

```text
AnimalEngine/
├── CMakeLists.txt
├── CMakePresets.json
├── PROJECT.md                # this document
├── README.md
├── cmake/                    # compiler flags, platform helpers, shader compile rules
├── docs/
│   ├── ARCHITECTURE.md
│   └── ADR/                  # Architecture Decision Records
├── external/                 # pinned third-party sources (SDL3, glslang, SPIRV-Cross, ...)
├── engine/
│   ├── CMakeLists.txt
│   ├── include/animal/
│   └── src/
│       ├── platform/
│       ├── core/             # logging, clock, handles, events
│       ├── rhi/              # backend-agnostic graphics interface
│       │   ├── opengl/
│       │   └── vulkan/
│       ├── renderer/         # 2D renderer, built on RHI only
│       ├── resources/
│       ├── audio/
│       └── diagnostics/
├── framework/
│   ├── include/animal/
│   └── src/                  # entity, collision, animation, room, save
├── game/
│   ├── CMakeLists.txt
│   └── src/                  # player, creatures, items, puzzles
├── shaders/                  # GLSL 450 sources, compiled to SPIR-V at build time
├── tools/
│   ├── asset_cooker/
│   └── editor/               # initially in-game / ImGui-based
├── assets/
│   ├── source/
│   └── cooked/               # generated; do not hand-edit
├── tests/
└── build/                    # generated (incl. Xcode / VS projects); never commit
```

## 4. 实施路线与里程碑

按依赖顺序推进，不按“最有趣的功能”随意跳跃。一个里程碑只有在验收条件满足后才算完成；若暂时跳过某项，必须记录为明确的延期项。

| 阶段 | 目标 | 主要工作 | 完成 / 验收标准 |
|---|---|---|---|
| M0 | 基础运行时与像素画布 | SDL3、CMake（Xcode / VS / Ninja 预设）、日志、计时、输入、RHI 最小接口与 OpenGL 后端、着色器构建流程、纹理、Sprite Batch、Camera、320×180 Render Target、整数缩放、Debug Overlay。 | macOS（Xcode、Ninja）与 Windows（VS、Ninja）均能干净构建并运行；可在一个房间中绘制 Tile 和角色；窗口缩放无模糊/意外拉伸；可显示 FPS、帧时间与调试信息；RHI 后端目录之外没有任何 OpenGL 调用。 |
| M1 | 最小可玩游戏框架 | Tile Map、房间数据、角色移动/跳跃、碰撞、触发器、动画状态机、简单敌人、道具、房间切换、基础存档。 | 完成一个可从入口走到出口的关卡；重启后持久状态正确；碰撞和房间边界可重复测试；两平台行为一致。 |
| M2 | 像素光照与合成 | 材质参数、法线贴图、点光源、遮罩、边缘光、调色、渲染层与合成策略。 | 同一场景在不同光照下产生稳定且可控的变化；渲染目标尺寸、混合和采样规则有文档；所有 Pass 只通过 RHI 实现。 |
| M2.5 | Vulkan 后端完善 | 补齐 M1–M2 期间只在 OpenGL 实现的 RHI 功能；抽出两后端共用的参数校验；拆分 `vk_device.cpp`；细化屏障、内存子分配与管线缓存；Windows 原生驱动验证。 | M0–M2 全部测试场景在 Vulkan 与 OpenGL 下输出一致（截图对比在容差内）；可通过启动参数切换后端；验证层无报错；上层代码无需修改。 |
| M3 | 粒子与流体原型 | GPU 粒子、低分辨率速度场、平流、耗散、外力/染料注入、烟雾/水的视觉合成。 | 角色或物件能扰动模拟；固定测试场景下结果稳定；有开关、参数控制与性能计数；OpenGL 4.1 下有片元着色器实现路径。 |
| M4 | 内容工具与热迭代 | 调试面板、参数热调、纹理/着色器热重载、内嵌关卡编辑器、动画导入器。 | 不修改引擎代码即可摆放实体、光源、触发器并快速测试；资源重载失败能报错且不崩溃。 |
| M5 | 资源烘焙与发行优化 | Asset Cooker、图集、去重、格式转换、压缩、资源目录/版本、包体分析、Release 构建。 | 源资源能确定性地生成发行资源；缺失/重复/未引用资源可报告；可比较优化前后的体积与加载性能。 |
| M6 | 完整纵向切片与稳定性 | 多房间世界、更多生物/机关、音频、地图探索、存档健壮性、性能分析、崩溃诊断、自动化验证。 | 从启动到通关一段完整区域无需开发者手动修补；干净构建可重复；已知问题有清单。 |
| M7 | 平台与发行准备 | macOS .app 包（签名、公证）与 Windows 发行包、控制器、窗口/全屏、高 DPI、音频设备变化、输入映射、资源校验、打包和回归测试。 | 两个平台的目标机器上安装包可独立运行；没有依赖开发机的绝对路径；发行包清单可审计；两平台 × 两后端回归通过。 |

## 5. 推荐的具体实现顺序

1. 建立 Git 仓库、CMakeLists.txt、CMakePresets.json（macOS：Ninja / Xcode；Windows：VS 2022 / Ninja）、README 与构建说明。
2. 以源码方式引入固定版本的 SDL3；创建最小应用：窗口、主循环、固定时间/帧时间测量、退出处理。
3. 建立日志、断言、错误返回和 Debug Overlay；让错误可见。
4. 定义 RHI 最小接口（Device、Buffer、Texture、Sampler、Shader、Pipeline、CommandList、RenderPass、Swapchain）并实现 OpenGL 4.1 后端；建立 GLSL 450 → SPIR-V → GLSL 410 的着色器构建流程。
5. 在 RHI 之上建立 Renderer 的最小边界：Texture、RenderTarget、Sprite、Camera；不要先做通用渲染图。
6. 实现 320×180 离屏画布、整数倍缩放、视口/黑边策略与最近邻采样。
7. 实现图集、批量绘制、图层排序、Tile Map 与一个测试房间。
8. ~~M0 完成后做一次限时的 Vulkan 技术验证~~：已完成并合入主线，结论见 ADR-0005。
9. 实现 EntityId 生命周期、Transform、Sprite、碰撞体、碰撞查询和玩家控制。
10. 实现动画状态机、触发器、房间切换、道具和持久状态。
11. 建立资源导入约定和调试可视化；确保每个系统都有最小测试场景。
12. 添加法线贴图和光照 Pass；先用少量光源验证正确性，再优化批处理和性能。
13. 添加遮罩、边缘光、调色和最终合成；明确每个 Render Target 的格式和尺寸。
14. 完善 Vulkan 后端（M2.5）：补齐功能、拆分与优化，并在 Windows 原生驱动上验证。
15. 独立实现粒子；再做低分辨率流体模拟，避免一开始混合多个未知系统。
16. 加入参数热调、资源热重载与内嵌关卡编辑器。
17. 最后实现发行资源烘焙、压缩、去重、包体分析和两个平台的发行流程。

## 6. 工程硬规则（必须长期遵守）

- **可运行优先**：主分支应始终能够配置、编译和启动。大功能使用短分支或小步提交，避免长时间无法构建。
- **跨平台持续可编译**：主分支在 macOS 与 Windows 上都必须可配置、编译、启动。源文件统一 UTF-8（MSVC 使用 `/utf-8`），行尾由 `.gitattributes` 管理，文件名大小写与 include 保持一致；不提交生成的 Xcode / VS 工程。
- **图形 API 隔离**：只有 `engine/src/rhi/<backend>/` 可以包含 OpenGL / Vulkan 头文件或调用图形 API。Renderer 与上层只依赖 RHI 接口，不出现任何图形 API 类型。
- **RHI 按 Vulkan 语义设计**：管线状态预先创建且不可变；资源创建时声明用途；渲染通道显式声明 load/store；命令先录制再提交；资源绑定按 set/binding 布局。由 OpenGL 后端模拟这些语义，而不是让 RHI 迁就 OpenGL 的全局状态机。
- **主后端推进，副后端不掉队**：OpenGL 是主开发后端。Vulkan 后端必须始终可编译，并在每个里程碑验收时通过截图一致性对比（`tools/scripts/compare_images.py`，容差 0）。新的 RHI 功能可以先只在 OpenGL 实现，但必须在 Vulkan 中给出明确的错误日志而不是静默错误，并在 M2.5 前补齐。
- **逻辑分辨率规则**：游戏世界逻辑坐标与窗口像素坐标必须分离。明确整数缩放、视口、鼠标坐标反变换和最近邻采样规则。
- **固定/可变时间规则**：物理与模拟优先采用固定时间步；渲染可变帧率。明确最大补帧数，避免卡顿后无限追赶。
- **资源所有权**：资源使用明确的 Handle/生命周期管理。避免裸指针跨模块长期持有；热重载必须处理旧资源引用。
- **渲染线程规则**：第一版优先单线程渲染与清晰的资源更新边界。只有 profiler 证明有必要时再引入多线程复杂度。
- **错误处理**：文件缺失、着色器编译失败、资源格式版本不匹配等情况必须给出可定位日志；开发构建可失败快速，发行构建需优雅报错。
- **数据与代码分离**：关卡、实体参数和资源引用尽可能数据驱动；独特行为仍可由 C++ 实现。不要把所有内容都硬编码进底层。
- **开发资源与发行资源分离**：源文件用于创作，Cooked 数据由工具生成。生成目录不得手工修改，必须可重复生成。
- **优化必须测量**：每次性能或包体优化前后都记录基线。不要凭感觉引入复杂压缩、内存池或 ECS。
- **模块依赖方向**：Platform 不依赖 Game；Renderer 不知道敌人/机关；Engine 不依赖具体游戏内容。
- **外部依赖有边界**：每个第三方库都记录用途、版本、许可证、平台支持和是否进入发行包。
- **调试能力不是事后补丁**：每个复杂系统至少有可视化、开关、参数查看和可复现测试场景。
- **文档随决策更新**：改变模块边界、坐标约定、资源格式或构建方式时，同一提交更新文档或 ADR。

## 7. 坐标、渲染和资源的关键约定

### 7.1 坐标系

- 世界坐标：浮点数或明确的定点方案；用于角色运动、碰撞和房间定位。
- 逻辑画布坐标：默认 320×180；用于像素风格画面合成。
- 窗口坐标：真实窗口/显示器像素；仅在平台输入、呈现和 DPI 处理处使用。
- 所有坐标变换必须集中在 Camera/Viewport/Platform 边界，禁止各模块自行猜测缩放比例。
- 鼠标/控制器指针命中逻辑画布时，必须使用窗口坐标到逻辑坐标的统一反变换。

### 7.2 像素清晰度

- 低分辨率 Render Target 使用最近邻采样；禁止默认开启会模糊像素的线性过滤。
- 整数缩放优先；若窗口比例无法整除，明确选择留边或特殊缩放，不允许隐式非整数缩放。
- 相机亚像素移动策略必须明确：可以采用整数像素相机，也可以允许平滑世界运动后在渲染时量化，但不能混用。
- 透明混合、预乘 Alpha 与普通 Alpha 的使用规则必须统一，并在渲染器文档中说明。
- 所有 Pass 的输入/输出尺寸、纹理格式、颜色空间和采样方式必须明确。

### 7.3 资源与格式

- 开发期源资源保留原始文件；Cooked 资源必须可从源文件重新生成。
- 资源包需要 magic、版本、目录、偏移/长度、资源类型和可选校验信息。
- 压缩按资源类型选择；不要对已压缩的 PNG/音频再次盲目压缩。
- 资源引用使用稳定 Handle 或 ID；避免依赖绝对路径或目录枚举顺序。
- 先记录每种资源的体积、加载时间和运行时内存，再决定是否量化、压缩或去重。

### 7.4 渲染抽象与跨后端约定

- RHI 以 Vulkan 约定为准：裁剪空间深度范围 [0, 1]，纹理与帧缓冲坐标原点在左上。OpenGL 后端在内部修正差异（macOS 的 OpenGL 4.1 不支持 `glClipControl`，需在投影与呈现阶段处理）；上层与着色器不得出现后端特定分支。
- 着色器使用显式 `layout(set = N, binding = M)`；着色器工具把 set/binding 写入绑定表，OpenGL 后端据此映射为绑定槽。上层代码不按名称查询 uniform。
- 常量数据通过 Uniform Buffer / Push Constant 抽象传递；OpenGL 后端用 UBO 模拟 Push Constant。
- RHI 从一开始引入 frames-in-flight（同时在途帧）概念（OpenGL 后端可为 1），动态缓冲按帧轮换，避免迁移 Vulkan 时重构上层的数据更新方式。
- 可选能力（如计算着色器）通过 Device 能力查询暴露。OpenGL 4.1 不支持计算着色器，依赖它的功能必须有片元着色器路径或明确的降级方案。
- 后端在运行时通过命令行参数或配置选择；同一个可执行文件可包含多个后端。

## 8. 测试与验收策略

| 测试类别 | 最小要求 | 典型验证 |
|---|---|---|
| 构建测试 | 两个平台、各生成器干净目录可配置并编译 | macOS（Xcode、Ninja）与 Windows（VS、Ninja）删除 build 目录后执行 preset 构建。 |
| 渲染回归 | 固定测试场景可重复截图 | 像素缩放、Alpha、图层、光照和后处理。 |
| 后端一致性 | 同一场景在不同后端输出一致 | 每个里程碑验收时，用 `--hide-overlay --no-vsync` 截图并逐像素对比 OpenGL 与 Vulkan（目前容差 0）。 |
| 物理/碰撞 | 边界与角落案例可重复 | 墙角、落地、连续碰撞、触发器进入/退出。 |
| 资源测试 | 缺失/损坏/版本不符可定位 | 加载故意损坏的纹理、动画和资源包。 |
| 存档测试 | 保存/加载状态一致 | 关卡状态、收集品、机关状态与地图探索；存档跨平台可读。 |
| 性能测试 | 固定场景有基准数据 | CPU/GPU 帧时间、绘制批次、活跃实体、粒子数；分平台、分后端记录。 |
| 发行测试 | 包体可独立运行 | 在没有开发资源目录的情况下，于两个平台启动并进入关卡。 |

每个里程碑至少保存一个可重复的测试场景。复杂系统应当能独立启用/禁用，以便判断画面问题来自哪个 Pass 或模拟模块。

## 9. 性能与体积预算：先建立基线，再设目标

- 第一阶段不要承诺最终发行包必须达到某个 MB 数。先做内容原型并测量各类资源占比。
- 每次 Release 构建输出包体报告：可执行文件、纹理、音频、关卡、动画、着色器/其他资源和第三方依赖。
- 性能报告至少记录 CPU 帧时间、GPU 帧时间（可获得时）、Draw Calls、纹理内存、活跃实体和粒子数量。
- 优化顺序通常为：移除未使用内容 → 资源复用/去重 → 合理编码和压缩 → 发行链接与符号设置 → 更复杂的专用格式。
- 体积小不等于运行时内存小；压缩包可能需要解压到更大的内存缓冲区。分别记录磁盘体积、加载峰值和运行时占用。

## 10. 风险清单与缓解策略

| 风险 | 表现 | 缓解 |
|---|---|---|
| 过度设计 | 长时间建设框架但没有可玩关卡 | 每个里程碑都交付可运行演示；优先垂直切片。 |
| RHI 泄漏 OpenGL 语义 | 接口隐含 OpenGL 全局状态，接入 Vulkan 时需要重写上层 | RHI 按 Vulkan 语义设计；Vulkan 后端常驻主线并做截图一致性对比（ADR-0005）；代码审查禁止上层出现图形 API 类型。 |
| RHI 过度抽象 | 为覆盖各 API 全部特性而膨胀，拖慢游戏进度 | 只抽象当前用到的能力；新增接口必须有实际使用者。 |
| macOS 上的 Vulkan 依赖 MoltenVK | 部分特性/扩展缺失或性能差异 | 只使用 MoltenVK 支持的特性子集；必要时评估 Metal 后端（RHI 已预留扩展点）。 |
| 过早做编辑器 | 工具开发时间超过内容开发 | 先 Debug Overlay、参数热调和内嵌编辑，再逐步独立。 |
| 模拟效果拖慢帧率 | 流体 Pass 成本过高、调试困难 | 低分辨率模拟、固定测试场景、逐步增加复杂度。 |
| 资源格式过早冻结 | 每次改字段都要迁移全部内容 | 开发期格式保持可读；等需求稳定后定义版本化 Cooked 格式。 |
| 热重载造成崩溃 | 资源生命周期、函数指针或状态不一致 | 先热重载数据/着色器，再考虑游戏代码 DLL 热重载；定义状态迁移和安全边界。 |
| 包体优化误判 | 二进制化后没有明显缩小 | 每项优化前后测量；避免对已压缩资源重复压缩。 |
| 垂直同步在无显示时阻塞 | macOS 上 OpenGL 交换在显示器休眠 / 锁屏时无限阻塞；MoltenVK 同样情况下不限速 | 自动化运行使用 `--no-vsync`；M7 前处理遮挡与休眠（暂停渲染或带超时的帧节奏）。见 ADR-0005。 |
| 跨平台问题积累 | 一个平台长期无法编译；平台条件散落在各模块 | 平台差异集中在 Platform 层和构建脚本；两个平台持续做干净构建（建议 CI 矩阵覆盖 macOS + Windows）。 |

## 11. Git、构建和交付规则

- 每个可交付阶段打 Git tag，例如 milestone/m0-runtime、milestone/m1-playable。
- 提交应围绕单一目的；避免把格式化、重构和功能变更混在同一个巨大提交中。
- build/、生成的 Xcode / VS 工程、临时截图、日志和自动生成的 Cooked 资源默认不提交；若项目明确决定提交发行资源，需单独记录原因。
- CMakePresets.json 中共享的配置应可移植；机器私有路径、SDK 路径和本地覆盖放在不提交的 CMakeUserPresets.json 或本地配置中。
- 第三方依赖固定版本并记录获取方式；禁止依赖开发机上未记录的全局库。
- Release 构建必须从源文件和已声明依赖可重复生成，不依赖 IDE 手动配置的隐藏状态。

### 11.1 最小构建命令基线

```bash
# macOS · Ninja（日常开发 / CI）
cmake --preset macos-ninja-debug
cmake --build --preset macos-ninja-debug

# macOS · 生成 Xcode 工程
cmake --preset macos-xcode
open build/macos-xcode/AnimalEngine.xcodeproj

# Windows · 生成 Visual Studio 2022 工程
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022-debug   # 或打开 build/windows-vs2022/AnimalEngine.sln

# Windows · Ninja（在 VS Developer 命令行中）
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug

# 测试（接入 CTest 后）
ctest --test-dir build/<preset> --output-on-failure

# 清理后重建：删除对应 build 目录，再重新配置
```

具体 preset 名称、CMake 最低版本、SDL3 的引入方式和图形依赖应在首次提交时确认，并以仓库中的实际配置为准。

## 12. 项目决策记录（ADR）模板

```markdown
# ADR-XXXX: <决策标题>

- 状态：Proposed / Accepted / Superseded
- 日期：
- 背景与问题：
- 备选方案：
- 决策：
- 主要理由：
- 代价与风险：
- 影响的模块：
- 验证方式：
- 重新评估条件：
```

- 以下决策建议记录 ADR：RHI 设计与 Vulkan 迁移策略、着色器工具链、坐标与缩放策略、资源包格式、实体架构、线程模型、热重载方案、音频方案、依赖管理和跨平台支持范围。
- 已确认、需在建仓时写入 `docs/ADR/` 的决策（2026-10-22）：
  - ADR-0001 图形架构：自研 RHI 按 Vulkan 语义设计；OpenGL 4.1 为首个后端，Vulkan 为后续目标后端（Accepted）。
  - ADR-0002 平台层：采用 SDL3，替代 v1.0 中的 SDL2（Accepted）。
  - ADR-0003 跨平台构建：macOS 与 Windows 均为一等平台；CMake 为唯一来源，生成 Xcode、Visual Studio 与 Ninja 构建（Accepted）。
  - ADR-0004 依赖管理与着色器工具链：git 浅子模块固定 tag，单头文件依赖复制进 `external/`；`animal_shaderc` 从源码构建 glslang 与 SPIRV-Cross（Accepted）。
  - ADR-0005 Vulkan 技术验证结论：RHI 接口成立，两后端逐像素一致；确定 Vulkan 基线与虚拟交换链约定（Accepted）。

## 13. 当前待确认项（不阻塞 M0）

- macOS 最低系统版本（预设中暂定 13.3，libc++ 的 `std::format` 需要）、是否需要 Universal Binary（arm64 + x86_64）；Windows 最低版本。
- 是否评估 SDL3 自带的 GPU API（SDL_GPU，支持 Vulkan / Metal / D3D12）作为 RHI 的一个后端或替代方案。
- CI 平台（例如 GitHub Actions 的 macOS + Windows 矩阵）。
- 音频模块采用 SDL3 音频能力还是单独音频库。
- 流体模拟最终使用 GPU 还是 CPU；以原型性能测试为准。
- 资源包压缩算法、音频格式、纹理压缩与平台兼容策略。
- 是否需要游戏逻辑 DLL 热重载；优先实现资源和着色器热重载。
- 目标帧率、最低硬件规格和发行包体预算；先在 M0/M1 建立测量基线。

## 14. 立刻开始：首个可执行任务清单

1. 创建 AnimalEngine Git 仓库、`.gitignore`、`.gitattributes` 与 `docs/ADR/` 目录，写入 ADR-0001 ~ ADR-0004。
2. 创建根 CMakeLists.txt 和 CMakePresets.json：macOS（Ninja、Xcode）与 Windows（VS 2022、Ninja），并显式指定编译器。
3. 以源码方式固定 SDL3 依赖来源和版本，在两个平台验证干净构建。
4. 建立应用主循环、窗口、输入、计时器和日志。
5. 定义 RHI 最小接口并实现 OpenGL 4.1 后端；接入 GLSL 450 → SPIR-V → GLSL 410 着色器构建流程。
6. 经由 RHI 实现 320×180 Render Target 与最近邻整数倍呈现。
7. 实现一个 Sprite、一个 Tile Map 和一个可移动的占位角色。
8. 增加 FPS、帧时间、渲染尺寸、缩放倍率和当前图形后端的调试显示。
9. 在两个平台录制或保存 M0 验收截图，并将通过条件写入 README。
10. 通过后打 tag：milestone/m0-runtime；再开始 M1。

## 附录 A. 外部参考原则

Animal Well 的公开访谈和演讲可以帮助理解设计方向，但本项目应以自己的测试结果为准。不要把媒体报道中的概括当成完整源码规格；未公开的实现细节应由原型验证决定。

建议持续记录的参考主题：像素渲染与法线光照、低分辨率流体模拟、扁平实体架构、资源导入/烘焙、运行时关卡编辑、热重载与发行包分析。

## 附录 B. 版本记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-10-20 | 建立项目愿景、初始技术决策、模块架构、里程碑、硬规则、测试策略和启动任务。 |
| v1.1 | 2026-10-22 | 确认三项基线决策：图形层采用按 Vulkan 语义设计的 RHI，OpenGL 为首个后端、Vulkan 为后续目标；平台层由 SDL2 改为 SDL3；macOS 与 Windows 均为一等平台，支持 Xcode、Visual Studio 与 Ninja 构建。相应更新技术决策、架构分层、仓库目录、里程碑（新增 M2.5 Vulkan 后端）、实现顺序、硬规则、渲染抽象约定（§7.4）、测试、风险、待确认项与任务清单。项目尚无代码，无迁移成本。 |
| v1.2 | 2026-10-22 | 确认房间与镜头方式（单屏房间、8 px Tile、出屏切换）和关卡数据格式（Tiled `.tmj`）；确定 M0 之后先做 Vulkan 限时验证再进入 M1。M0 测试房间（16 px Tile、滚动镜头）为占位实现，M1 中替换。 |
| v1.3 | 2026-10-22 | Vulkan 技术验证通过（ADR-0005），验证版 Vulkan 后端合入主线。硬规则“单后端推进”改为“主后端推进，副后端不掉队”：OpenGL 为主开发后端，Vulkan 必须始终可编译并在里程碑验收时通过截图一致性对比；M2.5 改为 Vulkan 后端完善。 |
