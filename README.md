# Animal Engine

C++20 专用 2D 像素探索游戏引擎。项目目标、范围与里程碑见 [PROJECT.md](PROJECT.md)，架构与渲染约定见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，关键决策见 [docs/ADR/](docs/ADR/)。

当前阶段：**M3 模拟效果生产化已完成**（macOS）——流体休眠与提速、粒子效果预设与发射器、水、风场与植物种类、GPU 粒子（瀑布）；按计划接下来是 M2.5 Vulkan 后端完善（M2 光照生产化已在 macOS 上完成）（M1b 游戏结构与 V1 视觉样板已完成）（M0、M1a 已在 macOS 上完成；Windows 验证待做）。美术资源规范见 [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md)。

## 环境要求

| 平台 | 工具 |
|---|---|
| macOS 13.3+ | Xcode（Apple Clang）、CMake ≥ 3.25、Ninja（可选） |
| Windows 10+ x64 | Visual Studio 2022（MSVC v143）、CMake ≥ 3.25（VS 自带即可）、Ninja（可选） |

所有第三方依赖以子模块形式随仓库固定版本（见 [external/README.md](external/README.md)），不需要安装 SDL 或 Vulkan SDK。

```bash
git clone <repo-url> AnimalEngine
cd AnimalEngine
git submodule update --init --depth 1
```

## 构建

所有工程文件都由 CMake 生成到 `build/<preset>/`，不要提交、也不要在 IDE 中手改工程设置。

```bash
# macOS · Ninja
cmake --preset macos-ninja-debug
cmake --build --preset macos-ninja-debug

# macOS · Xcode（生成后可用 Xcode 打开，scheme 选 animal_game）
cmake --preset macos-xcode
open build/macos-xcode/AnimalEngine.xcodeproj
cmake --build --preset macos-xcode-debug          # 或命令行构建

# Windows · Visual Studio 2022（animal_game 已设为启动项目）
cmake --preset windows-vs2022
start build\windows-vs2022\AnimalEngine.sln
cmake --build --preset windows-vs2022-debug       # 或命令行构建

# Windows · Ninja（在 "x64 Native Tools Command Prompt for VS 2022" 中）
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug
```

产物统一位于 `build/<preset>/bin/<Config>/`：可执行文件 `AnimalGame`，运行时数据在旁边的 `data/`（着色器包与纹理，由构建自动生成/复制）。

## 运行

```bash
./build/macos-ninja-debug/bin/Debug/AnimalGame
```

| 键盘 | 手柄 | 作用 |
|---|---|---|
| 方向键 / A D | 左摇杆 / 十字键 | 跑动；在藤蔓上荡动藤蔓 |
| ↑ / W（靠近藤蔓） | 摇杆上 / 十字键上 | 抓住藤蔓；在藤蔓上 ↑↓ 爬，底端按 ↓ 松手 |
| Space / Z | A（Xbox）/ ×（PlayStation） | 跳跃（按住跳得更高）；在藤蔓上跳离，可带方向 |
| F1 | | 开关调试覆盖层 |
| F2 | | 开关碰撞视图（实心 Tile 与角色碰撞框，绿色 = 着地） |
| F3 | | 开关光照（关闭时显示原始颜色） |
| F4 | | 切换光照量化：房间观感 / 平滑 / 3 / 5 / 8 级 |
| F5 | | 开关流体烟雾 |
| F6 | | 开关泛光与调色 |
| F7 | | 开关阴影 |
| F8 | | 删除存档，从起点重新开始 |
| F9 | | 重新读取观感预设（`data/world/looks/*.look.json`，可直接编辑后按 F9 查看） |
| F10 | | 开关粒子效果（覆盖层显示各效果数量与 CPU 耗时） |
| F12 | | 截图到 `screenshots/` |
| Esc | | 退出 |

命令行参数：

| 参数 | 作用 |
|---|---|
| `--backend=opengl\|vulkan` | 选择图形后端（Vulkan 需要 Vulkan 加载器；macOS 开发期使用 Vulkan SDK） |
| `--no-vsync` | 关闭垂直同步（自动化运行请始终加上，见 ADR-0005） |
| `--hide-overlay` | 隐藏调试覆盖层（用于后端截图对比） |
| `--resize-test` | 运行中多次改变窗口尺寸，验证交换链重建 |
| `--deterministic` | 每帧恰好推进一个固定步，使动画与截图可复现（截图对比时使用） |
| `--demo` | 游戏用脚本输入自己操作：抓住入口的长藤蔓爬升并荡动、跳离，经隧道进入水晶长廊、坠入烟雾深坑；配合 `--deterministic` 可复现交互截图 |
| `--frames=N` | 渲染 N 帧后退出 |
| `--screenshot=path.png` | 与 `--frames` 合用，在最后一帧截图（用于验收与渲染回归） |
| `--canvas-screenshot=path.png` | 与 `--frames` 合用，保存最后一帧的 320×180 最终画布（与窗口大小、DPI 无关，渲染回归使用） |
| `--profile` | 退出时打印帧时间与各渲染 pass 的 GPU 耗时（平均 / 最大，跳过 60 帧预热；见 ADR-0016） |
| `--stress-lights=N` | （示例游戏）额外加入 N 盏游走的阴影光源，用于性能测试 |
| `--start-room=NAME` | （示例游戏）从指定房间开始（如 `smoke_pit`），用于测试；不读写存档 |

玩法：收集散落在三个房间中的 7 颗宝石（右上角计数）；紫色甲虫在地面巡逻，碰到会被弹开；走进提示区域时屏幕下方显示提示；烟雾深坑右下角有水池（上方有一道 GPU 粒子瀑布），掉进去会漂浮在水面，按跳跃出水。拾取宝石、进入新房间后第一次站稳时自动保存，下次启动从该处继续（存档位置见 ADR-0011；`--demo` / `--deterministic` 不读写存档）。

## 编辑关卡（Tiled）

关卡位于 `assets/source/world/`：用 [Tiled](https://www.mapeditor.org/) 打开 `test.world` 即可看到并编辑所有房间。约定（Tile 属性、对象类型、地图属性）见 [ADR-0010](docs/ADR/ADR-0010-tiled-levels.md)，玩法对象（触发器、宝石、甲虫）见 [ADR-0011](docs/ADR/ADR-0011-gameplay-elements-and-save.md)，房间观感（`look` 预设与 `look.*` 覆盖，预设位于 `looks/`）见 [ADR-0015](docs/ADR/ADR-0015-looks.md)，粒子效果（`emitter` 对象，预设位于 `effects/`）见 [ADR-0017](docs/ADR/ADR-0017-effect-presets.md)，水域（`water` 对象）见 [ADR-0018](docs/ADR/ADR-0018-water.md)，植物（`plants` 对象，种类位于 `plants/`）与风（`wind.*` 地图属性、`wind` 对象）见 [ADR-0019](docs/ADR/ADR-0019-wind-and-plants.md)。保存后重新构建即可部署到 `data/world/`。目前这些文件由 `tools/scripts/make_test_world.py` 生成；开始在 Tiled 中手工编辑后请不要再运行该脚本。

### 关卡数据速查

| 放在哪里 | 名称 | 作用 | 详见 |
|---|---|---|---|
| Tileset Tile 属性 | `solid`、`grass`、`light*` | 碰撞、长草（`plants/grass.plant.json`）、发光 Tile | ADR-0010、0013 |
| 地图属性 | `look`、`look.*` | 观感预设与单项覆盖 | ADR-0015 |
| 地图属性 | `wind.strength`、`wind.gusts`、`wind.gustInterval`、`wind.gustSpeed` | 微风与阵风 | ADR-0019 |
| 点对象 | `spawn`、`candle`、`vent`、`vine`、`light`、`lantern`、`gem`、`crawler`、`wind` | 起点、道具、光源、宝石、甲虫、气流 | ADR-0010、0011、0013、0019 |
| 矩形对象 | `trigger`、`water`、`plants`、`emitter`（也可为点） | 提示区域、水域、植物、粒子发射器 | ADR-0011、0017、0018、0019、0020 |
| 预设文件 | `looks/*.look.json`、`effects/*.effect.json`（`"gpu": true` 走 GPU）、`plants/*.plant.json` | 可复用的观感、粒子效果、植物种类；格式错误会给出文件名与字段 | ADR-0015、0017、0019、0020 |

## 测试

```bash
ctest --preset macos-ninja-debug        # 或 windows-vs2022-debug / windows-ninja-debug / macos-xcode-debug
```

渲染回归（在两个后端上跑 6 个固定场景，读回 320×180 最终画布：两后端必须逐像素相同，并与 `tests/golden/` 中的基准截图逐像素相同；Vulkan 验证层必须零报错）：

```bash
python3 tools/scripts/render_regression.py            # 全部场景
python3 tools/scripts/render_regression.py --only demo_pit
python3 tools/scripts/render_regression.py --update   # 有意改变画面后刷新基准截图（提交时可见哪些画面变了）
```

后端一致性（OpenGL 与 Vulkan 截图逐像素对比）：

```bash
B=build/macos-ninja-debug/bin/Debug/AnimalGame
$B --backend=opengl --no-vsync --deterministic --hide-overlay --frames=120 --screenshot=screenshots/gl.png
$B --backend=vulkan --no-vsync --deterministic --hide-overlay --frames=120 --screenshot=screenshots/vk.png
python3 tools/scripts/compare_images.py screenshots/gl.png screenshots/vk.png
```

性能（Release；macOS 上分 pass 数据以 Vulkan 为准，预算见 ADR-0016）：

```bash
R=build/macos-ninja-release/bin/Release/AnimalGame
$R --backend=vulkan --demo --deterministic --no-vsync --hide-overlay --profile --frames=600
$R --backend=vulkan --demo --deterministic --no-vsync --hide-overlay --profile --frames=600 --stress-lights=256
```

## M0 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| macOS（Ninja、Xcode）干净构建并运行 | 删除 `build/` 后按上文构建 | ✅ 已验证（Apple M2，macOS 27） |
| Windows（VS 2022、Ninja）干净构建并运行 | 同上 | ⏳ 待在 Windows 机器上验证 |
| 一个房间中绘制 Tile 和可移动角色 | 运行 `AnimalGame` | ✅ |
| 窗口缩放无模糊、无意外拉伸（整数倍 + 留黑边） | 拖动窗口大小；单元测试 `present_layout_*` | ✅ |
| 显示 FPS、帧时间、渲染尺寸、缩放倍率、当前后端 | F1 覆盖层 | ✅ |
| RHI 后端目录之外没有图形 API 调用 | `engine/src/rhi/opengl/`、`engine/src/rhi/vulkan/` 之外搜索 `gl[A-Z]` / `vk[A-Z]` 无结果 | ✅ |
| OpenGL 与 Vulkan 输出一致 | 见上文“后端一致性”，容差 0 | ✅（macOS / MoltenVK） |
| 单元测试通过 | `ctest` | ✅ |
| 两个平台的验收截图 | `AnimalGame --frames=60 --screenshot=screenshots/m0_<platform>.png` | macOS ✅ / Windows ⏳ |

全部通过后打 tag `milestone/m0-runtime`。

## V1 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 光源照亮法线贴图场景，自发光与像素化明暗过渡 | 运行 `AnimalGame`，F3 / F4 对比 | ✅ |
| 草被角色拨开、藤蔓被撞动并随风摆动 | `AnimalGame --demo` | ✅ |
| 萤火虫 / 火星 / 灰尘粒子，萤火虫照亮周围 | 运行 `AnimalGame` | ✅ |
| 通风口烟雾被光照亮、被角色搅动 | 运行 `AnimalGame`，F5 对比 | ✅ |
| 泛光与调色 | 运行 `AnimalGame`，F6 对比 | ✅ |
| 各效果可单独开关，覆盖层显示数量与帧时间 | F1 覆盖层 | ✅ |
| 两后端截图一致，模拟可复现 | `--demo --deterministic` 截图逐像素对比，容差 0 | ✅ |

## M1a 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 在 8 px Tile 单屏房间中用键盘操控角色跑跳 | 运行 `AnimalGame` | ✅ |
| 手柄操控（摇杆 / 十字键 / A 跳） | 连接手柄运行 | ⏳ 已实现，待真机验证 |
| 碰撞边界情况有可重复的单元测试（贴墙、余量、高速不穿墙、1 Tile 通道、落地） | `ctest` | ✅ |
| 跳跃手感：coyote time、跳跃缓冲、可变跳高、天花板角修正 | `ctest` + 实际游玩 | ✅（参数待手感调优） |
| OpenGL 与 Vulkan 截图一致 | 见“后端一致性”，容差 0 | ✅ |

## M1b 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 可从入口走到出口的多房间关卡（Tiled） | `AnimalGame --demo`：入口 → 隧道 → 水晶长廊 → 烟雾深坑；从深坑跳回长廊见 ADR-0010 | ✅ |
| 触发器、攀爬、动画状态机、简单敌人、道具 | 实际游玩；`ctest`（触发器 / 巡逻 / 存档 / 攀爬 / 动画测试） | ✅ |
| 重启后持久状态正确（房间、位置、已收集宝石） | 收集宝石后退出再启动；F8 重置 | ✅ |
| 房间边界与切换有可重复测试 | `ctest`（level 测试：世界查找、重叠检测）+ `--demo --deterministic` 回放 | ✅ |
| 两平台行为一致 | Windows 上重复以上步骤 | ⏳ 待在 Windows 机器上验证 |
| OpenGL 与 Vulkan 截图一致 | `--demo --deterministic` 截图逐像素对比，容差 0 | ✅ |

## M2 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 遮罩（阴影）、边缘光、更多光源类型 | F7 对比阴影；入口天花板裂缝的聚光、长廊灯具；角色与甲虫的边缘光（ADR-0012 ~ 0014） | ✅ |
| 光照参数随房间配置 | 三个房间分别引用 cave / crystal / ember 观感预设，切换时渐变；F9 重读预设（ADR-0015） | ✅ |
| 多个房间使用光照且帧时间在预算内 | `--profile`（Vulkan，Release）：示例房间 GPU 约 2.7 ms ≤ 4 ms，光照约 0.3 ms ≤ 1 ms；`--stress-lights=256` 约 7.7 ms ≤ 10 ms（ADR-0016） | ✅（Apple M2） |
| 渲染目标尺寸、混合和采样规则有文档 | [docs/ARCHITECTURE.md §5.1](docs/ARCHITECTURE.md) | ✅ |
| 所有 pass 只通过 RHI 实现 | `engine/src/rhi/opengl/`、`engine/src/rhi/vulkan/` 之外搜索 `gl[A-Z]` / `vk[A-Z]` 调用与图形 API 头文件，无结果 | ✅ |
| OpenGL 与 Vulkan 截图一致（含 256 盏随机阴影光源的压力场景） | `--demo --deterministic [--stress-lights=256]` 截图逐像素对比，容差 0；Vulkan 验证层零报错 | ✅ |
| Windows 上的帧时间与一致性 | Windows 上重复 `--profile` 与截图对比 | ⏳ 待在 Windows 机器上验证 |

## M3 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| GPU 粒子、水面与更多流体类型、植被种类与风场 | 烟雾深坑：瀑布与水雾（GPU 粒子）、水池；三个房间的垂苔 / 发光花 / 芦苇、阵风与气流（ADR-0018 ~ 0020） | ✅ |
| 效果可在关卡数据中配置并复用 | 见上文“关卡数据速查”：观感、粒子（CPU / GPU）、植物都是预设文件，在 Tiled 中引用与覆盖；水、风、发射器、植物区域都是 Tiled 对象 | ✅ |
| 固定测试场景下结果稳定 | `--deterministic` 下同一后端两次运行截图逐像素相同（演示路线、含瀑布 / 烟 / 水的烟雾深坑 600 帧），两后端之间也逐像素相同；模拟测试覆盖各系统的确定性 | ✅ |
| 有开关、参数控制与性能计数 | F5 烟雾（含休眠状态）、F10 粒子（CPU 与 GPU）、F3 / F4 / F6 / F7；参数在预设与 Tiled 属性中；覆盖层显示各效果数量、CPU 耗时、GPU 粒子数与分 pass GPU 耗时，`--profile` 输出平均 / 最大值（预算见 ADR-0016）。水与植物没有单独的运行时开关 | ✅ |
| OpenGL 4.1 下有片元着色器实现路径 | 烟雾、水、GPU 粒子都只用片元着色器，OpenGL 4.1 为主开发后端 | ✅ |
| 性能预算 | Vulkan、Release、Apple M2：三个房间 GPU 整帧 1.2–2.2 ms，各效果均在 ADR-0016 的预算内 | ✅（Apple M2） |
| Windows 上的表现与一致性 | Windows 上重复以上步骤 | ⏳ 待在 Windows 机器上验证 |
