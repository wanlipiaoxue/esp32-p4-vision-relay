# 图传通信与灯控状态待定版（2026-11-04）

**待定候选，不加入烧录队列，不自动烧录。** 本目录独立于固定4倍颜色版；基线代码、镜像和板上固件均未因本轮发生变更。

直接基线：`ARCHIVE/rm-p4-uvc-fixed4-relay-20261104`。
本版入口：`<USERPROFILE>\Desktop\RM\01-主工作总表\P4图传通信与灯控网页-待定版-2026-11-04.md`。
验证证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-preview-led-pending\work\verification.json`。
生命周期：同目录 `state.json`，`flash_queue_eligible=false`、`flash_queued=false`。

本版增加TCP_NODELAY、图传编码/发送统计；网页跳过过期帧后仍推进游标，分开600ms请求期限和300ms帧龄限额，过期清空旧图。红绿蓝状态点及文字展示P4的未识别/待用/本批/已用逻辑模式与倍率。暂停画面时状态继续更新，状态失联超过有效期转为未知。

灯点按逻辑模式提示常亮、快闪、慢闪，不代表继电器触点测量，也未与物理闪烁相位锁定。帧龄从应用收到帧计起，不是相机曝光起算的端到端延迟。真实手机、P4/C6负载和继电器效果均未验证。

固定4倍颜色仍默认红色，ENTER/DONE/全清零仍为GPIO4/5/22，三灯输出20/21/23。状态机、识别门槛、语音、相机640×480 YUY2@10FPS、现有依赖与3MiB分区保留。

本机复现：

```powershell
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-preview-led-pending-20261104/tests/run_tests.py'
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-preview-led-pending-20261104/tests/run_preview_tests.py'
node 'ARCHIVE/rm-p4-uvc-preview-led-pending-20261104/tests/browser_preview.mjs'
& "$env:USERPROFILE\Documents\Codex\2026-11-04\rm-preview-led-pending\work\build.ps1"
```

上述入口不访问串口、不操作硬件。主机GCC、便携Python、Node和已安装Chrome/Edge是本机前提；浏览器测试复用现有design-taste技能中的CDP工具，不安装软件。重编译后须刷新验证与镜像哈希，不能沿用旧证据。历史继承README不代表本待定版状态，以本文件和本版交付说明为准。
