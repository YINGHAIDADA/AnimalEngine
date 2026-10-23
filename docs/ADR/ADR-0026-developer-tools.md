# ADR-0026: 开发工具框架：RHI 上的 Dear ImGui 与源资源目录

- 状态：Accepted
- 日期：2026-10-23
- 背景与问题：M4 要求调试面板、参数热调、热重载与内嵌关卡编辑器。此前只有 F1 文字覆盖层与 F 键开关，没有可交互的界面，平台层也没有鼠标与文本输入；编辑结果应该写到哪里（构建目录中部署的 `data/` 还是仓库里的源文件）也没有约定。
- 决策：
  - **UI 库**：Dear ImGui（`external/imgui`，子模块固定 v1.93.0，MIT）。渲染后端自己写在 RHI 上（`tools/devtools` 的 `ImGuiLayer`）：专用 `imgui.vert/.frag`（直通 Alpha 混合），每帧一个顶点缓冲、一个索引缓冲，按绘制命令设置裁剪矩形与纹理绑定组；实现 ImGui 的纹理协议（`RendererHasTextures`），字体按显示密度光栅化，Retina 下清晰；支持 `VtxOffset` 与采样器回调（引擎纹理默认最近邻）。因此 UI 在 OpenGL 与 Vulkan 上走同一条路径，不引入任何后端相关代码。
  - **输入**：平台层补齐完整按键表、鼠标位置 / 按键 / 滚轮、文本输入、剪贴板、系统光标，每帧把 OS 事件整理成 `FrameEvents::input` 列表交给工具；`Input::setCaptured()` 在 ImGui 占用键盘 / 鼠标时让游戏读到“未按下”。关闭 ImGui 的键盘导航：只有在编辑文本时键盘才属于工具，其余时间游戏按键照常可用。
  - **接入方式**：引擎不依赖 ImGui。`Application` 只多了 `EngineContext::events`、`onToolsPass()`（屏幕 pass 之后、在交换链上 Load/Store 的一个渲染通道）、`overlayTop` / `overlaySuppressed`（给菜单栏让位、编辑器全屏时隐藏覆盖层）与 `escapeQuits`。游戏侧的 `DevTools` 组件持有 ImGui 层与各面板；`` ` `` 显示 / 隐藏，默认隐藏，所以截图与渲染回归不受影响。
  - **构建开关**：CMake 选项 `ANIMAL_DEV_TOOLS`（默认 ON）。关闭时不编译 ImGui、`animal_devtools` 与游戏的工具代码（`dev_tools_off.cpp` 提供空实现），为 M5 / M7 的发行构建剔除开发工具做好准备。
  - **源资源是唯一真相**（`SourceAssets`）：开发构建编译时记录源码目录（`ANIMAL_SOURCE_DIR`）。工具的写入同时写源文件与部署副本（都原子写入）：`data/world/…` ↔ `assets/source/world/…`，`data/textures/…` ↔ `assets/source/textures/…`，`data/shaders/X.ashd` ↔ `shaders/X`。找不到源码目录（构建被拷到别的机器）时只改部署副本，并在日志里说明。
  - **面板**：性能（CPU 帧时间与各 GPU pass 的历史曲线）、渲染（F 键开关）、房间（双击传送）、观感 / 效果 / 植物（直接编辑预设 JSON，用格式自己的解析器校验后实时应用，保存回源文件；`formatJson` 输出与手写文件逐字节一致）、参数（ADR-0027）、动画（ADR-0030）、控制台（日志接收器，菜单栏提示未读警告 / 错误）。
- 主要理由：ImGui 是游戏开发工具的事实标准，内嵌在游戏里迭代最快（PROJECT.md：先调试工具与内嵌编辑，再考虑独立编辑器）；自己写 RHI 后端只有约 400 行，却让工具在两个后端上自动一致，也顺便验证了 RHI（动态缓冲、裁剪、基顶点偏移、局部纹理更新）。把源文件当真相，避免了“改了 data/ 下次构建被覆盖”或“改了源文件游戏看不到”的两种常见错误。
- 代价与风险：ImGui 每帧重建 UI，CPU 开销约 0.1–0.3 ms（只在工具显示时）；字体图集在高密度屏幕上较大；源码目录写死在开发构建中，属于有意为之的开发期依赖，发行构建关闭 `ANIMAL_DEV_TOOLS` 即可去掉。
- 验证方式：ImGui 演示窗口在 OpenGL 与 Vulkan 上截图一致、零校验错误；`ANIMAL_DEV_TOOLS=OFF` 的 Release 构建通过；单元测试覆盖 JSON 格式化（逐个预设文件往返）与源 / 部署路径映射。
- 重新评估条件：需要多窗口 / 停靠布局时换用 ImGui docking 分支；做独立编辑器时把面板移到单独的工具程序。
