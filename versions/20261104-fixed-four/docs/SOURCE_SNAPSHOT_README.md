# P4 固定4倍颜色版（2026-11-04，本地候选未烧录）

本目录基于已烧录的 `ARCHIVE/rm-p4-uvc-handpad-relay-20261104` 新建，移除原4倍脉冲输入，改为赛前固定颜色。此前继承的其他README描述的是历史版本；本版以本文件为入口。

赛前改色只改 `main\rm_relay_config.h` 第9行：

```c
#define RM_FOUR_COLOR RM_RED
```

可选 `RM_RED` / `RM_GREEN` / `RM_BLUE`。它初始化实际配置变量 `const rm_color_t rm_four_color`。修改后重新编译并烧录才对P4生效；清零、暂停和恢复不改变此配置。

该颜色仍须经过原L1/L2多帧确认才登记4倍；上电不直接识别或点亮，其他颜色登记2倍。ENTER/DONE按4>2、同倍先登记排序，空批与批内冻结规则保留。

ENTER=GPIO4/P1-12；DONE=GPIO5/P1-11；GPIO22仍为全清零。本版不配置或读取GPIO6，没有第三个事件或一次性标记，也没有大写F注入入口。保留的两个输入可继续使用高200ms、低200ms脉冲（代码高/低各20ms去抖）。

RM交付说明：`<USERPROFILE>\Desktop\RM\01-主工作总表\P4固定4倍颜色版-候选交付-2026-11-04.md`。
本地测试/编译/核验入口：`<USERPROFILE>\Desktop\RM\tools\fixed-four-relay-candidate-20261104`。
证据：`<USERPROFILE>\Documents\Codex\2026-11-04\rm-fixed4-relay\work\verification.json`。

本版已完成主机C测试与ESP-IDF构建，未访问串口或烧录，板上仍为上一版。继电器极性仍为代码默认高有效、实物未确认；本次没有硬件或比赛规则验收结论。
