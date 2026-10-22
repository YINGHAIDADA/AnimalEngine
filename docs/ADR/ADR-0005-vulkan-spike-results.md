# ADR-0005: Vulkan 技术验证结论

- 状态：Accepted
- 日期：2026-10-22
- 背景与问题：ADR-0001 要求 RHI 按 Vulkan 语义设计，并在 M0 后做一次限时验证，确认在 Vulkan 下画出 M0 测试场景时 Renderer 与游戏代码不需要修改。
- 验证环境：macOS 27 / Apple M2，MoltenVK 1.2.11（设备 API Vulkan 1.2.296），Vulkan SDK 1.4.313 提供加载器与验证层；Debug 构建开启验证层与同步验证（synchronization validation）。

## 结论

1. **RHI 接口成立**。Vulkan 后端（`engine/src/rhi/vulkan/`）完成后，`Renderer2D`、`SpriteBatch` 与游戏代码零修改；OpenGL 与 Vulkan 的 2560×1440 截图逐像素完全一致（容差 0）。
2. **验证层零报错**，包括同步验证；连续 4 次窗口尺寸变化（交换链重建）后结果仍逐像素一致。反向验证：删除上传后的屏障会立即产生同步错误，说明同步验证确实生效。
3. **“先录制、后提交”的设计被证明是必要的**：dynamic rendering 内不允许插入屏障，后端在提交时预扫描每个渲染通道，把该通道需要的布局转换（附件 → COLOR_ATTACHMENT，被采样纹理 → SHADER_READ_ONLY）统一放在 `vkCmdBeginRenderingKHR` 之前。
4. **队列顺序的 `writeBuffer` / `writeTexture` 语义可直接实现**：帧内写入进入暂存缓冲与上传命令缓冲，提交时排在帧命令之前；帧外写入立即执行并等待完成。

## 由此确定的约定

- **Vulkan 基线**：Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`；MoltenVK 上额外启用 `VK_KHR_portability_subset`。（解决 PROJECT.md §13 中“最低 Vulkan 版本与必需扩展”。）
- **虚拟交换链**：两个后端的 `swapchainTexture()` 都是 RGBA8 离屏图像，呈现时再复制到真实交换链（Vulkan 表面通常是 BGRA8）。这样 `swapchainFormat()` 在所有后端一致，Renderer 可以对画布和屏幕使用同一条管线。代价是每帧一次全屏复制，对 2D 可以忽略。
- **每帧写入必须发生在 `beginFrame()` 与 `submit()` 之间**；帧外写入会走同步慢路径，只适合加载阶段。
- **描述符布局**：所有绑定的 stage 标志统一为 VS | FS，使相同 set 布局在不同管线之间兼容；布局按签名缓存。

## 发现的问题与后续工作

| 问题 | 处理 |
|---|---|
| macOS 上按文件名 `dlopen` 不再搜索 `/usr/local/lib`，SDL 找不到 Vulkan 加载器 | 平台层按候选路径加载（可执行文件目录、`$VULKAN_SDK`、`/usr/local/lib`）。发行版必须随包携带加载器、MoltenVK 与 ICD 清单（M7）。 |
| macOS 上 OpenGL 的垂直同步由 SDL 用 CVDisplayLink 模拟；显示器休眠或锁屏时 `SDL_GL_SwapWindow` 会无限阻塞。MoltenVK 的 FIFO 在同样情况下不限速 | 自动化运行一律使用 `--no-vsync`；M7 前需处理遮挡 / 休眠（暂停渲染或带超时的帧节奏）。已加入风险清单。 |
| 管线、绑定组的参数校验在两个后端各写了一份 | M2.5 抽到 RHI 公共代码。 |
| `vk_device.cpp` 约 1900 行 | M2.5 拆分为实例/设备、交换链、资源、命令翻译等文件。 |
| 屏障使用粗粒度的 ALL_COMMANDS，内存每个资源单独分配 | 正确但不高效；M2.5 按用途细化屏障，引入子分配（或 VMA）。 |
| OpenGL 后端尚不支持多颜色附件（Vulkan 已支持） | M2 需要 MRT 时补上。 |
| 未验证：Windows 上的原生 Vulkan 驱动、性能、`firstInstance` | M2.5 / Windows 验证时补测。 |

## 工具

- `--backend=vulkan`、`--hide-overlay`、`--resize-test`、`--no-vsync` 命令行选项。
- `tools/scripts/compare_images.py`：两张截图的逐像素对比（可设容差），作为后端一致性测试的基础。

- 重新评估条件：Windows 原生驱动或后续功能（光照、粒子、流体）在 Vulkan 下需要 RHI 结构性修改。
