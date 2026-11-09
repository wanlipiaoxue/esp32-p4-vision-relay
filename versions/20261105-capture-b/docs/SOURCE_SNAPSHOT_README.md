# B版：YUY2声明帧率选择候选

交付日期：2026-11-05。目录沿用2026-11-04开工命名。本文件是本版入口，继承的其他README记录历史版本，不能代表本版状态。

**独立待定候选，不入烧录队列，未访问串口，未烧录或操作硬件。源码/主机测试/构建的核验见证据JSON；真实摄像头帧率、手机延迟、稳定性和识别效果未实测。**

工程：`ARCHIVE/rm-p4-uvc-yuy2-highfps-pending-20261104`。
直接基线：`ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104`。
证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-bc-highfps\work\verification.json`、`B\build-result.json`。

## 本版行为

- 继续采集640×480 YUY2。`main/rm_capture_config.h`中的`RM_CAPTURE_TARGET_FPS=30`是选档目标，不是已达到的帧率。
- 遍历摄像头声明的离散帧间隔或连续区间，选不超过目标的最快合法档位；连续档位按步长取整。UVC间隔数组由3扩到255，覆盖8位离散计数，不靠只读默认FPS猜支持范围。
- 若只有10 FPS就选择10 FPS，不伪造15/20/30档。若设备只声明高于目标的档位，则使用声明默认档；预览另限约30 FPS。打开目标档失败时可尝试同模式的设备声明默认档，不自动换采集格式。
- 预览仍为320×240灰度硬件JPEG、质量55；识别取帧间隔仍为200 ms。USB队列积压时归还旧缓冲，优先处理最新帧；逐帧日志降至DEBUG，保留30秒流状态日志。
- 继承TCP_NODELAY、最新JPEG/序号、过期帧丢弃、600 ms请求预算与300 ms应用帧龄、网页灯控逻辑状态及后台/暂停规则。此次没有重构完整网络故障恢复链路。

P1默认蓝2倍，P2绿2倍，P3红4倍；优先级P3>P2>P1，本批冻结。颜色配置仍在`main/rm_relay_config.h`。三份继电器源码/配置、识别/确认门槛、GPIO、音频、清除和暂停逻辑保留。

## 已有证据与限制

主机入口：`tests/run_capture_tests.py`，结果`tests/out/capture/results.json`。包含实际生产P123核心/集成函数、选档/限帧函数、HTTP后端与JS策略；API桩不代表硬件。

历史日志`<USERPROFILE>\Desktop\RM\tools\tag-toggle\results\toggle-smoke-20261025-213738.log`记录过0BDA:1376的YUY2 640×480默认10 FPS，未保留完整帧间隔列表，不能据此宣称更高YUY2档位已可用。本版为核实和比较该路线准备代码，实际输入/发出/手机有效帧率须在另行授权的实机测试中读取。

`/status`和网页诊断会显示格式与“协商请求”FPS，`received/encoded/sent`增量是各阶段应用计数。网页帧龄从应用取得帧起算，不含完整曝光/USB/屏幕延迟；600 ms是请求预算。旧帧丢弃计数也包括主动限帧，不等于全部是传输故障。

## 复现与依赖

只使用本机既有ESP-IDF5.5.4、UVC2.6.0、esp_hosted2.12.8、esp_wifi_remote1.5.3和原依赖锁；未升级或新增依赖。UVC和JPEG接口来自Espressif Apache-2.0实现。

```powershell
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-yuy2-highfps-pending-20261104/tests/run_capture_tests.py'
& "$env:USERPROFILE\Documents\Codex\Tools\bin\pwsh\pwsh.exe" -NoProfile -File '<USERPROFILE>\Documents\Codex\2026-11-04\rm-bc-highfps\work\build_candidate.ps1' -Variant B
```

以上仅主机检查和构建，不访问串口。新build目录独立生成；主镜像在`build/basic_uvc_stream.bin`，哈希、大小与3 MiB分区余量以verification.json为准。修改源码后不能沿用旧验证或哈希。任何硬件测试需另行明确授权。
