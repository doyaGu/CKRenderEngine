"""Compare locally supplied, loaded animation controllers with the original DLL.

All corpus-derived input/output stays in the requested local output directory.
The comparison tests controller Evaluate/Dump, not original object/file Load.
"""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def read_capture(path):
    raw = path.read_bytes()
    if raw[:8] != struct.pack('<2I', 0x434B4145, 1):
        raise ValueError('Unrecognized capture format')
    offset = 8
    while offset < len(raw):
        object_id, slot, kind, length, words, samples = struct.unpack_from('<3If2I', raw, offset)
        offset += 24
        payload = raw[offset:offset + words * 4]
        offset += words * 4
        values = []
        for _ in range(samples):
            time, success, *value = struct.unpack_from('<fi4f', raw, offset)
            offset += 24
            values.append({'time': time, 'success': success, 'value': value})
        saved_count, = struct.unpack_from('<I', raw, offset)
        offset += 4
        saved = list(struct.unpack_from('<' + 'I' * saved_count, raw, offset))
        offset += saved_count * 4
        native_input = struct.pack('<IfI', kind, length, words) + payload + struct.pack('<I', samples)
        native_input += struct.pack('<' + 'f' * samples, *(sample['time'] for sample in values))
        yield {'object': object_id, 'slot': slot, 'type': f'{kind:08X}', 'samples': values,
               'serialized': saved, 'input': native_input}
    if offset != len(raw):
        raise ValueError('Truncated controller capture')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--exporter', type=Path, required=True)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--reference-dir', type=Path, help='Reuse original-DLL captures keyed by exact input hash')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    runner = args.runner.resolve(strict=True)
    exporter = args.exporter.resolve(strict=True)
    corpus = args.corpus.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    native_dir = args.reference_dir.resolve() if args.reference_dir else output / 'native'
    native_dir.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for path, expected in [
        ('RenderEngines/CK2_3D.dll', '5c8fc5c5c491ef347df24d98813584865bef240e66dbd325b26eb576cfecc590'),
        ('Bin/VxMath.dll', 'bd17dddb747c943e6e090471305aeda7f87cfbca401b3fada36be400285973a2'),
    ]:
        actual = hashlib.sha256((root / path).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f'{path} does not match the reference implementation')
        hashes[path] = actual

    files = sorted(path for path in corpus.rglob('*') if path.suffix.lower() in ('.nmo', '.cmo', '.vmo'))
    if not files:
        parser.error('No corpus files found')

    def export(item):
        index, path = item
        capture = output / f'{index:04d}.bin'
        try:
            run = subprocess.run([str(exporter), str(path), '--capture-controllers', str(capture)],
                                 capture_output=True, timeout=30)
            (output / f'{index:04d}.log').write_bytes(run.stdout + run.stderr)
            record = {'file': str(path.relative_to(corpus)), 'returncode': run.returncode}
            return record, list(read_capture(capture)) if not run.returncode else []
        except (subprocess.TimeoutExpired, ValueError, struct.error) as error:
            return {'file': str(path.relative_to(corpus)), 'error': str(error)}, []

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        exports = list(pool.map(export, enumerate(files)))
    unique = {}
    coverage = collections.Counter()
    inconsistencies = []
    for record, controllers in exports:
        for controller in controllers:
            coverage[controller['type']] += 1
            key = hashlib.sha256(controller['input']).hexdigest()
            origin = {'file': record['file'], 'object': controller['object'], 'slot': controller['slot']}
            if key not in unique:
                unique[key] = {**controller, 'origins': [origin]}
            else:
                if (controller['samples'], controller['serialized']) != (unique[key]['samples'], unique[key]['serialized']):
                    inconsistencies.append(origin)
                unique[key]['origins'].append(origin)
    print(f'Exported {sum(coverage.values())} controller allocations; {len(unique)} unique inputs', flush=True)

    def compare(item):
        key, controller = item
        input_path = native_dir / f'{key}.bin'
        input_path.write_bytes(controller['input'])
        original_path = native_dir / f'{key}.json'
        if original_path.exists():
            original = json.loads(original_path.read_text(encoding='utf-8'))
        else:
            try:
                run = subprocess.run([str(runner), str(root), str(input_path), '--dump'], capture_output=True, timeout=10)
                if run.returncode:
                    return {'key': key, 'type': controller['type'], 'error': f'Original runtime exit {run.returncode}',
                            'origins': controller['origins']}
                original = json.loads(run.stdout)
                original_path.write_text(json.dumps(original), encoding='utf-8')
            except (subprocess.TimeoutExpired, ValueError) as error:
                return {'key': key, 'type': controller['type'], 'error': str(error), 'origins': controller['origins']}
        mismatches = []
        maximum_error = 0.0
        if len(original['samples']) != len(controller['samples']):
            raise ValueError('Original sample count changed')
        for expected, actual in zip(original['samples'], controller['samples']):
            if not actual['success']:
                mismatches.append({'time': actual['time'], 'error': 'Reconstruction returned false'})
                continue
            for component, (a, b) in enumerate(zip(actual['value'], expected['value'])):
                error = abs(a - b)
                maximum_error = max(maximum_error, error)
                # Large corpus coordinates need relative tolerance for x87/SSE
                # rounding; quaternion components still use the absolute bound.
                if not math.isfinite(error) or error > 5e-5 + 5e-6 * abs(b):
                    mismatches.append({'time': actual['time'], 'component': component, 'actual': a, 'expected': b})
        saved_equal = controller['serialized'] == original['serialized']
        return {'key': key, 'type': controller['type'], 'samples': len(controller['samples']),
                'max_absolute_error': maximum_error, 'serialized_equal': saved_equal,
                'mismatches': mismatches, 'origins': controller['origins']}

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        comparisons = list(pool.map(compare, unique.items()))
    failures = [r for r in comparisons if r.get('error') or r.get('mismatches') or not r.get('serialized_equal')]
    report = {'hashes': hashes, 'files': len(files), 'export_failures': [r for r, _ in exports if r.get('returncode') != 0],
              'controllers': sum(coverage.values()), 'coverage': dict(coverage), 'unique_inputs': len(unique),
              'samples': sum(r.get('samples', 0) for r in comparisons), 'different_reconstructed_outputs': inconsistencies,
              'failed_comparisons': len(failures), 'results': comparisons,
              'tolerance': 'abs <= 5e-5 + 5e-6 * abs(reference)',
              'scope': 'Evaluate and Dump on controllers exported after reconstructed object Load. Does not validate original file/object Load or full playback.'}
    (output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('results', 'hashes')}, indent=2, ensure_ascii=False))
    return int(bool(failures or report['export_failures'] or inconsistencies))


if __name__ == '__main__':
    raise SystemExit(main())
