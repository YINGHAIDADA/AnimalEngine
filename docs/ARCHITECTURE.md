# 架构与渲染约定

本文件描述代码的实际结构和必须遵守的约定，是 [PROJECT.md](../PROJECT.md) §3 与 §7 的落地说明。改变这里的任何约定都要同步更新本文件（必要时写 ADR）。

## 1. 模块与依赖

```text
game/            AnimalGame（M1b：Tiled 世界、CaveRoom、房间切换）─┐
framework/       Tile 地图、像素级碰撞、实体、   │
                 动作映射、平台跳跃控制器        │
engine/                                         │ 只能向下依赖
  app/           主循环、固定时间步、调试覆盖层  │
  renderer/      Renderer2D、SpriteBatch、画布   │
  resources/     图片 / 纹理 / 着色器包加载      │
  rhi/           图形抽象 + 后端（opengl/ vulkan/）│
  platform/      SDL3：窗口、输入、事件、时间    │
  core/          日志、断言、句柄、数学、文件   ─┘
tools/shaderc/   构建期着色器编译器（不进入游戏）
tools/devtools/  开发工具库（ANIMAL_DEV_TOOLS）：ImGui on RHI、面板组件、文件监视、源资源目录、Tiled 文档编辑
                 （依赖 engine + framework；游戏的 game/src/dev/ 在其上实现面板、热重载与关卡编辑器）
tools/rhi_conformance/  RHI 功能一致性与着色器热重载检查
shaders/         GLSL 450 着色器源码
```

- `engine/src/rhi/opengl/` 与 `engine/src/rhi/vulkan/` 是仅有的可以包含图形 API 头文件、调用 `gl*` / `vk*` 的目录。
- RHI 公共头文件（`animal/rhi/*.h`）不暴露任何后端类型。
- SDL 只出现在 `platform/`、`core/`（日志、文件）、RHI 后端（上下文 / Surface）和 `app/entry_point.h`。
- 引擎不依赖 ImGui 与开发工具：工具通过 `EngineContext::events`、`Application::onToolsPass` 与 `Input::setCaptured` 接入；`ANIMAL_DEV_TOOLS=OFF` 时整个工具层不编译（ADR-0026）。

## 2. 帧流程（光照细节见 ADR-0007）

```text
platform::pollEvents（+ --input-script 注入的事件）→ Input::update → game.onFrame（开发工具：热重载轮询、ImGui 帧、编辑器）
  → 固定步长 onFixedUpdate（60 Hz，每帧最多 5 步，超出丢弃积压；关卡编辑时玩家冻结）
Device::beginFrame（窗口最小化时返回 false，跳过渲染）
  Renderer2D::beginFrame
    game.onRender:  beginWorld(camera) … drawSprite / drawGlow / addLight / addFluidSplat … endWorld
                    → [有 GPU 粒子时] GPU 粒子模拟一步（ADR-0020）
                    → 材质 pass（albedo / normal / emissive / 材质标志；末尾画 lit 样式的 GPU 粒子）→ 流体烟雾一步（ADR-0008）
                    → 光照 pass（光照 + 边缘光两个 RGBA16F，按遮挡图做硬边阴影）→ 合成到 320×180 画布（含被照亮的烟与边缘光）
                    → [有水时] 合成改写到 lit，水 pass（折射、倒影、水色）读 lit 写画布（ADR-0018）
                    → 发光 pass（drawGlow 收集的像素与 glow 样式的 GPU 粒子，加色混合叠加到画布，不受光照影响）
                    → 后处理：泛光（亮部提取 → 半分辨率模糊 → 量化抖动加回）+ 调色 → 最终画布（ADR-0009）
                    [beginWorldOverlay … 调试图形 … endWorldOverlay]（无光照、不调色，叠加在最终画布上）
    beginScreen:    最终画布按整数倍居中绘制到 swapchain，剩余留黑边
                    调试覆盖层（drawText / fillRect，窗口像素坐标）
    endScreen
  game.onToolsPass       → 开发工具 UI（ImGui，交换链 Load/Store 的独立渲染通道，ADR-0026）
  Renderer2D::endFrame   → 一次性上传本帧所有精灵顶点
Device::submit(CommandList) → [截图 readTexture] → Device::endFrame（呈现）
```

## 3. RHI 约定（Vulkan 语义）

| 概念 | 约定 |
|---|---|
| 资源 | 通过代际句柄（`Handle<Tag>`）引用；过期句柄永远解析失败。创建失败返回无效句柄并记录日志。 |
| 管线 | `GraphicsPipelineDesc` 一次性描述着色器、顶点布局、拓扑、剔除、混合与颜色目标格式，创建后不可变。 |
| 资源绑定 | 着色器用 `layout(set, binding)`；`BindGroup` 对应 Vulkan descriptor set，布局取自管线，必须提供该 set 的全部绑定；着色器没有声明的条目被忽略（着色器可以不改代码就去掉一个资源）。上限：4 个 set × 每 set 8 个 binding。 |
| 命令 | `CommandList` 只记录、与后端无关；`Device::submit` 时由后端翻译。`beginRenderPass` 会把视口与裁剪重置为整个附件，pass 内的管线 / 缓冲 / 绑定必须重新设置。 |
| 数据写入 | `writeBuffer` / `writeTexture` 按队列顺序生效：对之后提交的所有命令可见，与命令的录制时间无关。同一提交前重复写同一区域，只有最后一次可见。 |
| 坐标 | 裁剪空间深度 [0, 1]；帧缓冲、视口、裁剪、纹理坐标的原点都在左上；纹理第 0 行是顶行。正面朝向按屏幕上看到的方向定义。 |
| 帧 | `beginFrame → submit → endFrame`。`DeviceCaps::framesInFlight` 表示同时在途帧数（OpenGL 为 1，Vulkan 为 2）。销毁的资源可能仍被在途帧引用，由后端延迟释放。每帧的数据写入必须发生在 `beginFrame` 与 `submit` 之间；帧外写入走同步慢路径，只适合加载阶段。 |
| 交换链 | `swapchainTexture()` 是后端持有的 RGBA8 离屏图像，`endFrame` 时复制到窗口；所有后端的 `swapchainFormat()` 都是 RGBA8。 |
| 读回 | `readTexture` 阻塞等待 GPU，只用于截图和渲染测试。 |
| 一致性 | `AnimalRhiConformance` 为每项 RHI 功能画一个格子，两后端逐像素一致且与基准相同（渲染回归的一部分，ADR-0025）。索引三角形条带不得使用最大索引值（部分后端无法关闭图元重启）。 |
| 校验 | Debug 构建中 `createDevice` 返回 `ValidatingDevice`：后端无关地检查资源描述与提交的命令列表，两后端报同样的错误（ADR-0021）。 |
| GPU 计时 | `CommandList::beginTimer` / `endTimer`（渲染通道之外，可嵌套）；`Device::gpuTimings()` 返回最近一个结果就绪帧的各计时域，晚几帧（ADR-0016）。 |
| 着色器热重载 | `Device::reloadShader` 原地替换着色器代码，重建引用它的管线，所有句柄（着色器、管线、绑定组）保持有效；阶段或资源绑定改变、编译 / 链接失败时什么都不改并返回 false；绑定改变时由 `Renderer2D::rebuild` 另建整套资源后交换（ADR-0028）。只在两帧之间调用。 |
| 纹理重建 | `Device::recreateTexture` 在同一句柄下改变纹理尺寸 / 格式，绑定组继续有效（Vulkan 等待空闲后改写描述符）；`Device::textureExtent` 返回当前尺寸，渲染器以它计算 UV（ADR-0028）。 |

### 3.1 OpenGL 4.1 后端如何模拟这些约定

- **行序**：所有 pass 都渲染到离屏纹理，并按“第 0 行是顶行”存放（即 Vulkan 的内存布局）。因此视口 / 裁剪 y、纹理上传与读回都无需翻转；只有呈现时用 `glBlitFramebuffer` 上下翻转复制到窗口。
- **正面朝向**：在上述布局下，屏幕上的逆时针在 OpenGL 窗口坐标中是顺时针，所以 `FrontFace` 取反映射。
- **深度范围**：macOS 的 OpenGL 4.1 没有 `glClipControl`；由着色器工具（SPIRV-Cross `fixup_clipspace`）在顶点着色器末尾把 z 从 [0, 1] 转为 [-1, 1]。
- **绑定槽**：GLSL 410 不能写 `binding`，着色器工具把资源重命名为 `ubo_s<set>_b<binding>` / `tex_s<set>_b<binding>`，后端链接程序后按名字把它们接到槽位 `set × 8 + binding`。
- **顶点输入**：4.1 没有独立的顶点格式状态，切换管线或顶点缓冲后在下一次 draw 前重新指定属性。
- **多颜色附件**：单附件使用纹理自带的 framebuffer；多附件按附件组合缓存 framebuffer，任一纹理销毁时一并删除。
- **实例编号**：着色器工具关闭 SPIRV-Cross 的非零 base instance 支持，`gl_InstanceIndex` 直接对应 `gl_InstanceID`（与不支持 `firstInstance` 一致）。
- **GPU 计时**：有 `GL_TIMESTAMP` 时用时间戳查询，否则（macOS）用 `GL_TIME_ELAPSED` 链合成时间戳；macOS 上分 pass 的数值不可信（ADR-0016）。
- **限制**：不支持 `firstInstance`、计算着色器。

### 3.2 Vulkan 后端（见 ADR-0005、M2.5）

- **文件**：`vk_device.h`（内部类型与 `VulkanDevice` 声明）、`vk_device.cpp`（实例 / 设备 / 帧槽位）、`vk_swapchain.cpp`（交换链与帧循环）、`vk_resources.cpp`（内存、上传、缓冲、纹理、采样器、延迟销毁）、`vk_pipelines.cpp`（着色器、管线、描述符布局与绑定组）、`vk_commands.cpp`（提交与命令翻译）、`vk_util.cpp`（枚举转换、错误检查）。
- 基线：Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`（MoltenVK 上加 `VK_KHR_portability_subset`）；同时在途 2 帧。
- **屏障**：每个纹理记录上一次的用途（附件、采样、拷贝、呈现），按“上一次用途的写 → 下一次用途的读写”推导精确的阶段 / 访问 / 布局；连续只读不发屏障。提交时预扫描每个渲染通道，把附件与被采样纹理需要的屏障统一放在 `vkCmdBeginRenderingKHR` 之前（ADR-0023）。
- **数据写入**：帧内写入复制到每帧的暂存缓冲，并录制进上传命令缓冲，提交时排在帧命令之前；帧外写入立即执行并等待完成。
- **绑定**：由着色器包中的绑定表生成 descriptor set layout（按签名缓存，stage 统一为 VS | FS），`BindGroup` 对应一个 descriptor set。
- **交换链**：渲染到 RGBA8 离屏图像，`endFrame()` 时 blit 到真实交换链图像再呈现。
- **内存**：缓冲与图像由 VMA 子分配（设备本地优先）；暂存与读回缓冲为常驻映射的主机一致内存；每帧暂存缓冲初始 1 MiB，某帧溢出时临时单独分配，该帧槽位下次开始时扩容到够用（ADR-0022）。
- **描述符池**：每个池 128 个 set，用完时新建一个；绑定组记住自己的池以便释放。
- **销毁**：帧内销毁放入当前帧槽位的删除队列，两帧之间的销毁放入最近提交那一帧的槽位，等该槽位的 fence 完成后释放（ADR-0028 修正）。
- **着色器热重载**：管线保存创建描述（去掉调试名），用同一 layout 重建 `VkPipeline`，旧管线与着色器模块延迟销毁。
- **管线缓存**：用户数据目录中的 `vulkan_pipelines.bin`，文件头与当前设备 / 驱动一致才加载，退出时原子写回（ADR-0024）。
- **GPU 计时**：每个在途帧一个时间戳查询池，fence 完成后读回。
- **调试**：Debug 构建开启验证层与同步验证；退出时打印验证错误总数。
- **加载器**：macOS 开发期由平台层在 `$VULKAN_SDK` 等位置查找；发行版需随包携带加载器与 MoltenVK。

## 4. 着色器工具链

`shaders/*.vert|*.frag`（GLSL 450，Vulkan 风格）→ `animal_shaderc`（glslang → SPIR-V 1.5；SPIRV-Cross → GLSL 410 + 绑定表）→ `data/shaders/<name>.ashd`。包格式定义在 `engine/include/animal/rhi/shader_package.h`。目前限制：只支持合并采样器（`sampler2D`）、uniform buffer；push constant 与存储缓冲会被工具拒绝并给出提示。

## 5. 渲染约定（PROJECT.md §7.2）

| 项 | 约定 |
|---|---|
| 逻辑画布 | 320×180，RGBA8，最近邻采样。 |
| 呈现缩放 | 取能放下的最大整数倍，居中，其余留黑边；窗口小于画布时按 1 倍居中裁切。窗口坐标 → 画布坐标统一用 `windowToCanvas`。 |
| 相机 | 整数像素相机：`Camera2D::center` 先四舍五入再构建视图矩阵。 |
| 精灵位置 | 世界 pass 中 `drawSprite` 把位置四舍五入到整像素；模拟可以使用浮点位置。 |
| Alpha | **预乘 Alpha**。颜色纹理在加载时预乘（法线、自发光等数据纹理不预乘）；API 接收直通 Alpha 的 `Color`，写入顶点前预乘；混合为 `One, OneMinusSrcAlpha`。 |
| 泛光与调色 | 后处理 pass：阈值提取亮部、160×90 两轮高斯模糊、按光照级数量化抖动后加回；曝光 / 分区色调 / 饱和度 / 对比度 / 暗角（ADR-0009）。 |
| 烟雾 | 160×90 网格的 stable fluids（平流、浮力、涡量约束、Jacobi 压力投影），片元着色器实现；合成时被同一光照照亮（ADR-0008）。 |
| 光照 | 画布分辨率的延迟光照：材质 pass 写 albedo / normal / emissive（RGBA8），光照 pass 加色累积到 RGBA16F，合成 `albedo × (ambient + light) + emissive`，可选量化 + 有序抖动（ADR-0007）。 |
| 光源类型 | `PointLight`：点光源，或设置 `coneAngle` + `direction` 成为聚光灯（锥体外缘五分之一软过渡，再被光照分级切成色阶）；每个光源可关闭阴影（ADR-0013）。 |
| 边缘光 | `SpriteMaterial::rim` 写入材质标志；光照 pass 对“朝向光源的邻居不是边缘光精灵”的像素累积光色，合成时与环境边缘光一起加色叠加，不乘 albedo（ADR-0014）。 |
| 阴影 | 画布尺寸 R8 遮挡图（`setLightOccluders`，画布空间）；光照 pass 中每个像素沿整数 Bresenham 走向光源：穿出一块遮挡物即挡住，所在遮挡物内按深度衰减（`shadowPenetration`）（ADR-0012）。 |
| 颜色空间 | 全程 UNorm、在显示空间计算（不做 sRGB 转换）。M2 评估后保留：美术、光照分级与抖动、调色都在这个空间里调好，两后端逐像素一致；改为线性空间会改变全部观感与色阶分布，收益不足。 |
| 调试文字 | 内置 3×5 像素字体（不依赖资源文件），覆盖层缩放 = 画布缩放 / 2（至少 1）。 |

### 5.1 渲染目标与 pass 一览

画布 = 320×180（`Renderer2D::kCanvasExtent`），流体网格 = 160×90（`kFluidGrid`）。“采样”一列中，`texelFetch` 与“最近邻”都按整数纹素读取。

| 目标 | 尺寸 | 格式 | 写入（pass，加载，混合） | 读取（采样） |
|---|---|---|---|---|
| albedo | 画布 | RGBA8 | 材质 pass，Clear = 背景色，预乘 over | 合成（最近邻） |
| normal | 画布 | RGBA8 | 材质 pass，Clear = (0.5, 0.5, 1, 0)，预乘 over | 光照 pass（最近邻） |
| emissive | 画布 | RGBA8 | 材质 pass，Clear = 0，预乘 over | 合成（最近邻） |
| 材质标志（r = 边缘光强度） | 画布 | RGBA8 | 材质 pass，Clear = 0，预乘 over | 光照 pass、合成（`texelFetch`） |
| 光照遮挡图 | 画布 | R8 | CPU `setLightOccluders`（进入房间时） | 光照 pass（`texelFetch`） |
| light | 画布 | RGBA16F | 光照 pass，Clear = 0，加色 One/One | 合成（最近邻） |
| rim light | 画布 | RGBA16F | 光照 pass（第二个附件），Clear = 0，加色 One/One | 合成（最近邻） |
| 流体速度 ×2、烟 ×2、压力 ×2、散度、旋度 | 流体网格 | RGBA16F | 流体各 pass，DontCare，不透明（乒乓，跨帧保留；`resetFluid` 时 Clear） | 流体各 pass（平流的速度 / 来源 / 密度为线性，其余最近邻）；烟 [0] 由合成读取（线性） |
| GPU 粒子状态 A / B ×2 | 128×128 | RGBA32F | GPU 粒子模拟，DontCare，不透明（两个附件，乒乓） | 模拟与绘制（`texelFetch`，绘制在顶点着色器中） |
| 流体障碍 | 流体网格 | R8 | CPU `setFluidObstacles` | 流体各 pass（最近邻） |
| lit（仅有水时） | 画布 | RGBA8 | 合成，DontCare，不透明 | 水 pass（`texelFetch`） |
| 水列数据 | 画布宽 × 1 | RGBA8 | CPU 每帧 `writeTexture`（水面行、水底行、样式） | 水 pass（`texelFetch`） |
| canvas | 画布 | RGBA8 | 合成（无水时）或水 pass，DontCare，不透明；发光 pass，Load，加色 One/One | 泛光亮部提取（线性）、调色（最近邻） |
| bloom ×2 | 画布 / 2 | RGBA16F | 亮部提取 / 模糊，DontCare，不透明（首次使用前 Clear） | 模糊、调色（线性） |
| final | 画布 | RGBA8 | 调色，DontCare，不透明；世界调试叠加，Load，预乘 over | 屏幕 pass（最近邻，整数倍放大） |
| swapchain（虚拟） | 窗口 | RGBA8 | 屏幕 pass，Clear = 留边色，预乘 over（画布、HUD、调试覆盖层） | 呈现（最近邻 blit）、截图读回 |

规则：

- **尺寸**：与画面像素对应的目标都是画布尺寸，1 个画布像素 = 1 个纹素；只有模拟场（流体）和模糊（泛光）使用低分辨率，读取时线性放大。
- **格式**：颜色与 [0, 1] 数据用 RGBA8；可能超过 1 的累积量（光照、边缘光、流体、泛光）用 RGBA16F；需要完整精度的模拟状态（GPU 粒子位置）用 RGBA32F；掩码用 R8。
- **混合**：材质 pass 的所有附件都是预乘 over（含材质标志，所以遮挡关系正确）；光照累积是加色；全屏 pass 不透明且覆盖整个目标，因此用 DontCare（Debug 构建的 OpenGL 后端把 DontCare 填成品红，暴露误用）。
- **生命周期**：除流体场（跨帧乒乓）外，每个目标在被读取前都会在本帧完整重写；泛光目标首次使用前清零以免读到未定义内容。
- **采样**：画布空间的读取一律最近邻 / `texelFetch`；屏幕 uv 由裁剪坐标计算（不用 `gl_FragCoord`），整数像素坐标为 `floor(uv × 尺寸)`。
- **两后端一致**：选择“哪个像素”的判定（阴影光线、边缘光邻居）只用整数运算，光源所在像素在 CPU 上算好（ADR-0012、ADR-0014），保证 OpenGL 与 Vulkan 逐像素相同。

## 6. 游戏框架（framework/，ADR-0006）

| 模块 | 约定 |
|---|---|
| 坐标与单位 | 世界单位 = 像素；速度 px/s；模拟固定 60 Hz。房间 40×22 Tile（8 px），在画布中垂直居中，上下各留 2 px。 |
| `TileMap` | Tile id 网格 + 每种 id 的“实心”属性表；地图之外视为空（留给出屏切换）。 |
| `Body` / `moveX` / `moveY` | 整数像素位置 + 小数余量；每次移动 1 px 并检测碰撞，碰到即停（贴合、不穿墙）；先 X 后 Y。 |
| `EntityWorld` | 代际 `EntityId` + 扁平 `Entity` 记录；销毁延迟到 `flushDestroyed()`。按需加字段，不做通用 ECS。 |
| `ActionMap` | 游戏动作绑定到键盘键、手柄按键和摇杆方向；每帧在 `onFrame` 中采样，“按下”会锁存到被一个固定步消费（`endFixedStep()`），因此每次按下恰好被一个模拟步看到。 |
| `stepPlatformer` | 跑动加减速（地面 / 空中不同）、重力（下落加重、顶点减轻）、coyote time、跳跃缓冲、可变跳高、天花板角修正（≤ 2 px）。参数全部在 `PlatformerParams`；落地时记录 `landingSpeed`。 |
| `GrassField` / `PlantSpecies` | 植物：每根是带阻尼的角度弹簧；`*.plant.json` 种类（高度、密度、直立 / 下垂、弹簧、颜色、发光花）在区域内暴露的 Tile 顶面或底面生长（`plant`），Tile 属性 `grass` 用 0 号种类。目标角度 = 风 + 抖动 + 影响者推开；落地给附近草叶角速度冲量（ADR-0019）。 |
| 风场 | `windVectorAt(位置, 时间)`：基础微风 + 阵风前锋 + 局部气流（`WindSource`），草、藤蔓、粒子共用（ADR-0019）。 |
| `VineSystem` | Verlet 链条，第 0 点固定在锚点。每个固定步拆成 4 个子步，每个子步做正向 + 反向交替的距离约束迭代，使任意位置的推动沿整根藤蔓双向传播（整体摆动，而不是只有末端甩动），且不被重力拉长。风对整根藤蔓施加均匀的力（取锚点处的风值）。与影响者的接触是力而不是位置修正（按穿透深度朝较近的一侧推，并把被碰到的链节速度向影响者速度混合、有上限），所以藤蔓有重量：被推时倾斜到重力与张力平衡，角色中心越过后藤蔓滑到其身后。暂不与 Tile 碰撞，游戏按下方第一个实心 Tile 计算藤蔓长度。 |
| `ParticleSystem` | 通用粒子（`EffectSystem` 的底层）：恒定加速度、阻力、风、平滑游走、亮度闪烁、颜色随寿命插值与淡入淡出、被 `Influence` 推开、碰到实心 Tile 消失；效果预设用这些参数组合出萤火虫、火星、灰尘。随机数用 `animal::Random`（PCG32，跨平台一致）。 |
| `stepClimb` | 藤蔓攀爬：按住上且碰撞框附近有藤蔓时抓住（`VineSystem::findGrip`）；沿弧长上下爬、左右输入与体重作为力施加到抓点；角色按精确（亚像素）位置以逐像素碰撞跟随抓点（按整数位置跟随会让角色在两个像素间抖动）；被抓住的藤蔓用更轻的阻尼（`heldDamping`），抓点在约束中视为重物（`climberInverseMass`），所以下方的藤尾跟随角色、左右按键能把摆荡逐渐荡大；攀爬中的角色不作为推动藤蔓的影响者（`Influence::pushesVines`）；跳离时继承藤蔓速度并有再抓冷却；底端按下松手。在平台跳跃步之前调用，攀爬时跳过平台跳跃步。 |
| `Animator` | 片段 = 帧序列 + 帧率（或逐帧时长）+ 是否循环；`play` 对正在播放的片段无操作。状态机在游戏中：攀爬 → climb（仅移动时播放），落地 → land，地面 → run / idle，空中 → jump / fall。 |
| `SpriteSheet` | Aseprite 精灵表导出（`.json` + `.png`）：帧矩形 / 裁剪偏移 / 时长与标签；每个标签是一个 `AnimationClip`，时长相同的标签转为固定帧率（ADR-0030）。 |
| `Tuning` | 代码登记的命名参数（float / int / bool + 滑条范围）；`world/tuning.json` 只记录与内置值不同的项，可试运行校验（ADR-0027）。 |
| `LightAnimation` | 光源亮度动画（`none` / `flicker` / `pulse`）：只由时间和每个光源的相位计算出倍数，确定、无状态；倍数保持在 1 ± amount 之内（ADR-0013）。 |
| `WaterBody` / `swimmingParams` | 水域：每像素列的一维表面波（扰动、水面高度、淹没比例）；漂浮：按淹没比例改写平台跳跃参数（重力变浮力、减速）并施加水阻，够深时可起跳出水（ADR-0018）。 |
| `EffectSystem` | 粒子效果：`*.effect.json` 预设（发射、运动、外观、随粒子的光，严格校验）+ 房间中的点 / 区域发射器；按发射器保持数量、按速率发射、一次性爆发；可整体开关，报告各效果数量与 CPU 耗时（ADR-0017）。 |
| `Look` | 观感预设：光照、边缘光、泛光与调色、烟雾颜色的扁平字段表；`*.look.json` 可继承（`base`）、严格校验；房间用 Tiled `look` / `look.*` 引用与覆盖；`mixLooks` 用于房间切换过渡（ADR-0015）。 |
| `TriggerSet` | 矩形触发区域；`update(碰撞框)` 返回本步刚进入的触发器，离开后重新进入会再次触发，`once` 只触发一次（ADR-0011）。 |
| `stepPatrol` | 地面巡逻：着地时前方脚下没有实心 Tile（可关闭）或撞墙就转身，悬空时按重力下落；逐像素碰撞同 `Body`。 |
| `SaveData` / `readSave` / `writeSave` | 版本化 JSON 存档（房间、位置、标志集合）；原子写入（临时文件 + 重命名）；格式、版本、字段错误时拒绝并给出原因（ADR-0011）。 |
| `level`（Tiled） | `loadWorld` / `loadRoom` 读取 `.world` / `.tmj` / `.tsj`；`Room` 含 `TileMap`（实心来自 Tile 属性）、按 Tile id 的属性、对象与地图属性；错误带文件名与字段（ADR-0010）。 |
| `Influence` | 推动植被与粒子的对象（包围盒 + 速度 + 是否刚落地），由游戏每步提供；模拟与绘制分离，框架不依赖渲染器。 |

## 7. 运行时数据路径

开发期数据在可执行文件旁的 `data/`（`build/<preset>/bin/<Config>/data/`），由 CMake 生成 / 复制：`data/shaders/*.ashd` 来自 `shaders/`，`data/textures/*` 来自 `assets/source/textures/`，`data/world/*` 来自 `assets/source/world/`。路径通过 `SDL_GetBasePath()` 计算，不依赖工作目录。M5 起改为 Asset Cooker 生成的资源包。

开发构建中源文件是唯一真相（ADR-0026）：工具（面板保存、关卡编辑器、参数表）同时写源文件与部署副本；热重载监视源文件，变化时复制（或编译着色器）到 `data/` 再载入，被拒绝的数据会恢复部署副本（ADR-0028）。
