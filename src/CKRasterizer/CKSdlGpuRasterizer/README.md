# SDL_gpu rasterizer

`CKSdlGpuRasterizer` and `CKSdlGpuRasterizerStatic` are the complete SDL_gpu
rasterizer targets. They contain the concrete driver, SDL resources, native pipelines,
command encoding, fences, presentation, and the native shader catalog. Both
use `CKFFPLib` for fixed-function state translation, geometry preparation, and
scene composition; there is no separately packaged SDL backend target.

The SDL_gpu rasterizer advertises complete format/profile targets before device creation,
then supplies the matching catalog after initialization. Its native device
consumes explicit shader and program descriptors from the compatibility layer.

The Player owns its `SDL_Window` and controls fullscreen and window events.
The backend claims that window on the main thread and releases it during
shutdown. Presentation acquires the actual drawable extent each frame.
Scene rendering always uses an internal sampleable target, followed by
resolve/postprocessing, window-size composition and the swapchain blit.
An unavailable swapchain during minimization does not discard resource work.

Draw packets retain their constants, geometry and bindings. Updates, copies,
clears and target changes flush the pending packet batch before encoding the
next operation. Each native render pass binds its complete required state.
Buffers retain a CPU shadow and upload complete versions when cycled.
Texture version changes copy all defined mip/face/slice subresources before
applying patches. MSAA attachments retain their multisample contents across
pass boundaries and keep a separate sampleable resolve texture.

Uploads generate automatic mips from native-format CPU shadows, one cube
face or complete volume at a time. Signed bump components are filtered as
signed values. BC1/2/3 textures requesting automatic mips are decoded into
BGRA8 storage, retaining alpha and allowing individual texel patches; explicit
compressed mip chains retain native BC storage. A GPU copy or RTT write
switches the affected face or volume to GPU mip generation; later CPU patches
therefore cannot resurrect stale CPU pixels. Volume mips use an offline shader
to average all three axes, including trailing texels of odd dimensions.
Finite explicit mip counts beyond the texture extent are
invalid, rather than alternate spellings of `CKRST_MIPMAP_GENERATE`.

Volume attachments use a 2D slice target, loading the existing slice before
continued draws and copying it back before subsequent commands. This also
avoids SDL 3.4.8 D3D12's creation of out-of-range RTV slices in smaller volume
mips. Mip generation snapshots the previous level into a separate sampled
volume and renders each output slice into a 2D target. All copies and draws
remain in the ordered GPU command stream; no CPU readback or idle wait is used.

Transient geometry reuses capacity-bucketed GPU/transfer buffer pairs. Each
batch retains its pair through submission; only a completed submission fence
returns it to the free pool. Pending batches never share upload storage, even
within one submission. Selection uses buffer usage and smallest sufficient
capacity. The idle pool retains at most 16 MiB of combined GPU/transfer storage.
Draw validates transient allocations and snapshots their requested ranges
directly into CPU batch arenas. Packets store offsets, so arena growth and
later caller writes cannot change earlier draws. CPU allocation storage is
reused by ordinal after Submit, with a separate 16 MiB retention budget;
submission invalidates all allocation tokens even if their addresses recur.
Partial persistent resource updates keep their existing preservation path.

Readback tickets own transfer storage until their submission fence completes,
including after cancellation. Resource destruction is deferred by SDL until
queued GPU references complete. Normal frames bound the number of in-flight
submissions with fences; shutdown waits for the device before releasing it.
Resource-table generations survive device reinitialization so old handles
cannot identify newly created resources.

The shader directory contains complete offline DXIL, SPIR-V and MSL FFP
families owned by the rasterizer composition, plus independent native clear
and volume helper programs. Program declarations provide uniform packing,
sampler slots and vertex defaults; the backend does not infer FFP roles from
those resources. Build-time validation checks source/ABI hashes, entry points
and reflected bindings. Runtime device selection intersects complete families
with driver support; it never invokes a shader compiler. Windows defaults to
D3D12 and Apple platforms to Metal, whose MSL SPIRV-Cross translates from the
SPIR-V; the FF JIT emits no MSL and is off there. `CKRE_SDL_GPU_DRIVER=vulkan`
explicitly selects Vulkan. SDL-only builds do not build or stage bgfx shader
containers.

Run the native and public-interface tests on the interactive desktop with
`--visible`, `CKRE_GPU_TEST_INTERACTIVE_START=1`, and `CKRE_GPU_TEST_HOLD=1`.
Bring the test window to the foreground and press Enter, inspect and record
the final window, then close it normally. Run GPU tests serially. A skipped
CTest gate is not a GPU pass. See the [reference index](../../../tests/reference/README.md)
for original-DX8 comparisons and dated acceptance records.
