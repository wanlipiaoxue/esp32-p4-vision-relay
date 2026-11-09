# RM 手柄事件与继电器灯控候选（2026-11-04）

本目录是从已烧录手机图传 v0.1 `ARCHIVE/rm-p4-uvc-wifi-20261103` 新建的独立候选。此候选未烧录。继承的 `README_WIFI_PREVIEW.md` 不代表本候选的当前交付状态。

完整说明：`<USERPROFILE>\Desktop\RM\01-主工作总表\手柄事件与继电器灯控-独立候选交付-2026-11-04.md`。
测试、构建、只读核验入口：`<USERPROFILE>\Desktop\RM\tools\handpad-relay-candidate-20261104`。
证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-handpad-relay\work\verification.json`。

事件输入 ENTER=GPIO4/P1-12，BATCH_DONE=GPIO5/P1-11，NEXT_FOUR=GPIO6/P2-12；GPIO22 仍为全清零。默认高有效，20ms高/低去抖，新输入清除后先等稳定释放。详细来源和冲突核对见完整说明。

输出 GPIO20/21/23 的继电器极性集中在 `main/rm_relay_config.h`，默认1/1/1只是代码默认、未获实物确认。串口事件注入默认关闭。

`tests/run_tests.py` 编译真实 `main/rm_relay.c` 和从本生产源文件提取的集成函数；不访问串口或硬件。主机测试与目标编译通过不等于真实手柄、机械继电器、相机/手机并发验收，也不等于规则获得官方认可。
