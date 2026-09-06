#!/usr/bin/env python3
"""Summarize CKRE NVTX ranges in a local Nsight Systems SQLite export.

Report wall time, not CPU execution time. Only complete CKRE.CK3D.Render
frames on the selected thread contribute to the frame decomposition. Nested
ranges are subtracted once, OS wait API intervals are unioned, and partial
capture-boundary events are counted separately. Never export capture environment
metadata; only RUN_DURATION_MS is read from META_DATA_CAPTURE.
"""

import argparse
from collections import defaultdict
from contextlib import closing
from dataclasses import dataclass
import json
from pathlib import Path
import sqlite3
import statistics
import sys


@dataclass
class Range:
    start: int
    end: int
    name: str
    children_ns: int = 0


def distribution(values, divisor=1000):
    values = sorted(values)
    if not values:
        return {"count": 0, "total": 0}
    return dict(count=len(values), total=sum(values) / divisor,
                mean=statistics.mean(values) / divisor,
                median=statistics.median(values) / divisor,
                p95=values[int((len(values) - 1) * .95)] / divisor,
                p99=values[int((len(values) - 1) * .99)] / divisor,
                maximum=values[-1] / divisor)


def union_intervals(intervals):
    result = []
    for start, end in sorted(intervals):
        if result and start <= result[-1][1]:
            result[-1][1] = max(result[-1][1], end)
        else:
            result.append([start, end])
    return result


def decompose(ranges):
    """Compute exclusive wall times for a properly nested, single-thread tree."""
    for current in ranges:
        current.children_ns = 0
    stack = []
    result = defaultdict(lambda: {"inclusive": [], "exclusive": []})
    for current in sorted(ranges, key=lambda r: (r.start, -r.end, r.name)):
        while stack and current.start >= stack[-1].end:
            stack.pop()
        if stack:
            parent = stack[-1]
            if current.end > parent.end:
                raise ValueError(f"crossing CKRE ranges: {parent.name}, {current.name}; incomplete trace")
            parent.children_ns += current.end - current.start
        stack.append(current)
    for current in ranges:
        inclusive = current.end - current.start
        exclusive = inclusive - current.children_ns
        if exclusive < 0:
            raise ValueError("negative exclusive time; invalid or duplicate range tree")
        result[current.name]["inclusive"].append(inclusive)
        result[current.name]["exclusive"].append(exclusive)
    return result


def summarize(source, thread_id):
    with closing(sqlite3.connect(source.resolve().as_uri() + "?mode=ro", uri=True)) as db:
        duration_row = db.execute(
            "SELECT value FROM META_DATA_CAPTURE WHERE name='RUN_DURATION_MS'").fetchone()
        if not duration_row:
            raise ValueError("capture duration is missing")
        duration = int(duration_row[0]) * 1_000_000
        rows = db.execute("""SELECT n.start,n.end,coalesce(s.value,n.text),n.globalTid,n.uint64Value
            FROM NVTX_EVENTS n LEFT JOIN StringIds s ON s.id=n.textId
            WHERE (n.globalTid & 16777215)=? AND coalesce(s.value,n.text) LIKE 'CKRE.%'
            ORDER BY n.start,n.end DESC""", (thread_id,)).fetchall()
        tids = {row[3] for row in rows}
        if len(tids) != 1:
            raise ValueError("expected one process/thread with CKRE annotations; specify the recorded render thread")
        global_tid = next(iter(tids))
        complete, marks, partial = [], [], 0
        for start, end, name, _, value in rows:
            if value is not None:
                if 0 <= start <= duration:
                    marks.append((start, name, value))
            elif end is None or start < 0 or end > duration or end <= start:
                partial += 1
            else:
                complete.append(Range(start, end, name))
        frames = sorted((r for r in complete if r.name == "CKRE.CK3D.Render"), key=lambda r: r.start)
        if not frames:
            raise ValueError("no complete CKRE.CK3D.Render frames")
        if any(a.end > b.start for a, b in zip(frames, frames[1:])):
            raise ValueError("nested/overlapping Render frames need separate context attribution")

        selected, frame_index = [], 0
        for current in sorted(complete, key=lambda r: (r.start, -r.end)):
            while frame_index < len(frames) and frames[frame_index].end <= current.start:
                frame_index += 1
            if frame_index < len(frames):
                frame = frames[frame_index]
                if frame.start <= current.start and current.end <= frame.end:
                    selected.append(current)
        scopes = decompose(selected)
        frame_ns = sum(r.end - r.start for r in frames)
        self_ns = sum(sum(times["exclusive"]) for times in scopes.values())
        if self_ns != frame_ns:
            raise ValueError("exclusive scope times do not partition complete Render frames")

        counters, frame_index = defaultdict(list), 0
        for start, name, value in sorted(marks):
            while frame_index < len(frames) and frames[frame_index].end <= start:
                frame_index += 1
            if frame_index < len(frames) and frames[frame_index].start <= start:
                counters[name].append(value)

        tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        waits = []
        if "OSRT_API" in tables:
            waits = db.execute("""SELECT a.start,a.end FROM OSRT_API a JOIN StringIds s ON s.id=a.nameId
                WHERE a.globalTid=? AND a.start>=0 AND a.end<=? AND a.end>a.start
                AND (s.value='Win32 Wait API' OR s.value LIKE '%WaitFor%Object%'
                     OR s.value LIKE '%WaitOnAddress%')""",
                (global_tid, duration)).fetchall()
        # Clip the union to the same complete frames, then partition at range
        # boundaries. Even a wait containing a nested marker is counted once.
        wait_self = defaultdict(int)
        boundaries = []
        for r in selected:
            boundaries.extend([(r.start, 1, r.end, r.name), (r.end, -1, r.end, r.name)])
        for start, end in union_intervals(waits):
            boundaries.extend([(start, 2, end, ""), (end, -2, end, "")])
        active, waiting, previous = [], False, None
        for time, kind, end, name in sorted(boundaries):
            if previous is not None and waiting and active:
                # Select the innermost scope. Frame containment was validated.
                inner = max(active, key=lambda r: (r[0], -r[1]))
                wait_self[inner[2]] += time - previous
            if kind == 1:
                active.append((time, end, name))
            elif kind == -1:
                active.remove(next(r for r in active if r[1] == time and r[2] == name))
            else:
                waiting = kind == 2
            previous = time

        result = dict(source=str(source), global_tid=global_tid, thread_id=thread_id,
                      units="microseconds unless named otherwise; all ranges are wall time",
                      capture_seconds=duration / 1e9,
                      complete_render_frames=len(frames), partial_ranges=partial,
                      ranges_outside_complete_frames=len(complete) - len(selected),
                      render_wall_us=distribution([r.end-r.start for r in frames]),
                      render_start_intervals_us=distribution([b.start-a.start for a,b in zip(frames,frames[1:])]),
                      between_render_frames_us=distribution([b.start-a.end for a,b in zip(frames,frames[1:])]),
                      scopes={name: dict(inclusive_us=distribution(times["inclusive"]),
                                         exclusive_us=distribution(times["exclusive"]),
                                         exclusive_us_per_frame=sum(times["exclusive"])/len(frames)/1000)
                              for name,times in sorted(scopes.items())},
                      counters={name: dict(samples=distribution(values,1), per_frame=sum(values)/len(frames))
                                for name,values in sorted(counters.items())},
                      wait_api_wall_us_by_exclusive_scope={name: ns/1000 for name,ns in sorted(wait_self.items())},
                      complete_wait_api_events=len(waits),
                      partition_error_ns=self_ns-frame_ns)
        if "DX12_WORKLOAD" in tables and "DX12_API" in tables:
            execute = db.execute("""SELECT a.start,a.end,a.correlationId FROM DX12_API a
                JOIN StringIds s ON s.id=a.nameId WHERE a.globalTid=? AND a.start>=0 AND a.end<=?
                AND s.value='ID3D12CommandQueue::ExecuteCommandLists' ORDER BY a.start""",
                (global_tid,duration)).fetchall()
            gpu = db.execute("""SELECT start,end,correlationId FROM DX12_WORKLOAD
                WHERE globalTid=? AND start>=0 AND end<=? AND end>start ORDER BY start""",
                (global_tid,duration)).fetchall()
            api = {r[2]:r for r in execute}
            busy = union_intervals([(s,e) for s,e,_ in gpu])
            result["d3d12_capture"] = dict(
                gpu_batch_us=distribution([e-s for s,e,_ in gpu]),
                execute_api_us=distribution([e-s for s,e,_ in execute]),
                submit_to_gpu_start_us=distribution([s-api[c][0] for s,_,c in gpu if c in api]),
                application_gpu_interval_coverage=sum(e-s for s,e in busy)/duration if gpu else None,
                gpu_batches_observed=bool(gpu),
                coverage_is_device_utilization=False)
        if "DIAGNOSTIC_EVENT" in tables:
            result["capture_diagnostics"] = db.execute(
                "SELECT severity,text FROM DIAGNOSTIC_EVENT ORDER BY timestamp").fetchall()
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sqlite", type=Path)
    parser.add_argument("--thread-id", type=int, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(summarize(args.sqlite, args.thread_id), indent=2))
    except (OSError, ValueError, sqlite3.Error) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
