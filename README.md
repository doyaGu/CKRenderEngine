# CKRenderEngine

CKRenderEngine implements the Virtools rendering layer used by Ballanced: the `CK2_3D` engine module, a translation core that turns the engine's Direct3D 7 style calls into GPU work, the default `CKSdlGpuRasterizer` plugin, and the optional `CKBgfxRasterizer` plugin.

## Architecture

The responsibilities follow `CK_3D → CKRasterizer → CKRasterizerBackend → Graphics API`:

- **CK_3D** owns the public engine API, scene objects and traversal. It issues public rasterizer revision 5 calls and loads providers through the registration entry points.
- **CKRasterizerLib** implements fixed-function state, shader variants, geometry preparation and frame composition: render scale, MSAA, postprocessing, overlays and logical readbacks.
- **CKRasterizerBackend** defines modern graphics resources, explicit programs, constant blocks, resolved pipeline state, ordered commands and completion. It is an internal contract independent of FFP and plugin loading.
- **Native backends** implement that contract: `CKSdlGpuBackend` for SDL GPU, optional `CKBgfxBackend` for bgfx, and `CKNullBackend` for fallback and deterministic tests. The dynamic/static rasterizer plugins compose a native backend with the translation core and their own shader artifact catalog.

See [ARCHITECTURE.md](src/CKRasterizer/ARCHITECTURE.md) for the current implementation's ownership and dependency boundaries.

The default provider is SDL GPU. bgfx is available only when explicitly enabled; NULL remains an independent engine fallback. Player selects providers by stable names `sdlgpu`, `bgfx` and `null`. Runtime settings live in `src/CK2_3D.ini` and, when enabled, `src/CKRasterizer/CKBgfxRasterizer/CKBgfxRasterizer.ini`.

Tests and reference provenance live in `tests/` and `tests/reference/`. The original `CKDX8Rasterizer.dll` is the visual oracle. The scene capture tool compares procedural scenes; GPU pixel gates require a real visible window for local acceptance.

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

Shader tool selection distinguishes the build host from the target OS and
architecture. A cross-compiled shaderc is not assumed executable on the host;
the host build uses the host executable suffix. macOS universal targets are
checked for a native host slice; Rosetta or other emulators are not assumed.
Set `CKRE_SHADERC_EXECUTABLE` to supply a compatible host tool explicitly.

The adapter owns the D3D11 rectangular-clear program and preserves independent
color/depth/stencil write masks through public bgfx APIs. Full attachment clears
retain the native fast path. Its offline shader generator takes explicit
`--shaderc` and `--bgfx-source` paths; runtime does not invoke a compiler.
Scene capture uses stb headers independently of bgfx/bimg.

RenderEngine's standard `Runtime` installation owns configuration files, disabled
provider cleanup and runtime layout verification. This also applies to standalone
component installs and `cmake --install --prefix`. Ballanced's `stage` simply
invokes installation. Cleanup removes only known provider binaries inside an
isolated build prefix, preserves configurations and other plugins, and refuses
to modify stale binaries in external prefixes. Use a fresh external prefix or
remove the reported stale plugin. `RasterizerInstallSelection` tests these rules.
Library naming is shared by cleanup and validation, including MinGW `lib*.dll`,
ELF `.so` and Mach-O `.dylib` names. Windows prefix comparisons ignore case while
preserving resolved-path containment checks.

## Versioning

CKRenderEngine is versioned independently. Ballanced releases pin the renderer and bgfx commits used for a runtime.

## License

Apache License 2.0. See [LICENSE](LICENSE).
