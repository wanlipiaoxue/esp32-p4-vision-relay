# C Plus：有界长连接图传候选

日期：2026-11-05。**独立待定版本；未入烧录队列、未访问串口、未烧录或操作硬件。** 本文件优先于目录内继承的历史README与旧PRODUCT口径。

工程：`ARCHIVE/rm-p4-uvc-mjpeg-cplus-pending-20261105`。
基线：`ARCHIVE/rm-p4-uvc-mjpeg-highfps-pending-20261104`。
验证入口：`<USERPROFILE>\Documents\Codex\2026-11-05\rm-cplus\work\verification.json`。

## 相对C版的实现

- 采集目标由30提高为60 FPS，仍只选择摄像头声明的合法MJPEG帧间隔；没有目标档位时按原策略使用可用声明档。目标不是实测帧率。
- 网页与状态查询保留HTTP80，视频改为独立HTTPD81的WebSocket。原`/frame.jpg`轮询接口不作为C Plus视频入口。使用现有ESP-IDF WebSocket/queue_work接口，无新播放器或第三方依赖。
- 单观看端、每次一帧额度。浏览器收到、验证并处理完一帧后回执并申请下一帧，不积压图片或并行解码。发送从30 FPS起按回执在5～60之间调节，慢发送、过期丢弃或超时会降档。独立服务任务减少大图发送对状态处理的直接阻塞；无线链路仍共用，不能据此保证完全无影响。
- 规范化JPEG直接写三槽引用池；识别持有当前引用，发布持有最新引用，发送持有只读引用至SDK send返回。移除C版发布和HTTP暂存的两次整帧复制。仍有规范化写入、SDK/TCP内部复制，不宣称全链路零拷贝。
- 无观看额度且不需识别或诊断抓拍时，不逐帧规范化JPEG。保留原捕获容量614400 bytes及补DHT空间，视频单帧上限仍256 KiB；超过视频上限的合法输入仍可供识别。
- 网络初始化、AP及Hosted故障采用外层最多3次退避恢复，要求TRANSPORT_UP事件确认；不使用完整Hosted反初始化或主动P4重启，不重置P123状态。依赖内部等待沿用SDK，单次耗时和极端SDIO挂死恢复未实测。
- 浏览器先从状态接口取得视频端口；保留300 ms保守帧龄与600 ms请求预算。过期清空、暂停/后台关闭视频、迟到解码不重绘、断线重连均有本地Mock验证。灯点只展示逻辑模式，不代表触点电压或真实闪烁相位。

## 保留内容与代价

P1蓝2倍、P2绿2倍、P3红4倍，P3>P2>P1，批内冻结。继电器核心/配置、识别确认门槛、200 ms最小取样间隔、音频、清除和GPIO逻辑保留。彩色JPEG仍解码为RGB888后取灰度：该SDK彩色采样的直接GRAY DMA表不可用，未盲目开启。

三槽换取更少复制与可控所有权；按申请量计算，比C版相关JPEG缓冲多约706,656 bytes（约0.67 MiB）PSRAM，不含分配器对齐与元数据。TCP发送缓冲5760→11520 bytes，socket上限10→16；CPU400 MHz、PSRAM200 MHz、SDIO四位40 MHz及依赖版本保留。更高输入帧率/有损压缩/无线负载对识别和稳定性的影响仍需实机对比。

## 验证与复现

- 主机：原P123核心/集成2594项与两例拒编译；引用池/额度/自适应/恢复658项；实际WebSocket适配函数82项；实际状态JSON26项；JPEG解析/补DHT/灰度3×22项及软件解码像素一致；桥接C配硬件API桩36项；7组JS策略/实际C帧头解析。
- 浏览器：真实嵌入HTML/JS、本机既有Chromium、本地RFC6455子集Mock，9个场景和4种视口；测试画面明确标LOCAL MOCK，不是设备性能。模拟后台不等于真实Android后台测试。
- 目标构建、源码绑定、受保护B/C/P123全量哈希与镜像大小/哈希见verification.json。仅执行者自检，不是独立或实物验收。

```powershell
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-mjpeg-cplus-pending-20261105/tests/run_cplus_tests.py'
node 'ARCHIVE/rm-p4-uvc-mjpeg-cplus-pending-20261105/tests/browser_cplus.mjs'
& "$env:USERPROFILE\Documents\Codex\Tools\bin\pwsh\pwsh.exe" -NoProfile -File '<USERPROFILE>\Documents\Codex\2026-11-05\rm-cplus\work\build_cplus.ps1'
```

这些入口不访问串口；浏览器仅连本地Mock并复用已安装Chrome/Edge。固件在`build/basic_uvc_stream.bin`。实际摄像头/P4硬解/Android手机FPS、完整拍摄到屏幕延迟、持续运行/恢复和压缩后识别均未测试。后续硬件动作需另行明确授权，修改源码后须更新证据与镜像哈希。
