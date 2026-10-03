"""Capture synthetic Bezier curves using the standalone Win32 reference runner."""
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
    if actual != expected:
        raise RuntimeError(f'{path} does not match the reference implementation')
    hashes[path] = actual

types = (0x921AB801, 0x18AB4404)
modes = [('Smooth', 1), ('Linear', 2), ('Step', 4), ('Fast', 8), ('Slow', 16), ('Explicit', 32)]
points = [(0, 1, -2, 3), (2, -4, 3, 2), (9, 6, -1, -2), (20, -2, 5, 7)]
times = [-1, 0, .5, 1, 2, 2.01, 3.75, 5.5, 8.99, 9, 11.75, 14.5, 19.99, 20, 21]

def wire(points, flags):
    raw = struct.pack('<I', len(points))
    for i, (point, (incoming, outgoing)) in enumerate(zip(points, flags)):
        raw += struct.pack('<4fI', *point, incoming | (outgoing << 16))
        if incoming & 32: raw += struct.pack('<3f', -1-i, .5+i, -2)
        if outgoing & 32: raw += struct.pack('<3f', 2+i, -1, 1+i)
    return raw

fixtures = []
for group, (in_name, incoming) in enumerate(modes):
    for out_name, outgoing in modes:
        fixtures.append((in_name + out_name, group, wire(points, [(incoming, outgoing)]*4), times))
fixtures += [
    ('TwoSmooth', 6, wire([points[0], points[-1]], [(1, 1)]*2), [-1, 0, 2, 5, 10, 15, 20, 21]),
    ('CoincidentSmooth', 6, wire([(0, 1, 2, 3), (2, 1, 2, 3), (9, 6, -1, -2), (20, 6, -1, -2)], [(1, 1)]*4), times),
    ('MixedNeighbors', 6, wire(points, [(32, 2), (1, 4), (2, 8), (32, 1)]), times),
    ('SingleExplicit', 6, wire([(2, 1, 2, 3)], [(32, 32)]), [-1, 0, 2, 3, 21]),
    ('SingleSmooth', 6, wire([(2, 1, 2, 3)], [(1, 1)]), [-1, 0, 2, 3, 21]),
    ('RepeatedTimes', 6, wire([(0, 1, -2, 3), (0, -4, 3, 2), (9, 6, -1, -2), (20, -2, 5, 7)], [(1, 1)]*4), times),
    ('TwoMixed', 6, wire([points[0], points[-1]], [(32, 1), (1, 32)]), [-1, 0, 2, 5, 10, 15, 20, 21]),
]

def f(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text: text += '.0'
    return text + 'f'

header = ['// Synthetic Bezier samples captured by executing the original Win32 DLL.',
          '// See tests/tcb_reference/capture_bezier.py for fixtures and binary hashes.',
          '#pragma once', '', 'namespace BezierReference {',
          'struct Sample { float time; float value[3]; };',
          'struct Fixture { const char *name; int group; int wordCount; const unsigned int *words; const unsigned int *saved; const float (*tangents)[6]; int sampleCount; const Sample *samples; };', '']
records = []
for name, group, raw, sample_times in fixtures:
    samples = None
    first_result = None
    for kind in types:
        path = output_dir / f'{name}-{kind:08x}.bin'
        path.write_bytes(struct.pack('<IfI', kind, 20., len(raw)//4) + raw + struct.pack('<I', len(sample_times)) + struct.pack('<'+'f'*len(sample_times), *sample_times))
        run = subprocess.run([str(runner), str(root), str(path), '--dump'], capture_output=True, timeout=10)
        if run.returncode: raise RuntimeError((name, kind, run.returncode, run.stdout, run.stderr))
        result = json.loads(run.stdout)
        records.append({'fixture': name, **result})
        if first_result is not None:
            for field in ('samples', 'serialized', 'tangents'):
                if first_result[field] != result[field]:
                    raise RuntimeError((name, field, 'position/scale implementations differ'))
        first_result = result
        samples = result['samples']
    words = struct.unpack('<'+'I'*(len(raw)//4), raw)
    header.append(f'static const unsigned int {name}Words[] = {{')
    for i in range(0, len(words), 8):
        header.append('    ' + ', '.join(f'0x{word:08x}u' for word in words[i:i+8]) + ',')
    header += ['};', f'static const unsigned int {name}Saved[] = {{']
    saved = result['serialized']
    if len(saved) != len(words): raise RuntimeError((name, 'serialized size changed'))
    for i in range(0, len(saved), 8):
        header.append('    ' + ', '.join(f'0x{word:08x}u' for word in saved[i:i+8]) + ',')
    header += ['};', f'static const float {name}Tangents[][6] = {{']
    header += ['    {' + ', '.join(map(f, tangent)) + '},' for tangent in result['tangents']]
    header += ['};', f'static const Sample {name}Samples[] = {{']
    header += ['    {' + f(sample['time']) + ', {' + ', '.join(map(f, sample['value'][:3])) + '}},' for sample in samples]
    header += ['};', '']
header += ['static const Fixture Fixtures[] = {']
for name, group, raw, sample_times in fixtures:
    header.append(f'    {{"{name}", {group}, {len(raw)//4}, {name}Words, {name}Saved, {name}Tangents, {len(sample_times)}, {name}Samples}},')
header += ['};', '} // namespace BezierReference', '']
(output_dir / 'BezierReferenceSamples.h').write_text('\n'.join(header), encoding='utf-8')
(output_dir / 'bezier-reference.json').write_text(json.dumps({'hashes':hashes, 'records':records}, indent=2), encoding='utf-8')
print(f'Captured {len(records)} controller fixtures, {sum(len(r["samples"]) for r in records)} evaluations')
