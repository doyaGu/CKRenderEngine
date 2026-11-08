# Renderer regression frames

Each platform directory contains frozen procedural frames and `baseline.json`,
which identifies their renderer and scene source revision. Native font
rasterization requires separate Windows, Linux and Darwin references. Font
heights are specified in pixels so desktop DPI does not change the fixture.

`scene_capture_golden` renders every scene at 640x480 with five requested frames
(scene-specific minimum frame counts still apply), then requires every reference
image at threshold 2/255 and 99.5% passing pixels. Missing references fail. Tests
never generate or update expected images, and scene exclusions are rejected.

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

Darwin references have not been captured yet. The test renders candidate frames
and fails explicitly when a reviewed platform baseline is absent; CI retains
those frames for review instead of reporting a skipped or passing comparison.
