# ESP32-P4 视觉与灯控实验

使用 ESP32-P4、USB UVC 摄像头和 AprilTag 的机器人视觉实验工程，包含灰度/彩色网页预览、角色优先级、灯控及音频提示等不同阶段的实现。

## 从哪里开始

当前选定源码在 `firmware/`，基线为 2026-11-07 的 P123 红蓝绿灰度图传版：P1 红 2 倍、P2 蓝 2 倍、P3 绿 4 倍，批内选择冻结，优先级 P3 > P2 > P1。

原记录已证明该灰度版在一块新板上完成应用烧录及启动、C6 SDIO 和热点初始化；真实相机取流、Tag 识别、灯控、手机画面与长期稳定性未完成整体验收。本次整理没有接板或烧录。

## 版本迭代

`versions/` 保存 17 个有来源的代码快照，含 3 个早期硬件探测和 14 个视觉/控制/图传快照。见 [版本索引](docs/VERSION_HISTORY.md)，每项有 `snapshot-*` 标签。B、C、C Plus、彩色、诊断和无图传版属于不同路线，不能按目录日期推定已逐级替换。

这些 Git 提交是 2026-11-10 对既有归档的整理导入，提交时间不回填，原阶段日期只作为快照说明。真实派生关系由版本索引记录。

## 构建

已有工程使用 ESP-IDF 5.5.4；组件版本见各快照 `dependencies.lock` 与 `main/idf_component.yml`。先激活匹配的 ESP-IDF 环境，在 `firmware/` 内执行：

```sh
idf.py build
```

不在此给通用烧录地址。板卡修订、bootloader、分区和 C6 配套固件必须按使用者实物核对，不能复用另一块板的地址。

源码中的 AP 密码已替换为 `CHANGE_ME_12345`；使用前在 `main/wifi_preview.c` 设置自己的密码。构建缓存与 managed_components 不入库，按组件锁恢复。AprilTag 所需上游源码及原许可随快照保留。

## 主机测试

在具备 Python、GCC 与 Node.js 的主机上：

```sh
python firmware/tests/run_tests.py
python firmware/tests/run_preview_tests.py
```

这些测试不访问串口，不等于实物验收。早期探测片段可能依赖原板卡 SDK，按各快照说明恢复。

## 来源与许可

保留 Espressif 源文件的 Apache-2.0 声明、AprilRobotics/AprilTag 的 BSD 声明及已有第三方说明；详见 `THIRD_PARTY_NOTICES.md`。新增部分没有在本次上传中擅自改授统一许可。

开发过程使用过 AI 协作；各版原有代码、测试和阶段说明构成本仓库的来源，不将测试桩或合成素材结果写成实机效果。
