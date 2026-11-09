# P123 灰度版：P1红 / P2蓝 / P3绿（2026-11-07）

本文件是本版本入口，优先于目录内继承的历史 README。新工程以原 P123 灰度工程 `ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104` 为基础，独立保存。

| 角色 | 颜色 | 倍率 | ENTER优先级 |
|---|---|---|---|
| P1 | 红 | 2倍 | 低 |
| P2 | 蓝 | 2倍 | 中 |
| P3 | 绿 | 4倍 | 高 |

配置位于 `main/rm_relay_config.h`。三色均已登记且未使用时，各次 ENTER/DONE 按绿→蓝→红选择。批内选择冻结，后到的高优先级项等待下一批。

生产源码仅修改三项角色颜色默认值。灰度预览、640×480 YUY2配置10 FPS采集、320×240 JPEG质量55、识别、音频和GPIO定义沿用基线。未升级SDK，未新增依赖，沿用现有许可证。

测试入口沿用原工程，`tests/run_tests.py` 默认映射与集成测试中的颜色/TagID场景同步调整为新配置；集成测试硬件桩和提取的生产逻辑未改写。

复现入口：

```powershell
python -X utf8 ARCHIVE/rm-p4-uvc-p123-rbg-grayscale-20261107/tests/run_tests.py
python -X utf8 ARCHIVE/rm-p4-uvc-p123-rbg-grayscale-20261107/tests/run_preview_tests.py
& "$env:USERPROFILE\Documents\Codex\2026-11-07\rm-p123-rbg\work\build.ps1"
```

构建和主机回归结果见 `<USERPROFILE>\Documents\Codex\2026-11-07\rm-p123-rbg\work\verification.json`。本轮仅生成候选固件，没有访问串口或烧录；本机验证不代表真实相机、手机图传、识别或继电器已通过实机验收。
