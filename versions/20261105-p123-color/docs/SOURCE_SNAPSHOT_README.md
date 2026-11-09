> **后续状态更新：已于 2026-11-05T23:35:15.3848064+08:00 按用户授权以921600烧录，写入哈希校验通过并硬复位；未做功能测试。以下“未烧录”等文字记录此前开发验证阶段；以本条及烧录结果为最新状态。**
> 烧录结果：`<USERPROFILE>\Desktop\RM\01-主工作总表\P4-P123彩色版-烧录结果-2026-11-05.md`。

# P123 彩色预览独立候选（2026-11-05）

**本地实现、主机验证及 ESP-IDF 5.5.4 目标构建通过；未访问串口、未烧录、未做实机测试。**
本说明优先于继承的历史 README。工程：`ARCHIVE/rm-p4-uvc-p123-color-pending-20261105`；直接基线：`ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104`。

## 本版行为

摄像头仍配置为 640×480 YUY2、10 FPS。预览仍为 320×240、HTTP 单帧 JPEG、质量55；只将灰度预览改为彩色。使用与原版相同的2倍抽样位置，复用官方 UVC 示例的 BT.601 limited-range 转换公式，保留8位色阶，按本机JPEG驱动要求写BGR24，再用既有硬件JPEG编码API、YUV422采样输出。此色彩矩阵是实现假设，真实相机颜色准确度尚未核验。

P1蓝2倍/P2绿2倍/P3红4倍、P3>P2>P1、批内冻结全部保留。`basic_uvc_stream.c`、三份继电器源码/配置、网页HTML/JS与基线逐字节一致；识别门槛、200ms最小采样、语音、GPIO、清零/暂停、网页亮灯颜色与状态继续沿用基线。600ms请求预算、300ms前端帧龄规则和250ms板端过期处理保留。

新增转换模块 `main/preview_color.c/.h`，预览模块接入彩色缓冲并调整编码格式；CMake增加该模块。状态接口版本标记为 `p123-color-pending-v0.1`。没有重新设计网页或改用WebSocket/MJPEG采集。

原始预览缓冲76800→230400 bytes，名义增加153600 bytes（150 KiB）；不含分配器元数据。三个JPEG缓冲继续各64 KiB，沿用原超限/编码错误丢帧处理，没有无界扩容。更高CPU/带宽占用及64 KiB上限在真实场景的影响未测。

## 实际验证

- 原P123生产状态机/集成共2594项断言、2例错误配置按预期拒编译；后端55项、实际状态JSON及5组JS策略通过。
- 实际生产转换C通过八色色条、整帧独立浮点BT.601参考比对（最大通道误差1级）、抽样位置/边界及非法缓冲检查。软件JPEG色条回环使用Pillow，仅证明软件参考颜色，不能当作P4硬件输出。
- 提取实际提交/编码函数的主机桥接34项检查通过，覆盖BGR输入、RGB888/YUV422配置、USB缓冲复用后数据、发布与错误释放；JPEG及RTOS是API桩。
- 既有SDK 5.5.4示例素材数值比对支持BGR顺序，记录见 `sdk-pixel-layout-check.json`。原网页未变，本轮未重复浏览器布局测试。
- 新工程目标构建退出0，源码根路径核对正确。首次构建会话中断且原因不确定，未生成镜像；从缓存续编退出0。保留中断记录和两份日志。
- 仅继承3条AprilTag依赖警告；新增代码无目标编译警告。原P123、B、C、C Plus共12372个文件全量大小/哈希不变；依赖锁、SDK配置、分区表不变。没有安装或升级软件。

## 镜像与复现

应用：`ARCHIVE/rm-p4-uvc-p123-color-pending-20261105/build/basic_uvc_stream.bin`，1,471,152 bytes；3 MiB应用分区剩余1,674,576 bytes。
SHA-256：`12fda5d6333dce8c35312e5be16aa6fc4413b6e624ba25b9007b9a3a6baaefb1`。

证据入口：`<USERPROFILE>\Documents\Codex\2026-11-05\rm-p123-color\work\verification.json`。
测试：用既有Python运行工程下 `tests/run_tests.py`、`tests/run_preview_tests.py`、`tests/run_color_tests.py`。构建入口：`<USERPROFILE>\Documents\Codex\2026-11-05\rm-p123-color\work\build.ps1`。这些入口不访问串口。

## 复用依据与边界

- [官方ESP-IDF 5.5.4 JPEG接口](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/api-reference/peripherals/jpeg.html)；本机相同版本头文件、驱动和示例已核对。
- [官方编码器BGR24说明](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/jpeg.rst)；本机5.5.4素材另作数值核对，未用新版本API。
- 转换公式取自已安装的官方 `espressif/usb_host_uvc` 2.6.0 `examples/camera_display/main/yuy2.c`，Apache-2.0；保留归属说明及项目中原许可证。未增加第三方依赖。
- 已查阅[上游JPEG解码器安全公告](https://github.com/espressif/esp-idf/security/advisories/GHSA-v6r2-f6p2-88cj)，公告列出5.5.4受影响。本候选只走编码API，生产主程序没有调用硬件JPEG解码入口；本轮未升级SDK，也未进行SDK整体安全审计。

仍未验证：真实相机输入到彩色JPEG、P4硬件编码、Android手机显示/帧率/端到端延迟、识别并发、继电器电气表现、弱网与持续运行。本版保留为本地候选，烧录需另行授权；本轮未改变板上应用。
