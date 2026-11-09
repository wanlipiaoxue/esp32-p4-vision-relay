"""Compile the actual firmware C core and extracted integration functions.
No serial, flashing, or hardware APIs are called. Requires existing host GCC.
"""
import argparse
import hashlib
import itertools
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'tests'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(command, log):
    result = subprocess.run([str(item) for item in command], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=90)
    log.append({'command': [str(item) for item in command], 'exit_code': result.returncode,
                'stdout': result.stdout, 'stderr': result.stderr})
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode:
        raise RuntimeError(f'Failed with code {result.returncode}: {command[0]}')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=TESTS / 'out')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which('gcc')
    if not compiler:
        raise RuntimeError('Existing host GCC required; no software is installed by this runner')
    source = (ROOT / 'main/basic_uvc_stream.c').read_text(encoding='utf-8')
    a = source.index('/* Every state change and GPIO write')
    b = source.index('static esp_err_t tag_audio_init', a)
    c = source.index('static bool tag_commit_lock')
    d = source.index('static void tag_detection_task', c)
    extracted = source[a:b] + '\n' + source[c:d]
    include = args.out / 'production_integration.inc'
    include.write_text(extracted, encoding='utf-8')
    log = []
    try:
        run([compiler, '--version'], log)
        common = [compiler, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                  '-I', ROOT / 'main', '-I', args.out]
        defaults = ('RM_BLUE', 'RM_GREEN', 'RM_RED')
        for mapping in [defaults] + [p for p in itertools.permutations(('RM_RED','RM_GREEN','RM_BLUE')) if p != defaults]:
            label = '-'.join(mapping)
            definitions = [] if mapping == defaults else [f'-DRM_P{i+1}_COLOR={color}' for i,color in enumerate(mapping)]
            core = args.out / f'test_relay-{label}.exe'
            run(common + definitions + [TESTS / 'test_relay.c', ROOT / 'main/rm_relay.c', '-o', core], log)
            run([core], log)
        for label, definition in [('duplicate',['-DRM_P1_COLOR=RM_GREEN']), ('invalid',['-DRM_P3_COLOR=9'])]:
            command = common + definition + ['-c',ROOT / 'main/rm_relay.c','-o',args.out / f'rejected-{label}.o']
            rejected = subprocess.run([str(x) for x in command],capture_output=True,text=True,encoding='utf-8',timeout=90)
            assert rejected.returncode != 0 and 'P1/P2/P3' in rejected.stderr, label
            log.append({'command':[str(x) for x in command], 'exit_code':rejected.returncode,
                        'expected_rejection':True,'stdout':rejected.stdout,'stderr':rejected.stderr})
            print(f'PASS compile rejects {label} role colors')
        for label, defines in [
            ('default', []),
            ('low-debug', ['-DRM_RED_ACTIVE_LEVEL=0', '-DRM_GREEN_ACTIVE_LEVEL=0',
                           '-DRM_BLUE_ACTIVE_LEVEL=0', '-DRM_UART_EVENT_INJECTION=1']),
            ('mixed', ['-DRM_RED_ACTIVE_LEVEL=0', '-DRM_GREEN_ACTIVE_LEVEL=1',
                       '-DRM_BLUE_ACTIVE_LEVEL=0']),
        ]:
            exe = args.out / f'test_integration-{label}.exe'
            # IDF task callbacks retain their original unused void *arg signature.
            run(common + ['-Wno-unused-parameter'] + defines + [TESTS / 'test_integration.c', ROOT / 'main/rm_relay.c',
                                    '-o', exe], log)
            run([exe], log)
        passed = True
    except Exception:
        passed = False
        raise
    finally:
        report = {'passed': passed, 'evidence_scope': 'host actual production C with hardware/RTOS stubs',
                  'production_source_sha256': digest(ROOT / 'main/basic_uvc_stream.c'),
                  'core_source_sha256': digest(ROOT / 'main/rm_relay.c'),
                  'extracted_integration_sha256': digest(include), 'runs': log}
        (args.out / 'test-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
