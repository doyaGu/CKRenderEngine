#ifndef CKRE_FFP_RECORDING_HARNESS_H
#define CKRE_FFP_RECORDING_HARNESS_H

// Recording backend for the fixed-function pipeline and translation core
// tests: the NULL backend plus a log of everything the pipeline sends down
// (passes, draws with their state / textures / constants, presents, resource
// traffic) and a few failure knobs.

#include "CKNullBackend.h"
#include "CKTranslatedRasterizer.h"
#include "TestTriangleMultiset.h"

#include <string.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct FFPTextureBinding {
    CKDWORD Stage;        // backend slot
    CKDWORD Uniform;      // pseudo sampler uniform of the slot
    CKDWORD Texture;
    CKSamplerDesc Sampler;
};

struct FFPPassClearRecord {
    CKDWORD Pass;         // index into the frame's passes
    CKDWORD Flags;
    CKDWORD Color;
    float Z;
    CKDWORD Stencil;
    CKRECT Rect;          // pass rect
};

// One Draw() on the backend.
struct FFPDrawRecord {
    CKDWORD Pass;         // index into the frame's passes
    CKDWORD Program;
    CKDWORD Flags;
    XString Marker;       // marker set before the draw, consumed
    CKDWORD Target;       // colour texture of the pass's render target (0 = swap chain)
    CKRECT Rect;          // pass rect
};

struct FFPPassState {
    CKRECT Rect;
    CKDWORD FrameBuffer;  // render target of the pass
    FFPPassState() : FrameBuffer(0) { memset(&Rect, 0, sizeof(Rect)); }
};

// The draw-level log: one record per draw plus the running counters of
// everything the pipeline pushed down with it.
struct FFPBackendLog {
    CKDrawState LastState = {};
    CKDWORD StateSetCount = 0;
    CKDWORD LastProgram = 0;
    CKDWORD DrawCount = 0;
    CKDWORD DiscardCount = 0;          // draws / uploads the backend refused
    CKDWORD LastDiscardFlags = 0;
    CKDWORD PassCount = 0;             // passes begun
    CKDWORD LastPass = 0;
    CKDWORD TextureBindCount = 0;
    CKDWORD UniformSetCount = 0;
    CKDWORD MatrixUniformSetCount = 0;
    CKDWORD StencilRefSetCount = 0;
    CKDWORD StencilMaskSetCount = 0;
    CKDWORD LastStencilRef = 0;
    CKDWORD LastStencilReadMask = 0;
    CKDWORD LastStencilWriteMask = 0;
    CKDWORD LastTextureStage = 0;
    CKDWORD LastTextureUniform = 0;
    CKDWORD LastTextureHandle = 0;
    CKSamplerDesc LastTextureSampler = {};
    float LastPointSize = 0.0f;
    CKDWORD PointSizeSetCount = 0;
    CKBOOL ScissorEnabled = FALSE;
    CKRECT LastScissor = {0, 0, 0, 0};
    CKDWORD ScissorSetCount = 0;
    CKDWORD VertexBufferSetCount = 0;
    CKDWORD IndexBufferSetCount = 0;
    CKDWORD DrawFlags[32] = {};
    CKDWORD DrawPasses[32] = {};
    std::vector<FFPDrawRecord> Draws;
    std::vector<CKDWORD> PassOrder;
    XString LastMarker;
    CKDWORD VertexBufferOrder[32] = {};
    CKDWORD IndexBufferOrder[32] = {};
    // Failure knobs: the next Draw / PushConstants fails with this status.
    CKERROR StateError = CK_OK;
    CKERROR UniformError = CK_OK;
    CKERROR DrawError = CK_OK;
    std::vector<FFPTextureBinding> TextureBindings;
    std::vector<CKBYTE> LastVertexBytes;   // transient geometry of the last draw
    std::vector<CKBYTE> LastIndexBytes;
    std::unordered_map<CKDWORD, std::vector<float> > FloatUniforms;   // pseudo block uniform -> floats of the last push
    std::unordered_map<CKDWORD, CKDWORD> UniformCounts;               // pseudo block uniform -> element count
};

class FFPRecordingBackend;

// Backend driver of the recording backends: the shader profile and the
// framebuffer conventions the backends report.
class FFPRecordingDriver : public CKNullBackendDriver {
public:
    explicit FFPRecordingDriver(CK_SHADER_PROFILE profile = CKRST_SHADER_PROFILE_DX11, CKDWORD flags = 0)
    {
        Profile = profile;
        HomogeneousDepth = (flags & CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE) ? TRUE : FALSE;
        OriginBottomLeft = (flags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) ? TRUE : FALSE;
        m_Desc = "Recording backend";
        m_3DCaps.MaxNumberTextureStage = 8;
        m_3DCaps.MaxTextureWidth = 4096;
        m_3DCaps.MaxTextureHeight = 4096;
    }

protected:
    CKNullBackend *NewBackend() override;
};

class FFPRecordingBackend : public CKNullBackend {
public:
    explicit FFPRecordingBackend(CKNullBackendDriver *driver) : CKNullBackend(driver) {}

    // --- Knobs
    CKBOOL FailCreateProgram = FALSE;
    CKBOOL FailCreateTexture = FALSE;
    CKBOOL FailUpdateTexture = FALSE;
    CKERROR FrameResult = CK_OK;
    CKERROR DeviceStatus = CK_OK;
    CKDWORD TransientVertexCapacity = 0xFFFFFFFFu;
    CKDWORD TransientIndexCapacity = 0xFFFFFFFFu;
    CKDWORD Width = 64;      // size StartedBackend() initialises with
    CKDWORD Height = 64;

    // --- Log
    FFPBackendLog Log;
    CKDWORD TransientVertexAllocations = 0;   // successful allocations
    CKDWORD TransientIndexAllocations = 0;
    CKDWORD ReadTextureCount = 0;
    CKDWORD CreatedShaderCount = 0;
    CKDWORD CreatedProgramCount = 0;
    CKDWORD CreatedTextureCount = 0;
    CKDWORD UpdatedTextureCount = 0;
    CKDWORD DeletedObjectCount = 0;
    CKDWORD PaletteSetCount = 0;
    CKDWORD FrameSerial = 0;
    CKDWORD PaletteColors[16] = {};
    CKDWORD LastCreatedTexture = 0;
    CKDWORD LastUpdatedTexture = 0;
    CKDWORD LastUpdateMip = 0;
    CKDWORD LastUpdateFace = 0;
    CKDWORD LastDeletedObject = 0;
    CKDWORD LastDeletedObjectType = 0;
    CKTextureDesc LastTextureDesc = {};
    VxImageDescEx LastTextureUpdateDesc = {};
    CKRECT LastTextureUpdateRegion = {};
    CKBOOL LastTextureUpdateHadRegion = FALSE;
    const void *LastVertexShaderCode = nullptr;
    CKDWORD LastVertexShaderCodeSize = 0;
    const void *LastPixelShaderCode = nullptr;
    CKDWORD LastPixelShaderCodeSize = 0;
    std::vector<CKVertexElementDesc> LastVertexLayoutElements;
    std::vector<FFPPassClearRecord> PassClears;
    std::unordered_map<CKDWORD, FFPPassState> PassStates;   // pass index -> its rect and target
    std::unordered_map<CKDWORD, CKDWORD> FrameBufferColorTexture;   // render target -> colour texture
    std::vector<CKBackendPresentMode> Frames;
    std::unordered_set<CKDWORD> LiveHandles;   // every allocated, not yet destroyed handle

    // Colour texture (0 = swap chain) a pass draws into.
    CKDWORD PassTarget(CKDWORD pass) const {
        std::unordered_map<CKDWORD, FFPPassState>::const_iterator it = PassStates.find(pass);
        if (it == PassStates.end() || it->second.FrameBuffer == 0)
            return 0;
        std::unordered_map<CKDWORD, CKDWORD>::const_iterator fb = FrameBufferColorTexture.find(it->second.FrameBuffer);
        return fb == FrameBufferColorTexture.end() ? 0 : fb->second;
    }

    // Backend the fixed-function unit tests drive the pipeline through:
    // initialised on first use with Width x Height and one open pass that
    // stays open for the whole test (IsIdle stays TRUE so the pipeline can
    // shut down at the end).
    CKRasterizerBackend *StartedBackend() {
        if (!m_Initialized) {
            CKBackendInitDesc init;
            init.Width = (int)Width;
            init.Height = (int)Height;
            init.Bpp = 32;
            init.ZBpp = 24;
            init.StencilBpp = 8;
            TestCheck(Init(&init) == CK_OK, "recording backend must initialise");
            CKBackendPassDesc pass;
            pass.Rect.right = (int)Width;
            pass.Rect.bottom = (int)Height;
            TestCheck(BeginPass(&pass) == CK_OK, "recording backend must open its pass");
        }
        return this;
    }
    CKBOOL IsIdle() const override { return TRUE; }

    // --- Recording overrides
    CKERROR GetDeviceStatus() const override {
        const CKERROR base = CKNullBackend::GetDeviceStatus();
        return base != CK_OK ? base : DeviceStatus;
    }
    CKERROR CreateTexture(const CKTextureDesc *desc, const VxImageDescEx *data, CKDWORD *out) override {
        ++CreatedTextureCount;
        if (desc)
            LastTextureDesc = *desc;
        if (FailCreateTexture) {
            if (out) *out = 0;
            return CKERR_INVALIDPARAMETER;
        }
        const CKERROR result = CKNullBackend::CreateTexture(desc, data, out);
        if (result == CK_OK) {
            LastCreatedTexture = *out;
            LiveHandles.insert(*out);
        }
        return result;
    }
    CKERROR UpdateTexture(CKDWORD texture, CKDWORD mip, CKDWORD face, const CKRECT *region,
                          const VxImageDescEx *desc) override {
        ++UpdatedTextureCount;
        LastUpdatedTexture = texture;
        LastUpdateMip = mip;
        LastUpdateFace = face;
        LastTextureUpdateHadRegion = region != nullptr;
        if (region)
            LastTextureUpdateRegion = *region;
        if (desc)
            LastTextureUpdateDesc = *desc;
        if (FailUpdateTexture)
            return CKERR_INVALIDPARAMETER;
        return CKNullBackend::UpdateTexture(texture, mip, face, region, desc);
    }
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKNullBackend::CreateDepthTexture(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKNullBackend::CreateRenderTarget(desc, out);
        if (result == CK_OK) {
            LiveHandles.insert(*out);
            FrameBufferColorTexture[*out] = desc->ColorTexture;
        }
        return result;
    }
    CKERROR CreateBuffer(const CKBackendBufferDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKNullBackend::CreateBuffer(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *desc, CKDWORD *out) override {
        LastVertexLayoutElements.clear();
        if (desc) {
            for (CKDWORD i = 0; i < desc->ElementCount; ++i)
                LastVertexLayoutElements.push_back(desc->Elements[i]);
        }
        const CKERROR result = CKNullBackend::CreateVertexLayout(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateShader(const CKShaderDesc *desc, CKDWORD *out) override {
        ++CreatedShaderCount;
        if (desc && desc->Stage == CKRST_SHADER_VERTEX) {
            LastVertexShaderCode = desc->Code;
            LastVertexShaderCodeSize = desc->CodeSize;
        }
        if (desc && desc->Stage == CKRST_SHADER_PIXEL) {
            LastPixelShaderCode = desc->Code;
            LastPixelShaderCodeSize = desc->CodeSize;
        }
        const CKERROR result = CKNullBackend::CreateShader(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateProgram(CKDWORD vs, CKDWORD ps, CKDWORD *out) override {
        if (FailCreateProgram) {
            if (out) *out = 0;
            return CKERR_INVALIDPARAMETER;
        }
        const CKERROR result = CKNullBackend::CreateProgram(vs, ps, out);
        if (result == CK_OK) {
            ++CreatedProgramCount;
            LiveHandles.insert(*out);
        }
        return result;
    }
    CKERROR DestroyObject(CKDWORD object, CKDWORD type) override {
        ++DeletedObjectCount;
        LastDeletedObject = object;
        LastDeletedObjectType = type;
        const CKERROR result = CKNullBackend::DestroyObject(object, type);
        if (result == CK_OK)
            LiveHandles.erase(object);
        return result;
    }
    CKERROR BeginPass(const CKBackendPassDesc *desc) override {
        const CKERROR result = CKNullBackend::BeginPass(desc);
        if (result != CK_OK)
            return result;
        const CKDWORD pass = GetCurrentPass();
        PassStates[pass].Rect = desc->Rect;
        PassStates[pass].FrameBuffer = desc->RenderTarget;
        FFPPassClearRecord record = { pass, GetPasses().back().ClearFlags, desc->ClearColor, desc->ClearZ,
                                      desc->ClearStencil, desc->Rect };
        PassClears.push_back(record);
        Log.PassOrder.push_back(pass);
        Log.LastPass = pass;
        ++Log.PassCount;
        return CK_OK;
    }
    CKERROR PushConstants(CKBackendConstantBlock block, const void *data, CKDWORD vec4Count) override {
        if (!data || vec4Count == 0)
            return CKERR_INVALIDPARAMETER;
        ++Log.UniformSetCount;
        if (Log.UniformError != CK_OK) {
            ++Log.DiscardCount;
            Log.LastDiscardFlags = CKRST_DISCARD_ALL;
            return Log.UniformError;
        }
        const CKERROR result = CKNullBackend::PushConstants(block, data, vec4Count);
        if (result != CK_OK) {
            ++Log.DiscardCount;
            Log.LastDiscardFlags = CKRST_DISCARD_ALL;
            return result;
        }
        const CKDWORD uniform = GetBlockUniformForTests(block);
        const CKBackendConstantBlockDesc &info = CKBackendConstantBlockInfo(block);
        if (info.Mat4)
            ++Log.MatrixUniformSetCount;
        const float *values = static_cast<const float *>(data);
        Log.FloatUniforms[uniform].assign(values, values + vec4Count * 4);
        Log.UniformCounts[uniform] = info.Mat4 ? vec4Count / 4 : vec4Count;
        return CK_OK;
    }
    void SetMarker(const char *name) override {
        CKNullBackend::SetMarker(name);
        Log.LastMarker = name ? name : "";
    }
    CKBOOL AllocTransientVertices(CKDWORD count, CKDWORD layout, CKBackendTransientVertices *out) override {
        if (count > TransientVertexCapacity)
            return FALSE;
        if (!CKNullBackend::AllocTransientVertices(count, layout, out))
            return FALSE;
        ++TransientVertexAllocations;
        return TRUE;
    }
    CKBOOL AllocTransientIndices(CKDWORD count, CKBOOL index32, CKBackendTransientIndices *out) override {
        if (count > TransientIndexCapacity)
            return FALSE;
        if (!CKNullBackend::AllocTransientIndices(count, index32, out))
            return FALSE;
        ++TransientIndexAllocations;
        return TRUE;
    }
    CKERROR Draw(const CKBackendDraw *draw) override;
    CKERROR Present(CKBackendPresentMode mode, CKDWORD *frameNumber) override {
        const CKERROR result = CKNullBackend::Present(mode, frameNumber);
        if (result != CK_OK)
            return result;
        ++FrameSerial;
        Frames.push_back(mode);
        return FrameResult;
    }
    CKERROR ReadTexture(CKDWORD texture, CKDWORD mip, CKReadbackDesc *desc, CKDWORD *available) override {
        const CKERROR result = CKNullBackend::ReadTexture(texture, mip, desc, available);
        if (result == CK_OK)
            ++ReadTextureCount;
        return result;
    }
    CKERROR SetPaletteColor(CKDWORD index, CKDWORD color) override {
        if (index < 16)
            PaletteColors[index] = color;
        ++PaletteSetCount;
        return CKNullBackend::SetPaletteColor(index, color);
    }
};

inline CKNullBackend *FFPRecordingDriver::NewBackend()
{
    return new FFPRecordingBackend(this);
}

// Records the sticky state and the slot bindings the draw carries, then the
// draw itself; the failure knobs refuse the draw before (StateError) or after
// (DrawError) recording it.
inline CKERROR FFPRecordingBackend::Draw(const CKBackendDraw *draw)
{
    if (!draw || !draw->Program)
        return CKERR_INVALIDPARAMETER;
    if (!IsPassOpen())
        return CKERR_INVALIDOPERATION;
    if (Log.StateError != CK_OK) {
        ++Log.DiscardCount;
        Log.LastDiscardFlags = CKRST_DISCARD_ALL;
        return Log.StateError;
    }
    const CKBackendPipelineState &state = GetPipelineState();
    Log.LastState = state.State;
    ++Log.StateSetCount;
    Log.LastStencilRef = state.StencilRef & 0xFF;
    Log.LastStencilReadMask = state.StencilReadMask & 0xFF;
    Log.LastStencilWriteMask = state.StencilWriteMask & 0xFF;
    ++Log.StencilRefSetCount;
    ++Log.StencilMaskSetCount;
    Log.ScissorEnabled = state.ScissorEnabled;
    if (state.ScissorEnabled)
        Log.LastScissor = state.Scissor;
    ++Log.ScissorSetCount;
    Log.LastPointSize = state.PointSize;
    ++Log.PointSizeSetCount;
    for (CKDWORD slot = 0; slot < CKRST_BACKEND_SLOT_COUNT; ++slot) {
        const CKDWORD texture = GetBoundTexture(slot);
        if (!texture)
            continue;
        FFPTextureBinding binding;
        binding.Stage = slot;
        binding.Uniform = GetSamplerUniformForTests(slot);
        binding.Texture = texture;
        binding.Sampler = m_Samplers[slot];
        Log.TextureBindings.push_back(binding);
        Log.LastTextureStage = slot;
        Log.LastTextureUniform = binding.Uniform;
        Log.LastTextureHandle = texture;
        Log.LastTextureSampler = binding.Sampler;
        ++Log.TextureBindCount;
    }
    if (draw->VertexBuffer) {
        if (Log.VertexBufferSetCount < 32)
            Log.VertexBufferOrder[Log.VertexBufferSetCount] = draw->VertexBuffer;
        ++Log.VertexBufferSetCount;
    }
    if (draw->IndexBuffer) {
        if (Log.IndexBufferSetCount < 32)
            Log.IndexBufferOrder[Log.IndexBufferSetCount] = draw->IndexBuffer;
        ++Log.IndexBufferSetCount;
    }

    const CKDWORD pass = GetCurrentPass();
    const CKERROR result = CKNullBackend::Draw(draw);
    if (result != CK_OK) {
        ++Log.DiscardCount;
        Log.LastDiscardFlags = CKRST_DISCARD_ALL;
        return result;
    }
    Log.LastVertexBytes.clear();
    Log.LastIndexBytes.clear();
    if (draw->TransientVertices && draw->TransientVertices->Data) {
        const CKBYTE *begin = static_cast<const CKBYTE *>(draw->TransientVertices->Data);
        Log.LastVertexBytes.assign(begin, begin + (size_t)draw->TransientVertices->Count * draw->TransientVertices->Stride);
    }
    if (draw->TransientIndices && draw->TransientIndices->Data) {
        const CKBYTE *begin = static_cast<const CKBYTE *>(draw->TransientIndices->Data);
        Log.LastIndexBytes.assign(begin, begin + (size_t)draw->TransientIndices->Count * (draw->TransientIndices->Index32 ? 4 : 2));
    }
    if (Log.DrawCount < 32) {
        Log.DrawPasses[Log.DrawCount] = pass;
        Log.DrawFlags[Log.DrawCount] = CKRST_DISCARD_ALL;
    }
    FFPDrawRecord record;
    record.Pass = pass;
    record.Program = draw->Program;
    record.Flags = CKRST_DISCARD_ALL;
    record.Marker = Log.LastMarker;
    record.Target = PassTarget(pass);
    memset(&record.Rect, 0, sizeof(record.Rect));
    std::unordered_map<CKDWORD, FFPPassState>::const_iterator it = PassStates.find(pass);
    if (it != PassStates.end())
        record.Rect = it->second.Rect;
    Log.LastMarker = "";
    Log.Draws.push_back(record);
    Log.LastProgram = draw->Program;
    ++Log.DrawCount;
    if (Log.DrawError != CK_OK) {
        ++Log.DiscardCount;
        Log.LastDiscardFlags = CKRST_DISCARD_ALL;
        return Log.DrawError;
    }
    return CK_OK;
}

// ===========================================================================
// Recording backend behind the v3 translation core
// ===========================================================================

class FFPRecordingLibrary : public CKNullBackendLibrary {
protected:
    CKNullBackendDriver *NewDriver() override { return new FFPRecordingDriver(); }
};

// A started translated rasterizer over the recording backend. Add texture
// formats to BackendDriver() before CreateContext(); the translated driver
// syncs them when the context is created.
struct FFPTranslatedWorld {
    CKRasterizer *Rasterizer;
    CKTranslatedDriver *Driver;
    CKTranslatedContext *Context;
    FFPRecordingBackend *Backend;

    FFPTranslatedWorld() : Rasterizer(NULL), Driver(NULL), Context(NULL), Backend(NULL) {
        FFPRecordingLibrary *library = new FFPRecordingLibrary();
        library->Start(NULL);
        Rasterizer = CKTranslatedRasterizerStart(library, NULL);
        TestCheck(Rasterizer != NULL && Rasterizer->GetDriverCount() == 1, "translated rasterizer over the recording backend");
        Driver = Rasterizer ? static_cast<CKTranslatedDriver *>(Rasterizer->GetDriver(0)) : NULL;
    }

    ~FFPTranslatedWorld() {
        if (Rasterizer)
            CKTranslatedRasterizerClose(Rasterizer);
    }

    FFPRecordingDriver *BackendDriver() const {
        return Driver ? static_cast<FFPRecordingDriver *>(Driver->GetBackendDriver()) : NULL;
    }

    CKBOOL CreateContext(int width, int height) {
        if (!Driver)
            return FALSE;
        Context = static_cast<CKTranslatedContext *>(Driver->CreateContext());
        if (!Context)
            return FALSE;
        if (!Context->Create(NULL, 0, 0, width, height, 32, FALSE, 60, 24, 8))
            return FALSE;
        Backend = static_cast<FFPRecordingBackend *>(Context->GetBackend());
        return Backend != NULL;
    }
};

#endif // CKRE_FFP_RECORDING_HARNESS_H
