"""Capture synthetic Morph evaluations from the specific original Win32 DLL."""
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
output_dir = args.output_dir.resolve()
output_dir.mkdir(parents=True, exist_ok=True)
hashes = {}
for path, expected in [
    ('RenderEngines/CK2_3D.dll', '5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590'),
    ('Bin/VxMath.dll', 'bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2'),
]:
    actual = hashlib.sha256((root / path).read_bytes()).hexdigest()
    if actual != expected: raise RuntimeError(f'{path} does not match the reference implementation')
    hashes[path] = actual

key_times = [2., 10., 20.]
positions = [
    [(1, -2, 3), (4, 5, -6), (-3, 8, 1), (9, -4, 2)],
    [(-5, 1, 8), (7, -3, 2), (6, -2, 4), (-1, 3, 5)],
    [(3, 7, -4), (0, 9, 1), (-8, 5, -2), (4, -6, 9)],
]
normals = [
    [(100, 16000), (-100, 400), (-8192, -400), (1001, 0)],
    [(-99, 400), (99, 16000), (8191, 8000), (-1002, -8192)],
    [(333, -8200), (-555, 8192), (0, -8000), (1001, 16383)],
]
times = [0., 2., 2.5, 3., 3.33333, 4.125, 6., 9.75, 9.99999, 10., 11.25, 15., 19.99, 20., 22.]

def wire(key_count=3, has_normals=True):
    raw = struct.pack('<3I', key_count, 4, int(has_normals and key_count > 0))
    for i in range(key_count):
        raw += struct.pack('<f', key_times[i])
        for position in positions[i]: raw += struct.pack('<3f', *position)
        if has_normals:
            for normal in normals[i]: raw += struct.pack('<2h', *normal)
    return raw

fixtures = [
    ('PackedNormals', 0, wire(), 4, 12, 3),
    ('PaddedVertices', 1, wire(), 4, 28, 3),
    ('VertexSubset', 1, wire(), 2, 20, 3),
    ('NormalsOnly', 2, wire(), 4, 16, 2),
    ('VerticesOnly', 2, wire(), 4, 12, 1),
    ('NoOutput', 2, wire(), 4, 12, 0),
    ('NoNormalData', 2, wire(has_normals=False), 4, 20, 1),
    ('SingleKey', 3, wire(key_count=1), 4, 20, 3),
    ('EmptyKeys', 3, wire(key_count=0), 4, 20, 3),
    ('ZeroVertices', 4, wire(), 0, 12, 3),
]

def f(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text: text += '.0'
    return text + 'f'

header = ['// Synthetic Morph results captured by executing the original Win32 DLL.',
          '// See tests/tcb_reference/capture_morph.py for fixtures and binary hashes.',
          '#pragma once', '', 'namespace MorphReference {',
          'struct Sample { float time; int success; const float *vertices; const unsigned int *normals; };',
          'struct Fixture { const char *name; int group; int wordCount; const unsigned int *words; int vertexCount; int stride; int outputMask; int sampleCount; const Sample *samples; };', '']
records = []
for name, group, raw, count, stride, mask in fixtures:
    path = output_dir / f'{name}.bin'
    path.write_bytes(struct.pack('<IfI', 0x73847810, 20., len(raw)//4) + raw + struct.pack('<I', len(times)) +
                     struct.pack('<'+'f'*len(times), *times) + struct.pack('<3I', count, stride, mask))
    run = subprocess.run([str(runner), str(root), str(path), '--dump'], capture_output=True, timeout=10)
    if run.returncode: raise RuntimeError((name, run.returncode, run.stdout, run.stderr))
    result = json.loads(run.stdout)
    records.append({'fixture':name, **result})
    words = struct.unpack('<'+'I'*(len(raw)//4), raw)
    if list(words) != result['serialized']: raise RuntimeError((name, 'evaluation changed serialized keys'))
    header.append(f'static const unsigned int {name}Words[] = {{')
    for i in range(0, len(words), 8):
        header.append('    ' + ', '.join(f'0x{word:08x}u' for word in words[i:i+8]) + ',')
    header += ['};', f'static const float {name}Vertices[][{count*stride//4+8}] = {{']
    header += ['    {' + ', '.join(map(f, sample['vertices'])) + '},' for sample in result['samples']]
    header += ['};', f'static const unsigned int {name}Normals[][{count+2}] = {{']
    header += ['    {' + ', '.join(f'0x{n:08x}u' for n in sample['normals']) + '},' for sample in result['samples']]
    header += ['};', f'static const Sample {name}Samples[] = {{']
    for i, sample in enumerate(result['samples']):
        header.append(f'    {{{f(sample["time"])}, {sample["success"]}, {name}Vertices[{i}], {name}Normals[{i}]}},')
    header += ['};', '']
header += ['static const Fixture Fixtures[] = {']
for name, group, raw, count, stride, mask in fixtures:
    header.append(f'    {{"{name}", {group}, {len(raw)//4}, {name}Words, {count}, {stride}, {mask}, {len(times)}, {name}Samples}},')
header += ['};', '} // namespace MorphReference', '']
(output_dir/'MorphReferenceSamples.h').write_text('\n'.join(header), encoding='utf-8')
(output_dir/'morph-reference.json').write_text(json.dumps({'hashes':hashes,'records':records}, indent=2), encoding='utf-8')
print(f'Captured {len(records)} Morph fixtures, {sum(len(r["samples"]) for r in records)} evaluations')
