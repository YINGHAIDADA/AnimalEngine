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
shaders/         GLSL 450 着色器源码
```

- `engine/src/rhi/opengl/` 与 `engine/src/rhi/vulkan/` 是仅有的可以包含图形 API 头文件、调用 `gl*` / `vk*` 的目录。
- RHI 公共头文件（`animal/rhi/*.h`）不暴露任何后端类型。
- SDL 只出现在 `platform/`、`core/`（日志、文件）、RHI 后端（上下文 / Surface）和 `app/entry_point.h`。

## 2. 帧流程（光照细节见 ADR-0007）

```text
platform::pollEvents → Input::update → 固定步长 onFixedUpdate（60 Hz，每帧最多 5 步，超出丢弃积压）
Device::beginFrame（窗口最小化时返回 false，跳过渲染）
  Renderer2D::beginFrame
    game.onRender:  beginWorld(camera) … drawSprite / drawGlow / addLight / addFluidSplat … endWorld
                    → 材质 pass（albedo / normal / emissive）→ 流体烟雾一步（ADR-0008）
                    → 光照 pass（RGBA16F）→ 合成到 320×180 画布（含被照亮的烟）
                    → 发光 pass（drawGlow 收集的像素，加色混合叠加到画布，不受光照影响）
                    → 后处理：泛光（亮部提取 → 半分辨率模糊 → 量化抖动加回）+ 调色 → 最终画布（ADR-0009）
                    [beginWorldOverlay … 调试图形 … endWorldOverlay]（无光照、不调色，叠加在最终画布上）
    beginScreen:    最终画布按整数倍居中绘制到 swapchain，剩余留黑边
                    调试覆盖层（drawText / fillRect，窗口像素坐标）
    endScreen
  Renderer2D::endFrame   → 一次性上传本帧所有精灵顶点
Device::submit(CommandList) → [截图 readTexture] → Device::endFrame（呈现）
```

## 3. RHI 约定（Vulkan 语义）

| 概念 | 约定 |
|---|---|
| 资源 | 通过代际句柄（`Handle<Tag>`）引用；过期句柄永远解析失败。创建失败返回无效句柄并记录日志。 |
| 管线 | `GraphicsPipelineDesc` 一次性描述着色器、顶点布局、拓扑、剔除、混合与颜色目标格式，创建后不可变。 |
| 资源绑定 | 着色器用 `layout(set, binding)`；`BindGroup` 对应 Vulkan descriptor set，布局取自管线，必须提供该 set 的全部绑定。上限：4 个 set × 每 set 8 个 binding。 |
| 命令 | `CommandList` 只记录、与后端无关；`Device::submit` 时由后端翻译。`beginRenderPass` 会把视口与裁剪重置为整个附件，pass 内的管线 / 缓冲 / 绑定必须重新设置。 |
| 数据写入 | `writeBuffer` / `writeTexture` 按队列顺序生效：对之后提交的所有命令可见，与命令的录制时间无关。同一提交前重复写同一区域，只有最后一次可见。 |
| 坐标 | 裁剪空间深度 [0, 1]；帧缓冲、视口、裁剪、纹理坐标的原点都在左上；纹理第 0 行是顶行。正面朝向按屏幕上看到的方向定义。 |
| 帧 | `beginFrame → submit → endFrame`。`DeviceCaps::framesInFlight` 表示同时在途帧数（OpenGL 为 1，Vulkan 为 2）。销毁的资源可能仍被在途帧引用，由后端延迟释放。每帧的数据写入必须发生在 `beginFrame` 与 `submit` 之间；帧外写入走同步慢路径，只适合加载阶段。 |
| 交换链 | `swapchainTexture()` 是后端持有的 RGBA8 离屏图像，`endFrame` 时复制到窗口；所有后端的 `swapchainFormat()` 都是 RGBA8。 |
| 读回 | `readTexture` 阻塞等待 GPU，只用于截图和渲染测试。 |

### 3.1 OpenGL 4.1 后端如何模拟这些约定

- **行序**：所有 pass 都渲染到离屏纹理，并按“第 0 行是顶行”存放（即 Vulkan 的内存布局）。因此视口 / 裁剪 y、纹理上传与读回都无需翻转；只有呈现时用 `glBlitFramebuffer` 上下翻转复制到窗口。
- **正面朝向**：在上述布局下，屏幕上的逆时针在 OpenGL 窗口坐标中是顺时针，所以 `FrontFace` 取反映射。
- **深度范围**：macOS 的 OpenGL 4.1 没有 `glClipControl`；由着色器工具（SPIRV-Cross `fixup_clipspace`）在顶点着色器末尾把 z 从 [0, 1] 转为 [-1, 1]。
- **绑定槽**：GLSL 410 不能写 `binding`，着色器工具把资源重命名为 `ubo_s<set>_b<binding>` / `tex_s<set>_b<binding>`，后端链接程序后按名字把它们接到槽位 `set × 8 + binding`。
- **顶点输入**：4.1 没有独立的顶点格式状态，切换管线或顶点缓冲后在下一次 draw 前重新指定属性。
- **多颜色附件**：单附件使用纹理自带的 framebuffer；多附件按附件组合缓存 framebuffer，任一纹理销毁时一并删除。
- **限制**：不支持 `firstInstance`、计算着色器。

### 3.2 Vulkan 后端（验证版，见 ADR-0005）

- 基线：Vulkan 1.2 + `VK_KHR_dynamic_rendering` + `VK_KHR_synchronization2`（MoltenVK 上加 `VK_KHR_portability_subset`）；同时在途 2 帧。
- **布局转换**：每个纹理记录当前布局。提交时预扫描每个渲染通道，把附件与被采样纹理需要的转换统一放在 `vkCmdBeginRenderingKHR` 之前。
- **数据写入**：帧内写入复制到每帧的暂存缓冲，并录制进上传命令缓冲，提交时排在帧命令之前；帧外写入立即执行并等待完成。
- **绑定**：由着色器包中的绑定表生成 descriptor set layout（按签名缓存，stage 统一为 VS | FS），`BindGroup` 对应一个 descriptor set。
- **交换链**：渲染到 RGBA8 离屏图像，`endFrame()` 时 blit 到真实交换链图像再呈现。
- **销毁**：放入当前帧槽位的删除队列，等该槽位的 fence 完成后释放。
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
| 颜色空间 | 目前全程 UNorm（不做 sRGB 转换）。M2 光照前需要重新评估。 |
| 调试文字 | 内置 3×5 像素字体（不依赖资源文件），覆盖层缩放 = 画布缩放 / 2（至少 1）。 |

## 6. 游戏框架（framework/，ADR-0006）

| 模块 | 约定 |
|---|---|
| 坐标与单位 | 世界单位 = 像素；速度 px/s；模拟固定 60 Hz。房间 40×22 Tile（8 px），在画布中垂直居中，上下各留 2 px。 |
| `TileMap` | Tile id 网格 + 每种 id 的“实心”属性表；地图之外视为空（留给出屏切换）。 |
| `Body` / `moveX` / `moveY` | 整数像素位置 + 小数余量；每次移动 1 px 并检测碰撞，碰到即停（贴合、不穿墙）；先 X 后 Y。 |
| `EntityWorld` | 代际 `EntityId` + 扁平 `Entity` 记录；销毁延迟到 `flushDestroyed()`。按需加字段，不做通用 ECS。 |
| `ActionMap` | 游戏动作绑定到键盘键、手柄按键和摇杆方向；每帧在 `onFrame` 中采样，“按下”会锁存到被一个固定步消费（`endFixedStep()`），因此每次按下恰好被一个模拟步看到。 |
| `stepPlatformer` | 跑动加减速（地面 / 空中不同）、重力（下落加重、顶点减轻）、coyote time、跳跃缓冲、可变跳高、天花板角修正（≤ 2 px）。参数全部在 `PlatformerParams`；落地时记录 `landingSpeed`。 |
| `GrassField` | 每根草叶是带阻尼的角度弹簧，按固定种子哈希生成在露出的草地 Tile 顶部。目标角度 = 风 + 影响者推开（远离其中心，含拖拽）；落地给附近草叶角速度冲量。逐像素绘制，叶尖按角度二次弯曲。 |
| `VineSystem` | Verlet 链条，第 0 点固定在锚点。每个固定步拆成 4 个子步，每个子步做正向 + 反向交替的距离约束迭代，使任意位置的推动沿整根藤蔓双向传播（整体摆动，而不是只有末端甩动），且不被重力拉长。风对整根藤蔓施加均匀的力（取锚点处的风值）。与影响者的接触是力而不是位置修正（按穿透深度朝较近的一侧推，并把被碰到的链节速度向影响者速度混合、有上限），所以藤蔓有重量：被推时倾斜到重力与张力平衡，角色中心越过后藤蔓滑到其身后。暂不与 Tile 碰撞，游戏按下方第一个实心 Tile 计算藤蔓长度。 |
| `ParticleSystem` | 通用粒子：恒定加速度、阻力、风、平滑游走、亮度闪烁、颜色随寿命插值与淡入淡出、被 `Influence` 推开、碰到实心 Tile 消失；游戏用这些参数组合出萤火虫、火星、灰尘。随机数用 `animal::Random`（PCG32，跨平台一致）。 |
| `stepClimb` | 藤蔓攀爬：按住上且碰撞框附近有藤蔓时抓住（`VineSystem::findGrip`）；沿弧长上下爬、左右输入与体重作为力施加到藤蔓上；角色以逐像素碰撞跟随抓握点；跳离时继承藤蔓速度并有再抓冷却；底端按下松手。在平台跳跃步之前调用，攀爬时跳过平台跳跃步。 |
| `Animator` | 片段 = 帧序列 + 帧率 + 是否循环；`play` 对正在播放的片段无操作。状态机在游戏中：攀爬 → climb（仅移动时播放），落地 → land，地面 → run / idle，空中 → jump / fall。 |
| `TriggerSet` | 矩形触发区域；`update(碰撞框)` 返回本步刚进入的触发器，离开后重新进入会再次触发，`once` 只触发一次（ADR-0011）。 |
| `stepPatrol` | 地面巡逻：着地时前方脚下没有实心 Tile（可关闭）或撞墙就转身，悬空时按重力下落；逐像素碰撞同 `Body`。 |
| `SaveData` / `readSave` / `writeSave` | 版本化 JSON 存档（房间、位置、标志集合）；原子写入（临时文件 + 重命名）；格式、版本、字段错误时拒绝并给出原因（ADR-0011）。 |
| `level`（Tiled） | `loadWorld` / `loadRoom` 读取 `.world` / `.tmj` / `.tsj`；`Room` 含 `TileMap`（实心来自 Tile 属性）、按 Tile id 的属性、对象与地图属性；错误带文件名与字段（ADR-0010）。 |
| `Influence` | 推动植被与粒子的对象（包围盒 + 速度 + 是否刚落地），由游戏每步提供；模拟与绘制分离，框架不依赖渲染器。 |

## 7. 运行时数据路径

开发期数据在可执行文件旁的 `data/`（`build/<preset>/bin/<Config>/data/`），由 CMake 生成 / 复制：`data/shaders/*.ashd` 来自 `shaders/`，`data/textures/*.png` 来自 `assets/source/textures/`。路径通过 `SDL_GetBasePath()` 计算，不依赖工作目录。M5 起改为 Asset Cooker 生成的资源包。
