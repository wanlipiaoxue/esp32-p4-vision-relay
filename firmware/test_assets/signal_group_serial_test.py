from __future__ import annotations

import argparse
import re
import time
from pathlib import Path

import serial


def drain(port: serial.Serial, duration: float, chunks: list[bytes]) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        data = port.read(4096)
        if data:
            chunks.append(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--group", required=True, choices=("red", "green", "blue"))
    parser.add_argument("--id", required=True, type=int)
    parser.add_argument("--log", required=True, type=Path)
    args = parser.parse_args()

    chunks: list[bytes] = []
    with serial.Serial(args.port, 115200, timeout=0.05) as port:
        drain(port, 12.0, chunks)
        port.write(b"r")
        port.flush()
        drain(port, 1.0, chunks)
        port.write(b"ggg")
        port.flush()
        drain(port, 4.5, chunks)
        port.write(b"r")
        port.flush()
        drain(port, 1.5, chunks)

    text = b"".join(chunks).decode("utf-8", errors="replace")
    args.log.write_text(text, encoding="utf-8", newline="\n")

    lock_match = re.search(
        rf"TAG_EVENT: state=LOCK group={args.group} id={args.id} .* seq=", text
    )
    set_match = re.search(
        rf"TAG_SIGNAL: state=SET_HIGH group={args.group} id={args.id} "
        rf"gpio=\d+ level=1",
        text,
    )
    lost_match = re.search(
        rf"TAG_EVENT: state=LOST group={args.group} id={args.id} .* seq=", text
    )
    clear_matches = list(re.finditer(r"TAG_SIGNAL: state=CLEAR_ALL", text))

    failures: list[str] = []
    if not lock_match:
        failures.append("missing lock")
    if not set_match:
        failures.append("missing set_high")
    if not lost_match:
        failures.append("missing lost")
    if set_match and lost_match and lost_match.start() < set_match.start():
        failures.append("lost precedes set_high")
    clear_after_set = (
        [match for match in clear_matches if match.start() > set_match.start()]
        if set_match
        else []
    )
    if len(clear_after_set) != 1:
        failures.append(f"clear_after_set_count={len(clear_after_set)}")
    elif lost_match and lost_match.start() > clear_after_set[0].start():
        failures.append("lost follows clear")
    if failures:
        raise SystemExit(f"FAILED failures={failures} log={args.log}")
    print(f"PASS group={args.group} id={args.id} log={args.log}")


if __name__ == "__main__":
    main()
