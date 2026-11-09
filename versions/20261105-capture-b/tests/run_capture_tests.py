"""Host-only candidate verification. Never opens serial or runs flashing tools."""
import hashlib
import json
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/out/capture"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    mjpeg = "#define RM_CAPTURE_USE_MJPEG 1" in (ROOT/"main/rm_capture_config.h").read_text()
    compiler, node = shutil.which("gcc"), shutil.which("node")
    if not compiler or not node: raise RuntimeError("Existing GCC and Node required; nothing is installed")
    import sys
    logs, summary = [], {"variant":"C" if mjpeg else "B"}
    passed = False

    def run(command):
        command = [str(x) for x in command]
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
        logs.append({"command":command, "exit_code":result.returncode,
                     "seconds":round(time.monotonic()-started,3), "stdout":result.stdout,"stderr":result.stderr})
        if result.returncode: raise RuntimeError(result.stdout + "\n" + result.stderr)
        return result.stdout

    try:
        relay_output = run([sys.executable,"-X","utf8",ROOT/"tests/run_tests.py","--out",OUT/"relay"])
        summary["relay_output"] = [s for s in relay_output.splitlines() if s.startswith("PASS")]
        run([sys.executable,"-X","utf8",ROOT/"tests/run_preview_tests.py"])
        run([node,"--check",ROOT/"main/web/preview.js"])
        common = [compiler,"-std=c11","-O2","-Wall","-Wextra","-Werror","-I",ROOT/"main"]
        policy = OUT/"test_capture_modes.exe"
        run(common+[ROOT/"tests/test_capture_modes.c",ROOT/"main/rm_capture_mode.c","-o",policy])
        summary["capture_policy"] = run([policy]).strip()
        if mjpeg:
            from PIL import Image
            jpeg = OUT/"test_jpeg.exe"
            run(common+[ROOT/"tests/test_jpeg.c",ROOT/"main/rm_jpeg.c","-o",jpeg])
            summary["jpeg_cases"] = []
            fixtures = ROOT/"tests/fixtures"
            for name,missing in (("color-full",False),("color-no-dht",True),("gray-full",False)):
                target = OUT/(name+"-normalized.jpg")
                result = run([jpeg,fixtures/(name+".jpg"),target,int(missing)]).strip()
                expected = fixtures/("color-full.jpg" if missing else name+".jpg")
                with Image.open(expected) as original, Image.open(target) as normalized:
                    assert original.size == normalized.size == (640,480)
                    assert original.convert("RGB").tobytes() == normalized.convert("RGB").tobytes()
                summary["jpeg_cases"].append({"case":name,"production_c_result":result,"pillow_decoded_pixels_equal":True})
            bridge = OUT/"test_mjpeg_bridge.exe"
            run(common+["-I",ROOT/"tests/host_include",ROOT/"tests/test_mjpeg_bridge.c",
                        ROOT/"main/rm_mjpeg.c",ROOT/"main/rm_jpeg.c","-o",bridge])
            summary["mjpeg_bridge"] = run([bridge,fixtures/"color-no-dht.jpg",fixtures/"gray-full.jpg"]).strip()
        preview = json.loads((ROOT/"tests/out/preview/results.json").read_text())
        backend = preview["logs"][1]["stdout"]
        statuses = [json.loads(body) for _,body in re.findall(r"^STATUS_(\w+) (.+)$",backend,re.M)]
        assert len(statuses)==3
        for item in statuses:
            assert item["capture_format"] == ("MJPEG" if mjpeg else "YUY2")
            assert (item["width"],item["height"]) == ((640,480) if mjpeg else (320,240))
            assert item["target_fps"] == item["preview_limit_fps"] == 30
            assert item["capture_fps_config"] == 30
        summary["backend_assertions"] = re.search(r"PASS backend assertions=(\d+)",backend).group(1)
        summary["status_json_variant_fields"] = True
        summary["preview_client_cases"] = 5
        passed = True
        print("HOST_CHECKS_PASSED",summary["variant"], flush=True)
        print(json.dumps(summary,ensure_ascii=False), flush=True)
    finally:
        (OUT/"results.json").write_text(json.dumps({"passed":passed,"scope":"Host production C and API stubs; no physical JPEG/UVC/Wi-Fi/phone tests",
            "summary":summary,"logs":logs,"main_sha256":hashlib.sha256((ROOT/"main/basic_uvc_stream.c").read_bytes()).hexdigest()},ensure_ascii=False,indent=2),encoding="utf-8")


if __name__ == "__main__": main()
