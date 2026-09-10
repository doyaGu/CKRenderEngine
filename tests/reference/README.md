# Render engine visual references

This directory contains portable inputs for rasterizer regression tests. The
original `CKDX8Rasterizer.dll` is the visual oracle, while bgfx remains a
same-engine regression target.

## Versioned assets

- `caps-baseline.json` contains the legacy capability values used to generate
  `CKRasterizerCapsBaseline.generated.h`.
- `frames/oracle/<scene>.png` contains procedural reference frames. These
  fixtures do not contain game assets.
- `frames/golden/<runner>-<backend>/<scene>.png` is reserved for reviewed
  same-backend CI output.

Local captures, proprietary modules, machine details, process metadata and
acceptance records are intentionally not versioned.

## Comparing a build

Build with `CKRE_BUILD_TOOLS=ON`, then run the scene-capture tool against a
staged render-engine directory:

```text
bin/ckre_scene_capture --render-engine-dir <staged RenderEngines> --rasterizer sdlgpu \
    --scene all --frames 5 --out tests/reference/frames/local \
    --compare tests/reference/frames/oracle
```

Per-scene thresholds are reported by `--list-scenes`; `--threshold` and
`--min-pass` override them. `frames/local/` is scratch output and must remain
untracked.

To refresh the capability header after an authorized baseline update, run:

```text
python tools/gen_caps_baseline_header.py tests/reference/caps-baseline.json \
    src/CKRasterizer/CKRasterizerLib/CKRasterizerCapsBaseline.generated.h
```
