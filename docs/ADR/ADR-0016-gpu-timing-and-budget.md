# ADR-0016: GPU 计时与帧预算

- 状态：Accepted
- 日期：2026-10-23
- 背景与问题：M2 验收要求“多个房间使用光照且帧时间在预算内”。此前只有整帧的墙钟时间，看不出各 pass 的 GPU 开销，也无法在加入阴影、边缘光等效果时判断瓶颈；预算本身也没有定义。
- 决策：
  - **RHI 计时域**：`CommandList::beginTimer(name)` / `endTimer()` 记录为两条命令（只能在渲染通道之外，可嵌套，提交前必须闭合；`name` 用字面量）。`Device::gpuTimings()` 返回最近一个结果已就绪的帧的各计时域（名称、嵌套深度、毫秒），通常比当前帧晚几帧；`DeviceCaps::gpuTimers` 表示是否支持。槽位分配与换算由公共的 `rhi::TimerScopes` 完成（每帧最多 32 个计时域）。
  - **Vulkan**：每个在途帧一个时间戳查询池；该帧第一个命令缓冲开头 `vkCmdResetQueryPool`，计时点 `vkCmdWriteTimestamp2KHR(ALL_COMMANDS)`；该帧槽位的 fence 完成后用 `vkGetQueryPoolResults` 读回，按队列族 `timestampValidBits` 掩码、`timestampPeriod` 换算。
  - **OpenGL**：驱动支持 `GL_TIMESTAMP` 时每个计时点一个 `glQueryCounter`；macOS 的 OpenGL 时间戳位数为 0，改用 `GL_TIME_ELAPSED` 链：每个计时点结束上一段、开始下一段，读回时累加各段得到每个计时点相对第一个计时点的时间，因此嵌套计时域仍然成立。结果在 4 帧后读取，届时仍未就绪的帧丢弃。
  - **渲染器计时域**：`frame`（整帧）下分 `material`、`fluid`、`lighting`、`glow`、`post`、`world overlay`、`screen`。调试覆盖层显示一行“GPU 总计 + 各 pass”。
  - **测量工具**：`--profile` 在退出时打印跳过 60 帧预热后的墙钟帧时间与各计时域的平均 / 最大值；示例游戏的 `--stress-lights=N` 加入 N 盏在全房间游走、投射阴影的光源。配合 `--demo --deterministic --no-vsync --frames=N` 可复现。
  - **帧预算**（Apple M2，2560×1440，Release，以 Vulkan 的分 pass 数据为准）：

| 场景 | 预算 | 实测（Vulkan） |
|---|---|---|
| 示例房间，GPU 整帧 | ≤ 4 ms | 约 2.7 ms |
| 示例房间，光照 pass（含阴影、边缘光） | ≤ 1 ms | 约 0.3 ms |
| 压力测试：额外 256 盏阴影光源，GPU 整帧 | ≤ 10 ms（仍在 60 Hz 的 16.7 ms 内） | 约 7.7 ms（光照约 5.0 ms） |

- 主要理由：计时域是记录式命令，与 CommandList 的“录制—提交时翻译”模型一致，两个后端各自映射到原生查询；`GL_TIME_ELAPSED` 链让不支持时间戳的 macOS OpenGL 也有数据，且不需要上层区分。
- 代价与风险：
  - **macOS OpenGL 的分 pass 数据不可信**：驱动把 GL 命令合批成 Metal 命令缓冲，工作被计入恰好刷新时所在的计时域（例如压力测试中 OpenGL 报告流体 0.004 ms、光照 4.6 ms，而 Vulkan 为 2.2 / 5.0 ms）。重负载 pass 的量级一致，但小 pass 的拆分不能用；macOS 上做预算请用 Vulkan。Windows 原生 OpenGL 驱动支持时间戳，待 M2.5 验证。
  - 计时数据晚几帧；每帧计时域数量有上限，超出的不计时。
  - **已知热点**：流体约 1.5 ms，是正常场景中最大的开销。它由约 35 个 160×90 的小 pass 组成，在 TBDR GPU 上主要是每个 pass 的固定开销而不是计算量；列为 M3（模拟效果生产化）的优化目标。
- 验证方式：单元测试覆盖计时域嵌套、槽位上限、刻度换算与命令记录；两后端加入计时域前后截图逐像素一致，Vulkan 验证层零报错；压力测试中光照耗时随光源数增长（0 → 256 盏：0.3 → 5.0 ms），两后端的光照耗时一致。
- 重新评估条件：Windows 上的实测、加入 GPU 粒子 / 更多流体（M3）、或需要 CPU 侧分段计时时。
