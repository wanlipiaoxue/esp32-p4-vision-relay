from __future__ import annotations

import re
from pathlib import Path


SOURCE = Path.home() / "Documents" / "Codex" / "user-memory.md"
OUTPUT = (
    Path.home()
    / "Documents"
    / "Codex"
    / "2026-10-25"
    / "p4-group-signal-output"
    / "work"
    / "user-memory.updated.md"
)
OLD = "正式 STM32 UART 与 GPIO 尚未接。"
NEW = (
    "**组别 GPIO 信号（2026-10-25）**：红 510/514/519/530→GPIO20、"
    "绿 533/542/547/562→GPIO21、蓝 568/573/579/582→GPIO23；"
    "仅在 TAG_LOCK 后拉高并锁存，TAG_LOST、无 Tag 和切换 profile 均不自动拉低，"
    "串口 `r` 三路清零，上电默认低。最终固件 0x94370，经 `--no-stub` 烧录校验；"
    "红 ID530、绿 ID533、蓝 ID568 已分别实机验证 LOCK→SET_HIGH→LOST 后保持→"
    "`r` CLEAR_ALL。尚未接 STM32 或驱动管，也未用万用表实测引脚电压；"
    "三路是 3.3 V 逻辑电平，不能直接驱动灯带或继电器线圈。"
)


def main() -> None:
    original_bytes = SOURCE.read_bytes()
    had_bom = original_bytes.startswith(b"\xef\xbb\xbf")
    text = original_bytes.decode("utf-8-sig")
    if text.count(OLD) != 1:
        raise RuntimeError(f"expected one memory anchor, found {text.count(OLD)}")
    updated = text.replace(OLD, NEW, 1)

    old_headings = len(re.findall(r"^#{1,6} ", text, flags=re.MULTILINE))
    new_headings = len(re.findall(r"^#{1,6} ", updated, flags=re.MULTILINE))
    if len(updated) < len(text) * 0.6 or new_headings < old_headings * 0.8:
        raise RuntimeError("preflight size/heading check failed")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes((b"\xef\xbb\xbf" if had_bom else b"") + updated.encode("utf-8"))
    print(f"output={OUTPUT}")
    print(f"old_chars={len(text)} new_chars={len(updated)}")
    print(f"old_headings={old_headings} new_headings={new_headings}")


if __name__ == "__main__":
    main()
