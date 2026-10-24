# ADR-0037: 发行包与发行校验

- 状态：Accepted（Developer ID 签名与公证待接入）
- 日期：2026-10-24
- 背景与问题：M5 的发行布局只是构建目录里的可执行文件和 `data.pak`。M7 要求两个平台都有能独立运行的安装包、不依赖开发机路径、清单可审计，并且发行版出错时玩家能知道原因（发行版没有控制台）。
- 决策：
  - **打包**（`animal_package` → `cmake/AssemblePackage.cmake`）：产出 `AnimalEngine-<版本>-<平台>/` 与同名 zip。macOS：`AnimalEngine.app`（`Info.plist`：标识、版本、最低系统 13.3、高分辨率、游戏分类；图标；`data.pak` 在 `Contents/Resources`，`SDL_GetBasePath` 即指向该处），默认 ad-hoc 签名，`ANIMAL_CODESIGN_IDENTITY` 指定 Developer ID 时以加固运行时签名（为公证做准备）。Windows：`AnimalGame.exe` 内嵌图标与版本信息，Debug 之外使用窗口子系统（无控制台窗口；SDL 的日志在从终端启动时附着到父控制台，被重定向时写入管道）。两平台都附带给玩家的 `README.txt`、`THIRD_PARTY_NOTICES.txt`（发行版实际包含的依赖的许可证原文，由 `make_notices.py` 从 `external/` 生成）与 `manifest.json`（每个文件的路径、大小、SHA-256，产品、版本、平台、提交）。版本号来自 CMake 项目版本（0.7.0），显示在标题画面。
  - **启动失败时的提示**：资源包损坏或无法读取、或启动失败时，有窗口的运行弹出系统对话框说明原因（资源包损坏时请玩家重新下载）并给出日志文件位置。
  - **校验**：`tools/scripts/check_package.py` 把 zip 解压到临时目录，核对清单与文件（无缺失、无多余、大小与哈希一致），扫描所有文件中是否含有构建机路径，并从该目录无头启动游戏，确认它从包内的 `data.pak` 运行。CI 的打包任务对两个平台的包都执行，并把 zip 与清单作为构建产物保存；开发工具打开时打包会给出警告（开发工具包含源码与构建路径）。
  - **测试**：ctest `data_pack_damaged`（不是资源包的文件必须以说明资源包的错误结束）与 `data_pack_packed`（测试数据烘焙成资源包后，能力测试照常通过）在所有构建中运行。
- 主要理由：打包步骤写成 CMake 脚本，两平台同一套逻辑、CI 与本机一致；清单让任何人都能核对一个包；在临时目录里从包启动，比检查文件列表更直接地证明“不依赖开发机”。
- 结果：macOS 包 7 个文件（含签名资源），Windows 包 4 个文件；两平台的 `data.pak` 哈希一致；`check_package.py` 在关闭开发工具的包上通过，在开启开发工具的包上准确报告可执行文件中的源码路径。
- 代价与风险：macOS 发行版只带 OpenGL（Vulkan 需要随包附带 Vulkan 加载器与 MoltenVK，尚未做）；只构建本机架构（尚无 arm64 + x86_64 通用二进制）；Developer ID 签名、公证与 Windows 代码签名需要账号与证书；错误对话框未经自动化验证（模态窗口），需在目标机器上目视确认；Windows 包未做安装程序。
- 验证方式：`check_package.py`（本机与 CI 两个平台）；ctest `data_pack_*`；本机从解压目录运行 `.app` 并检查 `codesign --verify --deep --strict`。
- 重新评估条件：需要自动更新、商店发行（Steam / App Store）或安装程序时，在此基础上增加对应的打包目标。
