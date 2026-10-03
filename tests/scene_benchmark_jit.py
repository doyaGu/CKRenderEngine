"""Repeated CK2 scene timings with disabled, fragment-only and full JIT.

Uses the capture tool's visible, unpaced CPU wall-time profiler. It does not
measure GPU timestamps or clear the driver's shader cache. Run without other
GPU workloads. Each sample starts a new process with the JIT manifest disabled.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


MODES = ("off", "fragment", "on")
VERTEX_FIELDS = ("positiont", "unlit", "lit", "tween", "blend", "clip", "pad")
SNAPSHOT_FIELDS = (
    "Requests", "Specialized", "Unavailable", "Capacity", "Rejected", "QueueDeferred",
    "CompileQueued", "CompileCompleted", "CompileFailed", "VertexCompileCompleted", "VertexCompileFailed",
    "PipelineSelections", "PipelineReady", "PositionTReady", "UnlitReady", "LitReady", "TweenReady",
    "MatrixBlendReady", "ClipReady", "DepthPadReady", "ShaderPending", "PipelinePending",
    "PipelineDeferred", "PipelineFailed", "PipelineQueued", "PipelineCompleted", "PipelineBuildFailed",
    "SynchronousRequests", "Evictions", "PipelineEvictions")


def mode_order(repeat):
    # Cover all permutations: rotations alone never reverse adjacent modes.
    orders = (("off", "fragment", "on"), ("fragment", "on", "off"),
              ("on", "off", "fragment"), ("off", "on", "fragment"),
              ("on", "fragment", "off"), ("fragment", "off", "on"))
    return orders[repeat % len(orders)]


def distribution(values):
    if not values or any(not math.isfinite(x) or x < 0 for x in values):
        raise ValueError("expected finite, nonnegative timing samples")
    ordered = sorted(values)
    return dict(mean=statistics.mean(values), median=statistics.median(values),
                p95=ordered[math.ceil(len(values) * .95) - 1],
                p99=ordered[math.ceil(len(values) * .99) - 1],
                minimum=ordered[0], maximum=ordered[-1])


def summarize_jit_interval(profile, mode, scene, measured_draws, require_steady=False):
    interval = profile.get("jitInterval")
    if interval is None:
        if require_steady:
            raise ValueError("steady JIT measurement requires boundary snapshots")
        return None
    if (interval["schemaVersion"] != 1 or interval["firstFrame"] != profile["warmupFrames"] or
            interval["endFrameExclusive"] != len(profile["frames"])):
        raise ValueError("invalid JIT snapshot boundaries or schema")
    before, after = interval["before"], interval["after"]
    for snapshot in (before, after):
        if any(type(snapshot[k]) is not int or not 0 <= snapshot[k] < 2**64 for k in SNAPSHOT_FIELDS):
            raise ValueError("invalid JIT snapshot counter")
        if (snapshot["CompileCompleted"] > snapshot["CompileQueued"] or
                snapshot["PipelineCompleted"] > snapshot["PipelineQueued"] or
                snapshot["Requests"] != sum(snapshot[k] for k in
                    ("Specialized", "Unavailable", "Capacity", "Rejected", "QueueDeferred")) or
                snapshot["PipelineSelections"] != sum(snapshot[k] for k in
                    ("PipelineReady", "ShaderPending", "PipelinePending", "PipelineDeferred", "PipelineFailed"))):
            raise ValueError("inconsistent JIT snapshot outcomes")
        if any(snapshot[k] for k in ("CompileFailed", "VertexCompileFailed", "PipelineBuildFailed", "PipelineFailed")):
            raise ValueError("JIT snapshot reports a shader or pipeline failure")
    delta = {k: after[k] - before[k] for k in SNAPSHOT_FIELDS}
    if any(v < 0 for v in delta.values()):
        raise ValueError("JIT snapshot counters decreased during measurement")
    vertices = ("PositionTReady", "UnlitReady", "LitReady", "TweenReady", "MatrixBlendReady", "ClipReady", "DepthPadReady")
    if mode != "on" and any(after[k] for k in vertices):
        raise ValueError("disabled vertex JIT appears in boundary snapshots")
    if mode == "off" and (after["PipelineSelections"] or after["CompileQueued"]):
        raise ValueError("disabled JIT appears in boundary snapshots")
    if mode != "off" and not delta["PipelineReady"]:
        raise ValueError("no ready JIT selections between measurement boundaries")
    required = ("PositionTReady",) if scene == "composite_2d" else ("UnlitReady", "LitReady")
    if mode == "on" and any(not delta[k] for k in required):
        raise ValueError("required generated vertices absent between measurement boundaries")
    pending_before = dict(shader=before["CompileQueued"] - before["CompileCompleted"],
                          pipeline=before["PipelineQueued"] - before["PipelineCompleted"])
    pending_after = dict(shader=after["CompileQueued"] - after["CompileCompleted"],
                         pipeline=after["PipelineQueued"] - after["PipelineCompleted"])
    no_compile = not pending_before["shader"] and not any(delta[k] for k in
        ("CompileQueued", "CompileCompleted", "VertexCompileCompleted"))
    no_pipeline = not pending_before["pipeline"] and not any(delta[k] for k in
        ("PipelineQueued", "PipelineCompleted", "SynchronousRequests"))
    fallback = sum(delta[k] for k in ("ShaderPending", "PipelinePending", "PipelineDeferred", "PipelineFailed"))
    all_jit = delta["PipelineReady"] == measured_draws if mode != "off" else None
    steady = bool(no_compile and no_pipeline and not fallback and (mode == "off" or all_jit))
    result = dict(delta=delta, pendingBefore=pending_before, pendingAfter=pending_after,
        noShaderCompilation=no_compile, noPipelineCreation=no_pipeline,
        fallbackSelections=fallback, allMeasuredDrawsUsedJit=all_jit, steady=steady)
    if require_steady and not steady:
        raise ValueError("measurement is not steady: pending/new compilation, pipeline creation, or non-JIT draws")
    return result


def summarize_profile(profile, stats, mode, scene, warmup, measured, require_steady=False):
    frames = profile["frames"]
    if len(frames) != warmup + measured or profile["warmupFrames"] != warmup:
        raise ValueError("incomplete frame profile")
    if any(f["index"] != i or bool(f["warmup"]) != (i < warmup)
           for i, f in enumerate(frames)):
        raise ValueError("invalid frame indices or warmup boundary")
    if not profile["presentEveryFrame"] or profile["waitVBlankRequested"]:
        raise ValueError("profiling requires unpaced presentation")
    if any(f["width"] != profile["requestedWidth"] or
           f["height"] != profile["requestedHeight"] or not f["rasterizerStats"]
           for f in frames):
        raise ValueError("missing draw counters or unexpected render dimensions")
    if not stats or any(stats.get(k, 0) for k in
                        ("compile_failed", "vertex_failed", "pipeline_failed")):
        raise ValueError("missing JIT statistics or compilation failed")
    if stats.get("compile_completed") != stats.get("compile_queued"):
        raise ValueError("shader work still incomplete at process exit")
    if mode == "off" and (stats.get("selected", 0) or stats.get("compile_queued", 0)):
        raise ValueError("disabled run used JIT")
    if mode != "on" and any(stats.get(k, 0) for k in VERTEX_FIELDS):
        raise ValueError("disabled vertex JIT was used")
    warmup_draws = sum(f["rasterizerStats"]["drawCalls"] for f in frames[:warmup])
    measured_draws = sum(f["rasterizerStats"]["drawCalls"] for f in frames[warmup:])
    measured_jit = summarize_jit_interval(profile, mode, scene, measured_draws, require_steady)
    # Even if every warmup draw used JIT, excess ready selections necessarily
    # happened in the measured interval. This does not prove zero late jobs.
    ready_lower_bound = max(0, stats.get("ready", 0) - warmup_draws)
    if measured_jit is not None:
        ready_lower_bound = measured_jit["delta"]["PipelineReady"]
    if mode != "off" and not ready_lower_bound:
        raise ValueError("no proof of JIT execution inside the measured interval")
    required = ("positiont",) if scene == "composite_2d" else ("unlit", "lit")
    vertex_lower_bounds = {k: max(0, stats.get(k, 0) - warmup_draws) for k in VERTEX_FIELDS}
    if measured_jit is not None:
        vertex_lower_bounds = dict(zip(VERTEX_FIELDS, (measured_jit["delta"][k] for k in
            ("PositionTReady", "UnlitReady", "LitReady", "TweenReady", "MatrixBlendReady", "ClipReady", "DepthPadReady"))))
    if mode == "on" and any(not vertex_lower_bounds[k] for k in required):
        raise ValueError("no proof of required generated vertices inside the measured interval")
    result = dict(
        coldStartMilliseconds=profile["coldStartMilliseconds"],
        initial120RenderMilliseconds=distribution([f["renderMilliseconds"] for f in frames[:120]]),
        measuredRenderMilliseconds=distribution([f["renderMilliseconds"] for f in frames[warmup:]]),
        measuredDrawCalls=measured_draws,
        measuredPrimitives=sum(f["rasterizerStats"]["primitives"] for f in frames[warmup:]),
        measuredReadySelectionsLowerBound=ready_lower_bound,
        measuredVertexSelectionsLowerBounds=vertex_lower_bounds,
        allMeasuredFramesFocused=profile["allMeasuredFramesFocused"],
        timingScope=profile["timingScope"], driverName=profile["driverName"],
        driverDescription=profile["driverDescription"], jit=stats)
    if "jitInterval" in profile or require_steady:
        result["measuredJit"] = measured_jit
    return result


class ProcessMemory:
    """Track the capture tool's renderer child, not its small launcher."""
    def __init__(self):
        import psutil
        self.psutil = psutil
        self.renderer = None
        self.samples = 0
        self.peak_working_set = 0
        self.peak_commit = 0
        self.sampled_private = 0

    def sample(self, process):
        try:
            if self.renderer is None:
                parent = self.psutil.Process(process.pid)
                children = [p for p in parent.children() if p.name() == parent.name()]
                if not children:
                    return
                if len(children) != 1:
                    raise ValueError("expected exactly one scene renderer")
                self.renderer = children[0]
            if os.name != "nt":
                return
            counters = self.renderer.memory_info()
            self.samples += 1
            self.peak_working_set = max(self.peak_working_set, counters.peak_wset)
            self.peak_commit = max(self.peak_commit, counters.peak_pagefile)
            self.sampled_private = max(self.sampled_private, counters.private)
        except (self.psutil.NoSuchProcess, self.psutil.AccessDenied):
            pass

    def stop(self, process):
        # The renderer is a verified direct child of this particular launcher.
        # psutil checks process creation time before killing a reused PID.
        if self.renderer is not None:
            try:
                self.renderer.kill()
                self.renderer.wait(timeout=5)
            except self.psutil.NoSuchProcess:
                pass
        if process.poll() is None:
            process.kill()
        process.wait()

    def report(self):
        if not self.samples:
            return None
        return dict(processId=self.renderer.pid, samples=self.samples, samplePeriodSeconds=.05,
                    observedPeakWorkingSetBytes=self.peak_working_set,
                    observedPeakCommitBytes=self.peak_commit,
                    sampledMaxPrivateBytes=self.sampled_private)


def identities(tool, engine):
    paths = {tool, *engine.glob("*.dll"), *tool.parent.glob("*.dll")}
    paths.update(engine.glob("*.ini"))
    return [dict(path=str(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            for p in sorted(paths)]


def aggregate(records, foreground_only=False):
    groups = {}
    for row in records:
        if not row["issues"] and (not foreground_only or row["summary"]["allMeasuredFramesFocused"]):
            groups.setdefault((row["driver"], row["scene"], row["mode"]), []).append(row["summary"])
    result = []
    for (driver, scene, mode), rows in groups.items():
        result.append(dict(driver=driver, scene=scene, mode=mode, repetitions=len(rows),
            fullyFocusedRepetitions=sum(r["allMeasuredFramesFocused"] for r in rows),
            medianOfRunMediansMs=statistics.median(r["measuredRenderMilliseconds"]["median"] for r in rows),
            minimumRunMedianMs=min(r["measuredRenderMilliseconds"]["median"] for r in rows),
            maximumRunMedianMs=max(r["measuredRenderMilliseconds"]["median"] for r in rows),
            medianOfRunP95Ms=statistics.median(r["measuredRenderMilliseconds"]["p95"] for r in rows),
            medianOfRunP99Ms=statistics.median(r["measuredRenderMilliseconds"]["p99"] for r in rows),
            medianFirstRenderMs=statistics.median(r["coldStartMilliseconds"]["firstRender"] for r in rows),
            medianEngineInitMs=statistics.median(r["coldStartMilliseconds"]["engineInit"] for r in rows)))
    return result


def paired_foreground(records):
    """Compare modes from the same repetition, with no focus loss in any mode."""
    groups = {}
    for row in records:
        groups.setdefault((row["driver"], row["scene"], row["repeat"]), []).append(row)
    result = []
    for rows in groups.values():
        if len(rows) == 3 and {r["mode"] for r in rows} == set(MODES) and all(
                not r["issues"] and r["summary"]["allMeasuredFramesFocused"] for r in rows):
            result.extend(rows)
    return result


def paired_comparisons(records):
    """Subtract within each complete foreground triplet before summarizing."""
    groups = {}
    for row in paired_foreground(records):
        groups.setdefault((row["driver"], row["scene"], row["repeat"]), {})[row["mode"]] = row["summary"]
    result = []
    for (driver, scene, repeat), modes in groups.items():
        for baseline, mode in (("off", "fragment"), ("off", "on"), ("fragment", "on")):
            for metric in ("median", "p95", "p99"):
                reference = modes[baseline]["measuredRenderMilliseconds"][metric]
                value = modes[mode]["measuredRenderMilliseconds"][metric]
                delta = value - reference
                result.append(dict(driver=driver, scene=scene, repeat=repeat,
                    baselineMode=baseline, mode=mode, metric=metric,
                    baselineMs=reference, variantMs=value, deltaMs=delta,
                    changePercent=100 * delta / reference if reference else None))
    return result


def aggregate_comparisons(comparisons):
    groups = {}
    for row in comparisons:
        key = tuple(row[k] for k in ("driver", "scene", "baselineMode", "mode", "metric"))
        groups.setdefault(key, []).append(row)
    result = []
    for (driver, scene, baseline, mode, metric), rows in groups.items():
        deltas = [r["deltaMs"] for r in rows]
        percentages = [r["changePercent"] for r in rows if r["changePercent"] is not None]
        result.append(dict(driver=driver, scene=scene, baselineMode=baseline, mode=mode,
            metric=metric, repetitions=len(rows), medianPairedDeltaMs=statistics.median(deltas),
            minimumPairedDeltaMs=min(deltas), maximumPairedDeltaMs=max(deltas),
            higherRepetitions=sum(d > 0 for d in deltas), lowerRepetitions=sum(d < 0 for d in deltas),
            equalRepetitions=sum(d == 0 for d in deltas), percentageRepetitions=len(percentages),
            medianPairedChangePercent=statistics.median(percentages) if percentages else None))
    return result


def write_report(out, report):
    report["aggregates"] = aggregate(report["records"])
    report["foregroundAggregates"] = aggregate(report["records"], foreground_only=True)
    report["pairedForegroundAggregates"] = aggregate(paired_foreground(report["records"]))
    report["pairedForegroundComparisons"] = paired_comparisons(report["records"])
    report["pairedForegroundComparisonAggregates"] = aggregate_comparisons(report["pairedForegroundComparisons"])
    (out / "results.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    for key, filename in (("aggregates", "summary.csv"), ("foregroundAggregates", "foreground-summary.csv"),
                          ("pairedForegroundAggregates", "paired-foreground-summary.csv"),
                          ("pairedForegroundComparisons", "paired-foreground-comparisons.csv"),
                          ("pairedForegroundComparisonAggregates", "paired-foreground-deltas.csv")):
        if report[key]:
            with (out / filename).open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(report[key][0]))
                writer.writeheader()
                writer.writerows(report[key])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True, type=Path)
    parser.add_argument("--engine-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--drivers", default="direct3d12,vulkan")
    parser.add_argument("--scenes", default="composite_2d,composite_3d")
    parser.add_argument("--repeats", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=6000)
    parser.add_argument("--measured", type=int, default=12000)
    parser.add_argument("--size", default="1280x720")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--require-steady-jit", action="store_true",
                        help="require boundary snapshots proving no measured compilation, pipeline creation or fallback")
    args = parser.parse_args()
    if min(args.repeats, args.warmup, args.measured, args.timeout) < 1:
        parser.error("counts and timeout must be positive")
    if args.warmup + args.measured > 1000000:
        parser.error("the capture tool supports at most 1000000 frames")
    if any(s not in ("composite_2d", "composite_3d") for s in args.scenes.split(",")):
        parser.error("this baseline uses composite_2d and composite_3d")
    if any(d not in ("direct3d12", "vulkan") for d in args.drivers.split(",")):
        parser.error("unknown driver")
    if not re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*", args.size):
        parser.error("invalid --size")
    tool, engine, out = args.tool.resolve(), args.engine_dir.resolve(), args.out.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("--out must be empty")
    if not tool.is_file() or not engine.is_dir():
        parser.error("missing executable or engine directory")
    try:
        ProcessMemory()
    except ImportError:
        parser.error("process tracking requires psutil (python -m pip install psutil)")
    out.mkdir(parents=True, exist_ok=True)
    before = identities(tool, engine)
    (out / "binaries.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
    records = []
    for repeat in range(args.repeats):
        order = mode_order(repeat)
        for driver in args.drivers.split(","):
            for scene in args.scenes.split(","):
                for mode in order:
                    folder = out / f"run-{repeat + 1}" / driver / scene / mode
                    folder.mkdir(parents=True)
                    env = os.environ.copy()
                    env.update(CKRE_SDL_GPU_DRIVER=driver, CKRE_SDL_GPU_FF_JIT_CACHE="0",
                               CKRE_SDL_GPU_FF_JIT="0" if mode == "off" else "1",
                               CKRE_SDL_GPU_FF_VERTEX_JIT="0" if mode == "fragment" else "1",
                               CKRE_SDL_GPU_FF_JIT_STATS="1")
                    command = [str(tool), "--render-engine-dir", str(engine), "--rasterizer", "sdlgpu",
                               "--scene", scene, "--frames", str(args.warmup + args.measured),
                               "--profile-json", str(folder / "profile.json"),
                               "--profile-warmup", str(args.warmup), "--size", args.size, "--out", str(folder)]
                    if (engine / "CK2_3D.ini").is_file():
                        command += ["--settings-ini", str(engine / "CK2_3D.ini")]
                        # The scene launcher consumes --settings-ini; also pass
                        # the effective configuration to its renderer child.
                        env["CKRE_SETTINGS_FILE"] = str(engine / "CK2_3D.ini")
                    reference = out / "run-1" / driver / scene / "off"
                    compare = repeat != 0 or mode != "off"
                    if compare:
                        command += ["--compare", str(reference), "--threshold", "2",
                                    "--min-pass", "1", "--require-all"]
                    memory = ProcessMemory()
                    started = time.monotonic()
                    with (folder / "capture.log").open("w", encoding="utf-8") as log:
                        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                        while process.poll() is None:
                            memory.sample(process)
                            if time.monotonic() - started > args.timeout:
                                memory.stop(process)
                                break
                            try:
                                process.wait(timeout=.05)
                            except subprocess.TimeoutExpired:
                                pass
                    row = dict(driver=driver, scene=scene, mode=mode, repeat=repeat + 1,
                               modeOrder=list(order), modePosition=order.index(mode),
                               exit=process.returncode, processWallSeconds=time.monotonic() - started,
                               processMemory=memory.report(), command=command, issues=[])
                    try:
                        if process.returncode:
                            raise ValueError(f"capture exited {process.returncode}")
                        text = (folder / "capture.log").read_text(encoding="utf-8", errors="replace")
                        lines = re.findall(r"FFJIT_STATS ([^\r\n]+)", text)
                        stats = dict((k, int(v)) for k, v in re.findall(r"(\w+)=(\d+)", lines[-1])) if lines else {}
                        profile = json.loads((folder / "profile.json").read_text())
                        if os.name == "nt" and (not row["processMemory"] or
                                row["processMemory"]["processId"] != profile["processId"]):
                            raise ValueError("memory samples do not identify the profiled renderer")
                        row["summary"] = summarize_profile(profile, stats, mode, scene, args.warmup, args.measured,
                                                           args.require_steady_jit)
                        differences = re.findall(r"max diff (\d+)", text)
                        if compare and len(differences) != 1:
                            raise ValueError("missing final-image comparison")
                        row["maxChannelDiff"] = int(differences[0]) if differences else None
                        for previous in records:
                            if previous["driver"] == driver and previous["scene"] == scene and not previous["issues"]:
                                for field in ("measuredDrawCalls", "measuredPrimitives"):
                                    if previous["summary"][field] != row["summary"][field]:
                                        raise ValueError(f"workload changed: {field}")
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        row["issues"].append(str(error))
                    records.append(row)
                    report = dict(schemaVersion=1, records=records, aggregates=aggregate(records),
                                  complete=False, expectedRuns=args.repeats * len(args.drivers.split(",")) * len(args.scenes.split(",")) * 3,
                                  requireSteadyJit=args.require_steady_jit,
                                  gpuTimings=None, gpuMemory=None,
                                  notes=["Render CPU wall time includes presentation/backpressure; no GPU timestamps.",
                                         "Boundary JIT snapshots, when available, verify the measured interval; no per-frame localization.",
                                         "Fresh process and disabled JIT manifest; OS/driver caches are not cleared.",
                                         "Process memory includes engine, assets and drivers; it is not JIT/GPU memory."])
                    write_report(out, report)
                    label = "FAIL: " + "; ".join(row["issues"]) if row["issues"] else "PASS"
                    print(f"run {repeat + 1}/{driver}/{scene}/{mode}: {label}", flush=True)
    report["binariesUnchanged"] = before == identities(tool, engine)
    report["complete"] = len(records) == report["expectedRuns"]
    write_report(out, report)
    if not report["binariesUnchanged"]:
        raise RuntimeError("runtime binaries/settings changed during measurement")
    return int(any(row["issues"] for row in records))


if __name__ == "__main__":
    raise SystemExit(main())
