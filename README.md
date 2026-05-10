# CKRenderEngine

The Render Engine of Virtools.

## Standalone Build

Use this module's presets from the `Source/RenderEngine` directory:

```powershell
cmake --preset renderengine-bgfx-runtime-msvc-win32
cmake --build --preset renderengine-bgfx-runtime-win32-release
```

Available presets:

- `renderengine-bgfx-runtime-msvc-win32`
- `renderengine-bgfx-runtime-msvc-x64`
- `renderengine-bgfx-static-msvc-win32`
- `renderengine-bgfx-static-msvc-x64`
- `renderengine-bgfx-tests-msvc-win32`
- `renderengine-bgfx-tests-msvc-x64`

This submodule is independently buildable and does not require CMake helper modules from the root project.
