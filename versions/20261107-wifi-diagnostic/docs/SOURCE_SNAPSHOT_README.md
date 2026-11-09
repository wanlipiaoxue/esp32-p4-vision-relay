# P123 灰度红蓝绿版：Wi-Fi 读回诊断候选（2026-11-07）

本文件为当前候选入口，优先于继承的历史 README。独立基线：`ARCHIVE/rm-p4-uvc-p123-rbg-grayscale-20261107`。

用户授权排查“板子及摄像头运行，但 RM-P4-Camera 无线不可见”。唯一生产源码修改是 `main/wifi_preview.c`：注册 Wi-Fi 事件日志，增加低优先级诊断任务，分四次查询 C6 实际模式、AP配置、SSID隐藏状态、AP MAC、信道、发射功率、固件版本。所有查询经过既有官方 ESP-IDF/Hosted API，不写 C6 固件、不擦除 NVS、不自动重启无线。

P1红2倍/P2蓝2倍/P3绿4倍、P3>P2>P1、批内冻结、灯状态、识别、音频、GPIO及预览路径沿用基线。SDK/依赖锁/分区配置未升级，无新增依赖。

构建与检查入口、日志及核验结果位于 `<USERPROFILE>\Documents\Codex\2026-11-07\rm-p123-wifi-diag\work`。`verification.json` 只证明本地构建和回归；`flash-result-*.json`、`diagnostic-boot-*.log` 才记录实际烧录与读回结果。诊断日志没有读取密码字段。

首次实机使用前须核对 COM4 的 USB序列号5C93011621和P4 MAC e8:f6:0a:e7:fa:bf，核对3MiB factory分区。当前板已另行授权写入同基线配套bootloader，本候选须检查配套bootloader哈希一致后仅更新0x10000应用区。

官方API参考：https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c6/api-guides/wifi.html 。Hosted厂商issue #244包含相同0.0.0版本警告，但其报告是STA连接崩溃，不能据此认定本项目SoftAP缺失的根因。
