#ifndef CKRE_FFP_RECORDING_HARNESS_H
#define CKRE_FFP_RECORDING_HARNESS_H

// Recording backend for the fixed-function pipeline and translation core
// tests: the NULL backend plus a log of everything the pipeline sends down
// (passes, draws with their state / textures / constants, presents, resource
// traffic) and a few failure knobs.

#include "CKRecordingProvider.h"
#include "CKFFShaderInterface.h"
#include "CKTranslatedRasterizerInternal.h"
#include "TestTriangleMultiset.h"

#include <string.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct CKFFPipelineTestAccess {
    static CKDWORD CachedProgramCount(const CKFixedFunctionPipeline &pipeline)
    {
        return pipeline.m_ShaderCache.CachedProgramCount();
    }
};

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
    CKDWORD DrawErrorAt = 0;          // 0 = every draw, otherwise one-based draw number
    std::vector<FFPTextureBinding> TextureBindings;
    std::vector<CKBYTE> LastVertexBytes;   // transient geometry of the last draw
    std::vector<CKBYTE> LastIndexBytes;
    std::unordered_map<CKDWORD, std::vector<float> > FloatUniforms;   // pseudo block uniform -> floats of the last push
    std::unordered_map<CKDWORD, CKDWORD> UniformCounts;               // pseudo block uniform -> element count
};

class FFPRecordingBackend;

// Backend driver of the recording backends: the shader profile and the
// framebuffer conventions the backends report.
class FFPRecordingDriver : public CKRecordingBackendDriver {
public:
    explicit FFPRecordingDriver(CK_SHADER_PROFILE profile = CKRST_SHADER_PROFILE_DX11,
                                CKDWORD flags = 0,
                                CK_SHADER_FORMAT format = CKRST_SHADER_FORMAT_BGFX)
    {
        Format = format;
        Profile = profile;
        HomogeneousDepth = (flags & CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE) ? TRUE : FALSE;
        OriginBottomLeft = (flags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) ? TRUE : FALSE;
        m_Desc = "Recording backend";
        m_3DCaps.MaxNumberTextureStage = 8;
        m_3DCaps.MaxTextureWidth = 4096;
        m_3DCaps.MaxTextureHeight = 4096;
    }

    void GetShaderTargets(std::vector<CKBackendShaderTarget> &out) const override {
        ++ShaderTargetQueries;
        out = {{CKRST_SHADER_FORMAT_BGFX, Profile}};
    }
    CKBOOL GetShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out) const override {
        ++ShaderCatalogQueries;
        if (FailShaderCatalog) {
            out = CKBackendShaderSet();
            return FALSE;
        }
        if (!CKRecordingShaderSet(caps, out))
            return FALSE;
        // Deliberately keep this fixture's artifact family independent from
        // its reported device format, so mismatch tests fail before creation.
        for (CKShaderDesc &shader : out.Shaders)
            shader.Format = CKRST_SHADER_FORMAT_BGFX;
        return TRUE;
    }

    CKBOOL FailShaderCatalog = FALSE;
    mutable CKDWORD ShaderTargetQueries = 0;
    mutable CKDWORD ShaderCatalogQueries = 0;
    CKBOOL ForceDestroyBusy = FALSE;
    CKBOOL DestroyBackend(CKRasterizerBackend *backend) override;

protected:
    CKRecordingBackend *NewBackend() override;
};

class FFPRecordingBackend : public CKRecordingBackend {
public:
    explicit FFPRecordingBackend(CKRecordingBackendDriver *driver)
        : CKRecordingBackend(driver ? driver->GetBackendConventions() : CKBackendCaps()), TestProvider(driver) {}

    CKRecordingBackendDriver *TestProvider;

    // --- Knobs
    CKBOOL FailCreateProgram = FALSE;
    CKBOOL FailCreateTexture = FALSE;
    CKBOOL RequireIntermediateTarget = FALSE;
    mutable CKBackendCaps ReportedCaps;
    const CKBackendCaps &GetCaps() const override {
        ReportedCaps = CKRecordingBackend::GetCaps();
        ReportedCaps.RequiresIntermediateTarget = RequireIntermediateTarget;
        if (RequireIntermediateTarget && !ReportedCaps.MaxTextureSize) ReportedCaps.MaxTextureSize = 4096;
        return ReportedCaps;
    }
    CKBOOL FailUpdateTexture = FALSE;
    CKBOOL FailResize = FALSE;
    CKBOOL ForceNotIdle = FALSE;
    CKERROR FrameResult = CK_OK;
    CKERROR DeviceStatus = CK_OK;
    CKDWORD TransientVertexCapacity = 0xFFFFFFFFu;
    CKDWORD TransientIndexCapacity = 0xFFFFFFFFu;
    CKDWORD Width = 64;      // size StartedBackend() initialises with
    CKDWORD Height = 64;

    CKERROR Resize(int x, int y, int width, int height) override {
        return FailResize ? CKERR_INVALIDOPERATION : CKRecordingBackend::Resize(x, y, width, height);
    }

    // --- Log
    FFPBackendLog Log;
    CKDWORD TransientVertexAllocations = 0;   // successful allocations
    CKDWORD TransientIndexAllocations = 0;
    CKDWORD ReadTextureCount = 0;
    CKDWORD CreatedShaderCount = 0;
    CKDWORD CreatedProgramCount = 0;
    CKBackendProgramDesc LastProgramInterface;
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
    std::vector<CKBackendPresentSync> Frames;
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
    CKBackendShaderSet ShaderSet() {
        StartedBackend();
        CKBackendShaderSet shaders;
        TestCheck(TestProvider && TestProvider->GetShaderSet(GetCaps(), shaders), "recording shader catalog");
        return shaders;
    }
    CKBOOL IsIdle() const override { return m_Initialized && ForceNotIdle ? FALSE : TRUE; }

    // --- Recording overrides
    CKERROR GetDeviceStatus() const override {
        const CKERROR base = CKRecordingBackend::GetDeviceStatus();
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
        const CKERROR result = CKRecordingBackend::CreateTexture(desc, data, out);
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
        return CKRecordingBackend::UpdateTexture(texture, mip, face, region, desc);
    }
    CKERROR CreateDepthTexture(const CKBackendDepthDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKRecordingBackend::CreateDepthTexture(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateRenderTarget(const CKBackendRenderTargetDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKRecordingBackend::CreateRenderTarget(desc, out);
        if (result == CK_OK) {
            LiveHandles.insert(*out);
            FrameBufferColorTexture[*out] = desc->ColorTexture;
        }
        return result;
    }
    CKERROR CreateBuffer(const CKBackendBufferDesc *desc, CKDWORD *out) override {
        const CKERROR result = CKRecordingBackend::CreateBuffer(desc, out);
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
        const CKERROR result = CKRecordingBackend::CreateVertexLayout(desc, out);
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
        const CKERROR result = CKRecordingBackend::CreateShader(desc, out);
        if (result == CK_OK)
            LiveHandles.insert(*out);
        return result;
    }
    CKERROR CreateProgram(const CKBackendProgramDesc *desc, CKDWORD *out) override {
        if (FailCreateProgram) {
            if (out) *out = 0;
            return CKERR_INVALIDPARAMETER;
        }
        const CKERROR result = CKRecordingBackend::CreateProgram(desc, out);
        if (result == CK_OK) {
            ++CreatedProgramCount;
            LastProgramInterface = *desc;
            LiveHandles.insert(*out);
        }
        return result;
    }
    CKERROR DestroyObject(CKDWORD object, CKDWORD type) override {
        ++DeletedObjectCount;
        LastDeletedObject = object;
        LastDeletedObjectType = type;
        const CKERROR result = CKRecordingBackend::DestroyObject(object, type);
        if (result == CK_OK)
            LiveHandles.erase(object);
        return result;
    }
    CKERROR BeginPass(const CKBackendPassDesc *desc) override {
        const CKERROR result = CKRecordingBackend::BeginPass(desc);
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
    CKBOOL AllocTransientVertices(CKDWORD count, CKDWORD layout, CKBackendTransientVertices *out) override {
        if (count > TransientVertexCapacity)
            return FALSE;
        if (!CKRecordingBackend::AllocTransientVertices(count, layout, out))
            return FALSE;
        ++TransientVertexAllocations;
        return TRUE;
    }
    CKBOOL AllocTransientIndices(CKDWORD count, CKBOOL index32, CKBackendTransientIndices *out) override {
        if (count > TransientIndexCapacity)
            return FALSE;
        if (!CKRecordingBackend::AllocTransientIndices(count, index32, out))
            return FALSE;
        ++TransientIndexAllocations;
        return TRUE;
    }
    CKERROR Draw(const CKBackendDraw *draw) override;
    CKERROR Submit(const CKBackendSubmitDesc &desc, CKDWORD *frameNumber) override {
        const CKERROR result = CKRecordingBackend::Submit(desc, frameNumber);
        if (result != CK_OK)
            return result;
        ++FrameSerial;
        Frames.push_back(desc.Sync);
        return FrameResult;
    }
    CKERROR ReadTexture(CKDWORD texture, CKDWORD mip, CKReadbackDesc *desc, CKBackendReadbackTicket *ticket) override {
        const CKERROR result = CKRecordingBackend::ReadTexture(texture, mip, desc, ticket);
        if (result == CK_OK && ticket)
            ++ReadTextureCount;
        return result;
    }

};

inline CKRecordingBackend *FFPRecordingDriver::NewBackend()
{
    return new FFPRecordingBackend(this);
}

inline CKBOOL FFPRecordingDriver::DestroyBackend(CKRasterizerBackend *backend)
{
    if (!ForceDestroyBusy)
        return CKRecordingBackendDriver::DestroyBackend(backend);
    FFPRecordingBackend *recording = static_cast<FFPRecordingBackend *>(backend);
    const CKBOOL wasForcedBusy = recording->ForceNotIdle;
    recording->ForceNotIdle = FALSE;
    const CKBOOL result = CKRecordingBackendDriver::DestroyBackend(backend);
    if (!result)
        recording->ForceNotIdle = wasForcedBusy;
    return result;
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
        return Log.StateError;
    }
    if (Log.UniformError != CK_OK && draw->Constants) {
        ++Log.DiscardCount;
        return Log.UniformError;
    }
    if (draw->Constants) {
        for (CKDWORD block = 0; block < CKBACKEND_MAX_CONSTANT_SLOTS; ++block) {
            const auto &bytes = (*draw->Constants)[block].Bytes;
            if (bytes.empty()) continue;
            ++Log.UniformSetCount;
            const CKDWORD uniform = GetBlockUniformForTests(block);
            const CKFFConstantBlockDesc info = block < CKRST_BLOCK_COUNT
                ? CKFFConstantBlockInfo(static_cast<CKFFConstantBlock>(block)) : CKFFConstantBlockDesc{"", FALSE, 1};
            if (info.Mat4) ++Log.MatrixUniformSetCount;
            auto &values = Log.FloatUniforms[uniform];
            values.resize(bytes.size() / sizeof(float));
            memcpy(values.data(), bytes.data(), bytes.size());
            Log.UniformCounts[uniform] = CKDWORD(info.Mat4 ? bytes.size() / 64u : bytes.size() / 16u);
        }
    }
    Log.LastMarker = draw->Marker ? draw->Marker : "";
    const CKBackendPipelineState &state = draw->Pipeline;
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
    for (CKDWORD slot = 0; slot < CKFF_SLOT_COUNT; ++slot) {
        const CKDWORD texture = draw->Textures ? (*draw->Textures)[slot].Texture : 0;
        if (!texture)
            continue;
        FFPTextureBinding binding;
        binding.Stage = slot;
        binding.Uniform = GetSamplerUniformForTests(slot);
        binding.Texture = texture;
        binding.Sampler = (*draw->Textures)[slot].Sampler;
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
    const CKERROR result = CKRecordingBackend::Draw(draw);
    if (result != CK_OK) {
        ++Log.DiscardCount;
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
    }
    FFPDrawRecord record;
    record.Pass = pass;
    record.Program = draw->Program;
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
    if (Log.DrawError != CK_OK && (Log.DrawErrorAt == 0 || Log.DrawCount == Log.DrawErrorAt)) {
        ++Log.DiscardCount;
        return Log.DrawError;
    }
    return CK_OK;
}

// ===========================================================================
// Recording backend behind the v3 translation core
// ===========================================================================

class FFPRecordingLibrary : public CKRecordingBackendLibrary {
protected:
    CKRecordingBackendDriver *NewDriver() override { return new FFPRecordingDriver(); }
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
