# Render engine visual references

Reference material for the public CKRasterizer v3 contract and its SDL GPU,
bgfx and NULL implementations. Current ownership and dependency rules are in
[ARCHITECTURE.md](../../src/CKRasterizer/ARCHITECTURE.md).

## Acceptance records

These records describe specific tested revisions and binaries, not cumulative
test totals. A later build does not inherit earlier GPU passes.

- [SDL GPU performance](SDL_GPU_PERFORMANCE.md): GPU/CPU transient buffer reuse,
  index validation, corrected capture methodology and current regression evidence.
- [Redesign baseline](SDL_GPU_REDESIGN_BASELINE.md): separate foreground FPS,
  FFP/native timeline decomposition, wait evidence and capture limitations.
- [Redesign progress](SDL_GPU_REDESIGN_PROGRESS.md): implemented slices,
  revision changes, foreground regressions and stage performance checks.
- [Optional upstream bgfx](BGFX_OPTIONAL_ACCEPTANCE.md): current dependency,
  staging and unmodified-upstream validation.
- [Rasterizer layer refactor](RASTERIZER_LAYER_ACCEPTANCE.md): boundary and
  performance measurements before the optional dependency change.
- [Initial SDL implementation](SDL_GPU_ACCEPTANCE.md): historical implementation
  acceptance before the layer refactor.
- [Known visual differences](KNOWN_DIFFERENCES.md): DX8 comparisons and their
  observed limits. bgfx is a regression target, not the visual oracle.

## Reference files

| Path | Content | In git |
|---|---|---|
| `caps-baseline.json` | `Vx3DCapsDesc` / `Vx2DCapsDesc` reported by the original `CKDX8Rasterizer.dll`. Source of `src/CKRasterizer/CKRasterizerLib/CKRasterizerCapsBaseline.generated.h` via `tools/gen_caps_baseline_header.py`. | yes |
| `frames/oracle/<scene>.png` | Procedural frames rendered by the original `CK2.dll + CK2_3D.dll + CKDX8Rasterizer.dll` (the visual oracle). | yes |
| `oracle-manifest.json` | Original module hashes, capture conditions, image hashes and process IDs. DLLs and game assets stay local. | yes |
| `frames/oracle/<scene>.mask.png` | Optional per-scene mask (red channel non-zero = ignored pixel). None yet. | yes |
| `frames/golden/<runner>-<backend>/<scene>.png` | Location for reviewed same-backend CI frames (WARP / llvmpipe / Metal), when available. Compared with threshold 2/255 and 99.5 % passing pixels. | yes, when populated |
| `frames/local/` | Scratch output of local runs. | no |
| `KNOWN_DIFFERENCES.md` | Per-scene observations against the oracle, with capture provenance. | yes |

## How the oracle was captured

The original `CK2_3D.dll` cannot be loaded by the Ballanced
`CK2.dll`: the recorded `tools/check_dll_imports.py` comparison found 74 imports missing
from our `CK2.dll` and 55 from our `VxMath.dll` (MSVC6-mangled names of
methods we do not export, e.g. `CKContext::GetObjectA`, `CKStateChunk::*`,
`CKBitmapData::SaveImage`). `LoadLibrary` fails, so `ParsePlugins` finds no
render engine.

The fallback that works: build the same capture tool against the Virtools SDK
2.1 import libraries and let it load the whole original stack.

```
# 32-bit, Virtools SDK 2.1 checkout with Include/ and Lib/{CK2,VxMath}.lib
cmake -S Source/RenderEngine/tools/scene_capture -B build-sdk-x86 ^
      -G "Visual Studio 17 2022" -A Win32 ^
      -DVIRTOOLS_SDK_PATH=<sdk> -DCMAKE_PREFIX_PATH=<SDL3 x86 cmake dir>
cmake --build build-sdk-x86 --config Release
# next to the exe: original CK2.dll, VxMath.dll, CKZlib.dll (Ballance Bin/)
build-sdk-x86\bin\Release\ckre_scene_capture.exe ^
      --render-engine-dir <Ballance>\RenderEngines --native-window-handle ^
      --scene all --out Source\RenderEngine\tests\reference\frames\oracle ^
      --caps-json Source\RenderEngine\tests\reference\caps-baseline.json
```

`--native-window-handle` is mandatory for the original engine: it treats the
window handle as a Win32 `HWND`. The same flag exists in the Player
(`--native-window-handle`, `Graphics.NativeWindowHandle`) for whole-game
comparisons against the original render engine DLLs.

The tool creates a `CKLevel` and adds every scene object to its scene (2D
entities only render when they belong to the current scene) and registers the
render context as the level's main context.

Capture conditions of the current set: original Ballance retail DLLs,
NVIDIA GeForce RTX 4090 "T&L DX8" driver 0, Windows 11, x86, 640x480,
frame 5, captured 2026-09-05. All 17 scenes were inspected in real foreground
windows, serially, with desktop screenshots and process metadata retained in
the local build directory named in `oracle-manifest.json`.
Our engine reads the presented frame back through the
native target, so the tool captures after a single frame by default
(`--frames 1`; scenes that need more declare `MinFrames`, e.g. `dump_copy`).
The `scene_capture_golden` CTest gate explicitly captures frame 5, after
persistent mesh buffers become active.  `scene_capture_multiframe` also
compares frame 1 with frame 5 for every static scene so promotion to hardware
buffers cannot silently change the rendered result.
`present_*` scenes have no oracle: they only change CK2_3D.ini presentation
options of our engine (the tool writes `<scene>.CK2_3D.ini` next to the output
and points the engine at it through `CKRE_SETTINGS_FILE`). Older files named
`present_*` in the oracle directory were not valid DX8 references and have
been removed. Same-backend presentation references belong in `frames/golden/`.

## Comparing our engine

```
# our engine (any platform, shared build with CKRE_BUILD_TOOLS=ON)
bin/ckre_scene_capture --render-engine-dir <staged RenderEngines> --rasterizer sdlgpu ^
      --scene all --frames 5 --hold-ms 600000 --out tests/reference/frames/local ^
      --skip dump_copy,present_renderscale,present_fxaa,present_sharpness,present_msaa ^
      --compare tests/reference/frames/oracle
```

Per-scene thresholds live in the scene table (`--list-scenes`);
`--threshold` / `--min-pass` override them. `--skip` omits the named scenes
from the run. The example excludes five scenes only
from the DX8 comparison: run them separately in foreground windows as
functional tests. `dump_copy` validates every pasted RGB pixel against the
source readback; it must not reproduce DX8's failed `CopyToVideo` operation.
The other four exercise presentation features absent from this DX8 fixture.
bgfx is a regression target, not the visual oracle.

## Updating

- Caps baseline: rerun the oracle capture with `--caps-json`, then
  `python tools/gen_caps_baseline_header.py tests/reference/caps-baseline.json src/CKRasterizer/CKRasterizerLib/CKRasterizerCapsBaseline.generated.h`.
- Oracle frames: only when a scene changes. Re-capture every scene on the
  reference machine and record the conditions above.
- Golden frames: `workflow_dispatch` with `update-golden = true` uploads
  `frames/golden/*` as an artifact; commit it after checking the diff.
