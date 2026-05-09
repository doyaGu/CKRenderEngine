#ifndef CKRE_FFP_DIAGNOSTIC_HARNESS_H
#define CKRE_FFP_DIAGNOSTIC_HARNESS_H

#include "CKRasterizer.h"
#include "TestTriangleMultiset.h"

#include <unordered_map>
#include <vector>

class FFPDiagnosticDriver : public CKRasterizerDriver {
public:
    CKERROR GetShaderTarget(CKShaderTargetDesc *target) const override {
        if (!target)
            return CKERR_INVALIDPARAMETER;
        target->Format = CKRST_SHADER_FORMAT_NATIVE;
        target->Profile = CKRST_SHADER_PROFILE_DX11;
        target->Version = 0;
        target->Flags = 0;
        return CK_OK;
    }

    CKERROR GetProgrammableCaps(VxProgCapsDesc &) override { return CK_OK; }
};

class FFPDiagnosticEncoder : public CKRasterizerEncoder {
public:
    CKDrawState LastState = {};
    CKDWORD LastProgram = 0;
    CKDWORD SubmitCount = 0;
    CKDWORD TextureBindCount = 0;
    std::vector<CKBYTE> LastVertexBytes;
    std::unordered_map<CKDWORD, std::vector<float> > FloatUniforms;
    std::unordered_map<CKDWORD, CKDWORD> UniformCounts;

    void SetState(CKDrawState State) override { LastState = State; }
    void SetStencilRef(CKDWORD) override {}
    void SetStencilMask(CKDWORD, CKDWORD) override {}
    void SetScissor(const CKRECT *) override {}
    void SetPointSize(float) override {}
    void SetTransform(CKDWORD, CKDWORD) override {}
    void SetVertexLayout(CKDWORD) override {}
    void SetVertexBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {}
    void SetIndexBuffer(CKDWORD, CKDWORD, CKDWORD) override {}
    void SetInstanceBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD) override {}
    void SetTransientVertexBuffer(CKDWORD, CKTransientVertexBuffer *buffer) override {
        LastVertexBytes.clear();
        if (buffer && buffer->Data && buffer->Size > 0) {
            const CKBYTE *begin = static_cast<const CKBYTE *>(buffer->Data);
            LastVertexBytes.assign(begin, begin + buffer->Size);
        }
    }
    void SetTransientIndexBuffer(CKTransientIndexBuffer *) override {}
    void SetTransientInstanceBuffer(CKDWORD, CKTransientInstanceBuffer *) override {}
    void SetTexture(CKDWORD, CKDWORD, CKDWORD, CKSamplerDesc *) override { ++TextureBindCount; }
    void SetUniform(CKDWORD uniform, const void *data, CKDWORD count) override {
        if (!data)
            return;
        const float *values = static_cast<const float *>(data);
        FloatUniforms[uniform].assign(values, values + count * 4);
        UniformCounts[uniform] = count;
    }
    void SetComputeBuffer(CKDWORD, CKDWORD, CK_ACCESS_MODE) override {}
    void SetComputeImage(CKDWORD, CKDWORD, CKDWORD, CK_ACCESS_MODE) override {}
    void SetCondition(CKDWORD, CKBOOL) override {}
    void SetMarker(CKSTRING) override {}
    void Submit(CKRenderView, CKDWORD program, CKDWORD, CKDWORD) override {
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
    void Touch(CKRenderView) override {}
    void Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, const CKRECT *) override {}
};

class FFPDiagnosticContext : public CKRasterizerContext {
public:
    explicit FFPDiagnosticContext(CKRasterizerDriver *driver) {
        m_Driver = driver;
        m_Width = 64;
        m_Height = 64;
    }

    FFPDiagnosticEncoder Encoder;
    CKDWORD CreatedShaderCount = 0;
    CKDWORD CreatedProgramCount = 0;
    std::vector<CKDWORD> LastProgramSpecializationDwords;

    CKERROR CreateVertexBuffer(CKDWORD, CKVertexBufferDesc *, const void *) override { return CK_OK; }
    CKERROR CreateIndexBuffer(CKDWORD, CKIndexBufferDesc *, CKBOOL, const void *) override { return CK_OK; }
    CKERROR CreateTexture(CKDWORD, CKTextureDesc *, const VxImageDescEx *) override { return CK_OK; }
    CKERROR CreateShader(CKDWORD, CKShaderDesc *) override {
        ++CreatedShaderCount;
        return CK_OK;
    }
    CKERROR CreateProgram(CKDWORD, CKProgramDesc *desc) override {
        ++CreatedProgramCount;
        LastProgramSpecializationDwords.clear();
        if (desc && desc->SpecializationDwords && desc->SpecializationDwordCount > 0) {
            const CKDWORD *begin = desc->SpecializationDwords;
            LastProgramSpecializationDwords.assign(begin, begin + desc->SpecializationDwordCount);
        }
        return CK_OK;
    }
    CKERROR CreateUniform(CKDWORD, CKUniformDesc *) override { return CK_OK; }
    CKERROR CreateVertexLayout(CKDWORD layout, CKVertexLayoutDesc *desc) override {
        CKDWORD stride = 0;
        if (desc) {
            for (int i = 0; i < CKRST_MAX_VERTEX_STREAMS; ++i) {
                if (desc->Stride[i] > stride)
                    stride = desc->Stride[i];
            }
        }
        m_LayoutStride[layout] = stride;
        return CK_OK;
    }
    CKERROR CreateFrameBuffer(CKDWORD, CKFrameBufferDesc *) override { return CK_OK; }
    CKERROR CreateDepthTexture(CKDWORD, CKDepthTextureDesc *) override { return CK_OK; }
    CKERROR CreateOcclusionQuery(CKDWORD, CKOcclusionQueryDesc *) override { return CK_OK; }
    CKERROR CreateIndirectBuffer(CKDWORD, CKIndirectBufferDesc *) override { return CK_OK; }
    CKERROR DeleteObject(CKDWORD, CKDWORD) override { return CK_OK; }
    void FlushObjects(CKDWORD) override {}
    CKERROR UpdateVertexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateIndexBuffer(CKDWORD, CKDWORD, CKDWORD, const void *) override { return CK_OK; }
    CKERROR UpdateTexture(CKDWORD, CKDWORD, CKDWORD, const CKRECT *, const VxImageDescEx *) override { return CK_OK; }
    CKERROR ReadTexture(CKDWORD, CKDWORD, VxImageDescEx *) override { return CKERR_NOTIMPLEMENTED; }
    CKERROR ReadFrameBuffer(CKDWORD, VxImageDescEx *) override { return CKERR_NOTIMPLEMENTED; }
    CK_OCCLUSION_RESULT GetOcclusionResult(CKDWORD, CKDWORD *) override { return CKRST_OCCLUSION_NORESULT; }
    void SetPaletteColor(CKDWORD, CKDWORD) override {}
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
    void RequestScreenShot(CKDWORD, CKScreenShotCallback) override {}
    CKERROR SetViewName(CKRenderView, CKSTRING) override { return CK_OK; }
    CKERROR SetViewRect(CKRenderView, const CKRECT &) override { return CK_OK; }
    CKERROR SetViewScissor(CKRenderView, const CKRECT *) override { return CK_OK; }
    CKERROR SetViewClear(CKRenderView, CKDWORD, CKDWORD, float, CKDWORD) override { return CK_OK; }
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
    CKBOOL AllocTransientInstanceBuffer(CKTransientInstanceBuffer *, CKDWORD, CKDWORD) override { return FALSE; }
    CKDWORD GetAvailTransientVertexBuffer(CKDWORD vertexCount, CKDWORD) override { return vertexCount; }
    CKDWORD GetAvailTransientIndexBuffer(CKDWORD indexCount, CKBOOL) override { return indexCount; }
    CKDWORD GetAvailTransientInstanceBuffer(CKDWORD, CKDWORD) override { return 0; }
    CKRasterizerEncoder *BeginEncoder() override { return &Encoder; }
    void EndEncoder(CKRasterizerEncoder *) override {}
    CKERROR Frame(CKRST_FRAME_SYNC_MODE) override { return CK_OK; }

private:
    CKRenderStats m_Stats = {};
    std::unordered_map<CKDWORD, CKDWORD> m_LayoutStride;
    std::vector<CKBYTE> m_VertexStorage;
    std::vector<CKBYTE> m_IndexStorage;
};

#endif // CKRE_FFP_DIAGNOSTIC_HARNESS_H
