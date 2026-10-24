# ADR-0015: 观感预设（Look）与房间光照配置

- 状态：Accepted
- 日期：2026-10-23
- 背景与问题：M2 之前，每个房间只能设环境光；色阶、阴影、边缘光、泛光、曝光、对比度、饱和度、分区色调、暗角、烟雾颜色都是示例游戏代码里的常量，所有房间共用一套，调整要改 C++ 重新编译，换一个游戏也要重写。需要一种可复用、可在关卡数据里引用并局部覆盖的配置，并在房间切换时平滑过渡。
- 决策：
  - **Look**（框架 `animal/framework/look.h`）：一个扁平结构，含光照（`ambient`、`bands`、`dither`、`shadows`、`shadowPenetration`、`rimIntensity`、`rimAmbient`，以及 ADR-0039 的纵深与补光字段）、泛光与调色（`bloomThreshold`、`bloomKnee`、`bloomIntensity`、`bloomBands`、`exposure`、`contrast`、`saturation`、`shadowTint`、`highlightTint`、`vignette`）与烟雾颜色（`smokeTint`、`smokeOpacity`）。只包含“美术决定的观感”；各效果的开关（`enabled`）与烟雾动力学参数不在其中。`applyLook` 把它写入 `LightingSettings` / `PostSettings` / `FluidSettings`。
  - **预设文件** `*.look.json`：`{"format": "animal-look", "version": 1, "base": "…", 字段…}`。所有字段可选，缺省取 `Look::engineDefaults()`（即引擎各设置结构的缺省值）；`base`（相对本文件）先应用，可多级继承。颜色写 `"#rrggbb"`，色调写 `[r, g, b]`（可大于 1）。**严格校验**：未知字段、类型错误、格式名 / 版本不符、`base` 循环或缺失都是错误，信息含文件路径与字段名——拼错字段名不会被悄悄忽略。
  - **房间引用与覆盖**（Tiled 地图属性，取代 ADR-0010 的 `ambient`）：`look`（file，相对 `.tmj`）指定预设；`look.<字段>` 覆盖单项（float / int / bool 按字段类型，颜色字段用 Tiled 颜色，色调字段用颜色或字符串 `"r g b"`）。未知或类型错误的覆盖项逐条警告并跳过。
  - **过渡**：进入房间时从屏幕上当前的观感混合到新房间的观感，0.6 s，smoothstep；数值与颜色线性插值，整数和开关字段在过渡开始即切换到目标值（避免出现中间的色阶数）。过渡在固定步中推进，`--deterministic` 下可复现。
  - **调试**：F4 在“房间观感的色阶 / 平滑 / 3 / 5 / 8”之间切换，F7 关闭阴影，二者叠加在观感之上；F9 重新读取预设文件（直接编辑 `data/world/looks/` 下的部署副本即可即时调参），覆盖层显示当前预设名。
  - **示例**：`cave.look.json`（基础：原先代码中的参数）、`crystal.look.json`（继承 cave：更冷的阴影与轮廓、更强泛光与饱和度）、`ember.look.json`（继承 cave：暖色分区色调、暖褐色烟雾）；烟雾深坑另用 `look.vignette` 覆盖暗角。
- 主要理由：观感是关卡设计数据，应该能在不编译的情况下调整、复用和继承；扁平字段表让文件解析、Tiled 覆盖和插值共用一份字段定义；严格校验让数据错误在加载时就暴露。
- 代价与风险：预设读取失败时该房间退回引擎缺省观感（会有错误日志）；只有一个过渡时长；Look 字段增加时需要同步字段表与 `applyLook`；F9 只重读预设，不重读房间文件。
- 验证方式：look 测试覆盖各类字段解析、多级继承、循环与缺失、各类拒绝信息、Tiled 覆盖（含类型转换与问题报告）、混合规则、`applyLook` 不改动开关；入口房间迁移到 `cave.look.json` 后与迁移前截图逐像素一致；在部署副本中拼错字段名，日志给出文件与字段名；两后端截图一致，验证层零报错。
- 重新评估条件：需要按区域而非房间配置观感、在房间内随时间 / 事件变化（如开灯），或观感需要包含光照以外的参数（如音频混响）时。
