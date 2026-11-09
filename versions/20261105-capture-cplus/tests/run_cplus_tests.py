import json,shutil,subprocess,sys,hashlib,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"tests/out/cplus";OUT.mkdir(parents=True,exist_ok=True)
logs=[]
def run(command):
    command=[str(x) for x in command];r=subprocess.run(command,capture_output=True,text=True,encoding="utf-8",errors="replace",timeout=180)
    logs.append({"command":command,"exit_code":r.returncode,"stdout":r.stdout,"stderr":r.stderr})
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return r.stdout
passed=False
try:
    gcc=shutil.which('gcc');common=[gcc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',ROOT/'main','-I',OUT]
    policy=OUT/'test_cplus_policy.exe'
    run(common+[ROOT/'tests/test_cplus_policy.c',ROOT/'main/cp_frame_pool.c',ROOT/'main/cp_stream_policy.c',ROOT/'main/rm_capture_mode.c','-o',policy])
    print(run([policy,OUT/'packet-header.bin']).strip(),flush=True)
    source=(ROOT/'main/wifi_preview.c').read_text(encoding='utf-8')
    extracted=source.split('/* CP_PROTOCOL_HANDLERS_BEGIN: shared production adapter, host-verifiable. */',1)[1].split('/* CP_PROTOCOL_HANDLERS_END */',1)[0]
    (OUT/'cplus_adapter.inc').write_text(extracted,encoding='utf-8')
    adapter=OUT/'test_cplus_adapter.exe'
    run(common+[ROOT/'tests/test_cplus_adapter.c',ROOT/'main/cp_frame_pool.c',ROOT/'main/cp_stream_policy.c',ROOT/'main/rm_capture_mode.c','-o',adapter])
    print(run([adapter]).strip(),flush=True)
    publisher=source[source.index('/* Called inside'):source.index('static esp_err_t script_handler')]
    status=source[source.index('static esp_err_t status_handler'):source.index('/* CP_PROTOCOL_HANDLERS_BEGIN')]
    (OUT/'cplus_status.inc').write_text(publisher+status,encoding='utf-8')
    status_exe=OUT/'test_cplus_status.exe'
    run(common+[ROOT/'tests/test_cplus_status.c',ROOT/'main/cp_stream_policy.c',ROOT/'main/rm_capture_mode.c','-o',status_exe])
    status_output=run([status_exe]);records={key:json.loads(body) for key,body in re.findall(r'^STATUS_(\w+) (.+)$',status_output,re.M)}
    assert not records['COLD']['lights']['valid'] and records['LIVE']['lights']['valid'] and not records['STALE']['lights']['valid']
    assert records['LIVE']['stream_port']==81 and records['LIVE']['capture_fps_config']==60 and records['LIVE']['stream_target_fps']==30
    assert records['LIVE']['pool_free']==2 and records['LIVE']['net_attempts']==2
    print(re.search(r'PASS CPLUS status.+',status_output)[0],flush=True)
    from PIL import Image
    jpeg=OUT/'test_jpeg.exe';run(common+[ROOT/'tests/test_jpeg.c',ROOT/'main/rm_jpeg.c','-o',jpeg])
    for name,missing in [('color-full',0),('color-no-dht',1),('gray-full',0)]:
        target=OUT/(name+'-normalized.jpg');print(run([jpeg,ROOT/'tests/fixtures'/(name+'.jpg'),target,missing]).strip(),flush=True)
        expected=ROOT/'tests/fixtures'/('color-full.jpg' if missing else name+'.jpg')
        with Image.open(expected) as a,Image.open(target) as b:assert a.convert('RGB').tobytes()==b.convert('RGB').tobytes()
    bridge=OUT/'test_mjpeg_bridge.exe'
    run(common+['-I',ROOT/'tests/host_include',ROOT/'tests/test_mjpeg_bridge.c',ROOT/'main/rm_mjpeg.c',ROOT/'main/rm_jpeg.c',ROOT/'main/cp_frame_pool.c','-o',bridge])
    print(run([bridge,ROOT/'tests/fixtures/color-no-dht.jpg',ROOT/'tests/fixtures/gray-full.jpg']).strip(),flush=True)
    relay=run([sys.executable,'-X','utf8',ROOT/'tests/run_tests.py','--out',OUT/'relay'])
    print('\n'.join(line for line in relay.splitlines() if 'assertions=' in line),flush=True)
    run([shutil.which('node'),'--test',ROOT/'tests/test_preview_client.cjs',ROOT/'tests/test_cplus_client.cjs'])
    run([shutil.which('node'),'--check',ROOT/'main/web/preview.js'])
    passed=True
finally:
    (OUT/'core-results.json').write_text(json.dumps({"passed":passed,"scope":"Production C and actual stream adapter with SDK stubs; no hardware", "production_sha256":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/"main").rglob("*") if p.is_file()},"logs":logs},indent=2),encoding='utf-8')
