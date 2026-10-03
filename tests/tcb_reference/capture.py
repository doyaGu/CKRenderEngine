"""Capture synthetic reference curves from the specific original Win32 runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True, help='Original installation containing Bin and RenderEngines')
parser.add_argument('--runner', type=Path, required=True, help='Win32 tcb_oracle executable')
parser.add_argument('--output-dir', type=Path, required=True, help='Output directory for fixtures, samples and header')
args = parser.parse_args()
HERE = args.output_dir.resolve()
ROOT = args.root.resolve(strict=True)
RUNNER = args.runner.resolve(strict=True)
HERE.mkdir(parents=True, exist_ok=True)
TYPES = ((0x347e4a01, False), (0x1b545904, False), (0x45b52a02, True), (0x32595908, True))

# Entirely synthetic keys: time, vector/quaternion, tension, continuity, bias,
# ease-to, ease-from. Include nonuniform times, endpoint rules and quaternion signs.
fixtures = [
    ('VectorTwoKeys', False, [
        [2, 1, -2, 3, .25, .7, -.6, 0, 0],
        [10, 9, 4, -1, -.4, -.3, .8, 0, 0]], [2, 3, 4, 6, 8, 9, 10]),
    ('VectorZeroTCB', False, [
        [1, 0, 0, 0, 0, 0, 0, 0, 0],
        [4, 6, -3, 2, 0, 0, 0, 0, 0],
        [10, 10, 8, -4, 0, 0, 0, 0, 0]], [1, 1.75, 2.5, 3.25, 4, 5.5, 7, 8.5, 10]),
    ('VectorUnevenTCB', False, [
        [0, 1, -2, 3, .25, .5, -.25, .4, .2],
        [2, -4, 3, 2, -.35, -.6, .4, .3, .1],
        [9, 6, -1, -2, .5, .7, -.5, .2, .4],
        [20, -2, 5, 7, -.2, -.3, .6, .3, .2]], [0, .5, 1, 1.5, 2, 3.75, 5.5, 7.25, 9, 11.75, 14.5, 17.25, 20]),
    ('QuaternionTwoKeys', True, [
        [2, 0, 0, 0, 1, 0, 0, 0, 0, 0],
        [10, 0, 0, .6, .8, 0, 0, 0, 0, 0]], [2, 3, 4, 6, 8, 9, 10]),
    ('QuaternionUnevenTCB', True, [
        [0, 0, 0, 0, 1, .25, .5, -.25, .4, .2],
        [2, .6, 0, 0, .8, -.35, -.6, .4, .3, .1],
        [9, 0, .8, 0, .6, .5, .7, -.5, .2, .4],
        [20, 0, 0, -.6, .8, -.2, -.3, .6, .3, .2]], [0, .5, 1, 1.5, 2, 3.75, 5.5, 7.25, 9, 11.75, 14.5, 17.25, 20]),
    ('QuaternionOppositeSigns', True, [
        [0, 0, 0, 0, 1, .25, .5, -.25, 0, 0],
        [2, -.6, 0, 0, -.8, -.35, -.6, .4, 0, 0],
        [9, 0, .8, 0, .6, .5, .7, -.5, 0, 0],
        [20, 0, 0, .6, -.8, -.2, -.3, .6, 0, 0]], [0, .5, 1, 1.5, 2, 3.75, 5.5, 7.25, 9, 11.75, 14.5, 17.25, 20]),
]

def f(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text: text += '.0'
    return text + 'f'

digest = hashlib.sha256((ROOT / 'RenderEngines/CK2_3D.dll').read_bytes()).hexdigest()
math_digest = hashlib.sha256((ROOT / 'Bin/VxMath.dll').read_bytes()).hexdigest()
if digest != '5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590':
    raise RuntimeError('CK2_3D.dll does not match the controller addresses used by this runner')
if math_digest != 'bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2':
    raise RuntimeError('VxMath.dll does not match the reference math implementation')
output = ['// Synthetic TCB reference samples captured by executing the original Win32 DLL.',
          '// CK2_3D.dll SHA-256: ' + digest,
          '// VxMath.dll SHA-256: ' + math_digest,
          '// See docs/serialization-alignment.md for capture details and comparison scope.',
          '#pragma once', '',
          'namespace TCBReference {',
          'struct Sample { float time; float value[4]; };',
          'struct Fixture { const char *name; bool rotation; int keyCount; const float *keys; int sampleCount; const Sample *samples; };', '']
records = []
for name, rotation, keys, times in fixtures:
    samples = None
    for kind, is_rotation in TYPES:
        if is_rotation != rotation: continue
        raw = struct.pack('<I', len(keys)) + b''.join(struct.pack('<' + 'f'*len(key), *key) for key in keys)
        path = HERE / f'{name}-{kind:08x}.bin'
        path.write_bytes(struct.pack('<IfI', kind, 20., len(raw)//4) + raw + struct.pack('<I',len(times)) + struct.pack('<'+'f'*len(times), *times))
        run = subprocess.run([str(RUNNER), str(ROOT), str(path)], capture_output=True, timeout=10)
        if run.returncode: raise RuntimeError((name, kind, run.returncode, run.stdout, run.stderr))
        result = json.loads(run.stdout)
        records.append({'fixture':name, **result})
        if samples is not None: assert samples == result['samples'], (name, 'controller aliases differ')
        samples = result['samples']
    output.append(f'static const float {name}Keys[] = {{')
    output += ['    ' + ', '.join(map(f, key)) + ',' for key in keys]
    output += ['};', f'static const Sample {name}Samples[] = {{']
    output += ['    {' + f(sample['time']) + ', {' + ', '.join(map(f, sample['value'])) + '}},' for sample in samples]
    output += ['};', '']
output += ['static const Fixture Fixtures[] = {']
for name, rotation, keys, times in fixtures:
    output.append(f'    {{"{name}", {str(rotation).lower()}, {len(keys)}, {name}Keys, {len(times)}, {name}Samples}},')
output += ['};', '} // namespace TCBReference', '']
(HERE / 'tcb-reference.json').write_text(json.dumps({'dll_sha256':digest,'math_sha256':math_digest,'records':records}, indent=2))
(HERE / 'TCBReferenceSamples.h').write_text('\n'.join(output))
print(f'Captured {len(records)} controller fixtures, {sum(len(r["samples"]) for r in records)} evaluations; SHA256={digest}')
