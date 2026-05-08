#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKFFUniformState.h"

#include <cmath>
#include <cstring>

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr), m_ActiveLightCount(0), m_CurrentActiveTextureCount(0),
      m_CurrentLightingEnabled(false), m_DirtyFlags(CKFF_DIRTY_ALL) {
    Vx3DMatrixIdentity(m_World);
    Vx3DMatrixIdentity(m_View);
    Vx3DMatrixIdentity(m_Projection);
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(m_TexMatrix[i]);
    ResetMaterial();
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(m_TextureHandles, 0, sizeof(m_TextureHandles));
    memset(m_StageStates, 0, sizeof(m_StageStates));
    memset(m_UserClipPlanes, 0, sizeof(m_UserClipPlanes));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_Viewport[0] = 2.0f / 800.0f;
    m_Viewport[1] = -2.0f / 600.0f;
    m_Viewport[2] = -1.0f;
    m_Viewport[3] = 1.0f;
    m_MaterialSource[0] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[1] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[2] = (float)CKFF_MS_MATERIAL;
    m_MaterialSource[3] = (float)CKFF_MS_MATERIAL;
}

CKFixedFunctionPipeline::~CKFixedFunctionPipeline() {
    Shutdown();
}

void CKFixedFunctionPipeline::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    m_ShaderCache.Init(ctx);
    m_DrawStateCache.Reset();
    m_VertexLayoutCache.Init(ctx);
    m_TransientGeometry.Init(ctx, &m_VertexLayoutCache);
    m_RenderPipeline.Init(ctx);
    m_DirtyFlags = CKFF_DIRTY_ALL;
}

void CKFixedFunctionPipeline::Shutdown() {
    m_TransientGeometry.Shutdown();
    m_VertexLayoutCache.Shutdown();
    m_ShaderCache.Shutdown();
    m_RenderPipeline.Shutdown();
    m_Context = nullptr;
}

CKFFStateGuard::CKFFStateGuard(CKFixedFunctionPipeline &pipeline)
    : m_Pipeline(&pipeline),
      m_ColorWriteMask(pipeline.GetColorWriteMask()),
      m_World(pipeline.GetWorldMatrix()),
      m_View(pipeline.GetViewMatrix()),
      m_Projection(pipeline.GetProjectionMatrix()) {
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_RenderStates[i] = pipeline.GetRenderState((VXRENDERSTATETYPE)i);
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        pipeline.SaveTextureStage(stage, m_TextureStages[stage]);
}

CKFFStateGuard::~CKFFStateGuard() {
    Restore();
}

void CKFFStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_Pipeline->RestoreTextureStage(stage, m_TextureStages[stage]);
    m_Pipeline->SetTransform(VXMATRIX_WORLD, m_World);
    m_Pipeline->SetTransform(VXMATRIX_VIEW, m_View);
    m_Pipeline->SetTransform(VXMATRIX_PROJECTION, m_Projection);
    m_Pipeline->SetColorWriteMask(m_ColorWriteMask);
    for (int i = 0; i < CKFF_RS_COUNT; ++i)
        m_Pipeline->SetRenderState((VXRENDERSTATETYPE)i, m_RenderStates[i]);
    m_Pipeline = nullptr;
}

void CKFFStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

CKFFRenderStateGuard::CKFFRenderStateGuard(CKFixedFunctionPipeline &pipeline, VXRENDERSTATETYPE state, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_State(state),
      m_Value(active ? pipeline.GetRenderState(state) : 0) {
}

CKFFRenderStateGuard::~CKFFRenderStateGuard() {
    Restore();
}

void CKFFRenderStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetRenderState(m_State, m_Value);
    m_Pipeline = nullptr;
}

void CKFFRenderStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

// ============================================================================
// State tracking
// ============================================================================

void CKFixedFunctionPipeline::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    m_DrawStateCache.SetRenderState(state, value);

    switch (state) {
    case VXRENDERSTATE_FOGENABLE:
    case VXRENDERSTATE_FOGVERTEXMODE:
    case VXRENDERSTATE_FOGPIXELMODE:
    case VXRENDERSTATE_FOGSTART:
    case VXRENDERSTATE_FOGEND:
    case VXRENDERSTATE_FOGDENSITY:
    case VXRENDERSTATE_FOGCOLOR:
        m_DirtyFlags |= CKFF_DIRTY_FOG;
        break;
    case VXRENDERSTATE_AMBIENT:
        m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
        break;
    case VXRENDERSTATE_TEXTUREFACTOR:
        m_DirtyFlags |= CKFF_DIRTY_TEXFACTOR;
        break;
    case VXRENDERSTATE_ALPHATESTENABLE:
    case VXRENDERSTATE_ALPHAFUNC:
    case VXRENDERSTATE_ALPHAREF:
        m_DirtyFlags |= CKFF_DIRTY_ALPHATEST;
        break;
    default:
        break;
    }
}

CKDWORD CKFixedFunctionPipeline::GetRenderState(VXRENDERSTATETYPE state) const {
    return m_DrawStateCache.GetRenderState(state);
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a) {
    m_DrawStateCache.SetColorWriteMask(r, g, b, a);
}

CKDWORD CKFixedFunctionPipeline::GetColorWriteMask() const {
    return m_DrawStateCache.GetColorWriteMask();
}

void CKFixedFunctionPipeline::SetColorWriteMask(CKDWORD mask) {
    m_DrawStateCache.SetColorWriteMask(mask);
}

void CKFixedFunctionPipeline::ResetTextureStage(int stage) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_TextureHandles[stage] = 0;
    memset(m_StageStates[stage], 0, sizeof(m_StageStates[stage]));
    m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
    m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] = CKRST_TTF_NONE;
    Vx3DMatrixIdentity(m_TexMatrix[stage]);
}

void CKFixedFunctionPipeline::DisableTextureStagesFrom(int firstStage) {
    if (firstStage < 0)
        firstStage = 0;
    for (int stage = firstStage; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        ResetTextureStage(stage);
}

void CKFixedFunctionPipeline::SaveTextureStage(int stage, CKFFTextureStageSnapshot &snapshot) const {
    memset(&snapshot, 0, sizeof(snapshot));
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    snapshot.Texture = m_TextureHandles[stage];
    memcpy(snapshot.States, m_StageStates[stage], sizeof(snapshot.States));
    snapshot.TextureMatrix = m_TexMatrix[stage];
}

void CKFixedFunctionPipeline::RestoreTextureStage(int stage, const CKFFTextureStageSnapshot &snapshot) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return;

    m_TextureHandles[stage] = snapshot.Texture;
    memcpy(m_StageStates[stage], snapshot.States, sizeof(m_StageStates[stage]));
    m_TexMatrix[stage] = snapshot.TextureMatrix;
}

void CKFixedFunctionPipeline::SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type, CKDWORD value) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return;
    m_StageStates[stage][(int)type] = value;
}

CKDWORD CKFixedFunctionPipeline::GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE type) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    if ((int)type >= CKFF_MAX_TEXTURE_STAGE_STATES) return 0;
    return m_StageStates[stage][(int)type];
}

void CKFixedFunctionPipeline::SetViewport(const CKViewportData &viewport) {
    const float w = viewport.ViewWidth > 0 ? (float)viewport.ViewWidth : 1.0f;
    const float h = viewport.ViewHeight > 0 ? (float)viewport.ViewHeight : 1.0f;
    const float x = (float)viewport.ViewX;
    const float y = (float)viewport.ViewY;

    m_Viewport[0] = 2.0f / w;
    m_Viewport[1] = -2.0f / h;
    m_Viewport[2] = -1.0f - (2.0f * x / w);
    m_Viewport[3] = 1.0f + (2.0f * y / h);
}

void CKFixedFunctionPipeline::SetUserClipPlane(int index, const VxPlane &plane) {
    if (index < 0 || index >= 6)
        return;
    m_UserClipPlanes[index] = plane;
}

void CKFixedFunctionPipeline::SetTransform(VXMATRIX_TYPE type, const VxMatrix &matrix) {
    switch (type) {
    case VXMATRIX_WORLD:
        m_World = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES;
        break;
    case VXMATRIX_VIEW:
        m_View = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES | CKFF_DIRTY_LIGHTS;
        break;
    case VXMATRIX_PROJECTION:
        m_Projection = matrix;
        m_DirtyFlags |= CKFF_DIRTY_MATRICES;
        break;
    default:
        if (type >= VXMATRIX_TEXTURE0 && type <= VXMATRIX_TEXTURE7) {
            int idx = type - VXMATRIX_TEXTURE0;
            if (idx < CKFF_MAX_TEXTURE_STAGES)
                m_TexMatrix[idx] = matrix;
        }
        break;
    }
}

void CKFixedFunctionPipeline::ResetMaterial() {
    memset(&m_Material, 0, sizeof(m_Material));
    m_Material.Diffuse[0] = 1.0f;
    m_Material.Diffuse[1] = 1.0f;
    m_Material.Diffuse[2] = 1.0f;
    m_Material.Diffuse[3] = 1.0f;
    m_Material.Ambient[0] = 1.0f;
    m_Material.Ambient[1] = 1.0f;
    m_Material.Ambient[2] = 1.0f;
    m_Material.Ambient[3] = 1.0f;
    m_DirtyFlags |= CKFF_DIRTY_MATERIAL;
}

void CKFixedFunctionPipeline::SetMaterial(const CKMaterialData *mat) {
    if (!mat) return;
    m_Material.Diffuse[0] = mat->Diffuse.r;
    m_Material.Diffuse[1] = mat->Diffuse.g;
    m_Material.Diffuse[2] = mat->Diffuse.b;
    m_Material.Diffuse[3] = mat->Diffuse.a;
    m_Material.Ambient[0] = mat->Ambient.r;
    m_Material.Ambient[1] = mat->Ambient.g;
    m_Material.Ambient[2] = mat->Ambient.b;
    m_Material.Ambient[3] = mat->Ambient.a;
    m_Material.Specular[0] = mat->Specular.r;
    m_Material.Specular[1] = mat->Specular.g;
    m_Material.Specular[2] = mat->Specular.b;
    m_Material.Specular[3] = mat->Specular.a;
    m_Material.Emissive[0] = mat->Emissive.r;
    m_Material.Emissive[1] = mat->Emissive.g;
    m_Material.Emissive[2] = mat->Emissive.b;
    m_Material.Emissive[3] = mat->Emissive.a;
    m_Material.Power = mat->SpecularPower;
    m_DirtyFlags |= CKFF_DIRTY_MATERIAL;
}

void CKFixedFunctionPipeline::SetLight(int index, const CKLightData *light) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS || !light) return;

    CKFFLightData &dst = m_Lights[index];

    // Store in world space; will be transformed to view space at upload time
    dst.Position[0] = light->Position.x;
    dst.Position[1] = light->Position.y;
    dst.Position[2] = light->Position.z;
    dst.Position[3] = CKFFEncodeShaderLightType(light->Type);

    dst.Direction[0] = light->Direction.x;
    dst.Direction[1] = light->Direction.y;
    dst.Direction[2] = light->Direction.z;
    dst.Direction[3] = light->Range;

    dst.Diffuse[0] = light->Diffuse.r;
    dst.Diffuse[1] = light->Diffuse.g;
    dst.Diffuse[2] = light->Diffuse.b;
    dst.Diffuse[3] = light->Diffuse.a;

    dst.Specular[0] = light->Specular.r;
    dst.Specular[1] = light->Specular.g;
    dst.Specular[2] = light->Specular.b;
    dst.Specular[3] = light->Specular.a;

    dst.Ambient[0] = light->Ambient.r;
    dst.Ambient[1] = light->Ambient.g;
    dst.Ambient[2] = light->Ambient.b;
    dst.Ambient[3] = light->Ambient.a;

    dst.Attenuation[0] = light->Attenuation0;
    dst.Attenuation[1] = light->Attenuation1;
    dst.Attenuation[2] = light->Attenuation2;
    dst.Attenuation[3] = light->Falloff;

    dst.SpotParams[0] = cosf(light->InnerSpotCone * 0.5f);
    dst.SpotParams[1] = cosf(light->OuterSpotCone * 0.5f);
    dst.SpotParams[2] = 0.0f;
    dst.SpotParams[3] = 0.0f;

    m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
}

void CKFixedFunctionPipeline::EnableLight(int index, CKBOOL enable) {
    if (index < 0 || index >= CKFF_MAX_LIGHTS) return;
    m_LightEnabled[index] = enable;

    m_ActiveLightCount = 0;
    for (int i = 0; i < CKFF_MAX_LIGHTS; i++) {
        if (m_LightEnabled[i]) m_ActiveLightCount++;
    }
    m_DirtyFlags |= CKFF_DIRTY_LIGHTS;
}

void CKFixedFunctionPipeline::SetTexture(int stage, CKDWORD textureHandle) {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return;
    m_TextureHandles[stage] = textureHandle;
}

CKDWORD CKFixedFunctionPipeline::GetTexture(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES) return 0;
    return m_TextureHandles[stage];
}

void CKFixedFunctionPipeline::BeginDebugFrame() {
    m_DebugState.BeginFrame();
}

// ============================================================================
// Drawing
// ============================================================================

void CKFixedFunctionPipeline::DrawPrimitive(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
    VxDrawPrimitiveData *data)
{
    if (!encoder || !data || data->VertexCount == 0) return;

    bool hasNormal = (data->NormalPtr != nullptr);
    bool hasUV = (data->TexCoordPtr != nullptr);
    CKDWORD formatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(data->Flags, hasNormal, hasUV);
    const bool positionT = (formatFlags & CKFF_VF_POSITIONT) != 0;
    const int debugDrawSerial = m_DebugState.NextDrawSerial(view);
    CKFFDrawDebugInfo debugInfo = {};
    debugInfo.View = view;
    debugInfo.Type = type;
    debugInfo.Indices = indices;
    debugInfo.IndexCount = indexCount;
    debugInfo.Data = data;
    debugInfo.FormatFlags = formatFlags;
    debugInfo.DrawSerial = debugDrawSerial;
    debugInfo.World = &m_World;
    debugInfo.ViewMatrix = &m_View;
    debugInfo.Projection = &m_Projection;
    debugInfo.Viewport = m_Viewport;
    m_DebugState.LogDrawPrimitiveHeader(debugInfo);

    if (m_DebugState.ShouldSkip(view, debugDrawSerial, positionT)) {
        m_DebugState.LogDrawPrimitiveSkipped(debugInfo, positionT);
        return;
    }

    // Prepare transient geometry
    const CKDWORD wrapMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_WRAP0);
    CKFFPointSpriteParams pointParams;
    pointParams.Size = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE, 1.0f);
    pointParams.MinSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MIN, 1.0f);
    pointParams.MaxSize = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSIZE_MAX, 64.0f);
    pointParams.ScaleEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSCALEENABLE);
    pointParams.ScaleA = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_A, 1.0f);
    pointParams.ScaleB = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_B, 0.0f);
    pointParams.ScaleC = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_POINTSCALE_C, 0.0f);
    pointParams.World = m_World;
    pointParams.View = m_View;
    if (!m_TransientGeometry.Prepare(
            encoder, type, indices, indexCount, data, wrapMode,
            m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE), &pointParams)) {
        m_DebugState.LogDrawPrimitivePrepareFailed();
        return;
    }

    // Build the fixed-function state description and select the matching program.
    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(data->Flags, m_TextureHandles, m_StageStates);
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(data->Flags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    SetCurrentShaderKey(shaderKey);
    CKDWORD program = m_ShaderCache.GetProgram(shaderKey);
    if (program == 0) {
        m_DebugState.LogDrawPrimitiveProgramMissing();
        return;
    }

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawPrimitiveDetails(debugInfo);

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP ||
         (type == VX_POINTLIST && m_DrawStateCache.GetRenderState(VXRENDERSTATE_POINTSPRITEENABLE)))
            ? VX_TRIANGLELIST
            : type);
    encoder->SetState(drawState);
    encoder->SetStencilRef(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF));
    encoder->SetStencilMask(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK),
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK));

    // Bind textures
    BindTextures(encoder);

    // Submit
    float depth = ComputeDepthKey();
    encoder->Submit(view, program, *(CKDWORD *)&depth, CKRST_DISCARD_ALL);
}

void CKFixedFunctionPipeline::DrawVertexBuffer(
    CKRasterizerEncoder *encoder, CKRenderView view,
    VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
    CKDWORD baseVertex, CKDWORD vertexCount,
    CKDWORD startIndex, CKDWORD indexCount,
    CKDWORD dpFlags, CKDWORD formatFlags,
    CKDWORD vertexLayout)
{
    if (!encoder || !vb) return;

    // Build the fixed-function state description from the actual mesh vertex format.
    const bool positionT = (formatFlags & CKFF_VF_POSITIONT) != 0;
    const int debugDrawSerial = m_DebugState.NextDrawSerial(view);
    CKFFDrawDebugInfo debugInfo = {};
    debugInfo.View = view;
    debugInfo.Type = type;
    debugInfo.World = &m_World;
    debugInfo.ViewMatrix = &m_View;
    debugInfo.Projection = &m_Projection;
    debugInfo.VertexBuffer = vb;
    debugInfo.IndexBuffer = ib;
    debugInfo.BaseVertex = baseVertex;
    debugInfo.VertexCount = vertexCount;
    debugInfo.StartIndex = startIndex;
    debugInfo.PersistentIndexCount = indexCount;
    debugInfo.DPFlags = dpFlags;
    debugInfo.FormatFlags = formatFlags;
    debugInfo.VertexLayout = vertexLayout;
    debugInfo.DrawSerial = debugDrawSerial;
    m_DebugState.LogDrawVertexBufferHeader(debugInfo);

    if (m_DebugState.ShouldSkip(view, debugDrawSerial, positionT)) {
        m_DebugState.LogDrawVertexBufferSkipped(debugInfo, positionT);
        return;
    }

    m_CurrentActiveTextureCount = CKFFResolveActiveTextureCount(dpFlags, m_TextureHandles, m_StageStates);
    CKFFStateDesc stateDesc = BuildCurrentStateDesc(dpFlags, formatFlags);
    CKFFShaderKey shaderKey = BuildCurrentShaderKey(stateDesc);
    SetCurrentShaderKey(shaderKey);
    CKDWORD program = m_ShaderCache.GetProgram(shaderKey);
    if (program == 0) return;

    debugInfo.Program = program;
    debugInfo.ActiveTextureCount = m_CurrentActiveTextureCount;
    debugInfo.ActiveLightCount = m_ActiveLightCount;
    debugInfo.StateDesc = &stateDesc;
    debugInfo.DrawState = &m_DrawStateCache;
    debugInfo.Stage0.ColorOp = stateDesc.FS.GetStageColorOp(0);
    debugInfo.Stage0.ColorArg1 = CKFFResolveStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.ColorArg2 = CKFFResolveStageColorArg2(m_StageStates[0]);
    debugInfo.Stage0.AlphaOp = CKFFResolveStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg1 = CKFFResolveStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0);
    debugInfo.Stage0.AlphaArg2 = CKFFResolveStageAlphaArg2(m_StageStates[0]);
    debugInfo.Stage0.Texture = m_TextureHandles[0];
    m_DebugState.LogDrawVertexBufferDetails(debugInfo);

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
    encoder->SetState(drawState);
    encoder->SetStencilRef(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILREF));
    encoder->SetStencilMask(m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILMASK),
                            m_DrawStateCache.GetRenderState(VXRENDERSTATE_STENCILWRITEMASK));

    // Set vertex layout
    if (vertexLayout)
        encoder->SetVertexLayout(vertexLayout);

    // Bind buffers
    encoder->SetVertexBuffer(0, vb, baseVertex, vertexCount);
    if (ib)
        encoder->SetIndexBuffer(ib, startIndex, indexCount);

    // Bind textures
    BindTextures(encoder);

    // Submit
    float depth = ComputeDepthKey();
    encoder->Submit(view, program, *(CKDWORD *)&depth, CKRST_DISCARD_ALL);
}

// ============================================================================
// Internal methods
// ============================================================================

CKFFStateDesc CKFixedFunctionPipeline::BuildCurrentStateDesc(CKDWORD dpFlags, CKDWORD formatFlags) {
    CKFFStateDesc stateDesc;

    const bool hasFormat = formatFlags != 0;
    const bool positionT = hasFormat ? ((formatFlags & CKFF_VF_POSITIONT) != 0) : ((dpFlags & CKRST_DP_TRANSFORM) == 0);

    // Vertex state description
    stateDesc.VS.SetHasPosition(!positionT);
    stateDesc.VS.SetHasPositionT(positionT);
    stateDesc.VS.SetHasNormal(hasFormat ? ((formatFlags & CKFF_VF_NORMAL) != 0) : ((dpFlags & CKRST_DP_LIGHT) != 0));
    stateDesc.VS.SetHasColor0(hasFormat ? ((formatFlags & CKFF_VF_COLOR0) != 0) : ((dpFlags & CKRST_DP_DIFFUSE) != 0));
    stateDesc.VS.SetHasColor1(hasFormat ? ((formatFlags & CKFF_VF_COLOR1) != 0) : ((dpFlags & CKRST_DP_SPECULAR) != 0));
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        stateDesc.VS.SetHasTexCoord(
            stage,
            hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD(stage)) != 0) : (m_CurrentActiveTextureCount > stage));
        const CKDWORD packedTexcoord = m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        const CKDWORD transformFlags = m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS];
        stateDesc.VS.SetTexCoordIndex(stage, CKFFTexcoordIndex(packedTexcoord));
        stateDesc.VS.SetTextureTransformFlags(stage, transformFlags);
        if (!positionT) {
            const CKDWORD texgen = (packedTexcoord >> 16) & 0xFFFFu;
            const bool hasTransform = transformFlags != 0;
            stateDesc.VS.SetTexGen(stage, texgen, hasTransform);
        }
    }

    CKBOOL lighting = m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING);
    CKBOOL specular = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE);
    CKBOOL normalize = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS);

    if (m_DebugState.ForceUnlit())
        lighting = FALSE;

    stateDesc.VS.SetLightingEnabled(!positionT && lighting && stateDesc.VS.GetHasNormal());
    m_CurrentLightingEnabled = stateDesc.VS.GetLightingEnabled();
    stateDesc.VS.SetSpecularEnabled(specular != 0);
    stateDesc.VS.SetNormalizeNormals(normalize != 0);
    stateDesc.VS.SetLocalViewer(m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) != 0);
    stateDesc.VS.SetLightCount(stateDesc.VS.GetLightingEnabled() ? m_ActiveLightCount : 0);

    const CKFFVertexBlendState vertexBlend = CKFFResolveVertexBlendState(
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_VERTEXBLEND),
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE) != 0,
        positionT ? 0 : formatFlags);
    stateDesc.VS.SetVertexBlendMode(vertexBlend.Mode);
    stateDesc.VS.SetVertexBlendIndexed(vertexBlend.Indexed != 0);
    stateDesc.VS.SetVertexBlendCount(vertexBlend.Count);

    const CKBOOL colorVertex = m_DrawStateCache.GetRenderState(VXRENDERSTATE_COLORVERTEX);
    const CKDWORD diffuseSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD ambientSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD specularSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX),
        dpFlags, CKRST_DP_SPECULAR, CKFF_MS_COLOR1);
    const CKDWORD emissiveSource = CKFFResolveMaterialSource(
        stateDesc.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_EMISSIVEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);

    stateDesc.VS.SetDiffuseSource(diffuseSource);
    stateDesc.VS.SetAmbientSource(ambientSource);
    stateDesc.VS.SetSpecularSource(specularSource);
    stateDesc.VS.SetEmissiveSource(emissiveSource);
    m_MaterialSource[0] = (float)diffuseSource;
    m_MaterialSource[1] = (float)ambientSource;
    m_MaterialSource[2] = (float)specularSource;
    m_MaterialSource[3] = (float)emissiveSource;

    // Fog
    CKBOOL fogEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE);
    if (m_DebugState.DisableFog())
        fogEnable = FALSE;
    if (fogEnable) {
        CKDWORD vertexFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE);
        CKDWORD pixelFogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE);
        stateDesc.VS.SetFogMode(vertexFogMode);
        stateDesc.VS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
        stateDesc.FS.SetVertexFogMode(vertexFogMode);
        stateDesc.FS.SetPixelFogMode(pixelFogMode);
        stateDesc.FS.SetRangeFog(m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) != 0);
    }

    // Fragment state description mirrors the active fixed-function texture-stage contract.
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        const CKDWORD colorOp = CKFFResolveStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = CKFFResolveStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stateDesc.FS.SetStageColorOp(stage, colorOp);
        stateDesc.FS.SetStageColorArg0(stage, CKFFResolveStageColorArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageColorArg1(stage, CKFFBaseTextureArg(CKFFResolveStageColorArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageColorArg2(stage, CKFFBaseTextureArg(CKFFResolveStageColorArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageAlphaOp(stage, alphaOp);
        stateDesc.FS.SetStageAlphaArg0(stage, CKFFResolveStageAlphaArg0(m_StageStates[stage]));
        stateDesc.FS.SetStageAlphaArg1(stage, CKFFBaseTextureArg(CKFFResolveStageAlphaArg1(m_StageStates[stage], hasTexture)));
        stateDesc.FS.SetStageAlphaArg2(stage, CKFFBaseTextureArg(CKFFResolveStageAlphaArg2(m_StageStates[stage])));
        stateDesc.FS.SetStageResultIsTemp(stage, CKFFBaseTextureArg(CKFFResolveStageResultArg(m_StageStates[stage])) == CKRST_TA_TEMP);
        stateDesc.FS.SetStageProjectedSampler(stage, (m_StageStates[stage][CKRST_TSS_TEXTURETRANSFORMFLAGS] & CKRST_TTF_PROJECTED) != 0);

        if (colorOp == CKRST_TOP_DISABLE)
            break;
    }

    stateDesc.FS.SetSpecularAdd(specular != 0);
    stateDesc.FS.SetFogEnabled(fogEnable != 0);

    CKBOOL alphaTest = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    if (alphaTest) {
        stateDesc.FS.SetAlphaTestEnabled(true);
        stateDesc.FS.SetAlphaFunc(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC));
    }

    return stateDesc;
}

CKFFShaderKey CKFixedFunctionPipeline::BuildCurrentShaderKey(const CKFFStateDesc &stateDesc) const {
    CKDWORD textureBoundMask = 0;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        if (m_TextureHandles[stage] != 0)
            textureBoundMask |= (1u << stage);
    }
    return CKFFBuildShaderKey(stateDesc, textureBoundMask);
}

void CKFixedFunctionPipeline::SetCurrentShaderKey(const CKFFShaderKey &shaderKey) {
    m_CurrentSpecializationInfo = CKFFBuildSpecializationInfo(shaderKey.FS);
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder) {
    if (!encoder) return;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    Vx3DMultiplyMatrix4(modelView, m_View, m_World);
    Vx3DInverseMatrix(normalMatrix, modelView);
    Vx3DTransposeMatrix(normalMatrix, normalMatrix);
    Vx3DMultiplyMatrix4(viewProj, m_Projection, m_View);
    Vx3DMultiplyMatrix4(modelViewProj, viewProj, m_World);
    encoder->SetUniform(u.u_ckModelViewProj, &modelViewProj, 1);
    encoder->SetUniform(u.u_ckModel, &m_World, 1);
    encoder->SetUniform(u.u_ckModelView, &modelView, 1);
    encoder->SetUniform(u.u_ckNormalMatrix, &normalMatrix, 1);
    encoder->SetUniform(u.u_texMatrix, m_TexMatrix, CKFF_MAX_TEXTURE_STAGES);

    // bgfx uniform bindings are draw state. Since draws submit with
    // CKRST_DISCARD_ALL, upload the current fixed-function constants for every
    // draw instead of relying on dirty flags across submissions.
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    int packed = CKFFPackViewLights(m_Lights, m_LightEnabled, m_ActiveLightCount,
                                    m_CurrentLightingEnabled ? TRUE : FALSE, m_View, viewLights);

    if (packed > 0) {
        encoder->SetUniform(u.u_lights, viewLights, packed * 7);
    }

    // Light params: x=count, y/z/w = global ambient RGB
    CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
    float ambientColorF[4];
    CKFFPackColorARGB(ambientColor, ambientColorF);
    float lightParams[4];
    lightParams[0] = m_CurrentLightingEnabled ? (float)packed : -1.0f;
    lightParams[1] = ambientColorF[0];
    lightParams[2] = ambientColorF[1];
    lightParams[3] = ambientColorF[2];
    encoder->SetUniform(u.u_lightParams, lightParams, 1);

    encoder->SetUniform(u.u_material, &m_Material, 5);
    encoder->SetUniform(u.u_ffParams, m_MaterialSource, 1);
    float lightModelParams[4] = {
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_RANGEFOGENABLE) ? 1.0f : 0.0f,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGPIXELMODE) : 0.0f
    };
    encoder->SetUniform(u.u_lightModelParams, lightModelParams, 1);

    float fogParams[4];
    fogParams[0] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGSTART, 0.0f);
    fogParams[1] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGEND, 1.0f);
    fogParams[2] = CKFFReadFloatRenderState(m_DrawStateCache, VXRENDERSTATE_FOGDENSITY, 1.0f);
    fogParams[3] = (m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE) &&
                    !m_DebugState.DisableFog())
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE)
        : 0.0f;
    encoder->SetUniform(u.u_fogParams, fogParams, 1);

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    float fogColorF[4];
    CKFFPackColorARGB(fogColor, fogColorF);
    encoder->SetUniform(u.u_fogColor, fogColorF, 1);

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    float texFactor[4];
    CKFFPackColorARGB(tf, texFactor);
    encoder->SetUniform(u.u_texFactor, texFactor, 1);

    float alphaParams[4];
    alphaParams[0] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF) / 255.0f;
    alphaParams[1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC)
        : 0.0f;
    alphaParams[2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    alphaParams[3] = 0.0f;
    encoder->SetUniform(u.u_alphaParams, alphaParams, 1);

    float bumpEnv[2][4] = {};
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const CKDWORD op = m_StageStates[stage][CKRST_TSS_OP];
        if (op == CKRST_TOP_BUMPENVMAP || op == CKRST_TOP_BUMPENVMAPLUMINANCE) {
            CKFFPackBumpEnvUniform(m_StageStates[stage], bumpEnv);
            break;
        }
    }
    encoder->SetUniform(u.u_bumpEnv, bumpEnv, 2);

    encoder->SetUniform(u.u_viewport, m_Viewport, 1);

    CKFFStageParamsUniform stageParams;
    CKFFPackStageParams(m_StageStates, m_TextureHandles, m_CurrentActiveTextureCount, stageParams);
    encoder->SetUniform(u.u_stageParams, stageParams.Values, CKFF_MAX_TEXTURE_STAGES * 4);

    CKFFSpecUniform ffSpec;
    CKFFPackSpecializationDwords(m_CurrentSpecializationInfo, ffSpec);
    encoder->SetUniform(u.u_ffSpec, ffSpec.Values, CKFFSpecializationInfo::MaxSpecDwords);

    CKFFClipPlaneUniform clip;
    CKFFPackClipPlaneUniforms(m_UserClipPlanes, m_DrawStateCache.GetRenderState(VXRENDERSTATE_CLIPPLANEENABLE), clip);
    encoder->SetUniform(u.u_clipPlanes, clip.Planes, 6);
    encoder->SetUniform(u.u_clipParams, clip.Params, 1);

    m_DirtyFlags = 0;
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder) {
    if (!encoder) return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        CKSamplerDesc sampler = BuildSamplerDesc(i);
        const CKDWORD texture = (i < m_CurrentActiveTextureCount) ? m_TextureHandles[i] : 0;
        encoder->SetTexture(i, u.s_texture[i], texture, &sampler);
    }
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    if (stage < 0 || stage >= CKFF_MAX_TEXTURE_STAGES)
        return CKFFBuildSamplerDesc(nullptr);
    return CKFFBuildSamplerDesc(m_StageStates[stage]);
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_World[3][0] * m_View[0][2] +
              m_World[3][1] * m_View[1][2] +
              m_World[3][2] * m_View[2][2] +
              m_View[3][2];
    return z;
}
