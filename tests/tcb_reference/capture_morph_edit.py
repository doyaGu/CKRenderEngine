"""Capture synthetic Morph key edits from the specific original Win32 DLL."""
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


def wire(normals=True, keys=2, vertices=3):
    raw = struct.pack('<3I', keys, vertices, int(normals and keys > 0))
    for i in range(keys):
        raw += struct.pack('<f', [2., 10.][i])
        raw += struct.pack('<9f', *[i * 10. + j + 1 for j in range(9)])
        if normals:
            raw += struct.pack('<3I', *[0x100020 + i * 100 + j for j in range(3)])
    return raw


def op(kind, argument=0, time=0., flags=0):
    return (kind, argument, time, flags,
            [time + 31. + j for j in range(9)], [0x00300040 + int(time) + j for j in range(3)])


def insertions(flags, kind=1):
    return [op(kind, time=t, flags=flags) for t in [6., -4., 20., 6.]]


fixtures = [
    ('CopyPositions', wire(False), insertions(2)),
    ('CopyNormalsWithoutAllocationFlag', wire(), insertions(6)),
    ('AllocateMissingNormals', wire(), insertions(3)),
    ('GenericAddKey', wire(), insertions(10)),
    ('NormalsOnly', wire(), insertions(4)),
    ('EmptyInputWithNormals', wire(), insertions(1)),
    ('EmptyInputWithoutNormals', wire(False), insertions(0)),
    ('TimeKeysWithNormals', wire(), insertions(1, kind=2)),
    ('TimeKeysWithoutNormals', wire(False), insertions(0, kind=2)),
    ('DuplicateSources', wire(), [op(1, time=2., flags=6), op(1, time=10., flags=2),
                                 op(3, argument=0), op(3, argument=1, flags=8)]),
    ('DuplicateTimeKeepsNormals', wire(), [op(2, time=2.), op(2, time=10.),
                                         op(3, argument=0), op(3, argument=1, flags=8)]),
    ('DuplicateTimeKeepsAbsentNormals', wire(False), [op(2, time=2., flags=1), op(1, time=10., flags=14),
                                                     op(3, argument=0, flags=1), op(3, argument=1, flags=8)]),
    ('CountOnlyWithNormals', wire(), [op(0, argument=n) for n in [1, 0, 5]] + [op(0, argument=3, flags=1)]),
    ('CountOnlyWithoutNormals', wire(False), [op(0, argument=n) for n in [1, 0, 5]] + [op(0, argument=3, flags=1)]),
    ('RemoveAndReinsert', wire(), [op(4, argument=-1), op(4, argument=0), op(4, argument=0),
                                 op(2, time=3., flags=1), op(4, argument=0)]),
    ('NullAndEmpty', wire(keys=0), [op(5), op(5, flags=8), op(2, time=2., flags=1), op(5, flags=1)]),
    ('ZeroVerticesWithoutNormals', wire(keys=0, vertices=0), [op(2, time=2.), op(1, time=6.)]),
    ('ZeroVerticesWithNormals', wire(keys=0, vertices=0), [op(2, time=2., flags=1), op(1, time=6., flags=8)]),
    ('ZeroVertexCloneWithNormals', wire(keys=0, vertices=0), [op(2, time=2., flags=1), op(2, time=10., flags=1),
                                                           op(6), op(3, argument=0)]),
    ('NonemptyCloneWithNormals', wire(), [op(6), op(3, argument=1)]),
]


def f(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    return text + 'f'


header = ['// Synthetic Morph edits captured from the original Win32 DLL.',
          '// See tests/tcb_reference/capture_morph_edit.py for fixtures and binary hashes.',
          '// New keys without source positions are filled from Operation::positions before Save.',
          '#pragma once', '', 'namespace MorphEditReference {',
          'struct Operation { int kind; int argument; float time; unsigned int flags; float positions[9]; unsigned int normals[3]; };',
          'struct State { int result; int keyCount; int vertexCount; int stable; int aliases; unsigned int storage; int wordCount; const unsigned int *words; };',
          'struct Fixture { const char *name; int wordCount; const unsigned int *words; int operationCount; const Operation *operations; const State *states; };', '']


def emit_words(name, words):
    header.append(f'static const unsigned int {name}[] = {{')
    for i in range(0, len(words), 8):
        header.append('    ' + ', '.join(f'0x{word:08x}u' for word in words[i:i+8]) + ',')
    header.extend(['};', ''])


records = []
for name, raw, operations in fixtures:
    path = output_dir / f'{name}.bin'
    encoded = struct.pack('<I', len(raw) // 4) + raw + struct.pack('<I', len(operations))
    for kind, argument, time, flags, positions, normals in operations:
        encoded += struct.pack('<IifI9f3I', kind, argument, time, flags, *positions, *normals)
    path.write_bytes(encoded)
    run = subprocess.run([str(runner), str(root), str(path)], capture_output=True, timeout=10)
    if run.returncode:
        raise RuntimeError((name, run.returncode, run.stdout, run.stderr))
    result = json.loads(run.stdout)
    records.append({'fixture': name, **result})
    if len(result['states']) != len(operations):
        raise RuntimeError((name, 'missing operation results'))
    words = struct.unpack('<' + 'I' * (len(raw) // 4), raw)
    emit_words(name + 'Words', words)
    header.append(f'static const Operation {name}Operations[] = {{')
    for kind, argument, time, flags, positions, normals in operations:
        header.append(f'    {{{kind}, {argument}, {f(time)}, {flags}u, ' +
                      '{' + ', '.join(map(f, positions)) + '}, {' +
                      ', '.join(f'0x{n:08x}u' for n in normals) + '}},')
    header.extend(['};', ''])
    for i, state in enumerate(result['states']):
        if state['aliases'] or not state['stable']:
            raise RuntimeError((name, i, 'original changed buffer ownership'))
        if state['words']:
            emit_words(f'{name}Saved{i}', state['words'])
    header.append(f'static const State {name}States[] = {{')
    for i, state in enumerate(result['states']):
        storage = sum(mask << (2 * j) for j, mask in enumerate(state['storage']))
        saved = f'{name}Saved{i}' if state['words'] else 'nullptr'
        header.append('    {' + ', '.join(str(state[key]) for key in ['result', 'keyCount', 'vertexCount', 'stable', 'aliases']) +
                      f', {storage}u, {len(state["words"])}, {saved}' + '},')
    header.extend(['};', ''])
header.append('static const Fixture Fixtures[] = {')
for name, raw, operations in fixtures:
    header.append(f'    {{"{name}", {len(raw) // 4}, {name}Words, {len(operations)}, {name}Operations, {name}States}},')
header.extend(['};', '} // namespace MorphEditReference', ''])
(output_dir / 'MorphEditReferenceSamples.h').write_text('\n'.join(header), encoding='utf-8')
(output_dir / 'morph-edit-reference.json').write_text(json.dumps({'hashes': hashes, 'records': records}, indent=2), encoding='utf-8')
print(f'Captured {len(records)} Morph editing fixtures, {sum(len(r["states"]) for r in records)} operation results')
