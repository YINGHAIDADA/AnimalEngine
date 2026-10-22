# 第三方依赖

所有依赖固定版本，不使用系统包。克隆仓库后执行：

```bash
git submodule update --init --depth 1
```

| 依赖 | 用途 | 版本 / 来源 | 许可证 | 进入发行包 |
|---|---|---|---|---|
| SDL | 平台层：窗口、输入、控制器、OpenGL/Vulkan Surface | 子模块 `SDL`，tag `release-3.4.18` | Zlib | 是（静态链接） |
| glslang | 构建期：GLSL 450 → SPIR-V | 子模块 `glslang`，tag `vulkan-sdk-1.4.363.0`（16.6.0） | BSD-3-Clause 等（见仓库 LICENSE.txt） | 否（仅 `animal_shaderc` 使用） |
| SPIRV-Cross | 构建期：SPIR-V → GLSL 410，资源反射 | 子模块 `SPIRV-Cross`，tag `vulkan-sdk-1.4.363.0` | Apache-2.0 | 否（仅 `animal_shaderc` 使用） |
| stb_image | 开发期纹理解码（PNG） | `stb/stb_image.h` v2.30，nothings/stb@a7d4c21 | MIT / Public Domain | 是（M5 后可能仅工具使用） |
| stb_image_write | 截图与渲染回归输出 PNG | `stb/stb_image_write.h`，nothings/stb@a7d4c21 | MIT / Public Domain | 是（开发功能） |
| glcorearb.h | OpenGL Core 函数类型声明 | `gl/include/GL/glcorearb.h`，KhronosGroup/OpenGL-Registry@6af574a | MIT（Khronos） | 头文件，不单独分发 |
| khrplatform.h | Khronos 平台类型 | `gl/include/KHR/khrplatform.h`，KhronosGroup/EGL-Registry@e964211 | MIT（Khronos） | 头文件，不单独分发 |

升级任何依赖时：更新本表、在两个平台验证干净构建，并在提交信息中写明版本变化。
