"""Capture synthetic linear-controller boundary cases from the original DLL."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--runner', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
root = args.root.resolve(strict=True)
runner = args.runner.resolve(strict=True)
output = args.output_dir.resolve()
output.mkdir(parents=True, exist_ok=True)
hashes = {}
for path, expected in [
    ('RenderEngines/CK2_3D.dll', '5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590'),
    ('Bin/VxMath.dll', 'bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2'),
]:
    actual = hashlib.sha256((root / path).read_bytes()).hexdigest()
    if actual != expected:
        raise RuntimeError(f'{path} does not match the reference implementation')
    hashes[path] = actual

fixtures = [
    ('VectorSingle', False, [[2, 1, -2, 3]]),
    ('VectorUneven', False, [[0, 1, -2, 3], [2, -4, 5, -6], [9, 7, -8, 9], [20, 10, 2, -1]]),
    ('VectorRepeatedTimes', False, [[0, 1, -2, 3], [2, -4, 5, -6], [2, 7, -8, 9], [20, 10, 2, -1]]),
    ('QuaternionSingle', True, [[2, -.6, 0, 0, -.8]]),
    ('QuaternionOppositeSigns', True, [[0, 0, 0, 0, 1], [2, -.6, 0, 0, -.8],
                                       [9, 0, .8, 0, .6], [20, 0, 0, .6, -.8]]),
    ('QuaternionRepeatedTimes', True, [[0, 0, 0, 0, 1], [2, .6, 0, 0, .8],
                                       [2, 0, .8, 0, .6], [20, 0, 0, -.6, .8]]),
]
times = [-1., 0., 1., 2., 2.00001, 3., 5., 9., 9.00001, 15., 20., 21.]


def f(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


header = ['// Synthetic linear-controller samples captured from the original Win32 DLL.',
          '// See tests/tcb_reference/capture_linear.py for inputs and binary hashes.',
          '#pragma once', '', 'namespace LinearReference {',
          'struct Sample { float time; float value[4]; };',
          'struct Fixture { const char *name; bool rotation; int keyCount; const float *keys; int sampleCount; const Sample *samples; };', '']
records = []
for name, rotation, keys in fixtures:
    raw = struct.pack('<I', len(keys)) + b''.join(struct.pack('<' + 'f' * len(key), *key) for key in keys)
    pair = (0x49ED4002, 0x2F200B08) if rotation else (0x637C4301, 0x654A3A04)
    reference = None
    for kind in pair:
        path = output / f'{name}_{kind:08x}.bin'
        path.write_bytes(struct.pack('<IfI', kind, 20., len(raw) // 4) + raw +
                         struct.pack('<I', len(times)) + struct.pack('<' + 'f' * len(times), *times))
        run = subprocess.run([str(runner), str(root), str(path), '--dump'], capture_output=True, timeout=10)
        if run.returncode:
            raise RuntimeError((name, kind, run.returncode, run.stdout, run.stderr))
        result = json.loads(run.stdout)
        if result['serialized'] != list(struct.unpack('<' + 'I' * (len(raw) // 4), raw)):
            raise RuntimeError('Linear evaluation changed stored keys')
        records.append({'fixture': name, **result})
        if reference is None:
            reference = result
        elif result['samples'] != reference['samples']:
            raise RuntimeError('Paired linear controller types disagree')
    header.append(f'static const float {name}Keys[] = {{')
    header.extend('    ' + ', '.join(map(f, key)) + ',' for key in keys)
    header.extend(['};', f'static const Sample {name}Samples[] = {{'])
    for sample in reference['samples']:
        header.append('    {' + f(sample['time']) + ', {' + ', '.join(map(f, sample['value'])) + '}},')
    header.extend(['};', ''])
header.append('static const Fixture Fixtures[] = {')
for name, rotation, keys in fixtures:
    header.append(f'    {{"{name}", {str(rotation).lower()}, {len(keys)}, {name}Keys, {len(times)}, {name}Samples}},')
header.extend(['};', '} // namespace LinearReference', ''])
(output / 'LinearReferenceSamples.h').write_text('\n'.join(header), encoding='utf-8')
(output / 'linear-reference.json').write_text(json.dumps({'hashes': hashes, 'records': records}, indent=2), encoding='utf-8')
print(f'Captured {len(records)} linear fixtures, {sum(len(r["samples"]) for r in records)} evaluations')
