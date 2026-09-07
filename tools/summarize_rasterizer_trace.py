#!/usr/bin/env python3
"""Summarize CKRE NVTX ranges in a local Nsight Systems SQLite export.

Report wall time, not CPU execution time. Only complete CKRE.CK3D.Render
frames on the selected thread contribute to the frame decomposition. Nested
ranges are subtracted once, OS wait API intervals are unioned, and partial
capture-boundary events are counted separately. Never export capture environment
metadata; only RUN_DURATION_MS is read from META_DATA_CAPTURE.
"""

import argparse
import bisect
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


def correlate_acquires(ranges, execute, gpu, queue_depth):
    """Test a single-window, one-submit-per-present queue model.

    Nsight's exported batch correlation IDs identify Execute/GPU pairs, not
    the fence passed to SDL's acquire. Keep that distinction in the report.
    Missing/ambiguous submissions retain their position in the queue model.
    """
    if queue_depth not in (1, 2, 3):
        raise ValueError("queue depth must be between 1 and 3")
    submits = sorted((r for r in ranges if r.name == "CKRE.SDL.NativeSubmit"), key=lambda r: r.start)
    acquires = sorted((r for r in ranges if r.name == "CKRE.SDL.Acquire"), key=lambda r: r.start)
    api_by_key, gpu_by_key = defaultdict(list), defaultdict(list)
    for row in execute:
        api_by_key[row[3], row[2]].append(row)
    for row in gpu:
        gpu_by_key[row[3], row[2]].append(row)
    ordered_api = sorted(execute)
    api_starts = [r[0] for r in ordered_api]
    matched = []
    for submit in submits:
        first = bisect.bisect_left(api_starts, submit.start)
        last = bisect.bisect_left(api_starts, submit.end)
        calls = ordered_api[first:last]
        pair = None
        if len(calls) == 1 and calls[0][1] <= submit.end:
            api = calls[0]
            key = api[3], api[2]
            batches = gpu_by_key[key]
            if api[2] is not None and api[3] is not None and len(api_by_key[key]) == len(batches) == 1:
                batch = batches[0]
                if batch[0] >= api[0]:
                    pair = (api, batch)
        matched.append(pair)
    submit_starts = [r.start for r in submits]
    acquire_starts = [r.start for r in acquires]
    groups = {"all": [], "over_1ms": []}
    excluded = defaultdict(int)
    for acquire in acquires:
        stop = bisect.bisect_left(submit_starts, acquire.start)
        start = stop - queue_depth
        if start < 0:
            excluded["capture_boundary"] += 1
            continue
        previous = submits[start:stop]
        pairs = matched[start:stop]
        if any(r.end > acquire.start for r in previous) or any(pair is None for pair in pairs):
            excluded["missing_or_ambiguous_submission_pair"] += 1
            continue
        if len({pair[0][3] for pair in pairs}) != 1:
            excluded["multiple_queues"] += 1
            continue
        between = bisect.bisect_left(acquire_starts, acquire.start) - bisect.bisect_left(acquire_starts, previous[0].end)
        if between != queue_depth - 1:
            excluded["not_one_acquire_per_submission"] += 1
            continue
        api, batch = pairs[0]
        duration = acquire.end - acquire.start
        queued = min(duration, max(0, batch[0] - acquire.start))
        executing = max(0, min(acquire.end, batch[1]) - max(acquire.start, batch[0]))
        after = min(duration, max(0, acquire.end - batch[1]))
        record = dict(start=acquire.start, duration=duration, queued=queued, executing=executing, after=after,
                      submit_to_start=batch[0]-api[0], gpu_duration=batch[1]-batch[0],
                      finished_before=batch[1] <= acquire.start, finishes_after=batch[1] > acquire.end)
        assert queued + executing + after == duration
        groups["all"].append(record)
        if duration > 1_000_000:
            groups["over_1ms"].append(record)
    def stats(rows):
        return dict(count=len(rows),
                    acquire_us=distribution([r["duration"] for r in rows]),
                    start_intervals_us=distribution([b["start"]-a["start"] for a,b in zip(rows, rows[1:])]),
                    partition_us={name: distribution([r[name] for r in rows]) for name in ("queued", "executing", "after")},
                    candidate_submit_to_gpu_start_us=distribution([r["submit_to_start"] for r in rows]),
                    candidate_gpu_duration_us=distribution([r["gpu_duration"] for r in rows]),
                    candidate_finished_before_acquire=sum(r["finished_before"] for r in rows),
                    candidate_finishes_after_acquire=sum(r["finishes_after"] for r in rows))
    return dict(assumed_present_queue_depth=queue_depth, fence_identity_observed=False,
                model="single window, one native submission per present; candidate is Nth preceding submission",
                partition="acquire before candidate GPU start / overlapping candidate GPU / after candidate GPU end",
                native_submissions=len(submits), matched_native_submissions=sum(p is not None for p in matched),
                acquires=len(acquires), excluded=dict(excluded), groups={name: stats(rows) for name,rows in groups.items()})


def summarize(source, thread_id, queue_depth=None):
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
            execute = db.execute("""SELECT a.start,a.end,a.correlationId,a.longContextId FROM DX12_API a
                JOIN StringIds s ON s.id=a.nameId WHERE a.globalTid=? AND a.start>=0 AND a.end<=?
                AND s.value='ID3D12CommandQueue::ExecuteCommandLists' ORDER BY a.start""",
                (global_tid,duration)).fetchall()
            gpu = db.execute("""SELECT start,end,correlationId,longContextId FROM DX12_WORKLOAD
                WHERE globalTid=? AND start>=0 AND end<=? AND end>start ORDER BY start""",
                (global_tid,duration)).fetchall()
            api = {(r[3],r[2]):r for r in execute}
            busy = union_intervals([(s,e) for s,e,*_ in gpu])
            result["d3d12_capture"] = dict(
                gpu_batch_us=distribution([e-s for s,e,*_ in gpu]),
                execute_api_us=distribution([e-s for s,e,*_ in execute]),
                submit_to_gpu_start_us=distribution([s-api[q,c][0] for s,_,c,q in gpu if (q,c) in api]),
                application_gpu_interval_coverage=sum(e-s for s,e in busy)/duration if gpu else None,
                gpu_batches_observed=bool(gpu),
                coverage_is_device_utilization=False)
            if queue_depth is not None:
                result["d3d12_acquire_queue_model"] = correlate_acquires(complete, execute, gpu, queue_depth)
        elif queue_depth is not None:
            raise ValueError("queue correlation requires DX12 API and workload tables")
        if "DIAGNOSTIC_EVENT" in tables:
            result["capture_diagnostics"] = db.execute(
                "SELECT severity,text FROM DIAGNOSTIC_EVENT ORDER BY timestamp").fetchall()
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sqlite", type=Path)
    parser.add_argument("--thread-id", type=int, required=True)
    parser.add_argument("--queue-depth", type=int, choices=(1,2,3),
                        help="test the recorded single-window SDL in-flight depth; does not identify native fences")
    args = parser.parse_args()
    try:
        print(json.dumps(summarize(args.sqlite, args.thread_id, args.queue_depth), indent=2))
    except (OSError, ValueError, sqlite3.Error) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
