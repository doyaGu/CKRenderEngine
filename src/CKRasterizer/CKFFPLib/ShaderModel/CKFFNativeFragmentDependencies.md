# Native fixed-function fragment JIT state dependencies

The runtime key consists of the canonical `CKFFNativeFragmentKey` **and sampler
layout**. `CKFFNativeFragmentDrawKey` extracts the fragment program and switches
from resolved draw inputs; `CKFFCanonicalizeNativeFragmentKey` removes inputs
the selected program cannot observe. A raw draw key may change while its
canonical key remains equal. Vertex program selection and SDL pipeline state
are separate dependencies.

One state can affect several domains. Uniform blocks also contain copies of
specialized state for precompiled shaders. A changed uniform block alone does
not imply that JIT code reads the changed component, or that a shader or PSO
must be rebuilt.

## Dependency contract

| State change | Canonical fragment key | Runtime value or other dependency |
| --- | --- | --- |
| Active color/alpha operation, arguments, CURRENT/TEMP destination | Relevant operation and argument fields | Each stage saturates before its output is used by the next stage |
| Stage constant or texture factor value | Unchanged | Uniform value; only live operands affect pixels |
| Alpha-test enable/function | Relevant enabled comparison | Disabled tests and ALWAYS discard no fragments; their irrelevant function is removed |
| Alpha reference | Unchanged | Uniform reference; alpha is rounded to UNORM8 before comparison |
| Fog enable/pixel mode | Relevant fragment fog fields | Fog bounds, density and color remain uniforms; vertex fog/range fog belong to the vertex path |
| Flat shading or global specular add | Fragment switches | Vertex color generation is still the vertex shader's responsibility |
| Texture binding presence | Switch if the stage's texture is read | Texture contents/handle are resources, not key values |
| Texture kind, sampler ordinal, sampler layout | Relevant resource interface | Layout is an additional runtime cache key field; not all kinds/ordinals are meaningful in every layout |
| Texture matrix value or coordinate source | Unchanged unless a fragment switch changes | Vertex uniforms/varyings |
| PROJECTED or affine sampling | Switch when the stage samples | Projection division/interpolation behavior changes |
| Bump matrix/luminance scale/offset | Unchanged | Uniform values |
| Bump signed/UNORM encoding | Switch when bump offsets feed a sampled stage | Unused bump encoding is removed |
| LOD bias: zero to nonzero | Switch when the sampling path uses explicit bias | The numeric bias remains a uniform; +0 and -0 share a key |
| LOD bias: one nonzero value to another | Unchanged | Uniform and sampler descriptor update; crossing sign without crossing zero needs no new shader |
| Native filter, wrap/clamp, anisotropy limit | Unchanged while the sampling path is unchanged | Sampler state; shader-emulated sampling also consumes metadata |
| Border color | Unchanged | Shader sampler metadata; not part of SDL's native sampler cache key |
| Border, mirror-once, explicit-gradient, minimum-mip/manual-anisotropy mode | Conditional sampling switches | Canonicalization depends on texture kind and sampling/comparison path; numeric filter/mip/anisotropy values remain runtime data |
| Depth texture comparison | Conditional comparison function/resource count | Hardware comparison uses sampler state; manual comparison specializes the shader's comparison |
| Framebuffer blend, depth test/write, stencil operations/masks, culling | Unchanged for an otherwise unchanged fragment program | Graphics pipeline state; independent from texture-stage STAGEBLEND |
| Target formats, sample count, vertex layout, depth clipping | Separate from fragment key | Graphics pipeline key |
| Stencil reference and scissor rectangle | Unchanged | Dynamic command state, not SDL pipeline key fields |
| Disabled-stage state, unused arguments, disabled alpha/fog parameters | Removed from canonical key | May update an unused shared constant lane; must not change rendered output |

The sampling rules above are conditional, not a promise that every sampler
change requires or avoids specialization. For example, cube sampling removes
mirror-once flags; manual volume anisotropy retains different flags from the
native 2D path. Hardware depth comparison can move a comparison function out of
the shader key into the sampler. The canonicalizer is the executable authority
for these distinctions.

## Executable checks

`tests/test_ff_native_fragment_jit.cpp::TestStateDependencies` specifies 22
constant changes independently of the extractor and checks raw and canonical
key results for all three layouts. It covers affine/line/texture thresholds,
LOD signed zeros, zero/nonzero/sign/magnitude transitions, dynamic values,
inactive stage switches, and incomplete constant rows. Existing randomized
canonical-key tests cover ignored arguments and texture/comparison layouts;
the CPU shader reference remains independent of this dependency table.

`tests/TestFFJitReplay.inl` stores expected effects **relative to frame A** beside
each sequence: key (1), uniform/metadata (2), sampler (4), pipeline (8), pixels
(16). Actual effects come from resolved draw values and GPU readback. Revisions,
native handles and allocations are excluded so independent contexts compare
by value. Shared dynamic constant blocks and sampler metadata are observed;
uniform equality is asserted only for cases that specify it.

| Replay | B's required effects |
| --- | --- |
| `alpha-threshold` | Reference change: uniform + pixels; comparison-function change additionally changes key |
| `stage-saturation`, `texture-factor`, `pixel-fog`, `bump-luminance` | Uniform + pixels; same key, sampler and pipeline |
| `filter-border` | Metadata + pixels; same key, native sampler inputs and pipeline |
| `projected-coordinates` | Key + uniform + pixels |
| `native-filter` | Sampler + pixels; same key and pipeline |
| `output-blend`, `depth-function` | Pipeline + pixels; same fragment key |
| `disabled-alpha`, `inactive-stage` | Same key, sampler, pipeline and pixels; unused uniform changes are permitted |
| `nonzero-lod-bias` | Uniform + sampler + pixels; same key and pipeline |
| `zero-lod-bias` | Key + uniform + sampler + pixels |
| `positiont-border`, `positiont-coordinate-index`, `positiont-pixel-fog` | Uniform/metadata + pixels; same key, sampler and pipeline |
| `positiont-projection`, `positiont-affine` | Key + uniform + pixels |
| `3d-vertex-fog`, `3d-texgen-normal`, `3d-texgen-position`, `3d-texgen-reflection`, `3d-texgen-sphere` | Uniform + pixels; same key, sampler and pipeline |

Every sequence restores A exactly. Consecutive equal keys must queue no new
fragment compilation; when pipeline state is equal too, they must queue no
pipeline creation or synchronous pipeline request. Cold and warm runs compare
full images against a separate JIT-disabled context. These cases use ordinary
color textures, a fixed BGRA8 target, single sampling and one vertex stream;
their sampler/pipeline observations do not claim coverage of all depth-texture,
MSAA or vertex-layout combinations. They establish same-engine parity, not an
independent DX8 conformance result.

Ordinary POSITIONT vertex companions use the same canonical fragment key:
active texture stages and affine interpolation determine their specialization.
RHW, viewport, depth bias, expansion, fog, coordinate indices and projection flags
are vertex inputs/uniforms. Projection can still change the paired fragment key
as described above. The fallback format is fixed per context and selects the
position arithmetic; it is not another per-draw cache dimension. User clip-distance
variants have their own generated companions. Comparison-resource keys additionally
compile ordinary/clipped depth-pad POSITIONT companions. They apply the private
padding bit's draw-local texture matrix to XY before projection and trimming,
preserving the original Z/W. Ordinary POSITIONT bindings continue to ignore the
texture matrix. The existing precompiled binding identifies the padded variant;
no padding dimensions or border-depth values are added to the fragment key.
Only comparison keys need the two extra companions (eight instead of six).
Manifest revision 7 already records depth-pad variants and reconstructs their
new generated vertex bindings during prewarm; it stores no compiled bytecode.

Ordinary lit and unlit 3D companions use the same fragment-key specialization for texture
presence and affine interpolation. Matrix values, texgen, normal normalization,
texture transforms, fog, bias and expansion remain vertex uniforms. Eligibility
(light count -1 for unlit, an integer 0..8 for lit, and ordinary or canonical deformation state) is checked per
draw and is a separate program-binding dimension, not part of the fragment key.
Light count, types, colors, positions, attenuation, material sources and values,
normalization, local-viewer and specular-output flags remain uniforms. Zero lights
with lighting enabled still evaluates material emissive and global ambient.
Tween mode and factor stay uniform: position-only, normal-only and combined
streams reuse the ordinary lit/unlit bindings. Finite factors permit extrapolation;
malformed tween masks/factors retain the precompiled path. Matrix blending also
shares these bindings: four palette matrices, 0–3 explicit weights plus the
remaining weight, and sequential or integer-indexed addressing. Palette contents,
count and indexing state stay uniform; vertex layouts still distinguish pipelines.
Missing palettes or noncanonical matrix state retain the precompiled path.
User-clipped draws select the clipped companion of their vertex class. Plane
values and packed plane count remain uniforms, and sparse masks reuse that binding.
Manifest revision 7 preserves unlit, lit and precompiled bindings for the existing
ordinary or clipped 3D variant. The
ordinary/lit/tween/matrix/ordinary regression requires actual generated lit/unlit selections and warm
reuse without new shader or pipeline work. A stored lit flag cannot coexist with
unlit, POSITIONT, depth padding or a precompiled record.

## Cache policy implications

Admission, frequency tracking and replacement should use the canonical key plus
layout. Counting raw draw keys would treat irrelevant-state churn as new
programs. Uniform-only and sampler-only changes must retain shader/PSO reuse.
Pipeline reuse has a separate key and lifetime, and any replacement policy must
keep resources owned by queued draws alive. The current tests provide these
invariants before changing admission or eviction policy.
