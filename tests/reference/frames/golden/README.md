# Renderer regression frames

Each platform directory contains frozen procedural frames and `baseline.json`,
which identifies their renderer and scene source revision. Native font
rasterization requires separate Windows, Linux and Darwin references. Font
heights are specified in pixels so desktop DPI does not change the fixture.
Input-only refreshes record `renderer_revision`, `scene_revision` and the
`capture_run` on the updated scene entries. Other entries retain the manifest's
original provenance. The engine-neutral text sample is captured separately on
each platform using its native font rasterizer.
The Windows text refresh uses five requested frames with GPU validation enabled
and explicitly selected WARP. Differences outside the caption satisfy the
existing comparison limits.

`scene_capture_golden` renders every scene at 640x480 with five requested frames
(scene-specific minimum frame counts still apply), then requires every reference
image at threshold 2/255 and 99.5% passing pixels. Missing references fail. Tests
never generate or update expected images, and scene exclusions are rejected.

Format 2 manifests can name additional complete reference frames for a scene.
Every frame has its own SHA-256 and source revisions. Each captured image must
match one whole reviewed frame at the same threshold. The manifest covers every
registered scene and every reference PNG exactly once.

Windows includes a WARP reference for `cube_mip_dynamic`, alongside the NVIDIA
reference. Native cube anisotropic filtering and LOD estimates can differ across
GPU implementations; see the [Direct3D filtering specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
The WARP capture uses SDL 3.4.20. Its off, fragment and full JIT modes match at all
six replay checkpoints; the complete suite also checks compiled-pipeline use.

The initial Windows and Linux references use renderer production sources from
`99bb47aa26f44fd37221cda0039d201348a83d66`, unchanged by the test repairs. They
check renderer regressions; original-rasterizer comparisons remain a separate
diagnostic corpus under `../oracle`. For example, the original DX8 backend cannot
paste `dump_copy`, whose exact source-to-destination comparison checks 27,648 RGB pixels.

Capture proposed references with `ckre_scene_capture --scene all --size 640x480
--frames 5 --out <review-directory>`, review the changes and compare with the
original diagnostics before updating the versioned fixtures. Captures contain
procedural test scenes, never game assets. Original diagnostic images are a
historical corpus; verify their scene inputs before comparing another version.

Darwin references use SDL 3.4.20 and the Metal backend. Earlier SDL releases
reported a busy fence as completed, allowing upload buffers to be reused before
their draws finished. Both RTT scenes now match exactly between their first and
fifth frames. The suite compares ARM64 and Rosetta x86_64 execution against
these frames.
