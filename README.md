# CKRenderEngine

CKRenderEngine implements the Virtools rendering layer used by Ballanced: the `CK2_3D` engine module, a translation core that turns the engine's Direct3D 7 style calls into GPU work, the default `CKSdlGpuRasterizer` plugin, and the optional `CKBgfxRasterizer` plugin.

## Architecture

The render engine is layered around one contract and one thin backend interface:

- **Engine** (`src/`, `include/RCK*.h`) — scene graph, render objects, materials, textures, sprites. It drives the rasterizer through the D3D7-shaped `CKRasterizer` v3 contract in `include/CKRasterizer.h` (render states, texture stage states, transforms, lights, materials, `DrawPrimitive*`, vertex/index buffer locks, `Clear` / `BeginScene` / `EndScene` / `BackToFront`, render-target textures, readbacks).
- **Translation core** (`src/CKRasterizer/CKRasterizerLib/`, static library linked into `CK2_3D` and every rasterizer plugin) — `CKTranslatedRasterizer` / `CKTranslatedDriver` / `CKTranslatedContext` implement the contract: a verbatim state mirror, the fixed-function pipeline (`CKFixedFunctionPipeline`, `CKFF*`: one uber shader family plus per-draw specialization data), the virtual backbuffer and present stage (render scale, MSAA, FXAA, sharpening, readbacks), transient geometry and vertex layouts. Draws reach the GPU through the ~30-method `CKRasterizerBackend` interface (`CKRasterizerBackend.h`).
- **Backends** — `CKBgfxRasterizer` (`src/CKRasterizer/CKBgfxRasterizer/`, `CKBgfxBackend` on bgfx: Direct3D 11/12, Vulkan, OpenGL/ES, Metal) and the built-in NULL backend (`CKNullBackend`, the fallback when no plugin loads and the base of the test harness).
- **Reference and tests** — `tests/reference/` holds the capability baseline and the reference frames captured from the original Virtools rasterizer; `tools/scene_capture/` renders procedural scenes through any rasterizer and compares them (`ckre_scene_capture --compare`). Unit and contract tests live in `tests/` and `src/CKRasterizer/tests/`; the GPU pixel gate `rasterizer3_pixel_tests` runs when `CKRE_RUN_BGFX_BACKEND_RUNTIME_TESTS=1` (`CKBGFX_RENDERER_BACKEND` selects d3d11 / d3d12 / vulkan / opengl / opengles / metal).

The design is documented in the Ballanced superproject: `docs/spec/2026-09-01-render-engine-redesign-v3.md` (specification) and `docs/superpowers/plans/2026-09-02-render-engine-v3-implementation-plan.md` (phased implementation record).

Runtime configuration: `src/CK2_3D.ini` (engine and translation core, installed next to `CK2_3D`) and `src/CKRasterizer/CKBgfxRasterizer/CKBgfxRasterizer.ini` (bgfx backend selection, shader cache, logging).

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

A manual CMake build can be used on other CMake-supported hosts. From a Ballanced `Source/RenderEngine` checkout, sibling CK2 and VxMath projects are discovered locally. bgfx is fetched only when explicitly enabled.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCKRE_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/SDL3
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements include CMake 3.16+ (3.20+ with bgfx), SDL3, a C++ toolchain, and CK2/VxMath from sibling projects, installed packages, or a configured Virtools SDK fallback.

Checked-in generated shader headers are used by the Ballanced presets. Set `CKRE_GENERATE_SHADERS=ON` only when intentionally regenerating them with Python and a host-compatible bgfx `shaderc`.

## Optional bgfx dependency

`CKRE_BUILD_BGFX_RASTERIZER` defaults to `OFF`. SDL-only and NULL builds do not
fetch or build bgfx, bx, bimg, or bgfx.cmake. bgfx is no longer a Git submodule.
Enable it explicitly for either dynamic or static builds:

```powershell
cmake -S . -B build -DCKRE_BUILD_BGFX_RASTERIZER=ON
```

`cmake/CKREBgfx.cmake` pins upstream archive revisions and SHA-256 hashes.
CMake populates them under the build directory's `_deps`, without source patches.
Existing CMake caches retain their explicit option values. The named bgfx presets
also enable the option explicitly. Disable `CKRE_GENERATE_SHADERS` when disabling bgfx.
For offline enabled builds, prepopulate CMake's FetchContent cache or provide its
`FETCHCONTENT_SOURCE_DIR_CKRE_BGFX_CMAKE`, `FETCHCONTENT_SOURCE_DIR_CKRE_BGFX`,
`FETCHCONTENT_SOURCE_DIR_CKRE_BX` and `FETCHCONTENT_SOURCE_DIR_CKRE_BIMG` overrides.

The adapter owns the D3D11 rectangular-clear program and preserves independent
color/depth/stencil write masks through public bgfx APIs. Full attachment clears
retain the native fast path. Its offline shader generator takes explicit
`--shaderc` and `--bgfx-source` paths; runtime does not invoke a compiler.
Scene capture uses stb headers independently of bgfx/bimg.

## Versioning

CKRenderEngine is versioned independently. Ballanced releases pin the renderer and bgfx commits used for a runtime.

## License

Apache License 2.0. See [LICENSE](LICENSE).
