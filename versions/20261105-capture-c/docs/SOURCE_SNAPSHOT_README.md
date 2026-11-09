# C版：MJPEG采集与按需识别解码候选

交付日期：2026-11-05。目录沿用2026-11-04开工命名。本文件是本版入口，继承的其他README只记录历史版本。

**独立待定候选，不入烧录队列，未访问串口，未烧录或操作硬件。尚未证明真实摄像头、P4硬解、手机观看或压缩后识别效果。**

工程：`ARCHIVE/rm-p4-uvc-mjpeg-highfps-pending-20261104`。
直接基线：`ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104`。
证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-bc-highfps\work\verification.json`、`C\build-result.json`。

## 本版行为

- 请求640×480 MJPEG。默认目标30 FPS，按声明帧间隔选最接近且不超过目标的档位；若只支持更高档位则采用声明默认档，预览仍独立限制约30 FPS。此为请求/限额，不是实测性能。没有声明MJPEG时明确报错并不启动此候选采集，不静默冒充YUY2版。
- 捕获缓冲上限614400 bytes。每帧用纯C检查完整基线JPEG、640×480尺寸及所需DHT，去除EOI后的UVC填充；所有DHT都省略时补标准Annex K表，原有自定义DHT保留，部分缺失/非基线/错误尺寸等帧拒绝使用。没有逐帧动态分配。
- 正常MJPEG通过独立有界缓冲直接转发给手机，省掉预览重新编码。转发上限256 KiB/帧，超出则丢预览帧，识别路径仍可处理捕获容量以内的有效帧。网页原320×240画布按比例绘制640×480输入，布局不变。
- 识别仍按原200 ms间隔和检测缓冲空闲情况取帧。使用已有P4硬件JPEG驱动：彩色JPEG解码为RGB888再取灰度，灰度JPEG用GRAY输出，填充原320×240及按需640×480 L1/L2缓冲。解码失败不通知检测器，不把上次像素当新帧。
- UART诊断抓拍也跟随MJPEG解码路径，保留原抓拍协议；本轮未调用串口。USB积压时归还旧帧缓冲并处理较新帧；逐帧日志降至DEBUG。网络发送使用自己的拷贝，不持有USB缓冲。
- 继承原通信、灯点、超时与过期规则，没有加入A路线的完整网络故障恢复重构。网络稳定性与最终吞吐需实机判断，JPEG帧通常比旧320×240灰度预览更大。

P1默认蓝2倍，P2绿2倍，P3红4倍；优先级P3>P2>P1，本批冻结。`rm_relay.c/.h`及`rm_relay_config.h`与基线相同。识别门槛、确认规则、音频、GPIO、清除/暂停逻辑保留；有损输入的效果不能由“算法代码没改”推出。

## 验证范围

`tests/run_capture_tests.py`记录在`tests/out/capture/results.json`：实际生产P123核心/集成、选档/限帧、HTTP/实际发布函数、JS；3组JPEG解析/补DHT/像素转换；规范化JPEG经已安装Pillow解码，与对应完整JPEG的像素一致；真实桥接C配硬件API桩验证数据流和错误时不使用旧像素。

这些证据不包括P4硬件JPEG解码输出、真实摄像头码流兼容、实际AprilTag识别准确率、手机FPS、端到端延迟或长时间连接。20～30 FPS只是后续实机对比的目标。历史同型号摄像头描述符声明过MJPEG默认60 FPS，不能当作实测。

`/status`附格式、目标、预览限额、解码就绪、JPEG准备错误、补DHT次数、解码次数/错误和最近耗时。兼容键`encoded`在C版表示准备好可转发的JPEG数量，网页标“直传”，不表示执行了编码。网页帧龄不含完整曝光/USB/屏幕延迟，600 ms是请求预算。主动限帧也会计入丢帧。

## 复现与来源

依赖版本与B/基线一致，未新增、安装或升级组件；硬件JPEG接口已包含在ESP-IDF5.5.4的esp_driver_jpeg。UVC/JPEG驱动为Espressif Apache-2.0实现；标准Huffman常量由现有Pillow12.3.0/libjpeg以optimize=False序列化JPEG后提取，属于标准表数据，未复制来源不明代码。测试图为本轮合成数据，不含用户素材。

```powershell
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-mjpeg-highfps-pending-20261104/tests/run_capture_tests.py'
& "$env:USERPROFILE\Documents\Codex\Tools\bin\pwsh\pwsh.exe" -NoProfile -File '<USERPROFILE>\Documents\Codex\2026-11-04\rm-bc-highfps\work\build_candidate.ps1' -Variant C
```

以上不访问串口。镜像在`build/basic_uvc_stream.bin`，哈希/大小/3 MiB分区余量以verification.json为准。只在本地待定，硬件动作需另行授权；更改源码后须刷新测试、构建与哈希。
