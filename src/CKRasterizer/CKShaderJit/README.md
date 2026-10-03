# CKShaderJit current status

CKShaderJit is the small runtime compiler used by the SDL GPU rasterizer to
specialize Virtools fixed-function draws. Its job is to translate the resolved
fixed-function program into a compact typed IR and emit a native shader for the
active API. It is not the owner of fixed-function semantics: state resolution,
canonicalization, uniform packing and the precompiled fallback live in
CKFFPLib and the rasterizer.

This document describes the current implementation and its remaining work.
Historical measurements and investigations stay in the linked test reports.

## Data flow and ownership

```text
CK2 draw state
  -> CKFFPLib resolves state and resources
  -> canonical fragment key + sampler layout
  -> CKFFNative*Jit builds a fragment and eligible vertex program
  -> CKJitBuilder produces verified typed SSA dataflow IR
  -> CKJitDxbc or CKJitSpirv emits native bytecode
  -> SDL GPU creates shaders and pipelines in background jobs
  -> the draw selects the generated pipeline when ready
     or keeps using the matching precompiled fallback
```

| Responsibility | Main implementation |
| --- | --- |
| IR types, nodes, verification and dumps | `CKJitIR.h`, `CKJitIR.cpp` |
| Operations and their semantic contract | `CKJitOps.def` |
| Construction, folding, hash-consing and structured regions | `CKJitBuilder.h`, `CKJitBuilder.cpp` |
| DXBC shader-model emission | `CKJitDxbc.h`, `CKJitDxbc.cpp` |
| SPIR-V emission | `CKJitSpirv.h`, `CKJitSpirv.cpp` |
| Fixed-function fragment translation | `../CKFFPLib/ShaderModel/CKFFNativeFragmentJit.*` |
| POSITIONT and 3D vertex translation | `../CKFFPLib/ShaderModel/CKFFNativePositionTJit.*`, `../CKFFPLib/ShaderModel/CKFFNative3dJit.*` |
| Runtime admission, jobs, fallbacks, manifests and statistics | `../CKSdlGpuRasterizer/Rasterizer/CKSdlGpuRasterizerFFJit.cpp` |

CKShaderJit currently emits DXBC on the D3D12 path supported by SDL GPU and
SPIR-V on Vulkan. It has no DXIL, GLSL or Metal emitter and is not integrated
into the bgfx runtime. The precompiled fixed-function shaders remain required:
they provide immediate and failure-safe rendering while asynchronous shader or
pipeline work is pending, unavailable or rejected.

## IR contract

The IR currently has 66 operations. It supports scalar and two-to-four-component
boolean, signed integer and binary32 values; pure arithmetic and comparisons;
component routing; structured `if`/`else` regions with phi results; bounded
loops with carried values; derivatives; texture sampling, explicit LOD and
gradients; dimensions and mip counts; and depth comparison sampling.

Finished programs are dependency-ordered, reachable from their outputs and
verified before either backend emits them. The verifier checks types, resource
bounds, sampler dimensions, input/output linkage, region dominance, loop
structure and derivative use. Quad operations are restricted to fragment
programs and uniform control flow. Loops require a static upper bound, so a
runtime value cannot create unbounded shader work.

The IR deliberately does not promise bit-exact arithmetic across APIs or GPU
vendors. Native multiply-add, signed zero and ordered/unordered comparison
rules are explicit, while transcendental accuracy, denormal handling and some
NaN details remain backend properties. `CKJitOps.def` is the authoritative
operation-level contract.

## Translated fixed-function coverage

| Area | Current coverage |
| --- | --- |
| Fragment pipeline | Texture-stage color/alpha combiners, CURRENT/TEMP flow, saturation, alpha test, pixel fog, specular add, affine/perspective inputs and line coverage |
| Sampling | 2D, cube, volume and comparison resources; native and emulated addressing/filtering; border, mirror-once, LOD bias, minimum mip, mip blending and bounded anisotropy |
| POSITIONT vertices | Projection/affine variants, fog, texture coordinates, clipping and comparison-texture depth-border padding |
| Ordinary 3D vertices | Unlit and 0-8-light programs, materials, attenuation, texgen, texture transforms, vertex/range fog and specular output |
| Deformation | Tweening and matrix blending with sequential or packed palette indices, including lit and prelit paths |
| Clipping | Generated clip-distance variants for POSITIONT, ordinary, tweened and matrix-blended draws |

Program specialization uses the canonical fragment key plus sampler layout.
Numeric material, light, fog, transform, sampler and texture metadata remain
runtime inputs where they do not change program structure. Pipeline state such
as framebuffer blending, depth/stencil, culling, target formats and sample count
is cached separately. The exact dependency table is in
[`CKFFNativeFragmentDependencies.md`](../CKFFPLib/ShaderModel/CKFFNativeFragmentDependencies.md).

## Runtime bounds and recovery

The runtime keeps at most 256 canonical programs, 1,024 remembered draw keys
and 64 outstanding shader jobs. Pipeline creation has a separate bounded queue.
Admission uses frequency and recency, cold one-off keys remain on the fallback,
and evicted entries release their shader/cache ownership safely. Manifests store
program and pipeline identities rather than generated bytecode; compatible hot
entries are recompiled and prewarmed on a later run.

Every failure path continues with a precompiled program. Runtime counters expose
admission, compilation, pipeline creation, fallback and generated vertex-class
selection. The scene and benchmark gates use these counters to distinguish an
image that happened to match from an image that was actually drawn by generated
shaders.

## Current validation baseline

The current 2026-10-03 baseline on an RTX 4090 is:

- all 32 procedural CK2 scenes pass in disabled, fragment-only and full JIT
  modes on D3D12 and Vulkan: 192/192 processes and 656/656 full-image
  comparisons, maximum RGB difference 1/255 at a 2/255 threshold;
- generated POSITIONT, unlit, lit, tween, matrix-blend, clip and depth-pad paths
  have component or scene execution gates; shader and pipeline failure counters
  are zero in the full collection;
- the CPU IR/reference suites, DXBC and SPIR-V validation, and complete D3D12
  and Vulkan GPU pixel suites pass for the latest cube-filtering change;
- the strict original-DX8 collection passes 17 of 25 supported scenes in every
  modern mode/backend. It remains a failing compatibility gate: 186 of 774
  images exceed the same 2/255 threshold across eight known scenes;
- an earlier frozen Vulkan/full-JIT Player session received manual visual
  acceptance. The later cube-filtering build has full automated scene coverage,
  but no separate latest-build gameplay session is claimed.

See [`FFJitReplay.md`](../../../tests/FFJitReplay.md) for component coverage,
[`FFJitScenes.md`](../../../tests/FFJitScenes.md) for CK2 scenes and manual-play
evidence, [`FFJitDx8Oracle.md`](../../../tests/FFJitDx8Oracle.md) for the original
DX8 comparison, and [`FFJitBenchmark.md`](../../../tests/FFJitBenchmark.md) for
the timing methodology and measured-interval gate.

## Remaining work

The open items are compatibility, performance and hardware-coverage questions;
the complete same-engine matrix does not currently expose a JIT translation
failure.

| Priority | Item | Required outcome |
| --- | --- | --- |
| 1 | Decide 2D render-target visibility semantics | Choose immediate visibility or the original DX8 one-render delay, then encode that policy in a renderer-level regression. This belongs to render-target management rather than shader IR. |
| 1 | Decide `CopyToVideo` compatibility | Keep the working modern copy or emulate the original no-paste result for this callback sequence. Preserve the existing exact source-to-destination validation either way. |
| 2 | Resolve exact cube boundary and filter residuals | Explain and, if compatible with the selected policy, remove the remaining one-to-sixteen-pixel face/texel differences without masks, epsilon guesses or relaxed thresholds. |
| 2 | Close the spotlight reference split | Select the original hardware result or the bounded software/reference formula as the contract. The mismatch also occurs with JIT disabled, so it is not evidence of an IR translation defect. |
| 3 | Measure the new cube sampling path | Add a focused benchmark for multi-mip, mixed min/mag and worst-case anisotropic cube filtering. The existing composite benchmark predates this path and cannot quantify its cost. |
| 3 | Broaden device coverage | Run the complete scene and GPU suites on at least one non-NVIDIA device. Current evidence covers two APIs on one RTX 4090. |
| 3 | Refresh manual acceptance | Use the prepared manual launcher for the latest runtime on Vulkan and D3D12; cover menu/UI, ordinary gameplay, pause/resume and a transition. No automated playthrough is required. |
| 4 | Generalize replay capture if needed | The replay suite uses named versioned fixtures. Add an arbitrary captured-draw loader only when a real regression needs that feedback loop. |

The first two items require a compatibility decision before implementation.
Matching an original quirk and preserving the current useful behavior are both
coherent choices, but tests and documentation must state which contract wins.
The cube and spotlight rows should be diagnosed against the strict original
reference; they must not be hidden by looser scene tolerances.

## Change checklist

When adding or changing an IR operation:

1. update `CKJitOps.def`, builder type/folding rules and the verifier together;
2. implement and test both DXBC and SPIR-V emission;
3. add malformed-program rejection cases as well as successful emission cases;
4. add a CPU semantic/reference case at the fixed-function call site;
5. run native validators and both GPU pixel suites.

When changing fixed-function specialization:

1. update the canonical-key/runtime dependency contract;
2. prove inactive state is removed and dynamic state stays out of the key;
3. add cold, warm and A-B-A replay coverage where cache identity can change;
4. exercise the generated path in a normal CK2 2D or 3D scene;
5. compare with original DX8 when the behavior is intended to be legacy-exact.

Do not accept image parity alone as generated-code coverage. Enabled runs must
select ready JIT pipelines, full mode must execute the expected generated vertex
class, and all compile/pipeline failure counters must remain zero.
