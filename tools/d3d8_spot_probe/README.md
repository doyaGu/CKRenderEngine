# Native D3D8 spotlight reference

This optional Windows diagnostic distinguishes hardware and software vertex
processing in the **system D3D8 runtime**. It does not load CK2, the SDL
rasterizer, generated shaders, D3DX or a replacement D3D8 DLL.

A small normal-bearing quad faces a white spotlight. Diffuse material is 0.2;
ambient, emissive and specular contributions are zero; native distance
attenuation is one. The outer cone is 45 degrees and exponent is one. The
three inner angles are 0, 20 and approximately 34.38 degrees. At nonzero inner
angles the quad lies entirely within the inner cone, where the spotlight
factor must be one. All center RGB channels should be 50 or 51 after diffuse
cosine and 8-bit rounding. The zero-angle case is a transition-region control.

The probe creates separate HAL devices with software and hardware vertex
processing. API or initialization failures return 2; unexpected software
reference intensity returns 1. Hardware results are reported without turning
vendor-specific deviations into the required shader behavior. Exit 0 therefore
does **not** mean the two paths match. It also does not establish scene-level
or cross-vendor conformance.

## Build and run

Supply an external directory containing `d3d8.h`, `d3d8types.h` and
`d3d8caps.h`, such as a legacy DirectX SDK include directory. Nothing is
downloaded or vendored, and the superproject does not build this tool.

```powershell
cmake -S Source/RenderEngine/tools/d3d8_spot_probe -B build/d3d8-spot-probe `
  -G "Visual Studio 17 2022" -A Win32 -DD3D8_INCLUDE_DIR=C:/path/to/d3d8/headers
cmake --build build/d3d8-spot-probe --config Release
build/d3d8-spot-probe/Release/d3d8_spot_probe.exe
```

The recorded local run used Wine's declaration headers from
[wine-mirror/wine](https://github.com/wine-mirror/wine/tree/master/include)
with MSVC, but executed `C:\Windows\SYSTEM32\d3d8.dll`. Record the external
header hashes with the executable, system runtime and driver identity when
preserving a measurement. No Wine implementation code executes in this probe.

For measured values, the ordinary 3D scene, and the decision to retain the
bounded shader calculation, see [the DX8 comparison report](../../tests/FFJitDx8Oracle.md).

## Silhouette coverage diagnostic

`d3d8_spot_probe.exe --coverage` replays one unlit 3D triangle taken from the
central sphere in `lighting_spotlight`, with its original world, view and
projection matrices and a 640-by-480 viewport. It reports whether pixel
`(311,188)` is covered under each vertex-processing mode. Software VP also
exports its three screen-space positions using `ProcessVertices`; the fourth
component is reciprocal homogeneous W, as required by XYZRHW.

This mode returns 0 when the API calls succeed and 2 on an API or setup error.
It reports coverage without asserting a universal hardware result. Unlike the
default spotlight-intensity check, it is a numerical diagnostic, not a required
lighting rule. The measured native software and hardware paths both cover the
sample. The modern scene does not: a roughly 0.00003-pixel projection difference
moves one vertex across an 8-bit subpixel rounding boundary. The report retains
the exact coordinates and the resulting strict image-comparison failure.
