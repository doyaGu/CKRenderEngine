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
shutdown. Default presentation acquires the actual drawable extent each frame.
Scene rendering uses the native target directly when no scaling, multisampling
or postprocessing is requested; the other modes retain their necessary
intermediate passes. An unavailable swapchain during minimization does not
discard resource work.

The module root holds its external declarations: static registration and SDL
texture import data in `CKSdlGpuRasterizer.h`, and diagnostics in
`CKSdlGpuFFJitSnapshot.h`. Internal headers stay beside their implementations
in `Native/` and `Rasterizer/`. Only those two external headers
are installed; white-box tests and benchmarks opt into the internal include
paths. `shaders/generated/` remains owned by shader generation.

External presentation uses the same texture resource and target model as every
other render target. A host may offer its SDL GPU device through
`CKSDL_GPU_DEVICE_PROPERTY` before Context creation, create a regular
`COLOR_TARGET | SAMPLER` SDL texture, import it as a normal generational CK
texture handle, and bind that handle with `SetBackBufferTexture`. The import is
borrowed: deleting the handle or destroying the Context never releases the SDL
texture. A bound handle cannot be deleted, and resize removes the binding while
leaving the handle alive.

The bound texture is the virtual backbuffer for `SetTargetTexture(0, ...)`,
readback, scene composition and overlays. Temporary RTTs do not change the
binding. `BackToFront` submits the completed custom target without acquiring the
hidden window swapchain or encoding a final present blit; window VSync is
therefore irrelevant in this mode. Unsupported rasterizers simply reject import
or binding, so callers keep the generic `CopyToMemoryBuffer` path.

SDL texture cycling supplies in-flight storage without changing the logical
`SDL_GPUTexture *`. A complete pass whose load operation discards previous
contents cycles the attachment. A partial update of a referenced imported
texture first preserves its defined subresources through a temporary GPU copy,
then cycles the same external logical texture; it never performs a CPU readback
or substitutes another pointer. Submission order on the shared device orders
the rasterizer producer before the host consumer.

The virtual backbuffer is top-left and opaque. Its first pass initializes alpha
to one, clears force alpha to one, ordinary scene and overlay draws mask alpha
writes, and resolve/dither output writes RGB only. These rules are applied in
existing passes; there is no alpha-fix pass.

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
explicitly selects Vulkan.

Run the native and public-interface tests on the interactive desktop with
`--visible`, `CKRE_GPU_TEST_INTERACTIVE_START=1`, and `CKRE_GPU_TEST_HOLD=1`.
Bring the test window to the foreground and press Enter, inspect and record
the final window, then close it normally. Run GPU tests serially. A skipped
CTest gate is not a GPU pass. See the [reference index](../../../tests/reference/README.md)
for original-DX8 comparisons and dated acceptance records.
