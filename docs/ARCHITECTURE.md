# 架构与渲染约定

本文件描述代码的实际结构和必须遵守的约定，是 [PROJECT.md](../PROJECT.md) §3 与 §7 的落地说明。改变这里的任何约定都要同步更新本文件（必要时写 ADR）。

## 1. 模块与依赖

```text
game/            AnimalGame（M0 沙盒）         ─┐
engine/                                         │ 只能向下依赖
  app/           主循环、固定时间步、调试覆盖层  │
  renderer/      Renderer2D、SpriteBatch、画布   │
  resources/     图片 / 纹理 / 着色器包加载      │
  rhi/           图形抽象 + 后端（opengl/）      │
  platform/      SDL3：窗口、输入、事件、时间    │
  core/          日志、断言、句柄、数学、文件   ─┘
tools/shaderc/   构建期着色器编译器（不进入游戏）
shaders/         GLSL 450 着色器源码
```

- `engine/src/rhi/opengl/` 是唯一可以包含 OpenGL 头文件、调用 `gl*` 的目录。
- RHI 公共头文件（`animal/rhi/*.h`）不暴露任何后端类型。
- SDL 只出现在 `platform/`、`core/`（日志、文件）、RHI 后端（上下文 / Surface）和 `app/entry_point.h`。

## 2. 帧流程

```text
platform::pollEvents → Input::update → 固定步长 onFixedUpdate（60 Hz，每帧最多 5 步，超出丢弃积压）
Device::beginFrame（窗口最小化时返回 false，跳过渲染）
  Renderer2D::beginFrame
    game.onRender:  beginWorld(camera) … drawSprite … endWorld        → 320×180 画布
    beginScreen:    画布按整数倍居中绘制到 swapchain，剩余留黑边
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
| 帧 | `beginFrame → submit → endFrame`。`DeviceCaps::framesInFlight` 表示同时在途帧数（OpenGL 为 1）。销毁的资源可能仍被在途帧引用，由后端延迟释放。 |
| 读回 | `readTexture` 阻塞等待 GPU，只用于截图和渲染测试。 |

### 3.1 OpenGL 4.1 后端如何模拟这些约定

- **行序**：所有 pass 都渲染到离屏纹理，并按“第 0 行是顶行”存放（即 Vulkan 的内存布局）。因此视口 / 裁剪 y、纹理上传与读回都无需翻转；只有呈现时用 `glBlitFramebuffer` 上下翻转复制到窗口。
- **正面朝向**：在上述布局下，屏幕上的逆时针在 OpenGL 窗口坐标中是顺时针，所以 `FrontFace` 取反映射。
- **深度范围**：macOS 的 OpenGL 4.1 没有 `glClipControl`；由着色器工具（SPIRV-Cross `fixup_clipspace`）在顶点着色器末尾把 z 从 [0, 1] 转为 [-1, 1]。
- **绑定槽**：GLSL 410 不能写 `binding`，着色器工具把资源重命名为 `ubo_s<set>_b<binding>` / `tex_s<set>_b<binding>`，后端链接程序后按名字把它们接到槽位 `set × 8 + binding`。
- **顶点输入**：4.1 没有独立的顶点格式状态，切换管线或顶点缓冲后在下一次 draw 前重新指定属性。
- **限制**：不支持 `firstInstance`、计算着色器、多颜色附件（M2 需要时补上 MRT）。

### 3.2 Vulkan 后端（M2.5）需要补上的部分

- 在 pass 之间按纹理用途自动插入布局转换与屏障（例如画布从“渲染目标”变为“被采样”）。
- `writeBuffer` / `writeTexture` 通过 staging buffer 与传输命令实现，保持队列顺序语义。
- 由着色器包中的绑定表创建 descriptor set layout 与 pipeline layout。
- 资源销毁延迟到引用它的帧完成。
- macOS 通过 MoltenVK 运行，只使用其支持的特性。

## 4. 着色器工具链

`shaders/*.vert|*.frag`（GLSL 450，Vulkan 风格）→ `animal_shaderc`（glslang → SPIR-V 1.5；SPIRV-Cross → GLSL 410 + 绑定表）→ `data/shaders/<name>.ashd`。包格式定义在 `engine/include/animal/rhi/shader_package.h`。目前限制：只支持合并采样器（`sampler2D`）、uniform buffer；push constant 与存储缓冲会被工具拒绝并给出提示。

## 5. 渲染约定（PROJECT.md §7.2）

| 项 | 约定 |
|---|---|
| 逻辑画布 | 320×180，RGBA8，最近邻采样。 |
| 呈现缩放 | 取能放下的最大整数倍，居中，其余留黑边；窗口小于画布时按 1 倍居中裁切。窗口坐标 → 画布坐标统一用 `windowToCanvas`。 |
| 相机 | 整数像素相机：`Camera2D::center` 先四舍五入再构建视图矩阵。 |
| 精灵位置 | 世界 pass 中 `drawSprite` 把位置四舍五入到整像素；模拟可以使用浮点位置。 |
| Alpha | **预乘 Alpha**。纹理在加载时预乘；API 接收直通 Alpha 的 `Color`，写入顶点前预乘；混合为 `One, OneMinusSrcAlpha`。 |
| 颜色空间 | 目前全程 UNorm（不做 sRGB 转换）。M2 光照前需要重新评估。 |
| 调试文字 | 内置 3×5 像素字体（不依赖资源文件），覆盖层缩放 = 画布缩放 / 2（至少 1）。 |

## 6. 运行时数据路径

开发期数据在可执行文件旁的 `data/`（`build/<preset>/bin/<Config>/data/`），由 CMake 生成 / 复制：`data/shaders/*.ashd` 来自 `shaders/`，`data/textures/*.png` 来自 `assets/source/textures/`。路径通过 `SDL_GetBasePath()` 计算，不依赖工作目录。M5 起改为 Asset Cooker 生成的资源包。
