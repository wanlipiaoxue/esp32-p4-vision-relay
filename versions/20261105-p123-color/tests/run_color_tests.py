"""Actual C conversion + independent numeric reference + real pipeline API stubs."""
from pathlib import Path
import ctypes
import hashlib
import io
import json
import shutil
import subprocess
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'tests/out/color'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    logs = []
    report = {'passed': False, 'hardware_jpeg_tested': False, 'cases': []}
    def run(command):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=90)
        logs.append({'command': list(map(str, command)), 'exit_code': result.returncode,
                     'stdout': result.stdout, 'stderr': result.stderr})
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout
    def passed(name):
        report['cases'].append(name)
        print('PASS', name)
    try:
        compiler = shutil.which('gcc')
        if not compiler:
            raise RuntimeError('Existing host GCC required')
        common = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-I', ROOT / 'main', '-I', OUT]
        library = OUT / 'preview_color.dll'
        run(common + ['-shared', ROOT / 'main/preview_color.c', '-o', library])
        convert = ctypes.CDLL(str(library)).preview_color_convert
        convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        convert.restype = ctypes.c_bool
        def actual(frame):
            original = frame.copy()
            storage = np.full(320 * 240 * 3 + 32, 0xa5, dtype=np.uint8)
            assert convert(frame.ctypes.data, frame.nbytes, storage.ctypes.data, 320 * 240 * 3)
            assert np.all(storage[-32:] == 0xa5) and np.array_equal(frame, original)
            return storage[:-32].reshape(240, 320, 3).copy()
        frame = np.zeros((480, 320, 4), dtype=np.uint8)
        bars = [(16,128,128), (235,128,128), (82,90,240), (145,54,34),
                (41,240,110), (210,16,146), (170,166,16), (107,202,222)]
        expected = [(0,0,0), (255,255,255), (0,1,255), (1,255,0),
                    (255,0,0), (0,255,255), (255,255,1), (255,0,255)]
        for i, (y,u,v) in enumerate(bars):
            frame[:, i*40:(i+1)*40, :] = (y,u,y,v)
        result = actual(frame)
        for i, pixel in enumerate(expected):
            assert np.max(np.abs(result[:, i*40:(i+1)*40].astype(int) - pixel)) <= 2
        passed('8 known-color bars retain BGR channel order and chroma')
        rng = np.random.default_rng(20261105)
        random_frame = rng.integers(0, 256, frame.shape, dtype=np.uint8)
        result_random = actual(random_frame)
        sampled = random_frame[::2].astype(np.float64)
        y, u, v = sampled[:,:,0] - 16, sampled[:,:,1] - 128, sampled[:,:,3] - 128
        reference = np.stack([1.16438356*y + 2.01723214*u,
                              1.16438356*y - 0.39176229*u - 0.81296765*v,
                              1.16438356*y + 1.59602679*v], axis=2)
        reference = np.clip(np.rint(reference), 0, 255).astype(np.uint8)
        error = np.abs(result_random.astype(int) - reference.astype(int))
        report['independent_bt601_max_channel_error'] = int(error.max())
        assert error.max() <= 2
        passed('full-frame independent floating BT.601 reference within 2 levels')
        changed = random_frame.copy()
        changed[1::2] ^= 0xff
        changed[::2,:,2] ^= 0xff
        assert np.array_equal(actual(changed), result_random)
        passed('2x sample positions match original grayscale preview; final row/column covered')
        out = np.full(230432, 0xa5, dtype=np.uint8)
        for src, size, dst, capacity in [(None,frame.nbytes,out.ctypes.data,230400),
            (frame.ctypes.data,frame.nbytes,None,230400),
            (frame.ctypes.data,frame.nbytes-1,out.ctypes.data,230400),
            (frame.ctypes.data,frame.nbytes+1,out.ctypes.data,230400),
            (frame.ctypes.data,frame.nbytes,out.ctypes.data,230399)]:
            assert not convert(src,size,dst,capacity)
            assert np.all(out == 0xa5)
        passed('invalid buffer arguments rejected without modifying destination')
        rgb = result[:,:,::-1].copy()
        stream = io.BytesIO()
        Image.fromarray(rgb).save(stream, format='JPEG', quality=55, subsampling=1)
        jpeg = stream.getvalue()
        decoded = np.asarray(Image.open(io.BytesIO(jpeg)).convert('RGB'))
        for i in range(8):
            center = decoded[20:220,i*40+5:(i+1)*40-5].astype(int)
            target = rgb[20:220,i*40+5:(i+1)*40-5].astype(int)
            assert np.abs(center-target).mean() < 4
        assert len(jpeg) < 65536
        (OUT / 'SOFTWARE_REFERENCE_color_bars.jpg').write_bytes(jpeg)
        report['software_reference_jpeg_bytes'] = len(jpeg)
        passed('Pillow-only reference JPEG roundtrip preserves colored bars (not P4 hardware)')
        source = (ROOT / 'main/wifi_preview.c').read_text(encoding='utf-8')
        begin = source.index('void wifi_preview_submit_yuy2')
        end = source.index('static esp_err_t root_handler', begin)
        (OUT / 'color_pipeline.inc').write_text(source[begin:end], encoding='utf-8')
        executable = OUT / 'test_color_pipeline.exe'
        run(common + [ROOT/'tests/test_color_pipeline.c', ROOT/'main/preview_color.c', '-o', executable])
        print(run([executable]).strip())
        passed('production submit/encode config, USB buffer ownership, publication and error release')
        report['passed'] = True
    finally:
        report['logs'] = logs
        report['source_sha256'] = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
            for name in ('main/preview_color.c','main/preview_color.h','main/wifi_preview.c')}
        (OUT/'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
