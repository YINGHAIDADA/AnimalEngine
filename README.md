# Animal Engine

C++20 专用 2D 像素探索游戏引擎。项目目标、范围与里程碑见 [PROJECT.md](PROJECT.md)，架构与渲染约定见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，关键决策见 [docs/ADR/](docs/ADR/)。

当前阶段：**V1 视觉样板**——像素光照已完成，接下来是可互动植被、粒子、流体烟雾、泛光调色（M0、M1a 已在 macOS 上完成；Windows 验证待做）。美术资源规范见 [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md)。

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
| 方向键 / A D | 左摇杆 / 十字键 | 跑动 |
| Space / Z | A（Xbox）/ ×（PlayStation） | 跳跃（按住跳得更高） |
| F1 | | 开关调试覆盖层 |
| F2 | | 开关碰撞视图（实心 Tile 与角色碰撞框，绿色 = 着地） |
| F3 | | 开关光照（关闭时显示原始颜色） |
| F4 | | 切换光照量化：平滑 / 3 / 5 / 8 级 |
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
| `--frames=N` | 渲染 N 帧后退出 |
| `--screenshot=path.png` | 与 `--frames` 合用，在最后一帧截图（用于验收与渲染回归） |

## 测试

```bash
ctest --preset macos-ninja-debug        # 或 windows-vs2022-debug / windows-ninja-debug / macos-xcode-debug
```

后端一致性（OpenGL 与 Vulkan 截图逐像素对比）：

```bash
B=build/macos-ninja-debug/bin/Debug/AnimalGame
$B --backend=opengl --no-vsync --deterministic --hide-overlay --frames=120 --screenshot=screenshots/gl.png
$B --backend=vulkan --no-vsync --deterministic --hide-overlay --frames=120 --screenshot=screenshots/vk.png
python3 tools/scripts/compare_images.py screenshots/gl.png screenshots/vk.png
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

## M1a 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 在 8 px Tile 单屏房间中用键盘操控角色跑跳 | 运行 `AnimalGame` | ✅ |
| 手柄操控（摇杆 / 十字键 / A 跳） | 连接手柄运行 | ⏳ 已实现，待真机验证 |
| 碰撞边界情况有可重复的单元测试（贴墙、余量、高速不穿墙、1 Tile 通道、落地） | `ctest` | ✅ |
| 跳跃手感：coyote time、跳跃缓冲、可变跳高、天花板角修正 | `ctest` + 实际游玩 | ✅（参数待手感调优） |
| OpenGL 与 Vulkan 截图一致 | 见“后端一致性”，容差 0 | ✅ |
