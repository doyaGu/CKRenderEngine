#ifndef CKRE_FFP_DIAGNOSTIC_HARNESS_H
#define CKRE_FFP_DIAGNOSTIC_HARNESS_H

#include "CKRasterizerDevice.h"
#include "CKTranslatedRasterizer.h"
#include "TestTriangleMultiset.h"

#include <string.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>

struct FFPTextureBinding {
    CKDWORD Stage;
    CKDWORD Uniform;
    CKDWORD Texture;
    CKSamplerDesc Sampler;
};

struct FFPViewClearRecord {
    CKRenderView View;
    CKDWORD Flags;
    CKDWORD Color;
    float Z;
    CKDWORD Stencil;
    CKRECT Rect;          // view rect at the time of the call
};

// One Submit() on the encoder (a draw or the postprocess composite).
struct FFPSubmitRecord {
    CKRenderView View;
    CKDWORD Program;
    CKDWORD Flags;
    XString Marker;       // last SetMarker() before the submit, consumed
    CKDWORD Target;       // color texture of the view's frame buffer (0 = backbuffer)
    CKRECT Rect;          // view rect at the time of the submit
};

class FFPDiagnosticContext;

// Per-view configuration recorded from the SetView* calls.
struct FFPViewState {
    CKRECT Rect;
    CKDWORD FrameBuffer;
    CK_VIEW_MODE Mode;
    FFPViewState() : FrameBuffer(0), Mode(CKRST_VIEWMODE_DEFAULT) { memset(&Rect, 0, sizeof(Rect)); }
};

struct FFPScreenShotRequest {
    CKDWORD FrameBuffer;
    CKScreenShotCallback Callback;
    void *UserData;
};

class FFPDiagnosticDriver : public CKRasterizerDeviceDriver {
public:
    explicit FFPDiagnosticDriver(CK_SHADER_PROFILE profile = CKRST_SHADER_PROFILE_DX11,
                                  CKDWORD flags = 0)
        : Target() {
        Target.ShaderProfile = profile;
        Target.HomogeneousDepth = (flags & CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE) ? TRUE : FALSE;
        Target.OriginBottomLeft = (flags & CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT) ? TRUE : FALSE;
    }

    CKRasterizerTargetDesc Target;
};

class FFPDiagnosticEncoder : public CKRasterizerEncoder {
public:
    FFPDiagnosticContext *Owner = nullptr;   // set by FFPDiagnosticContext
    CKDrawState LastState = {};
    CKDWORD StateSetCount = 0;
    CKDWORD LastProgram = 0;
    CKDWORD SubmitCount = 0;
    CKDWORD DiscardCount = 0;
    CKDWORD LastDiscardFlags = 0;
    CKDWORD TouchCount = 0;
    CKRenderView LastTouchedView = 0;
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
    CKDWORD SubmitFlags[32] = {};
    CKRenderView SubmitViews[32] = {};
    std::vector<FFPSubmitRecord> Submits;
    std::vector<CKRenderView> Touches;
    XString LastMarker;
    CKDWORD VertexBufferOrder[32] = {};
    CKDWORD IndexBufferOrder[32] = {};
    CKDWORD TransientInstanceSetCount = 0;
    CKDWORD LastInstanceCount = 0;
    CKDWORD LastInstanceStride = 0;
    CKDWORD TotalInstanceCount = 0;
    CKDWORD TotalInstanceBytes = 0;
    CKERROR Status = CK_OK;
    CKERROR StateError = CK_OK;
    CKERROR UniformError = CK_OK;
    CKERROR SubmitError = CK_OK;
    std::vector<FFPTextureBinding> TextureBindings;
    std::vector<CKBYTE> LastVertexBytes;
    std::vector<CKBYTE> LastIndexBytes;
    std::vector<CKBYTE> LastInstanceBytes;
    std::unordered_set<CKDWORD> MatrixUniforms;
    std::unordered_map<CKDWORD, std::vector<float> > FloatUniforms;
    std::unordered_map<CKDWORD, CKDWORD> UniformCounts;

    CKERROR GetStatus() const override { return Status; }

    void Discard(CKDWORD flags) override {
        LastDiscardFlags = flags;
        ++DiscardCount;
        Status = CK_OK;
    }

    void SetState(CKDrawState State) override {
        LastState = State;
        ++StateSetCount;
        if (StateError != CK_OK)
            Status = StateError;
    }
    void SetStencilRef(CKDWORD Ref) override {
        LastStencilRef = Ref;
        ++StencilRefSetCount;
    }
    void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask) override {
        LastStencilReadMask = ReadMask;
        LastStencilWriteMask = WriteMask;
        ++StencilMaskSetCount;
    }
    void SetScissor(const CKRECT *rect) override {
        ScissorEnabled = rect != NULL;
        if (rect)
            LastScissor = *rect;
        ++ScissorSetCount;
    }
    void SetPointSize(float size) override {
        LastPointSize = size;
        ++PointSizeSetCount;
    }
    void SetTransform(CKDWORD, CKDWORD) override {}
    void SetVertexBuffer(CKDWORD, CKDWORD buffer, CKDWORD, CKDWORD, CKDWORD) override {
        if (VertexBufferSetCount < 32)
            VertexBufferOrder[VertexBufferSetCount] = buffer;
        ++VertexBufferSetCount;
    }
    void SetIndexBuffer(CKDWORD buffer, CKDWORD, CKDWORD) override {
        if (IndexBufferSetCount < 32)
            IndexBufferOrder[IndexBufferSetCount] = buffer;
        ++IndexBufferSetCount;
    }
    void SetInstanceBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {}
    void SetTransientVertexBuffer(CKDWORD, CKTransientVertexBuffer *buffer) override {
        LastVertexBytes.clear();
        if (buffer && buffer->Data && buffer->Size > 0) {
            const CKBYTE *begin = static_cast<const CKBYTE *>(buffer->Data);
            LastVertexBytes.assign(begin, begin + buffer->Size);
        }
    }
    void SetTransientIndexBuffer(CKTransientIndexBuffer *buffer) override {
        LastIndexBytes.clear();
        if (buffer && buffer->Data && buffer->Size > 0) {
            const CKBYTE *begin = static_cast<const CKBYTE *>(buffer->Data);
            LastIndexBytes.assign(begin, begin + buffer->Size);
        }
    }
    void SetTransientInstanceBuffer(CKDWORD, CKTransientInstanceBuffer *buffer) override {
        ++TransientInstanceSetCount;
        LastInstanceBytes.clear();
        LastInstanceCount = 0;
        LastInstanceStride = 0;
        if (buffer && buffer->Data && buffer->Size > 0) {
            const CKBYTE *begin = static_cast<const CKBYTE *>(buffer->Data);
            LastInstanceBytes.assign(begin, begin + buffer->Size);
            LastInstanceCount = buffer->InstanceCount;
            LastInstanceStride = buffer->Stride;
            TotalInstanceCount += buffer->InstanceCount;
            TotalInstanceBytes += buffer->Size;
        }
    }
    void SetTexture(CKDWORD stage, CKDWORD uniform, CKDWORD texture, CKSamplerDesc *sampler) override {
        LastTextureStage = stage;
        LastTextureUniform = uniform;
        LastTextureHandle = texture;
        memset(&LastTextureSampler, 0, sizeof(LastTextureSampler));
        if (sampler)
            LastTextureSampler = *sampler;
        FFPTextureBinding binding;
        binding.Stage = stage;
        binding.Uniform = uniform;
        binding.Texture = texture;
        binding.Sampler = LastTextureSampler;
        TextureBindings.push_back(binding);
        ++TextureBindCount;
    }
    void SetUniform(CKDWORD uniform, const void *data, CKDWORD count) override {
        if (!data)
            return;
        ++UniformSetCount;
        if (UniformError != CK_OK) {
            Status = UniformError;
            return;
        }
        if (MatrixUniforms.find(uniform) != MatrixUniforms.end())
            ++MatrixUniformSetCount;
        const float *values = static_cast<const float *>(data);
        const CKDWORD floatCount = MatrixUniforms.find(uniform) != MatrixUniforms.end()
            ? count * 16
            : count * 4;
        FloatUniforms[uniform].assign(values, values + floatCount);
        UniformCounts[uniform] = count;
    }
    void SetComputeBuffer(CKDWORD, CKDWORD, CK_ACCESS_MODE) override {}
    void SetComputeImage(CKDWORD, CKDWORD, CKDWORD, CK_ACCESS_MODE) override {}
    void SetCondition(CKDWORD, CKBOOL) override {}
    void SetMarker(CKSTRING name) override { LastMarker = name ? name : ""; }
    void Submit(CKRenderView view, CKDWORD program, CKDWORD, CKDWORD flags) override; // after FFPDiagnosticContext
    void SubmitOcclusionQuery(CKRenderView, CKDWORD program, CKDWORD, CKDWORD, CKDWORD) override {
        LastProgram = program;
        ++SubmitCount;
    }
    void SubmitIndirect(CKRenderView, CKDWORD program, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {
        LastProgram = program;
        ++SubmitCount;
    }
    void Dispatch(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {}
    void DispatchIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {}
    void Touch(CKRenderView view) override {
        LastTouchedView = view;
        Touches.push_back(view);
        ++TouchCount;
    }
    void Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, const CKRECT *) override {}
};

class FFPDiagnosticContext : public CKRasterizerDevice {
public:
    explicit FFPDiagnosticContext(CKRasterizerDeviceDriver *driver) {
        Encoder.Owner = this;
        m_Driver = driver;
        m_Width = 64;
        m_Height = 64;
        AllowTransientInstanceBuffer = TRUE;
        FailTransientInstanceBuffer = FALSE;
        FailCreateProgram = FALSE;
        EndEncoderResult = CK_OK;
        FrameResult = CK_OK;
        DeviceStatus = CK_OK;
        FailBeginEncoder = FALSE;
        TransientVertexCapacity = 0xFFFFFFFFu;
        TransientIndexCapacity = 0xFFFFFFFFu;
    }

    CKERROR GetTargetDesc(CKRasterizerTargetDesc *target) const override {
        if (!target || target->Size < sizeof(CKRasterizerTargetDesc))
            return CKERR_INVALIDPARAMETER;
        const FFPDiagnosticDriver *driver = static_cast<const FFPDiagnosticDriver *>(m_Driver);
        *target = driver->Target;
        return CK_OK;
    }

    CKERROR GetDeviceStatus() const override { return DeviceStatus; }

    FFPDiagnosticEncoder Encoder;
    CKBOOL AllowTransientInstanceBuffer = TRUE;
    CKBOOL FailTransientInstanceBuffer = FALSE;
    CKBOOL FailCreateProgram = FALSE;
    CKBOOL FailCreateTexture = FALSE;
    CKBOOL FailUpdateTexture = FALSE;
    CKERROR EndEncoderResult = CK_OK;
    CKERROR FrameResult = CK_OK;
    CKERROR DeviceStatus = CK_OK;
    CKBOOL FailBeginEncoder = FALSE;
    CKDWORD TransientVertexCapacity = 0xFFFFFFFFu;
    CKDWORD TransientIndexCapacity = 0xFFFFFFFFu;
    CKDWORD TransientVertexAllocations = 0;
    CKDWORD TransientIndexAllocations = 0;
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
    std::vector<FFPViewClearRecord> ViewClears;
    std::unordered_map<CKRenderView, FFPViewState> Views;
    std::unordered_map<CKDWORD, CKDWORD> FrameBufferColorTexture;   // frame buffer -> color attachment
    std::vector<CKRST_FRAME_SYNC_MODE> Frames;
    std::vector<FFPScreenShotRequest> ScreenShots;
    CKBOOL FailScreenShot = FALSE;

    // Target texture (0 = backbuffer) a view draws into.
    CKDWORD ViewTarget(CKRenderView view) const {
        std::unordered_map<CKRenderView, FFPViewState>::const_iterator it = Views.find(view);
        if (it == Views.end() || it->second.FrameBuffer == 0)
            return 0;
        std::unordered_map<CKDWORD, CKDWORD>::const_iterator fb = FrameBufferColorTexture.find(it->second.FrameBuffer);
        return fb == FrameBufferColorTexture.end() ? 0 : fb->second;
    }

    std::unordered_set<CKDWORD> LiveHandles;   // every allocated, not yet deleted handle

    CKERROR AllocateHandle(CKDWORD *out) {
        if (!out)
            return CKERR_INVALIDPARAMETER;
        *out = NextResourceHandle++;
        LiveHandles.insert(*out);
        return CK_OK;
    }
    CKBOOL IsObjectAlive(CKDWORD object, CKDWORD) const override {
        return LiveHandles.count(object) != 0;
    }
    CKERROR CreateVertexBuffer(const CKVertexBufferDesc *, const void *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR CreateIndexBuffer(const CKIndexBufferDesc *, CKBOOL, const void *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR CreateTexture(const CKTextureDesc *desc, const VxImageDescEx *, CKDWORD *out) override {
        ++CreatedTextureCount;
        if (desc)
            LastTextureDesc = *desc;
        if (FailCreateTexture) {
            if (out) *out = 0;
            return CKERR_INVALIDPARAMETER;
        }
        CKERROR result = AllocateHandle(out);
        LastCreatedTexture = out ? *out : 0;
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
        return AllocateHandle(out);
    }
    CKERROR CreateProgram(const CKProgramDesc *desc, CKDWORD *out) override {
        if (FailCreateProgram) {
            if (out) *out = 0;
            return CKERR_INVALIDPARAMETER;
        }
        ++CreatedProgramCount;
        return AllocateHandle(out);
    }
    CKERROR CreateUniform(const CKUniformDesc *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR CreateVertexLayout(const CKVertexLayoutDesc *desc, CKDWORD *out) override {
        CKDWORD stride = 0;
        LastVertexLayoutElements.clear();
        if (desc) {
            stride = desc->Stride;
            for (CKDWORD i = 0; i < desc->ElementCount; ++i)
                LastVertexLayoutElements.push_back(desc->Elements[i]);
        }
        CKERROR result = AllocateHandle(out);
        if (result == CK_OK)
            m_LayoutStride[*out] = stride;
        return result;
    }
    CKERROR CreateFrameBuffer(const CKFrameBufferDesc *desc, CKDWORD *out) override {
        CKERROR result = AllocateHandle(out);
        if (result == CK_OK && desc && desc->ColorCount > 0 && desc->Color)
            FrameBufferColorTexture[*out] = desc->Color[0].Texture;
        return result;
    }
    CKERROR CreateDepthTexture(const CKDepthTextureDesc *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR CreateOcclusionQuery(const CKOcclusionQueryDesc *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR CreateIndirectBuffer(const CKIndirectBufferDesc *, CKDWORD *out) override {
        return AllocateHandle(out);
    }
    CKERROR DeleteObject(CKDWORD object, CKDWORD type) override {
        ++DeletedObjectCount;
        LastDeletedObject = object;
        LastDeletedObjectType = type;
        LiveHandles.erase(object);
        return CK_OK;
    }
    CKERROR FlushObjects(CKDWORD) override { return CK_OK; }
    CKERROR UpdateVertexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateIndexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateTexture(CKDWORD texture, CKDWORD mip, CKDWORD face,
                          const CKRECT *region, const VxImageDescEx *desc) override {
        ++UpdatedTextureCount;
        LastUpdatedTexture = texture;
        LastUpdateMip = mip;
        LastUpdateFace = face;
        LastTextureUpdateHadRegion = region != nullptr;
        if (region)
            LastTextureUpdateRegion = *region;
        if (desc)
            LastTextureUpdateDesc = *desc;
        return FailUpdateTexture ? CKERR_INVALIDPARAMETER : CK_OK;
    }
    CKERROR ReadTexture(CKDWORD, CKDWORD, CKReadbackDesc *, CKDWORD *) override {
        return CKERR_NOTIMPLEMENTED;
    }
    CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD, CKDWORD *) override { return CKRST_OCCLUSION_NORESULT; }
    CKERROR SetPaletteColor(CKDWORD index, CKDWORD color) override {
        if (index < 16)
            PaletteColors[index] = color;
        ++PaletteSetCount;
        return index < 16 ? CK_OK : CKERR_INVALIDPARAMETER;
    }
    void DbgTextClear(CKDWORD, CKBOOL) override {}
    void DbgTextPrintf(CKWORD, CKWORD, CKDWORD, CKSTRING, ...) override {}
    void DbgTextImage(CKWORD, CKWORD, CKWORD, CKWORD, const void *, CKWORD) override {}
    void SetDebug(CKDWORD) override {}
    const CKRasterizerDeviceStats *GetStats() override { return &m_Stats; }
    void SetResourceName(CKDWORD, CKDWORD, CKSTRING) override {}
    CKDWORD GetShaderUniforms(CKDWORD, CKDWORD *, CKDWORD) override { return 0; }
    void GetUniformInfo(CKDWORD, CKUniformInfo *) override {}
    CKDWORD GetFrameBufferTexture(CKDWORD, CKDWORD) override { return 0; }
    CKBOOL IsTextureValid(CKDWORD, CKBOOL, CKWORD, CKDWORD, CKDWORD) override { return TRUE; }
    CKBOOL IsFrameBufferValid(CKDWORD, const CKFrameBufferAttachmentDesc *, const CKFrameBufferAttachmentDesc *) override { return TRUE; }
    void CalcTextureSize(CKTextureInfo *, CKWORD, CKWORD, CKWORD, CKBOOL, CKBOOL, CKWORD, CKDWORD) override {}
    // Screenshots are delivered by the next Frame() as a zero-filled image of
    // the device size (the recording device draws nothing).
    CKERROR RequestScreenShot(CKDWORD frameBuffer, CKScreenShotCallback callback, void *user) override {
        if (!callback)
            return CKERR_INVALIDPARAMETER;
        if (FailScreenShot)
            return CKERR_NOTIMPLEMENTED;
        FFPScreenShotRequest request = { frameBuffer, callback, user };
        ScreenShots.push_back(request);
        return CK_OK;
    }
    CKERROR CancelScreenShots(void *user) override {
        CKERROR result = CKERR_NOTFOUND;
        for (size_t i = 0; i < ScreenShots.size();) {
            if (ScreenShots[i].UserData == user) {
                FFPScreenShotRequest request = ScreenShots[i];
                ScreenShots.erase(ScreenShots.begin() + (ptrdiff_t)i);
                request.Callback(request.UserData, request.FrameBuffer, 0, 0, 0, UNKNOWN_PF, NULL, 0, FALSE);
                result = CK_OK;
            } else {
                ++i;
            }
        }
        return result;
    }
    void DeliverScreenShots() {
        std::vector<FFPScreenShotRequest> pending;
        pending.swap(ScreenShots);
        if (pending.empty())
            return;
        const CKDWORD pitch = m_Width * 4;
        std::vector<CKBYTE> zeros((size_t)pitch * m_Height, 0);
        for (size_t i = 0; i < pending.size(); ++i)
            pending[i].Callback(pending[i].UserData, pending[i].FrameBuffer, m_Width, m_Height, pitch,
                                _32_ARGB8888, zeros.data(), (CKDWORD)zeros.size(), FALSE);
    }
    CKERROR SetViewName(CKRenderView, CKSTRING) override { return CK_OK; }
    CKERROR SetViewRect(CKRenderView view, const CKRECT &rect) override {
        Views[view].Rect = rect;
        return CK_OK;
    }
    CKERROR SetViewScissor(CKRenderView, const CKRECT *) override { return CK_OK; }
    CKERROR SetViewClear(CKRenderView view, CKDWORD flags, CKDWORD color, float z, CKDWORD stencil) override {
        FFPViewClearRecord record = { view, flags, color, z, stencil, Views[view].Rect };
        ViewClears.push_back(record);
        return CK_OK;
    }
    CKERROR SetViewTransform(CKRenderView, const VxMatrix *, const VxMatrix *) override { return CK_OK; }
    CKERROR SetViewFrameBuffer(CKRenderView view, CKDWORD frameBuffer) override {
        Views[view].FrameBuffer = frameBuffer;
        return CK_OK;
    }
    CKERROR SetViewMode(CKRenderView view, CK_VIEW_MODE mode) override {
        Views[view].Mode = mode;
        return CK_OK;
    }
    CKERROR SetViewOrder(CKRenderView, CKWORD, const CKRenderView *) override { return CK_OK; }
    CKERROR ResetView(CKRenderView view) override {
        Views.erase(view);
        return CK_OK;
    }
    CKERROR TouchView(CKRenderView) override { return CK_OK; }
    CKDWORD AllocTransform(VxMatrix *, CKDWORD) override { return 1; }
    CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *buffer, CKDWORD vertexCount, CKDWORD layout) override {
        ++TransientVertexAllocations;
        const CKDWORD stride = m_LayoutStride[layout];
        TestCheck(stride > 0, "FFP diagnostic context must know transient vertex stride");
        m_VertexStorage.assign(vertexCount * stride, 0);
        buffer->Data = m_VertexStorage.data();
        buffer->Size = (CKDWORD)m_VertexStorage.size();
        buffer->StartVertex = 0;
        buffer->VertexCount = vertexCount;
        buffer->Stride = stride;
        buffer->Layout = layout;
        return TRUE;
    }
    CKBOOL AllocTransientIndexBuffer(CKTransientIndexBuffer *buffer, CKDWORD indexCount, CKBOOL index32) override {
        ++TransientIndexAllocations;
        m_IndexStorage.assign(indexCount * (index32 ? sizeof(CKDWORD) : sizeof(CKWORD)), 0);
        buffer->Data = m_IndexStorage.data();
        buffer->Size = (CKDWORD)m_IndexStorage.size();
        buffer->StartIndex = 0;
        buffer->IndexCount = indexCount;
        buffer->Index32 = index32;
        return TRUE;
    }
    CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *buffer, CKDWORD instanceCount, CKDWORD layout) override {
        if (!AllowTransientInstanceBuffer || FailTransientInstanceBuffer)
            return FALSE;
        const CKDWORD stride = m_LayoutStride[layout];
        TestCheck(stride > 0, "FFP diagnostic context must know transient instance stride");
        m_InstanceStorage.assign(instanceCount * stride, 0);
        buffer->Data = m_InstanceStorage.data();
        buffer->Size = (CKDWORD)m_InstanceStorage.size();
        buffer->StartInstance = 0;
        buffer->InstanceCount = instanceCount;
        buffer->Stride = stride;
        buffer->Layout = layout;
        return TRUE;
    }
    CKDWORD GetAvailTransientVertexBuffer(CKDWORD vertexCount, CKDWORD) override {
        return vertexCount <= TransientVertexCapacity
            ? vertexCount : TransientVertexCapacity;
    }
    CKDWORD GetAvailTransientIndexBuffer(CKDWORD indexCount, CKBOOL) override {
        return indexCount <= TransientIndexCapacity
            ? indexCount : TransientIndexCapacity;
    }
    CKDWORD GetAvailTransientInstanceBuffer(CKDWORD instanceCount, CKDWORD layout) override {
        if (!AllowTransientInstanceBuffer || FailTransientInstanceBuffer)
            return 0;
        return m_LayoutStride[layout] > 0 ? instanceCount : 0;
    }
    CKRasterizerEncoder *BeginEncoder(CKBOOL = FALSE) override {
        return FailBeginEncoder ? nullptr : &Encoder;
    }
    CKERROR EndEncoder(CKRasterizerEncoder *encoder) override {
        if (!encoder)
            return CKERR_INVALIDPARAMETER;
        return EndEncoderResult != CK_OK ? EndEncoderResult : encoder->GetStatus();
    }
    CKERROR Frame(CKRST_FRAME_SYNC_MODE mode, CKDWORD = CKRST_FRAME_NONE,
                  CKDWORD *frameNumber = nullptr) override {
        ++FrameSerial;
        Frames.push_back(mode);
        if (frameNumber)
            *frameNumber = FrameSerial;
        DeliverScreenShots();
        return FrameResult;
    }

private:
    CKRasterizerDeviceStats m_Stats = {};
    CKDWORD NextResourceHandle = 1;
    std::unordered_map<CKDWORD, CKDWORD> m_LayoutStride;
    std::vector<CKBYTE> m_VertexStorage;
    std::vector<CKBYTE> m_IndexStorage;
    std::vector<CKBYTE> m_InstanceStorage;
};

inline void FFPDiagnosticEncoder::Submit(CKRenderView view, CKDWORD program, CKDWORD, CKDWORD flags) {
    if (SubmitCount < 32) {
        SubmitViews[SubmitCount] = view;
        SubmitFlags[SubmitCount] = flags;
    }
    FFPSubmitRecord record;
    record.View = view;
    record.Program = program;
    record.Flags = flags;
    record.Marker = LastMarker;
    record.Target = Owner ? Owner->ViewTarget(view) : 0;
    memset(&record.Rect, 0, sizeof(record.Rect));
    if (Owner) {
        std::unordered_map<CKRenderView, FFPViewState>::const_iterator it = Owner->Views.find(view);
        if (it != Owner->Views.end())
            record.Rect = it->second.Rect;
    }
    LastMarker = "";
    Submits.push_back(record);
    LastProgram = program;
    ++SubmitCount;
    if (SubmitError != CK_OK)
        Status = SubmitError;
}

// ===========================================================================
// Recording device behind the v3 translation core
// ===========================================================================

// Reports a shader-capable backend once created, like the bgfx device does;
// the fixed-function pipeline builds programs only then.
class FFPRecordingContext : public FFPDiagnosticContext {
public:
    explicit FFPRecordingContext(CKRasterizerDeviceDriver *driver) : FFPDiagnosticContext(driver) {}

    CKERROR GetCaps(CKRasterizerDeviceCapsDesc *caps) const override {
        const CKERROR status = CKRasterizerDevice::GetCaps(caps);
        if (status != CK_OK)
            return status;
        caps->Features |= CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER |
                          CKRST_DEVCAPS_TRANSIENT_BUFFERS | CKRST_DEVCAPS_TEXTURE_READBACK;
        caps->MaxTextureBindings = 16;
        return CK_OK;
    }
};

class FFPRecordingDriver : public FFPDiagnosticDriver {
public:
    FFPRecordingDriver() : FFPDiagnosticDriver(CKRST_SHADER_PROFILE_DX11) {
        m_Hardware = FALSE;
        m_CapsUpToDate = TRUE;
        m_Desc = "Recording device";
        m_3DCaps.MaxNumberTextureStage = 8;
        m_3DCaps.MaxTextureWidth = 4096;
        m_3DCaps.MaxTextureHeight = 4096;
    }

    CKRasterizerDevice *CreateContext() override {
        FFPRecordingContext *context = new FFPRecordingContext(this);
        m_Contexts.PushBack(context);
        return context;
    }

    CKBOOL DestroyContext(CKRasterizerDevice *context) override {
        for (int i = 0; i < m_Contexts.Size(); ++i) {
            if (m_Contexts[i] == context) {
                m_Contexts.RemoveAt(i);
                delete static_cast<FFPRecordingContext *>(context);
                return TRUE;
            }
        }
        return FALSE;
    }
};

class FFPRecordingDeviceLibrary : public CKRasterizerDeviceLibrary {
public:
    ~FFPRecordingDeviceLibrary() override { Close(); }

    CKBOOL Start(WIN_HANDLE window) override {
        m_MainWindow = window;
        if (m_Drivers.Size() == 0) {
            FFPRecordingDriver *driver = new FFPRecordingDriver();
            driver->m_Owner = this;
            driver->m_DriverIndex = 0;
            m_Drivers.PushBack(driver);
        }
        return TRUE;
    }

    void Close() override {
        for (int i = 0; i < m_Drivers.Size(); ++i)
            delete m_Drivers[i];
        m_Drivers.Clear();
    }
};

// A started translated rasterizer over the recording device. Add texture
// formats to DeviceDriver() before CreateContext(); the translated driver
// syncs them when the context is created.
struct FFPTranslatedWorld {
    CKRasterizer *Rasterizer;
    CKTranslatedDriver *Driver;
    CKTranslatedContext *Context;
    FFPRecordingContext *Device;

    FFPTranslatedWorld() : Rasterizer(NULL), Driver(NULL), Context(NULL), Device(NULL) {
        FFPRecordingDeviceLibrary *library = new FFPRecordingDeviceLibrary();
        library->Start(NULL);
        Rasterizer = CKTranslatedRasterizerStart(library, NULL);
        TestCheck(Rasterizer != NULL && Rasterizer->GetDriverCount() == 1, "translated rasterizer over the recording device");
        Driver = Rasterizer ? static_cast<CKTranslatedDriver *>(Rasterizer->GetDriver(0)) : NULL;
    }

    ~FFPTranslatedWorld() {
        if (Rasterizer)
            CKTranslatedRasterizerClose(Rasterizer);
    }

    FFPRecordingDriver *DeviceDriver() const {
        return Driver ? static_cast<FFPRecordingDriver *>(Driver->GetDeviceDriver()) : NULL;
    }

    CKBOOL CreateContext(int width, int height) {
        if (!Driver)
            return FALSE;
        Context = static_cast<CKTranslatedContext *>(Driver->CreateContext());
        if (!Context)
            return FALSE;
        if (!Context->Create(NULL, 0, 0, width, height, 32, FALSE, 60, 24, 8))
            return FALSE;
        Device = static_cast<FFPRecordingContext *>(Context->GetDevice());
        return Device != NULL;
    }
};

#endif // CKRE_FFP_DIAGNOSTIC_HARNESS_H
