# CKRenderEngine

CKRenderEngine implements the Virtools rendering layer used by Ballanced: the
`CK2_3D` engine module, the public rasterizer library, a fixed-function
compatibility layer and the `CKSdlGpuRasterizer`.

## Architecture

The implementation has three rasterizer modules with one dependency direction:

```text
CKSdlGpuRasterizer ─> CKFFPLib ─> CKRasterizerLib ─> CK2 / VxMath

CKNullRasterizer is implemented directly inside CKRasterizerLib.
```

- **CKRasterizerLib** owns the public `CKRasterizer` interface, common
  configuration and diagnostics, capability baselines, registration support,
  and the direct NULL implementation.
- **CKFFPLib** contains the shared CPU implementation for fixed-function state,
  validation, shader keys and constants, and vertex/index conversion. It does
  not own devices, native resources, submission, synchronization or presentation.
- **CKSdlGpuRasterizer** owns the complete concrete rasterizer context,
  including native resources, command submission, synchronization,
  presentation, shader packs and their offline build tools.

The rasterizer is SDL GPU on Direct3D 12, Vulkan and Metal; NULL remains an independent engine fallback. Player selects rasterizers by the stable names `sdlgpu` and `null`. Runtime settings live in `src/CK2_3D.ini`.

Tests and reference provenance live in `tests/` and `tests/reference/`. The original `CKDX8Rasterizer.dll` is the visual oracle. The scene capture tool compares procedural scenes; GPU pixel gates require a real visible window for local acceptance.

## Support scope

The instructions in this document describe CKRenderEngine's `sdl` branch. That branch is continuously built through [Ballanced](https://github.com/doyaGu/Ballanced) on Windows, Linux, and macOS, including backend runtime gates for Direct3D 12, Vulkan and Metal.

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
cmake --preset renderengine-runtime-msvc-win32
cmake --build --preset renderengine-runtime-win32-release
```

Configure presets:

- `renderengine-runtime-msvc-win32`
- `renderengine-runtime-msvc-x64`
- `renderengine-static-msvc-win32`
- `renderengine-static-msvc-x64`
- `renderengine-tests-msvc-win32`
- `renderengine-tests-msvc-x64`

Build presets follow `renderengine-<mode>-<arch>-release`.

## Standalone manual build

A manual CMake build can be used on other CMake-supported hosts. From a Ballanced `Source/RenderEngine` checkout, sibling CK2 and VxMath projects are discovered locally.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCKRE_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/path/to/SDL3
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements include CMake 3.16+, SDL3, a C++ toolchain, and CK2/VxMath from sibling projects, installed packages, or a configured Virtools SDK fallback.

The SDL_gpu shader packs are checked in under
`src/CKRasterizer/CKSdlGpuRasterizer/shaders/generated/`; builds only verify
them. `shaders/compile_native_shaders.py` regenerates them with DXC, FXC and
SPIRV-Cross. Scene capture uses stb headers.

RenderEngine's standard `Runtime` installation owns configuration files, disabled
provider cleanup and runtime layout verification. This also applies to standalone
component installs and `cmake --install --prefix`. Ballanced's `stage` simply
invokes installation. Cleanup removes only known provider binaries inside an
isolated build prefix, including the retired `CKBgfxRasterizer`, preserves
configurations and other plugins, and refuses to modify stale binaries in
external prefixes. Use a fresh external prefix or remove the reported stale
plugin. `RasterizerInstallSelection` tests these rules.
Library naming is shared by cleanup and validation, including MinGW `lib*.dll`,
ELF `.so` and Mach-O `.dylib` names. Windows prefix comparisons ignore case while
preserving resolved-path containment checks.

## Versioning

CKRenderEngine is versioned independently. Ballanced releases pin the renderer commit used for a runtime.

## License

Apache License 2.0. See [LICENSE](LICENSE).
