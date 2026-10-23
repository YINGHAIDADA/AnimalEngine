# Animal Engine

[![CI](https://github.com/YINGHAIDADA/AnimalEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/YINGHAIDADA/AnimalEngine/actions/workflows/ci.yml)

C++20 专用 2D 像素探索游戏引擎。项目目标、范围与里程碑见 [PROJECT.md](PROJECT.md)，架构与渲染约定见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，关键决策见 [docs/ADR/](docs/ADR/)。

当前阶段：**M0–M5 已完成**（macOS 与 Windows），**M6 完整纵向切片与稳定性：内容与系统已完成，待试玩确认**——无头运行与自动通关测试、存档备份与恢复、CPU 分区计时、日志文件与崩溃报告（ADR-0032），音频（ADR-0033），地图界面，机关、下潜与 10 个房间的示例区域（ADR-0034），测试使用独立的测试数据（ADR-0035）；已知问题见 [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)。美术资源规范见 [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md)。

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
| F9 | | 重新读取观感预设（开发构建中保存源文件即自动热重载，见下文“开发工具”） |
| F10 | | 开关粒子效果（覆盖层显示各效果数量与 CPU 耗时） |
| F12 | | 截图到 `screenshots/` |
| `` ` `` | | 显示 / 隐藏开发工具（开发构建） |
| Tab | | 进入 / 退出关卡编辑器（开发构建） |
| Esc | | 退出（开发工具显示时不退出） |

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
| `--tools[=窗口,...]` | （开发构建）启动时显示开发工具并打开指定窗口：`performance` `rendering` `rooms` `look` `effects` `plants` `tuning` `animations` `console` `demo` |
| `--pack=data.pak` | 从资源包读取数据（不加时，可执行文件旁有 `data.pak` 就自动使用，否则读 `data/` 散文件；ADR-0031） |
| `--input-script=path.txt` | 在指定帧注入按键 / 鼠标（画布像素坐标）/ 文本事件，用于自动化 UI 测试与演示；格式见 `engine/src/app/input_script.h` |
| `--headless` | 不开窗口、不绘制（RHI Null 后端），模拟照常运行；配合 `--demo --deterministic --frames=N` 做自动通关测试（ADR-0032） |
| `--expect=room:NAME,gems:N,visited:N,flag:NAME,finished:true,health:N,grounded:true,respawns:N` | （示例游戏）退出时检查结果，未满足时退出码 3 |
| `--record-input=path.txt` | 把按键录成输入脚本（退出时写入）；配合 `--deterministic` 录制的脚本可逐帧重放 |
| `--data=dir` | 从指定目录读取数据（测试用 `test_data`，ADR-0035）；此时开发工具关闭、不读写存档 |
| `--flags=ability:dive,switch:bridge` / `--gems=N` | （示例游戏，测试用）开局即带有这些存档标志 / 已收集 N 颗宝石 |
| `--trace[=N]` | （示例游戏）每 N 个模拟步在日志中输出玩家所在房间、位置、速度与状态（默认 10） |
| `--strict` | RHI 校验层报告任何错误时运行失败（退出码 4） |
| `--mute` | 不打开音频设备（音频系统照常运行但不出声；无头运行自动静音） |
| `--crash-test=segv\|abort\|exception` | 启动后故意崩溃，检查崩溃报告；`--crash-dir=路径` 改变报告目录 |

玩法：10 个房间的洞穴（世界布局见 `tools/scripts/make_area.py` 开头），散落着 19 颗宝石（右上角计数）。断桥上有移动平台与坠落平台；坠入深渊就到了尖刺竖井，底部的宝珠赋予**下潜**能力（在水中按住下），旁边的开关打开断桥通往闸门大厅的门；有了下潜，烟雾深坑的水池下面是一片淹没的区域（水流会推着你走，神殿里的开关打开暗流中的小门），淹没隧道向上通往闸门大厅。闸门要 14 颗宝石才开，门后是终点。按 M（手柄 Back）打开地图：显示进入过的房间及其地形（岩石、水、红色的尖刺）、门（打开后变淡）与开关（按下后变绿）、其中还没拾取的宝石和自己的位置，所在房间正常亮度、其他房间变暗，打开时游戏暂停。左上角是血量：3 颗发光的心，每颗 4 滴；被甲虫撞到（会被弹开）、碰到尖刺（发着微微红光）或被平台挤住各掉 1 滴，后两种还会回到检查点（进入房间后第一次站稳的位置）；血掉光时回到检查点并回满（掉出世界不扣血）；走进提示区域时屏幕下方显示提示。拾取宝石、按下开关、获得能力、进入新房间后第一次站稳时与退出游戏时自动保存，下次启动从该处继续（存档位置见 ADR-0011；`--demo` / `--deterministic` / `--start-room` 不读写存档）。存档写入前会保留上一份为 `save.json.bak`：存档损坏时自动用备份恢复，来自更新版本的存档不会被覆盖（ADR-0032）。

音效：跳跃、落地、抓藤蔓、宝石、被撞、入水，以及洞穴环境声（ADR-0033）；源文件在 `assets/source/sounds/`（WAV 或 Ogg Vorbis，当前是 `tools/scripts/make_placeholder_audio.py` 生成的占位音效，同名替换即可），音量分组在参数表中（`audio.*`）。

诊断：日志写到用户数据目录下的 `logs/Sandbox.log`（保留最近 5 次）；崩溃时在 `crashes/` 写崩溃报告（原因、调用栈、日志位置；Windows 另有 minidump）。macOS 上用户数据目录是 `~/Library/Application Support/AnimalEngine/Sandbox/`，Windows 上是 `%APPDATA%\AnimalEngine\Sandbox\`。

## 开发工具（M4）

开发构建（CMake 选项 `ANIMAL_DEV_TOOLS`，默认开启）内置开发工具，按 `` ` `` 显示（ADR-0026）：

| 窗口（View 菜单） | 作用 |
|---|---|
| Performance | 帧时间、各 GPU pass 与 CPU 分区（`ANIMAL_PROFILE_ZONE`）耗时的历史曲线，精灵 / 绘制 / 光源 / 粒子计数，音频声部与峰值 |
| Rendering | F2–F10 的开关，重读观感，重建房间 |
| Rooms | 世界中的房间，双击传送 |
| World | 世界布局：拖动房间（吸附整房间或 8 px，重叠时拒绝）、新建空房间或复制当前房间、从世界中移除房间、撤销布局修改（ADR-0029） |
| Look / Effects / Plants | 直接编辑当前房间的观感预设、效果预设、植物种类：每次修改用格式自己的解析器校验，有效则立即生效，错误显示在窗口中；Save 写回源文件 |
| Tuning | 玩家、攀爬、游泳、烟雾参数的滑条；Save 只把与代码默认值不同的项写入 `world/tuning.json`（ADR-0027） |
| Animations | 角色精灵表（Aseprite 导出）的标签、帧率与实时预览（ADR-0030） |
| Console | 日志；有未读警告 / 错误时菜单栏出现提示 |

**热重载**（ADR-0028，开发构建默认开启，Reload 菜单可关闭）：保存 `assets/source/` 或 `shaders/` 下的文件，游戏在约半秒内载入——着色器由 `animal_shaderc` 重新编译后原地替换（资源绑定变了则整个渲染器重建），纹理原地更新（改了尺寸也可以），后加的 `_n` / `_e` 贴图自动挂到材质上，房间 / Tileset / 世界布局 / 观感 / 效果 / 植物 / 参数 / 精灵表 / 音效重新载入。文件有错误时（JSON 语法、字段类型、着色器编译失败或用了代码没有提供的绑定……）控制台给出原因、屏幕底部提示 `RELOAD FAILED`，游戏继续使用旧版本；修好再保存即可。`--deterministic` / `--demo` 下不监视文件。

**关卡编辑器**（Tab，ADR-0029）：编辑当前房间，保存后 Tiled 照常可以打开。

| 操作 | 方式 |
|---|---|
| 选择 / 移动 / 改大小 | V；点选对象（点在区域内时优先选小的），拖动移动，拖矩形右下角改大小；吸附 1 / 2 / 4 / 8 px |
| 放置对象 | O；列表或 1–9 / 0、`[` `]` 选类型（灯、灯笼、蜡烛、触发器、甲虫、宝石、藤蔓、起点、水、发射器、植物、通风口、气流），点击放置 |
| 画 Tile | B 画笔、R 矩形填充；右键擦除，Alt+点击取色 |
| 属性 | 检查器：类型、名称、位置、尺寸、带类型的属性（文件属性可从预设列表选择）；“Room properties”编辑 `look`、`look.*`、`wind.*` |
| 撤销 / 重做 / 保存 | Ctrl+Z、Ctrl+Shift+Z 或 Ctrl+Y、Ctrl+S（macOS 用 Cmd）；Del 删除、Ctrl+D 复制 |
| 试玩 | 再按 Tab 立即从当前状态继续游戏（无需保存）；编辑时右键“Move the player here”把玩家放到任意位置 |

每次编辑都通过游戏自己的关卡解析器立即生效；保存写入 `assets/source/world/rooms/*.tmj` 与部署副本，新对象的 id 从不复用（宝石的存档键）。

## 发行构建（M5）

发行版只包含可执行文件和一个资源包 `data.pak`（ADR-0031）：

```bash
cmake --preset macos-ninja-release -DANIMAL_DEV_TOOLS=OFF     # 或 windows-ninja-release
cmake --build --preset macos-ninja-release --target animal_package
./build/macos-ninja-release/package/Release/AnimalGame         # 自动使用旁边的 data.pak
```

`animal_package` 用 `AnimalCooker` 按 [`assets/cook.json`](assets/cook.json) 烘焙：从世界文件出发沿引用收集房间、Tileset、观感、效果、植物，加上代码直接用到的预设、精灵表、材质与渲染器的着色器；每个文件用游戏自己的解析器校验，JSON 压缩为紧凑格式，材质合成图集（原始 RGBA8），全部用 zstd 写进 `data.pak`（同样的源文件总是得到逐字节相同的包），再用 `--verify` 按游戏的方式加载一遍。旁边的 `cook_report.json` 列出各类资源的源 / 烘焙后 / 压缩后大小、重复内容、未被引用（因此没有打包）的文件；引用缺失或数据无效时烘焙失败。新增的运行时数据要能从世界文件引用到，或写进清单。

```bash
B=build/macos-ninja-release/bin/Release
$B/AnimalCooker --manifest=assets/cook.json --source=assets/source --shaders=$B/data/shaders --out=data.pak --report=report.json
$B/AnimalCooker --verify=data.pak --manifest=assets/cook.json
```

## 编辑关卡（Tiled）

关卡位于 `assets/source/world/`：用 [Tiled](https://www.mapeditor.org/) 打开 `test.world` 即可看到并编辑所有房间。约定（Tile 属性、对象类型、地图属性）见 [ADR-0010](docs/ADR/ADR-0010-tiled-levels.md)，玩法对象（触发器、宝石、甲虫）见 [ADR-0011](docs/ADR/ADR-0011-gameplay-elements-and-save.md)，房间观感（`look` 预设与 `look.*` 覆盖，预设位于 `looks/`）见 [ADR-0015](docs/ADR/ADR-0015-looks.md)，粒子效果（`emitter` 对象，预设位于 `effects/`）见 [ADR-0017](docs/ADR/ADR-0017-effect-presets.md)，水域（`water` 对象）见 [ADR-0018](docs/ADR/ADR-0018-water.md)，植物（`plants` 对象，种类位于 `plants/`）与风（`wind.*` 地图属性、`wind` 对象）见 [ADR-0019](docs/ADR/ADR-0019-wind-and-plants.md)。开发构建运行时保存即自动热重载（否则重新构建即可部署到 `data/world/`）；也可以用游戏内的关卡编辑器（见上文）。房间最初由 `tools/scripts/make_test_world.py` 生成，该脚本现在拒绝覆盖已存在的文件（`--force` 才覆盖，会丢失编辑结果）。

### 关卡数据速查

| 放在哪里 | 名称 | 作用 | 详见 |
|---|---|---|---|
| Tileset Tile 属性 | `solid`、`grass`、`light*`、`hazard`（`up` / `down` / `full`） | 碰撞、长草（`plants/grass.plant.json`）、发光 Tile、尖刺 | ADR-0010、0013、0034 |
| 地图属性 | `look`、`look.*` | 观感预设与单项覆盖 | ADR-0015 |
| 地图属性 | `wind.strength`、`wind.gusts`、`wind.gustInterval`、`wind.gustSpeed` | 微风与阵风 | ADR-0019 |
| 点对象 | `spawn`、`candle`、`vent`、`vine`、`light`、`lantern`、`gem`、`crawler`、`wind`、`switch`（`flag`）、`ability`（`name` = `dive`）、`entry` | 起点、道具、光源、宝石、甲虫、气流、开关、能力、测试与传送的落点 | ADR-0010、0011、0013、0019、0034 |
| 矩形对象 | `trigger`、`water`（`currentX` / `currentY` 水流、`deepening` 随深度加深）、`plants`、`emitter`（也可为点）、`platform`（`travelX` / `travelY`、`speed`、`pause`、`phase`、`falls`）、`door`（`flag` 或 `gems`）、`goal` | 提示区域、水域、植物、粒子发射器、移动 / 坠落平台、门与宝石门、终点 | ADR-0011、0017、0018、0019、0020、0034 |
| 预设文件 | `looks/*.look.json`、`effects/*.effect.json`（`"gpu": true` 走 GPU）、`plants/*.plant.json` | 可复用的观感、粒子效果、植物种类；格式错误会给出文件名与字段 | ADR-0015、0017、0019、0020 |
| 参数文件 | `world/tuning.json` | 覆盖代码中登记的玩家 / 攀爬 / 游泳 / 烟雾参数（只写与默认值不同的项） | ADR-0027 |
| 精灵表 | `textures/creature.json`（Aseprite 导出，与 `creature.png` 同名） | 动画帧与标签 `idle` `run` `jump` `fall` `climb` `land` | ADR-0030 |

## 测试

```bash
ctest --preset macos-ninja-debug        # 或 windows-vs2022-debug / windows-ninja-debug / macos-xcode-debug
```

持续集成（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）：每次推送到 `main` 和每个 PR，在 GitHub 的 macOS 与 Windows 机器上构建 Debug、Release 与关闭开发工具的 Release（`ANIMAL_DEV_TOOLS=OFF`），编译警告视为错误，并运行单元测试、无头自动通关（`demo_playthrough`：到达烟雾深坑、拾取 2 颗宝石、进入 3 个房间、没有掉出世界）与崩溃报告测试（`crash_report_*`）。托管机器没有 GPU，渲染回归仍在开发机上运行（见下）。

测试只用自己的数据（ADR-0035）：`tests/data/` 是一份独立的数据目录（房间、预设、贴图、音效），构建时部署到 `bin/<Config>/test_data/`，所有游戏测试都带 `--data=…/test_data` 运行。**编辑 `assets/source` 里的内容不会影响任何测试**，也不需要刷新基准截图。测试房间由 `tools/scripts/make_test_data.py` 生成：`render_*` 是渲染场景与演示路线用的房间，`test_*` 每个只测一项能力（ctest `capability_*`：房间切换、平台携带与挤压、坠落平台、尖刺与击倒、甲虫、开关与门、下潜、水流、宝石门与终点、游上进入上方房间）。引擎新增能力时，在 `make_test_data.py` 里加一个测试房间、在 `game/CMakeLists.txt` 里加一条 `animal_add_capability_test`。

渲染回归（在两个后端上跑 7 个固定游戏场景（读回 320×180 最终画布）和 RHI 功能一致性图（`AnimalRhiConformance`，逐项覆盖 RHI 功能，见 ADR-0025）：两后端必须逐像素相同，并与 `tests/golden/` 中的基准截图逐像素相同；Vulkan 验证层必须零报错）：

```bash
python3 tools/scripts/render_regression.py            # 全部场景
python3 tools/scripts/render_regression.py --only demo_pit
python3 tools/scripts/render_regression.py --update   # 有意改变画面后刷新基准截图（提交时可见哪些画面变了）
B=build/macos-ninja-release/bin/Release
$B/AnimalCooker --manifest=tests/data/cook.json --source=tests/data --shaders=$B/data/shaders --out=test_data.pak
python3 tools/scripts/render_regression.py --binary $B/AnimalGame --pack test_data.pak   # 测试数据的资源包，结果也必须与基准相同
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

`--profile` 同时打印 CPU 分区（事件、更新、模拟及其子项、渲染、提交、呈现）的平均 / 最大耗时（ADR-0032）。

## M0 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| macOS（Ninja、Xcode）干净构建并运行 | 删除 `build/` 后按上文构建 | ✅ 已验证（Apple M2，macOS 27） |
| Windows（VS 2022、Ninja）干净构建并运行 | 同上 | ✅ 已在 Windows 上验证 |
| 一个房间中绘制 Tile 和可移动角色 | 运行 `AnimalGame` | ✅ |
| 窗口缩放无模糊、无意外拉伸（整数倍 + 留黑边） | 拖动窗口大小；单元测试 `present_layout_*` | ✅ |
| 显示 FPS、帧时间、渲染尺寸、缩放倍率、当前后端 | F1 覆盖层 | ✅ |
| RHI 后端目录之外没有图形 API 调用 | `engine/src/rhi/opengl/`、`engine/src/rhi/vulkan/` 之外搜索 `gl[A-Z]` / `vk[A-Z]` 无结果 | ✅ |
| OpenGL 与 Vulkan 输出一致 | 见上文“后端一致性”，容差 0 | ✅（macOS / MoltenVK） |
| 单元测试通过 | `ctest` | ✅ |
| 两个平台的验收截图 | `AnimalGame --frames=60 --screenshot=screenshots/m0_<platform>.png` | macOS ✅ / Windows ✅ |

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
| 两平台行为一致 | Windows 上重复以上步骤 | ✅ 已在 Windows 上验证 |
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
| Windows 上的帧时间与一致性 | Windows 上重复 `--profile` 与截图对比 | ✅ 已在 Windows 上验证 |

## M3 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| GPU 粒子、水面与更多流体类型、植被种类与风场 | 烟雾深坑：瀑布与水雾（GPU 粒子）、水池；三个房间的垂苔 / 发光花 / 芦苇、阵风与气流（ADR-0018 ~ 0020） | ✅ |
| 效果可在关卡数据中配置并复用 | 见上文“关卡数据速查”：观感、粒子（CPU / GPU）、植物都是预设文件，在 Tiled 中引用与覆盖；水、风、发射器、植物区域都是 Tiled 对象 | ✅ |
| 固定测试场景下结果稳定 | `--deterministic` 下同一后端两次运行截图逐像素相同（演示路线、含瀑布 / 烟 / 水的烟雾深坑 600 帧），两后端之间也逐像素相同；模拟测试覆盖各系统的确定性 | ✅ |
| 有开关、参数控制与性能计数 | F5 烟雾（含休眠状态）、F10 粒子（CPU 与 GPU）、F3 / F4 / F6 / F7；参数在预设与 Tiled 属性中；覆盖层显示各效果数量、CPU 耗时、GPU 粒子数与分 pass GPU 耗时，`--profile` 输出平均 / 最大值（预算见 ADR-0016）。水与植物没有单独的运行时开关 | ✅ |
| OpenGL 4.1 下有片元着色器实现路径 | 烟雾、水、GPU 粒子都只用片元着色器，OpenGL 4.1 为主开发后端 | ✅ |
| 性能预算 | Vulkan、Release、Apple M2：三个房间 GPU 整帧 1.2–2.2 ms，各效果均在 ADR-0016 的预算内 | ✅（Apple M2） |
| Windows 上的表现与一致性 | Windows 上重复以上步骤 | ✅ 已在 Windows 上验证 |

## M2.5 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 补齐只在 OpenGL 实现的 RHI 功能 | 审计：没有缺失；`AnimalRhiConformance` 逐项执行全部 RHI 功能（含游戏未用到的），两后端逐像素一致（ADR-0025） | ✅ |
| 两后端共用的参数校验 | Debug 构建的 `ValidatingDevice`，创建与提交时统一检查，单元测试覆盖（ADR-0021） | ✅ |
| 拆分 `vk_device.cpp` | 7 个文件（设备、交换链、资源、管线、命令、工具、内部头文件），纯移动、回归不变 | ✅ |
| 细化屏障、内存子分配与管线缓存 | 按用途推导的屏障（ADR-0023）、VMA 与可扩容的描述符池 / 暂存缓冲（ADR-0022）、管线缓存（ADR-0024，19 个管线 21 ms → 3 ms） | ✅ |
| M0–M2（及 M3）全部测试场景两后端输出一致 | `render_regression.py`：6 个游戏场景 + RHI 一致性图，Debug 与 Release 构建都与基准逐像素相同 | ✅（macOS / MoltenVK） |
| 可通过启动参数切换后端 | `--backend=opengl|vulkan` | ✅ |
| 验证层无报错 | Vulkan 验证层（含同步验证）与 RHI 校验在全部回归场景、`--resize-test` 下零报错 | ✅ |
| 上层代码无需修改 | M2.5 期间 framework / game 未因后端改动而修改（校验层是透明的装饰器） | ✅ |
| Windows 原生驱动验证 | Windows 上构建、跑测试、渲染回归与 `--profile` | ✅ 已在 Windows 上验证 |

## M4 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| 调试面板 | `` ` `` 显示：性能（CPU / GPU 历史）、渲染开关、房间、观感 / 效果 / 植物预设编辑、参数、动画、控制台（ADR-0026）；Release + Vulkan 下工具 UI 的 GPU 开销约 0.07 ms | ✅ |
| 参数热调 | Tuning 窗口实时调整 33 个玩家 / 攀爬 / 游泳 / 烟雾参数，保存到 `world/tuning.json`（ADR-0027）；预设编辑实时生效并可保存 | ✅ |
| 纹理 / 着色器热重载 | 保存源文件即载入（ADR-0028）；`AnimalRhiConformance` 在两个后端上检查着色器原地替换（相同代码图像不变、替换后改变、换回逐像素恢复、绑定 / 阶段改变被拒绝）；运行中修改纹理与着色器的集成测试，Vulkan 零验证错误 | ✅ |
| 内嵌关卡编辑器 | Tab：对象放置 / 移动 / 属性、Tile 画笔、撤销、保存为 Tiled 文件（ADR-0029）；未修改的房间往返逐字节相同 | ✅ |
| 动画导入器 | Aseprite 精灵表导出驱动角色动画，与原硬编码片段逐位一致、渲染基准不变；精灵表热重载（ADR-0030） | ✅ |
| 不修改引擎代码即可摆放实体、光源、触发器并快速测试 | 输入脚本 `--input-script`：进入编辑器，放置灯、触发器、甲虫并拖动，按 Tab 立即试玩，角色走进新触发器显示提示；保存后的 diff 只包含新增对象（两个后端均验证） | ✅ |
| 资源重载失败能报错且不崩溃 | 运行中写入坏 JSON、类型错误的观感、语法错误的着色器、改尺寸的纹理、缺少标签的精灵表：均被拒绝并在控制台 / HUD 报告原因，游戏继续使用旧版本，部署副本恢复；修好后重新载入 | ✅ |
| 发行构建可剔除开发工具 | `-DANIMAL_DEV_TOOLS=OFF` 的 Release 构建通过，不含 ImGui 与工具代码 | ✅ |
| 渲染回归与单元测试 | `render_regression.py` 7 个场景两后端逐像素一致；`ctest` 101 个测试 | ✅ |
| Windows 上的工具与热重载 | Windows 上重复以上步骤 | ✅ 已在 Windows 上验证 |

## M5 验收条件

| 条件 | 验证方式 | 状态 |
|---|---|---|
| Asset Cooker、格式转换、压缩、去重 | `AnimalCooker`：紧凑 JSON、原始 RGBA8 图集、zstd、相同内容只存一份（ADR-0031） | ✅ |
| 图集 | 4 个材质（12 张 PNG）合成 128×16 图集三页；绘制调用平均 13.1 → 10.2（`--profile`，演示路线） | ✅ |
| 资源目录与版本 | 包内资源目录带每个条目的哈希与压缩信息，文件头有格式版本、数据版本与目录哈希；损坏或版本不符时拒绝并说明 | ✅ |
| 源资源确定性地生成发行资源 | 同一台机器两次烘焙逐字节相同（单元测试与 CI）；macOS 与 Windows 烘焙的 `data.pak` 哈希相同（CI） | ✅ |
| 缺失 / 重复 / 未引用资源可报告 | 烘焙报告：缺失引用与无效数据使烘焙失败；重复内容与未引用文件列出（当前 6 个只给开发工具和测试用的着色器未打包） | ✅ |
| 可比较优化前后的体积与加载性能 | 源数据 166 KB → `data.pak` 48.8 KB；启动数据加载（含渲染器初始化）OpenGL 约 21 → 20 ms、Vulkan 约 16 → 13 ms（Apple M2，Release，多次取中位数） | ✅ |
| Release 构建 | `animal_package`：可执行文件 + `data.pak` 的独立目录，不依赖 `data/`；CI 在两个平台上打包并保存产物 | ✅ |
| 发行资源与开发资源画面一致 | `render_regression.py --pack`：7 个场景两后端与基准逐像素一致 | ✅ |

## M6 进度

M6 验收条件：从启动到通关一段完整区域无需开发者手动修补；干净构建可重复；已知问题有清单。全部项目已完成，等待试玩确认后打 `milestone/m6-slice` 标签。

| 项目 | 内容与验证方式 | 状态 |
|---|---|---|
| 自动化验证 | `--headless`（RHI Null 后端）+ `--expect` + `--strict`；ctest `demo_playthrough` 在 CI 六个构建中跑完演示路线（ADR-0032） | ✅ |
| 存档健壮性 | 存档版本 2（已访问房间、游戏时间）与版本 1 迁移；`.bak` 备份、损坏时从备份恢复、新版本存档不覆盖、退出时保存；单元测试覆盖（ADR-0032） | ✅ |
| 性能分析 | CPU 分区计时：性能面板曲线与 `--profile` 报告；演示路线每帧模拟约 0.16 ms（ADR-0032） | ✅ |
| 崩溃诊断 | 轮转日志文件；崩溃报告（信号 / 未处理异常 / 未捕获的 C++ 异常，调用栈，Windows minidump）；ctest `crash_report_*` 在两个平台上检查（ADR-0032） | ✅ |
| 音频 | SDL3 设备 + 自有混音器（32 声部、分组、声像、变速、淡入淡出、限幅），WAV / Ogg Vorbis，资源包与热重载，游戏音效与环境声（占位）；32 声部混音约占一个核心的 1.2%（ADR-0033） | ✅ |
| 地图探索 | 已访问房间记录在存档中；M / Back 打开地图（按世界布局缩放，显示剩余宝石与玩家位置）；`--expect=visited:N` | ✅ |
| 机关与能力 | 移动 / 坠落平台、尖刺、开关与门、宝石门、水流、下潜、终点与通关画面；全部可在关卡编辑器中摆放（ADR-0034） | ✅ |
| 完整区域 | 10 个房间、19 颗宝石（ADR-0034）；录制路线逐段走通过，路线测试后来按 ADR-0035 移除（测试不再依赖内容） | ✅ |
| 能力测试 | 独立测试数据上的 16 个 `capability_*` 测试覆盖每种机关与能力（ADR-0035） | ✅ |
| 干净构建可重复 | CI：六个构建（警告视为错误）+ 两个平台打包，`data.pak` 逐字节相同 | ✅ |
| 已知问题清单 | [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) | ✅ |
| 从启动到通关的人工试玩 | 内容能否通关由试玩判断（自动测试只验证能力） | 待你试玩 |
