# P1/P2/P3颜色与优先级版（2026-11-04，待定未烧录）

本目录基于`ARCHIVE/rm-p4-uvc-preview-led-pending-20261104`独立创建，保留待定版的图传通信、帧龄处理和网页灯控状态。不加入烧录队列，不自动烧录。历史继承README以本文件为最新入口。

赛前修改`main\rm_relay_config.h`三处：

```c
#define RM_P1_COLOR RM_BLUE
#define RM_P2_COLOR RM_GREEN
#define RM_P3_COLOR RM_RED
```

实际变量是`rm_p1_color`、`rm_p2_color`、`rm_p3_color`，运行中固定，改色后重新编译和另行烧录。三个值必须是互不重复的`RM_RED`、`RM_GREEN`、`RM_BLUE`；重复或越界会编译失败。旧`rm_four_color`仅作为由P3派生的只读兼容值，不是第四个独立配置。

P1/P2为2倍，P3为4倍。ENTER只从当时已经登记且未使用的颜色中按P3>P2>P1选择。P1即使更早登记，也不高于P2。选中后冻结，后来的高优先级识别不替换本批；DONE后等待下一次ENTER。空批、去抖、重复事件、清零、p/u、LOST保留。

颜色必须经过原L1/L2多帧确认才登记；这些是用户指定的颜色角色映射，不是自动识别真实点位。GPIO4/5/22仍是ENTER/DONE/全清零，无新增4倍接收输入。

交付说明：`<USERPROFILE>\Desktop\RM\01-主工作总表\P4-P123颜色变量与优先级-待定版-2026-11-04.md`。
证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-p123-priority\work\verification.json`。
状态：同目录`state.json`。本轮主机C测试和ESP-IDF构建通过，不等于实机、总管或规则验收。

```powershell
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104/tests/run_tests.py'
& "$env:USERPROFILE\Documents\Codex\Tools\bin\python\python.exe" -X utf8 'ARCHIVE/rm-p4-uvc-p123-priority-pending-20261104/tests/run_preview_tests.py'
& "$env:USERPROFILE\Documents\Codex\2026-11-04\rm-p123-priority\work\build.ps1"
```

使用本机现有Python、GCC、Node和ESP-IDF 5.5.4，无新软件或依赖安装。上述入口不访问串口。重编译后需刷新镜像哈希和验证。
