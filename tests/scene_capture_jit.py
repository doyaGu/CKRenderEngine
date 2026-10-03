"""Scene-level JIT parity using CK2 objects and the dynamic render engine.

Only the standard library is required. The capture tool writes/compares PNGs.
Assets, captures and machine-specific reports belong in the build directory.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True, type=Path)
    parser.add_argument("--engine-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--drivers", default="direct3d12,vulkan")
    parser.add_argument("--scenes", default="all")
    args = parser.parse_args()
    tool, engine, output = args.tool.resolve(), args.engine_dir.resolve(), args.out.resolve()
    listing = subprocess.run([str(tool), "--list-scenes"], check=True, capture_output=True, text=True).stdout
    scenes = re.findall(r"^(\w+)\s+oracle=", listing, re.MULTILINE)
    if args.scenes != "all":
        wanted = args.scenes.split(",")
        if any(scene not in scenes for scene in wanted):
            parser.error("unknown scene")
        scenes = wanted
    if not scenes:
        parser.error("capture tool listed no scenes")
    output.mkdir(parents=True, exist_ok=True)
    results = []
    for driver in args.drivers.split(","):
        for mode in ("off", "fragment", "on"):
            folder = output / driver / mode
            folder.mkdir(parents=True, exist_ok=True)
            for scene in scenes:
                env = os.environ.copy()
                env.update(CKRE_SDL_GPU_DRIVER=driver, CKRE_SDL_GPU_FF_JIT_CACHE="0",
                           CKRE_SDL_GPU_FF_JIT="0" if mode == "off" else "1",
                           CKRE_SDL_GPU_FF_VERTEX_JIT="0" if mode == "fragment" else "1",
                           CKRE_SDL_GPU_FF_JIT_STATS="1")
                command = [str(tool), "--render-engine-dir", str(engine), "--rasterizer", "sdlgpu",
                           "--scene", scene, "--frames", "120", "--frame-delay-ms", "16",
                           "--capture-frames", "1,5,30,120",
                           "--size", "640x480", "--hidden", "--out", str(folder)]
                if mode != "off":
                    command += ["--compare", str(output / driver / "off"), "--threshold", "2",
                                "--min-pass", "1", "--require-all"]
                try:
                    run = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, errors="replace", timeout=90)
                    text, code = run.stdout, run.returncode
                except subprocess.TimeoutExpired as error:
                    text, code = str(error), -1
                (folder / (scene + ".log")).write_text(text, encoding="utf-8")
                rows = re.findall(r"FFJIT_STATS ([^\r\n]+)", text)
                stats = dict((name, int(value)) for name, value in re.findall(r"(\w+)=(\d+)", rows[-1])) if rows else {}
                issues = []
                if code: issues.append(f"capture/compare exit {code}")
                if not stats: issues.append("missing JIT execution evidence")
                if any(stats.get(name, 0) for name in ("compile_failed", "vertex_failed", "pipeline_failed")):
                    issues.append("shader or pipeline creation failed")
                if mode == "off":
                    if stats.get("selected", 0) or stats.get("compile_queued", 0): issues.append("reference used JIT")
                elif not stats.get("ready", 0):
                    issues.append("no ready JIT pipeline was drawn")
                if mode != "on" and (stats.get("positiont", 0) or stats.get("unlit", 0) or stats.get("lit", 0) or stats.get("tween", 0) or stats.get("blend", 0) or stats.get("clip", 0) or stats.get("pad", 0)):
                    issues.append("disabled vertex JIT was drawn")
                if mode == "on" and scene in ("composite_2d", "composite_3d"):
                    required = "positiont" if scene == "composite_2d" else "unlit"
                    if not stats.get(required, 0): issues.append(f"no generated {required} vertex draw")
                if mode == "on" and scene in ("composite_3d", "opaque_lit", "lighting_dynamic", "lighting_attenuation", "tween_3d", "skinning_3d") and not stats.get("lit", 0):
                    issues.append("no generated lit vertex draw")
                if mode == "on" and scene == "tween_3d":
                    if not stats.get("tween", 0) or not stats.get("unlit", 0):
                        issues.append("no generated tween/unlit vertex draw")
                if mode == "on" and scene == "skinning_3d":
                    if not stats.get("blend", 0) or not stats.get("unlit", 0):
                        issues.append("no generated matrix-blend/unlit vertex draw")
                if mode == "on" and scene == "clipping_3d":
                    for field in ("clip", "positiont", "unlit", "lit", "tween", "blend"):
                        if not stats.get(field, 0): issues.append(f"no generated {field} draw in clipping scene")
                maxima = [int(x) for x in re.findall(r"max diff (\d+)", text)]
                record = dict(driver=driver, mode=mode, scene=scene, exit=code, stats=stats,
                              comparisons=len(maxima), max_channel_diff=max(maxima, default=None), issues=issues)
                if mode != "off" and len(maxima) != 5: issues.append("expected final image and four checkpoints")
                results.append(record)
                (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
                print(f"{driver}/{mode}/{scene}: {'FAIL ' + '; '.join(issues) if issues else 'PASS'}", flush=True)
    return int(any(result["issues"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
