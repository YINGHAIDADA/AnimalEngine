# ADR-0003: 跨平台构建——CMake 为唯一来源，生成 Xcode、Visual Studio 与 Ninja 构建

- 状态：Accepted
- 日期：2026-10-22
- 背景与问题：项目需要在 macOS 与 Windows 上开发、调试和发行，开发者分别习惯 Xcode 与 Visual Studio。
- 备选方案：分别手工维护 Xcode / VS 工程；CMake 生成；Premake 等其他生成器。
- 决策：CMake + CMakePresets.json 是唯一工程描述。预设覆盖 macOS（Ninja、Xcode）与 Windows（Visual Studio 2022、Ninja），生成的工程文件位于 `build/<preset>/`，不提交。编译器在预设中显式指定：macOS 使用 Apple Clang，Windows 使用 MSVC。
- 主要理由：一套描述避免多份工程漂移；CMake 对 Xcode 与 VS 都有成熟的生成器；Ninja 用于快速增量构建与 CI。
- 代价与风险：IDE 内修改工程设置不会保留，必须改 CMake；多配置生成器（Xcode / VS）与单配置生成器（Ninja）的输出目录不同，需要统一。
- 影响的模块：全部构建脚本、输出目录约定、资源与着色器的部署路径。
- 验证方式：四个配置预设都能在干净目录下配置并构建，产物运行一致。
- 重新评估条件：新增 CMake 无法良好支持的目标平台。
