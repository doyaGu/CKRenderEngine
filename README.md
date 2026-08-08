# CKRenderEngine

CKRenderEngine implements the Virtools rendering layer used by Ballanced. It contains the `CK2_3D` engine module, scene and render-object implementations, a fixed-function compatibility pipeline, and the bgfx-backed `CKBgfxRasterizer` plugin.

## Support scope

The instructions in this document describe CKRenderEngine's `sdl` branch. That branch is continuously built through [Ballanced](https://github.com/doyaGu/Ballanced) on Windows, Linux, and macOS, including backend runtime gates for Direct3D, OpenGL/OpenGL ES, Vulkan, and Metal where applicable.

The standalone `CMakePresets.json` currently provides Visual Studio 2022 presets for Windows x86 and x64 only. That narrower preset list does not limit the platform matrix supported by the Ballanced superproject.

## Recommended: Ballanced superproject

```bash
git clone --recurse-submodules https://github.com/doyaGu/Ballanced.git
cd Ballanced
cmake --preset macos-arm64-tests # choose the preset for your host
cmake --build --preset macos-arm64-tests-release
ctest --preset macos-arm64-tests-release
```

The staged renderer is installed under `build/<preset>/stage/RenderEngines/`.

## Standalone Windows presets

Run these commands from the CKRenderEngine repository root:

```powershell
cmake --preset renderengine-bgfx-runtime-msvc-win32
cmake --build --preset renderengine-bgfx-runtime-win32-release
```

Configure presets:

- `renderengine-bgfx-runtime-msvc-win32`
- `renderengine-bgfx-runtime-msvc-x64`
- `renderengine-bgfx-static-msvc-win32`
- `renderengine-bgfx-static-msvc-x64`
- `renderengine-bgfx-tests-msvc-win32`
- `renderengine-bgfx-tests-msvc-x64`

Build presets follow `renderengine-bgfx-<mode>-<arch>-release`.

## Standalone manual build

A manual CMake build can be used on other CMake-supported hosts. From a Ballanced `Source/RenderEngine` checkout, sibling CK2 and VxMath projects and the nested bgfx dependencies are discovered locally.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCKRE_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/SDL3
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements include CMake 3.16+, SDL3, a C++ toolchain, initialized recursive submodules, and CK2/VxMath from sibling projects, installed packages, or a configured Virtools SDK fallback.

Checked-in generated shader headers are used by the Ballanced presets. Set `CKRE_GENERATE_SHADERS=ON` only when intentionally regenerating them with Python and a host-compatible bgfx `shaderc`.

## Versioning

CKRenderEngine is versioned independently. Ballanced releases pin the renderer and bgfx commits used for a runtime.

## License

Apache License 2.0. See [LICENSE](LICENSE).
