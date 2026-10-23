# ADR-0030: 动画导入：Aseprite 精灵表导出

- 状态：Accepted
- 日期：2026-10-23
- 背景与问题：角色动画的帧序列与帧率写死在 `sandbox_game.cpp`（`animator_.add({{0, 0, 0, 0, 0, 1}, 3.0f, true})` 等），美术改动画必须改代码。M4 要求“动画导入器”。需要选一种美术工具能直接产出的格式。
- 决策：
  - 格式采用 Aseprite 的 “Export Sprite Sheet” JSON（与 PNG 一起导出）：`frames` 为数组或以文件名为键的对象（保持文件顺序），每帧 `frame` 矩形、`spriteSourceSize` / `sourceSize`（裁剪偏移）与 `duration`（毫秒）；`meta.image`、`meta.size`、`meta.frameTags`（`from` / `to`、`direction`：forward / reverse / pingpong / pingpong_reverse，`repeat`："1" 播放一次，缺省或 "0" 循环）。这是像素美术最常用的工具与格式，其他工具（如 LibreSprite、TexturePacker 的 Aseprite 兼容输出）也能产出。
  - `animal::parseAsepriteSheet` / `loadSpriteSheet`（framework）得到 `SpriteSheet`：帧列表（来源矩形、偏移、未裁剪尺寸、时长）与每个标签的 `AnimationClip`（帧序号指向帧列表）。旋转帧、超出图像的帧、超出帧范围的标签、未知方向、重名标签都是错误。
  - `AnimationClip` 增加可选的逐帧时长 `durations`；标签内所有帧时长相同时转换为固定帧率（用 double 计算，`1000/3` ms 的帧得到精确的 3 fps），与原来的硬编码行为逐位一致，所以确定性运行与渲染基准不变。
  - 法线 / 自发光贴图沿用同名约定（`<name>_n.png`、`<name>_e.png`，ART_PIPELINE.md），导出时用同样的布局。
  - 示例游戏从 `textures/creature.json` 取 `idle` / `run` / `jump` / `fall` / `climb` / `land` 六个标签，按帧的来源矩形与偏移绘制（朝左时镜像偏移）；缺少任何一个标签则启动失败并说明缺哪个。占位美术脚本生成同格式的 JSON（按 Aseprite 的方式重复帧表示停顿）。
  - 精灵表纳入热重载：修改后重新导入；缺少必要标签的新文件被拒绝，继续使用旧动画。工具中的“Animations”窗口列出标签、帧数、帧率 / 循环方式并实时预览。
- 主要理由：直接读取美术工具的导出文件，省去中间转换步骤；导入结果映射到已有的 `Animator`，动画状态机不变。M5 的资源烘焙可以把同样的数据转成紧凑格式。
- 代价与风险：Aseprite 的时长是整数毫秒，非整除帧率会有亚毫秒差异（只影响新动画）；切片（slices）、用户数据、图层分离导出暂不使用；甲虫等其他精灵仍用代码中的两帧循环，按需迁移。
- 验证方式：单元测试（数组 / 对象帧、四种方向、播放一次、逐帧时长、各类错误）；`creature.json` 得到的六个片段与原硬编码完全相同（帧、精确帧率、循环）；渲染回归不变；热重载集成测试（改名标签被拒绝、合法修改重新载入）。
- 重新评估条件：需要事件帧（脚步声、攻击判定）时读取 Aseprite 的用户数据或切片；动画数量增多时由 M5 烘焙为二进制。
