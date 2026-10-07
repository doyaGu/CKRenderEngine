# CI golden frames

One directory per CI runner and SDL_gpu driver, named `<runner>-<driver>`:

- `ci-windows-x64-direct3d12` (WARP)
- `ci-linux-x64-vulkan` (lavapipe under xvfb)
- `ci-macos-arm64-metal`

Each directory holds `<scene>.png` produced by `ckre_scene_capture` with our
engine on that runner. `scene_capture_golden` (tests/CMakeLists.txt) compares
new frames against them with threshold 2/255 and 99.5 % passing pixels, and is
skipped when the directory for the current runner/backend does not exist.

Regenerate through the `Build` workflow (`workflow_dispatch`, `update-golden =
true`), download the `golden-frames-*` artifacts and commit them. Golden frames
are updated only after the oracle comparison (`frames/oracle/`) did not get
worse.
