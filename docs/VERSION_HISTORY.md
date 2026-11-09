# 版本索引

源日期来自归档工程和对应 README；Git 提交均为本次整理时间。状态以各快照入口和 README 的验证说明为准。

| 源日期 | 快照与标签 | 类别 | 变化 | 记录的直接基线 |
|---|---|---|---|---|
| 2026-10-25 | [hello](../versions/20261025-hello/) · `snapshot-20261025-hello` | 硬件探测 | Hello World 板卡启动例程 | 独立探测 |
| 2026-10-25 | [memory](../versions/20261025-memory/) · `snapshot-20261025-memory` | 硬件探测 | 内存与板卡检查片段 | 独立探测 |
| 2026-10-25 | [speaker](../versions/20261025-speaker/) · `snapshot-20261025-speaker` | 硬件探测 | 扬声器测试 | 独立探测 |
| 2026-10-25 | [uvc-tag](../versions/20261025-uvc-tag/) · `snapshot-20261025-uvc-tag` | 视觉基线 | UVC、灰度图像与 AprilTag 识别 | 基础视觉工程 |
| 2026-11-03 | [wifi-preview](../versions/20261103-wifi-preview/) · `snapshot-20261103-wifi-preview` | 图传 | 灰度 JPEG 网页预览 | uvc-tag |
| 2026-11-03 | [wifi-stallfix](../versions/20261103-wifi-stallfix/) · `snapshot-20261103-wifi-stallfix` | 修复候选 | 图传卡顿处理候选 | wifi-preview |
| 2026-11-04 | [handpad-relay](../versions/20261104-handpad-relay/) · `snapshot-20261104-handpad-relay` | 控制 | 手柄事件与继电器灯控 | wifi-preview |
| 2026-11-04 | [fixed-four](../versions/20261104-fixed-four/) · `snapshot-20261104-fixed-four` | 控制 | 赛前固定四倍颜色 | handpad-relay |
| 2026-11-04 | [preview-led](../versions/20261104-preview-led/) · `snapshot-20261104-preview-led` | 预览候选 | 图传帧龄与灯控网页 | fixed-four |
| 2026-11-04 | [p123-priority](../versions/20261104-p123-priority/) · `snapshot-20261104-p123-priority` | 控制候选 | P1/P2/P3 角色配置和优先级 | preview-led |
| 2026-11-05 | [capture-b](../versions/20261105-capture-b/) · `snapshot-20261105-capture-b` | 实验分支 | B：YUY2 帧率声明选择 | p123-priority |
| 2026-11-05 | [capture-c](../versions/20261105-capture-c/) · `snapshot-20261105-capture-c` | 实验分支 | C：MJPEG 采集和按需解码 | p123-priority |
| 2026-11-05 | [capture-cplus](../versions/20261105-capture-cplus/) · `snapshot-20261105-capture-cplus` | 实验分支 | C Plus：有界长连接图传 | capture-c |
| 2026-11-05 | [p123-color](../versions/20261105-p123-color/) · `snapshot-20261105-p123-color` | 彩色候选 | 彩色预览；记录后续已烧录但未验功能 | p123-priority |
| 2026-11-07 | [p123-gray](../versions/20261107-p123-gray/) · `snapshot-20261107-p123-gray` | 默认源码 | P1红/P2蓝/P3绿，灰度预览 | p123-priority |
| 2026-11-07 | [wifi-diagnostic](../versions/20261107-wifi-diagnostic/) · `snapshot-20261107-wifi-diagnostic` | 诊断分支 | Wi-Fi 事件与读回诊断 | p123-gray |
| 2026-11-07 | [recognition-only](../versions/20261107-recognition-only/) · `snapshot-20261107-recognition-only` | 实验分支 | 去掉图传的识别与灯控候选 | p123-gray |

## 验证边界

B/C/C Plus 的目录名含 20261104，但 README 交付日期为 2026-11-05。彩色版的后续说明记录已烧录但未做功能测试。灰度版的后续新板记录支持烧录与启动初始化，不支持整套硬件功能通过。无图传版是独立候选，未据此推定已烧录。

部分继承 README 写的是当时状态；它们放在快照的 docs/SOURCE_SNAPSHOT_README.md，不能覆盖较新的现场记录。硬件探测快照单独列示，不作为视觉产品功能版本。
