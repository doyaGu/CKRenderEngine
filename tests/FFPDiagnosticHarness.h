#ifndef CKRE_FFP_DIAGNOSTIC_HARNESS_H
#define CKRE_FFP_DIAGNOSTIC_HARNESS_H

#include "CKRasterizer.h"
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
};

class FFPDiagnosticDriver : public CKRasterizerDriver {
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
    CKDrawState LastState = {};
    CKDWORD StateSetCount = 0;
    CKDWORD LastProgram = 0;
    CKDWORD SubmitCount = 0;
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
    CKDWORD VertexBufferSetCount = 0;
    CKDWORD IndexBufferSetCount = 0;
    CKDWORD SubmitFlags[32] = {};
    CKRenderView SubmitViews[32] = {};
    CKDWORD VertexBufferOrder[32] = {};
    CKDWORD IndexBufferOrder[32] = {};
    CKDWORD TransientInstanceSetCount = 0;
    CKDWORD LastInstanceCount = 0;
    CKDWORD LastInstanceStride = 0;
    CKDWORD TotalInstanceCount = 0;
    CKDWORD TotalInstanceBytes = 0;
    std::vector<FFPTextureBinding> TextureBindings;
    std::vector<CKBYTE> LastVertexBytes;
    std::vector<CKBYTE> LastIndexBytes;
    std::vector<CKBYTE> LastInstanceBytes;
    std::unordered_set<CKDWORD> MatrixUniforms;
    std::unordered_map<CKDWORD, std::vector<float> > FloatUniforms;
    std::unordered_map<CKDWORD, CKDWORD> UniformCounts;

    void SetState(CKDrawState State) override {
        LastState = State;
        ++StateSetCount;
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
    void SetScissor(const CKRECT *) override {}
    void SetPointSize(float) override {}
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
    void SetMarker(CKSTRING) override {}
    void Submit(CKRenderView view, CKDWORD program, CKDWORD, CKDWORD flags) override {
        if (SubmitCount < 32) {
            SubmitViews[SubmitCount] = view;
            SubmitFlags[SubmitCount] = flags;
        }
        LastProgram = program;
        ++SubmitCount;
    }
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
        ++TouchCount;
    }
    void Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, const CKRECT *) override {}
};

class FFPDiagnosticContext : public CKRasterizerContext {
public:
    explicit FFPDiagnosticContext(CKRasterizerDriver *driver) {
        m_Driver = driver;
        m_Width = 64;
        m_Height = 64;
        AllowTransientInstanceBuffer = TRUE;
        FailTransientInstanceBuffer = FALSE;
        FailCreateProgram = FALSE;
    }

    CKERROR GetTargetDesc(CKRasterizerTargetDesc *target) const override {
        if (!target || target->Size < sizeof(CKRasterizerTargetDesc))
            return CKERR_INVALIDPARAMETER;
        const FFPDiagnosticDriver *driver = static_cast<const FFPDiagnosticDriver *>(m_Driver);
        *target = driver->Target;
        return CK_OK;
    }

    FFPDiagnosticEncoder Encoder;
    CKBOOL AllowTransientInstanceBuffer = TRUE;
    CKBOOL FailTransientInstanceBuffer = FALSE;
    CKBOOL FailCreateProgram = FALSE;
    CKBOOL FailCreateTexture = FALSE;
    CKBOOL FailUpdateTexture = FALSE;
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

    CKERROR AllocateHandle(CKDWORD *out) {
        if (!out)
            return CKERR_INVALIDPARAMETER;
        *out = NextResourceHandle++;
        return CK_OK;
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
    CKERROR CreateFrameBuffer(const CKFrameBufferDesc *, CKDWORD *out) override {
        return AllocateHandle(out);
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
    const CKRenderStats *GetStats() override { return &m_Stats; }
    void SetResourceName(CKDWORD, CKDWORD, CKSTRING) override {}
    CKDWORD GetShaderUniforms(CKDWORD, CKDWORD *, CKDWORD) override { return 0; }
    void GetUniformInfo(CKDWORD, CKUniformInfo *) override {}
    CKDWORD GetFrameBufferTexture(CKDWORD, CKDWORD) override { return 0; }
    CKBOOL IsTextureValid(CKDWORD, CKBOOL, CKWORD, CKDWORD, CKDWORD) override { return TRUE; }
    CKBOOL IsFrameBufferValid(CKDWORD, const CKFrameBufferAttachmentDesc *, const CKFrameBufferAttachmentDesc *) override { return TRUE; }
    void CalcTextureSize(CKTextureInfo *, CKWORD, CKWORD, CKWORD, CKBOOL, CKBOOL, CKWORD, CKDWORD) override {}
    CKERROR RequestScreenShot(CKDWORD, CKScreenShotCallback, void *) override {
        return CKERR_NOTIMPLEMENTED;
    }
    CKERROR SetViewName(CKRenderView, CKSTRING) override { return CK_OK; }
    CKERROR SetViewRect(CKRenderView, const CKRECT &) override { return CK_OK; }
    CKERROR SetViewScissor(CKRenderView, const CKRECT *) override { return CK_OK; }
    CKERROR SetViewClear(CKRenderView view, CKDWORD flags, CKDWORD color, float z, CKDWORD stencil) override {
        FFPViewClearRecord record = { view, flags, color, z, stencil };
        ViewClears.push_back(record);
        return CK_OK;
    }
    CKERROR SetViewTransform(CKRenderView, const VxMatrix *, const VxMatrix *) override { return CK_OK; }
    CKERROR SetViewFrameBuffer(CKRenderView, CKDWORD) override { return CK_OK; }
    CKERROR SetViewMode(CKRenderView, CK_VIEW_MODE) override { return CK_OK; }
    CKERROR SetViewOrder(CKRenderView, CKWORD, const CKRenderView *) override { return CK_OK; }
    CKERROR ResetView(CKRenderView) override { return CK_OK; }
    CKERROR TouchView(CKRenderView) override { return CK_OK; }
    CKDWORD AllocTransform(VxMatrix *, CKDWORD) override { return 1; }
    CKBOOL AllocTransientVertexBuffer(CKTransientVertexBuffer *buffer, CKDWORD vertexCount, CKDWORD layout) override {
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
    CKDWORD GetAvailTransientVertexBuffer(CKDWORD vertexCount, CKDWORD) override { return vertexCount; }
    CKDWORD GetAvailTransientIndexBuffer(CKDWORD indexCount, CKBOOL) override { return indexCount; }
    CKDWORD GetAvailTransientInstanceBuffer(CKDWORD instanceCount, CKDWORD layout) override {
        if (!AllowTransientInstanceBuffer || FailTransientInstanceBuffer)
            return 0;
        return m_LayoutStride[layout] > 0 ? instanceCount : 0;
    }
    CKRasterizerEncoder *BeginEncoder(CKBOOL = FALSE) override { return &Encoder; }
    CKERROR EndEncoder(CKRasterizerEncoder *encoder) override {
        return encoder ? encoder->GetStatus() : CKERR_INVALIDPARAMETER;
    }
    CKERROR Frame(CKRST_FRAME_SYNC_MODE, CKDWORD = CKRST_FRAME_NONE,
                  CKDWORD *frameNumber = nullptr) override {
        ++FrameSerial;
        if (frameNumber)
            *frameNumber = FrameSerial;
        return CK_OK;
    }

private:
    CKRenderStats m_Stats = {};
    CKDWORD NextResourceHandle = 1;
    std::unordered_map<CKDWORD, CKDWORD> m_LayoutStride;
    std::vector<CKBYTE> m_VertexStorage;
    std::vector<CKBYTE> m_IndexStorage;
    std::vector<CKBYTE> m_InstanceStorage;
};

#endif // CKRE_FFP_DIAGNOSTIC_HARNESS_H
