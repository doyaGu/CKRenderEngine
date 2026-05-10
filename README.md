# CKRenderEngine

The Render Engine of Virtools.

## Standalone Build

Use this module's presets from the `Source/RenderEngine` directory:

```powershell
cmake --preset renderengine-dx9-runtime-msvc-win32
cmake --build --preset renderengine-dx9-runtime-win32-release
```

Available presets:

- `renderengine-dx9-runtime-msvc-win32`
- `renderengine-dx9-runtime-msvc-x64`
- `renderengine-dx9-static-msvc-win32`
- `renderengine-dx9-static-msvc-x64`
- `renderengine-dx9-tests-msvc-win32`
- `renderengine-dx9-tests-msvc-x64`

This submodule is still independently buildable. It does not include CMake helper modules from the Ballanced root project.
