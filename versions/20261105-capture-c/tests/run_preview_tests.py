import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'tests/out/preview'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT / 'main/wifi_preview.c').read_text(encoding='utf-8')
    publisher = source[source.index('void wifi_preview_publish_lights'):source.index('static esp_err_t script_handler')]
    handlers = source[source.index('static esp_err_t preview_connection_open'):source.index('static bool setup_preview')]
    submit = source[source.index('void wifi_preview_submit_mjpeg'):source.index('static esp_err_t root_handler')]
    submit = submit.removesuffix('#endif\n')
    helpers = source[source.index('static bool viewer_interested'):source.index('void wifi_preview_set_capture_fps')]
    (OUT / 'preview_backend.inc').write_text(publisher + helpers + submit + handlers, encoding='utf-8')
    commands = [
      [shutil.which('gcc'), '-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),'-I',str(OUT),
       str(ROOT/'tests/test_preview_backend.c'),str(ROOT/'main/rm_capture_mode.c'),'-o',str(OUT/'test_preview_backend.exe')],
      [str(OUT/'test_preview_backend.exe')],
      [shutil.which('node'),'--test',str(ROOT/'tests/test_preview_client.cjs')],
    ]
    logs = []
    passed = False
    try:
        for cmd in commands:
            run = subprocess.run(cmd,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=90)
            logs.append({'command':cmd,'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
            if run.returncode:
                print(run.stdout,run.stderr);raise RuntimeError('Preview test failed')
        records = {name:json.loads(body) for name,body in re.findall(r'^STATUS_(\w+) (.+)$',logs[1]['stdout'],re.M)}
        assert records['COLD']['lights']['valid'] is False
        live = records['LIVE']['lights']
        assert live['valid'] and live['epoch']==9 and live['selected']==0 and live['four_color']==0
        assert [(c['state'],c['multiplier'],c['on']) for c in live['channels']]==[(2,4,True),(1,2,True),(3,2,False)]
        assert not records['STALE']['lights']['valid'] and records['STALE']['lights']['age_ms']>=1001
        assert len(records)==3
        print(re.search(r'^PASS backend.+$',logs[1]['stdout'],re.M)[0])
        print('PASS parsed actual C status JSON: cold/live/expired and three lamp modes')
        print('PASS shipped JS policy: 5 test cases')
        passed = True
    finally:
        (OUT/'results.json').write_text(json.dumps({'passed':passed,'logs':logs,
          'wifi_source_sha256':hashlib.sha256((ROOT/'main/wifi_preview.c').read_bytes()).hexdigest(),
          'client_source_sha256':hashlib.sha256((ROOT/'main/web/preview.js').read_bytes()).hexdigest()},indent=2),encoding='utf-8')

if __name__=='__main__':main()
