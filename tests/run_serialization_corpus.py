"""Run each external corpus file in its own process and retain failure logs."""
import argparse
import collections
import concurrent.futures
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    corpus = args.corpus.resolve(strict=True)
    files = sorted(p for p in corpus.rglob("*") if p.suffix.lower() in (".nmo", ".cmo", ".vmo"))
    if not files:
        parser.error("No .nmo/.cmo/.vmo files found")
    args.output.mkdir(parents=True, exist_ok=True)

    def run(item):
        index, path = item
        try:
            result = subprocess.run([str(executable), str(path)], capture_output=True, timeout=args.timeout)
            output = result.stdout + result.stderr
            code = result.returncode
        except subprocess.TimeoutExpired as error:
            output = (error.stdout or b"") + (error.stderr or b"")
            code = "timeout"
        log = args.output / f"{index:04d}.log"
        log.write_bytes(output)
        text = output.decode("utf-8", errors="replace")
        summary = re.search(r"RESULT tested=(\d+) skipped=(\d+) failures=(\d+)", text)
        return {
            "file": str(path.relative_to(corpus)), "returncode": code, "log": log.name,
            "tested": int(summary[1]) if summary else 0,
            "skipped": int(summary[2]) if summary else 0,
            "failures": [line for line in text.splitlines() if line.startswith("FAIL")],
            "last_line": text.splitlines()[-1] if text else "",
            "coverage": {cid: int(count) for cid, count in re.findall(r"COVERAGE class=(\d+) count=(\d+)", text)},
            "controller_coverage": {kind: int(count) for kind, count in re.findall(r"CONTROLLER type=([0-9A-F]+) count=(\d+)", text)},
        }

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        records = list(pool.map(run, enumerate(files)))
    coverage = collections.Counter()
    controller_coverage = collections.Counter()
    for record in records:
        coverage.update(record["coverage"])
        controller_coverage.update(record["controller_coverage"])
    report = {
        "files": len(records), "passed": sum(r["returncode"] == 0 for r in records),
        "tested": sum(r["tested"] for r in records), "coverage": dict(coverage), "results": records,
        "controller_coverage": dict(controller_coverage),
        "scope": "Render/Layer Load/PostLoad/Save and fresh-object reload, with a test layer-type registry. Core attributes, runtime managers, image pixels and behaviors are excluded; see SerializationCorpusTest.cpp.",
    }
    (args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "results"}, indent=2))
    return 0 if report["passed"] == len(records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
