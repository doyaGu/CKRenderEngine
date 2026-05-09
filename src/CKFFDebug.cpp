#include "CKFFDebug.h"
#include "CKDebugLogger.h"
#include "CKVertexLayoutCache.h"

#include <cmath>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool EnvEnabledRaw(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

static int EnvIntRaw(const char *name, int defaultValue) {
    const char *value = std::getenv(name);
    if (!value || value[0] == '\0')
        return defaultValue;

    char *end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value)
        return defaultValue;
    if (parsed < INT_MIN)
        return INT_MIN;
    if (parsed > INT_MAX)
        return INT_MAX;
    return (int)parsed;
}

const CKFFDebugConfig &CKFFDebugConfig::Get() {
    static const CKFFDebugConfig value = {
        EnvIntRaw("CK2_3D_DEBUG_DRAW_LOG_LIMIT", 0),
        EnvIntRaw("CK2_3D_DEBUG_REAL3D_LOG_LIMIT", 0),
        EnvIntRaw("CK2_3D_DEBUG_3D_CONTRACT_LOG_LIMIT", 0),
        EnvIntRaw("CK2_3D_DEBUG_POSITIONT_LOG_LIMIT", 0),
        EnvEnabledRaw("CK2_3D_DEBUG_DRAW_SERIAL_PER_FRAME")
    };
    return value;
}

CKFFDebugState::CKFFDebugState()
    : m_DrawLogCount(0),
      m_Real3DDrawLogCount(0),
      m_Real3DViewLogCount(0),
      m_PositionTDrawLogCount(0),
      m_Opaque3DDrawSerial(0),
      m_Transparent3DDrawSerial(0),
      m_3DContractLogCount(0) {
}

void CKFFDebugState::BeginFrame() {
    if (!CKFFDebugConfig::Get().DrawSerialPerFrame)
        return;

    m_Opaque3DDrawSerial = 0;
    m_Transparent3DDrawSerial = 0;
}

int CKFFDebugState::NextDrawSerial(CKRenderView view) {
    if (view == CKRP_VIEW_RENDERFIRST3D || view == CKRP_VIEW_OPAQUE3D)
        return m_Opaque3DDrawSerial++;
    if (view == CKRP_VIEW_TRANSPARENT)
        return m_Transparent3DDrawSerial++;
    return -1;
}

void CKFFDebugState::LogDrawPrimitiveHeader(const CKFFDrawDebugInfo &info) {
    const int limit = CKFFDebugConfig::Get().DrawLogLimit;
    if (limit > 0 && m_DrawLogCount < limit) {
        CK_LOG_FMT("FFPipeline", "DrawPrimitive: view=%d serial=%d verts=%d indices=%d flags=0x%X",
                   (int)info.View, info.DrawSerial, info.Data ? info.Data->VertexCount : 0,
                   info.IndexCount, info.Data ? info.Data->Flags : 0);
        ++m_DrawLogCount;
    }
}

void CKFFDebugState::LogDrawPrimitivePrepareFailed() {
    const int limit = CKFFDebugConfig::Get().DrawLogLimit;
    if (limit > 0 && m_DrawLogCount < limit) {
        CK_LOG("FFPipeline", "DrawPrimitive: Prepare FAILED");
        ++m_DrawLogCount;
    }
}

void CKFFDebugState::LogDrawPrimitiveProgramMissing() {
    const int limit = CKFFDebugConfig::Get().DrawLogLimit;
    if (limit > 0 && m_DrawLogCount < limit) {
        CK_LOG("FFPipeline", "DrawPrimitive: program == 0!");
        ++m_DrawLogCount;
    }
}

void CKFFDebugState::LogDrawPrimitiveDetails(const CKFFDrawDebugInfo &info) {
    if (!info.Data || !info.StateDesc || !info.DrawState || !info.World || !info.ViewMatrix || !info.Projection)
        return;

    const CKFFDebugConfig &config = CKFFDebugConfig::Get();
    if (Is3DView(info.View) && config.Real3DLogLimit > 0 && m_Real3DDrawLogCount < config.Real3DLogLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(info.FormatFlags);
        CK_LOG_FMT("FFPipeline",
                   "Real3D DrawPrimitive #%d: serial=%d view=%d type=%d(%s) verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u tex0=%u lightingRS=%u activeLights=%d program=%u",
                   m_Real3DDrawLogCount, info.DrawSerial, (int)info.View, (int)info.Type, PrimitiveName(info.Type),
                   info.Data->VertexCount, info.IndexCount, info.Data->Flags, info.FormatFlags, stride,
                   info.Stage0.Texture, info.DrawState->GetRenderState(VXRENDERSTATE_LIGHTING),
                   info.ActiveLightCount, info.Program);
        CK_LOG_FMT("FFPipeline",
                   "  contract VS: normal=%d color0=%d color1=%d tex0=%d lighting=%d lightCount=%u FSstage0=%u",
                   info.StateDesc->VS.GetHasNormal() ? 1 : 0,
                   info.StateDesc->VS.GetHasColor0() ? 1 : 0,
                   info.StateDesc->VS.GetHasColor1() ? 1 : 0,
                   info.StateDesc->VS.GetHasTexCoord0() ? 1 : 0,
                   info.StateDesc->VS.GetLightingEnabled() ? 1 : 0,
                   info.StateDesc->VS.GetLightCount(),
                   info.StateDesc->FS.GetStageColorOp(0));
        LogMatrixRows("World", *info.World);
        LogMatrixRows("View", *info.ViewMatrix);
        LogMatrixRows("Proj", *info.Projection);
        LogPrimitiveIndexContract(info.Type, info.Indices, info.IndexCount, info.Data);
        LogVertexClipSamples(*info.World, *info.ViewMatrix, *info.Projection, info.Data);
        ++m_Real3DDrawLogCount;
    }

    if (Is3DView(info.View) && config.Contract3DLogLimit > 0 && m_3DContractLogCount < config.Contract3DLogLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(info.FormatFlags);
        CK_LOG_FMT("FFPipeline",
                   "3D contract #%d: serial=%d view=%d path=DrawPrimitive type=%d(%s) verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u stateLighting=%d texCount=%d stage0C=%u/%u/%u stage0A=%u/%u/%u tex0=%u alpha=%u/%u/%u blend=%u/%u/%u z=%u/%u/%u cull=%u",
                   m_3DContractLogCount, info.DrawSerial, (int)info.View, (int)info.Type, PrimitiveName(info.Type),
                   info.Data->VertexCount, info.IndexCount, info.Data->Flags, info.FormatFlags, stride,
                   info.StateDesc->VS.GetLightingEnabled() ? 1 : 0, info.ActiveTextureCount,
                   info.Stage0.ColorOp, info.Stage0.ColorArg1, info.Stage0.ColorArg2,
                   info.Stage0.AlphaOp, info.Stage0.AlphaArg1, info.Stage0.AlphaArg2,
                   info.Stage0.Texture,
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHATESTENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHAFUNC),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHAREF),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_SRCBLEND),
                   info.DrawState->GetRenderState(VXRENDERSTATE_DESTBLEND),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZWRITEENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZFUNC),
                   info.DrawState->GetRenderState(VXRENDERSTATE_CULLMODE));
        LogPrimitiveIndexContract(info.Type, info.Indices, info.IndexCount, info.Data);
        LogVertexClipSamples(*info.World, *info.ViewMatrix, *info.Projection, info.Data);
        ++m_3DContractLogCount;
    }

    if ((info.FormatFlags & CKFF_VF_POSITIONT) &&
        config.PositionTLogLimit > 0 && m_PositionTDrawLogCount < config.PositionTLogLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(info.FormatFlags);
        CK_LOG_FMT("FFPipeline",
                   "PositionT DrawPrimitive #%d: view=%d type=%d verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u tex0=%u program=%u stage0=%u/%u/%u",
                   m_PositionTDrawLogCount, (int)info.View, (int)info.Type,
                   info.Data->VertexCount, info.IndexCount, info.Data->Flags, info.FormatFlags,
                   stride, info.Stage0.Texture, info.Program, info.Stage0.ColorOp,
                   info.Stage0.ColorArg1, info.Stage0.ColorArg2);
        LogPositionTSamples(info.Viewport, info.Data);
        ++m_PositionTDrawLogCount;
    }

    if (Is3DView(info.View) &&
        config.Real3DLogLimit > 0 && m_Real3DViewLogCount < config.Real3DLogLimit &&
        HasNonIdentityViewTranslation(*info.ViewMatrix)) {
        CK_LOG_FMT("FFPipeline", "Real3D non-identity view #%d: view=%d verts=%d indices=%d flags=0x%X program=%u",
                   m_Real3DViewLogCount, (int)info.View, info.Data->VertexCount, info.IndexCount,
                   info.Data->Flags, info.Program);
        LogMatrixRows("World", *info.World);
        LogMatrixRows("View", *info.ViewMatrix);
        LogMatrixRows("Proj", *info.Projection);
        LogVertexClipSamples(*info.World, *info.ViewMatrix, *info.Projection, info.Data);
        ++m_Real3DViewLogCount;
    }
}

void CKFFDebugState::LogDrawVertexBufferHeader(const CKFFDrawDebugInfo &info) {
    const int limit = CKFFDebugConfig::Get().DrawLogLimit;
    if (limit > 0 && m_DrawLogCount < limit) {
        CK_LOG_FMT("FFPipeline", "DrawVertexBuffer: view=%d vb=%u ib=%u verts=%u indices=%u layout=%u",
                   (int)info.View, info.VertexBuffer, info.IndexBuffer, info.VertexCount,
                   info.PersistentIndexCount, info.VertexLayout);
        if (info.World) {
            CK_LOG_FMT("FFPipeline", "  World row0: %.3f %.3f %.3f %.3f",
                       (*info.World)[0][0], (*info.World)[0][1], (*info.World)[0][2], (*info.World)[0][3]);
            CK_LOG_FMT("FFPipeline", "  World row1: %.3f %.3f %.3f %.3f",
                       (*info.World)[1][0], (*info.World)[1][1], (*info.World)[1][2], (*info.World)[1][3]);
            CK_LOG_FMT("FFPipeline", "  World row2: %.3f %.3f %.3f %.3f",
                       (*info.World)[2][0], (*info.World)[2][1], (*info.World)[2][2], (*info.World)[2][3]);
            CK_LOG_FMT("FFPipeline", "  World row3: %.3f %.3f %.3f %.3f",
                       (*info.World)[3][0], (*info.World)[3][1], (*info.World)[3][2], (*info.World)[3][3]);
        }
        ++m_DrawLogCount;
    }
}

void CKFFDebugState::LogDrawVertexBufferDetails(const CKFFDrawDebugInfo &info) {
    if (!info.StateDesc || !info.DrawState || !info.World || !info.ViewMatrix || !info.Projection)
        return;

    const CKFFDebugConfig &config = CKFFDebugConfig::Get();
    if (Is3DView(info.View) && config.Real3DLogLimit > 0 && m_Real3DDrawLogCount < config.Real3DLogLimit) {
        CK_LOG_FMT("FFPipeline",
                   "Real3D DrawVertexBuffer #%d: serial=%d view=%d type=%d(%s) vb=%u ib=%u base=%u verts=%u start=%u indices=%u dp=0x%X fmt=0x%X layout=%u tex0=%u lightingRS=%u activeLights=%d program=%u",
                   m_Real3DDrawLogCount, info.DrawSerial, (int)info.View, (int)info.Type, PrimitiveName(info.Type),
                   info.VertexBuffer, info.IndexBuffer, info.BaseVertex, info.VertexCount,
                   info.StartIndex, info.PersistentIndexCount, info.DPFlags, info.FormatFlags,
                   info.VertexLayout, info.Stage0.Texture,
                   info.DrawState->GetRenderState(VXRENDERSTATE_LIGHTING), info.ActiveLightCount, info.Program);
        CK_LOG_FMT("FFPipeline",
                   "  contract VS: normal=%d color0=%d color1=%d tex0=%d lighting=%d lightCount=%u FSstage0=%u",
                   info.StateDesc->VS.GetHasNormal() ? 1 : 0,
                   info.StateDesc->VS.GetHasColor0() ? 1 : 0,
                   info.StateDesc->VS.GetHasColor1() ? 1 : 0,
                   info.StateDesc->VS.GetHasTexCoord0() ? 1 : 0,
                   info.StateDesc->VS.GetLightingEnabled() ? 1 : 0,
                   info.StateDesc->VS.GetLightCount(),
                   info.StateDesc->FS.GetStageColorOp(0));
        LogMatrixRows("World", *info.World);
        LogMatrixRows("View", *info.ViewMatrix);
        LogMatrixRows("Proj", *info.Projection);
        ++m_Real3DDrawLogCount;
    }

    if (Is3DView(info.View) && config.Contract3DLogLimit > 0 && m_3DContractLogCount < config.Contract3DLogLimit) {
        CK_LOG_FMT("FFPipeline",
                   "3D contract #%d: serial=%d view=%d path=DrawVertexBuffer type=%d(%s) vb=%u ib=%u base=%u verts=%u start=%u indices=%u dp=0x%X fmt=0x%X layout=%u stateLighting=%d texCount=%d stage0C=%u/%u/%u stage0A=%u/%u/%u tex0=%u alpha=%u/%u/%u blend=%u/%u/%u z=%u/%u/%u cull=%u",
                   m_3DContractLogCount, info.DrawSerial, (int)info.View, (int)info.Type, PrimitiveName(info.Type),
                   info.VertexBuffer, info.IndexBuffer, info.BaseVertex, info.VertexCount,
                   info.StartIndex, info.PersistentIndexCount, info.DPFlags, info.FormatFlags,
                   info.VertexLayout, info.StateDesc->VS.GetLightingEnabled() ? 1 : 0,
                   info.ActiveTextureCount, info.Stage0.ColorOp, info.Stage0.ColorArg1,
                   info.Stage0.ColorArg2, info.Stage0.AlphaOp, info.Stage0.AlphaArg1,
                   info.Stage0.AlphaArg2, info.Stage0.Texture,
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHATESTENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHAFUNC),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHAREF),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_SRCBLEND),
                   info.DrawState->GetRenderState(VXRENDERSTATE_DESTBLEND),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZWRITEENABLE),
                   info.DrawState->GetRenderState(VXRENDERSTATE_ZFUNC),
                   info.DrawState->GetRenderState(VXRENDERSTATE_CULLMODE));
        LogMatrixRows("World", *info.World);
        LogMatrixRows("View", *info.ViewMatrix);
        LogMatrixRows("Proj", *info.Projection);
        ++m_3DContractLogCount;
    }

    if (Is3DView(info.View) &&
        config.Real3DLogLimit > 0 && m_Real3DViewLogCount < config.Real3DLogLimit &&
        HasNonIdentityViewTranslation(*info.ViewMatrix)) {
        CK_LOG_FMT("FFPipeline", "Real3D VB non-identity view #%d: view=%d vb=%u ib=%u verts=%u indices=%u layout=%u program=%u",
                   m_Real3DViewLogCount, (int)info.View, info.VertexBuffer,
                   info.IndexBuffer, info.VertexCount, info.PersistentIndexCount,
                   info.VertexLayout, info.Program);
        LogMatrixRows("World", *info.World);
        LogMatrixRows("View", *info.ViewMatrix);
        LogMatrixRows("Proj", *info.Projection);
        ++m_Real3DViewLogCount;
    }
}

bool CKFFDebugState::Is3DView(CKRenderView view) const {
    return view == CKRP_VIEW_RENDERFIRST3D || view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT;
}

bool CKFFDebugState::HasNonIdentityViewTranslation(const VxMatrix &view) const {
    return fabsf(view[3][0]) > 0.001f || fabsf(view[3][1]) > 0.001f || fabsf(view[3][2]) > 0.001f;
}

void CKFFDebugState::LogMatrixRows(const char *label, const VxMatrix &m) const {
    CK_LOG_FMT("FFPipeline", "  %s row0: %.6f %.6f %.6f %.6f", label, m[0][0], m[0][1], m[0][2], m[0][3]);
    CK_LOG_FMT("FFPipeline", "  %s row1: %.6f %.6f %.6f %.6f", label, m[1][0], m[1][1], m[1][2], m[1][3]);
    CK_LOG_FMT("FFPipeline", "  %s row2: %.6f %.6f %.6f %.6f", label, m[2][0], m[2][1], m[2][2], m[2][3]);
    CK_LOG_FMT("FFPipeline", "  %s row3: %.6f %.6f %.6f %.6f", label, m[3][0], m[3][1], m[3][2], m[3][3]);
}

void CKFFDebugState::LogVertexClipSamples(const VxMatrix &world, const VxMatrix &view,
                                          const VxMatrix &proj, const VxDrawPrimitiveData *data) const {
    if (!data || !data->PositionPtr) return;

    VxMatrix viewProj, mvp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            viewProj[r][c] = view[r][0] * proj[0][c] + view[r][1] * proj[1][c] +
                             view[r][2] * proj[2][c] + view[r][3] * proj[3][c];
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            mvp[r][c] = world[r][0] * viewProj[0][c] + world[r][1] * viewProj[1][c] +
                        world[r][2] * viewProj[2][c] + world[r][3] * viewProj[3][c];
        }
    }
    LogMatrixRows("CPU MVP", mvp);

    int samples = data->VertexCount < 3 ? data->VertexCount : 3;
    float minNdc[3] = { 1e30f, 1e30f, 1e30f };
    float maxNdc[3] = { -1e30f, -1e30f, -1e30f };
    int finiteCount = 0;
    for (int i = 0; i < data->VertexCount; ++i) {
        const float *p = (const float *)((const CKBYTE *)data->PositionPtr + i * data->PositionStride);
        float x = p[0], y = p[1], z = p[2];
        float cx = x * mvp[0][0] + y * mvp[1][0] + z * mvp[2][0] + mvp[3][0];
        float cy = x * mvp[0][1] + y * mvp[1][1] + z * mvp[2][1] + mvp[3][1];
        float cz = x * mvp[0][2] + y * mvp[1][2] + z * mvp[2][2] + mvp[3][2];
        float cw = x * mvp[0][3] + y * mvp[1][3] + z * mvp[2][3] + mvp[3][3];
        if (fabsf(cw) <= 0.000001f)
            continue;
        float nx = cx / cw;
        float ny = cy / cw;
        float nz = cz / cw;
        if (!isfinite(nx) || !isfinite(ny) || !isfinite(nz))
            continue;
        if (nx < minNdc[0]) minNdc[0] = nx;
        if (ny < minNdc[1]) minNdc[1] = ny;
        if (nz < minNdc[2]) minNdc[2] = nz;
        if (nx > maxNdc[0]) maxNdc[0] = nx;
        if (ny > maxNdc[1]) maxNdc[1] = ny;
        if (nz > maxNdc[2]) maxNdc[2] = nz;
        ++finiteCount;
    }
    CK_LOG_FMT("FFPipeline",
               "  ndc bounds vertices=%d finite=%d min=(%.3f %.3f %.3f) max=(%.3f %.3f %.3f)",
               data->VertexCount, finiteCount, minNdc[0], minNdc[1], minNdc[2],
               maxNdc[0], maxNdc[1], maxNdc[2]);

    if (data->TexCoordPtr) {
        float minUv[2] = { 1e30f, 1e30f };
        float maxUv[2] = { -1e30f, -1e30f };
        for (int i = 0; i < data->VertexCount; ++i) {
            const float *uv = (const float *)((const CKBYTE *)data->TexCoordPtr + i * data->TexCoordStride);
            if (!isfinite(uv[0]) || !isfinite(uv[1]))
                continue;
            if (uv[0] < minUv[0]) minUv[0] = uv[0];
            if (uv[1] < minUv[1]) minUv[1] = uv[1];
            if (uv[0] > maxUv[0]) maxUv[0] = uv[0];
            if (uv[1] > maxUv[1]) maxUv[1] = uv[1];
        }
        CK_LOG_FMT("FFPipeline",
                   "  uv0 stride=%d min=(%.3f %.3f) max=(%.3f %.3f)",
                   data->TexCoordStride, minUv[0], minUv[1], maxUv[0], maxUv[1]);
    } else {
        CK_LOG("FFPipeline", "  uv0 missing");
    }

    for (int i = 0; i < samples; ++i) {
        const float *p = (const float *)((const CKBYTE *)data->PositionPtr + i * data->PositionStride);
        float x = p[0], y = p[1], z = p[2];
        float cx = x * mvp[0][0] + y * mvp[1][0] + z * mvp[2][0] + mvp[3][0];
        float cy = x * mvp[0][1] + y * mvp[1][1] + z * mvp[2][1] + mvp[3][1];
        float cz = x * mvp[0][2] + y * mvp[1][2] + z * mvp[2][2] + mvp[3][2];
        float cw = x * mvp[0][3] + y * mvp[1][3] + z * mvp[2][3] + mvp[3][3];
        float invw = (fabsf(cw) > 0.000001f) ? (1.0f / cw) : 0.0f;
        CK_LOG_FMT("FFPipeline",
                   "  v%d pos=(%.6f %.6f %.6f) clip=(%.6f %.6f %.6f %.6f) ndc=(%.6f %.6f %.6f)",
                   i, x, y, z, cx, cy, cz, cw, cx * invw, cy * invw, cz * invw);
    }
}

void CKFFDebugState::LogPrimitiveIndexContract(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                                               const VxDrawPrimitiveData *data) const {
    if (!data)
        return;

    const int srcCount = (indices && indexCount > 0) ? indexCount : data->VertexCount;
    int triCount = 0;
    if (type == VX_TRIANGLELIST)
        triCount = srcCount / 3;
    else if (type == VX_TRIANGLESTRIP || type == VX_TRIANGLEFAN)
        triCount = srcCount >= 3 ? srcCount - 2 : 0;

    int minIndex = INT_MAX;
    int maxIndex = INT_MIN;
    int invalidCount = 0;
    int duplicateTriCount = 0;
    for (int i = 0; i < srcCount; ++i) {
        int idx = indices ? indices[i] : i;
        if (idx < minIndex) minIndex = idx;
        if (idx > maxIndex) maxIndex = idx;
        if (idx < 0 || idx >= data->VertexCount)
            ++invalidCount;
    }

    if (type == VX_TRIANGLELIST && srcCount >= 3) {
        for (int i = 0; i + 2 < srcCount; i += 3) {
            int a = indices ? indices[i] : i;
            int b = indices ? indices[i + 1] : i + 1;
            int c = indices ? indices[i + 2] : i + 2;
            if (a == b || b == c || a == c)
                ++duplicateTriCount;
        }
    }

    if (srcCount == 0) {
        minIndex = 0;
        maxIndex = 0;
    }

    CK_LOG_FMT("FFPipeline",
               "  index contract: srcCount=%d indexed=%d min=%d max=%d invalid=%d tris=%d degenerateListTris=%d",
               srcCount, indices ? 1 : 0, minIndex, maxIndex, invalidCount, triCount, duplicateTriCount);

    if (srcCount > 0) {
        char sample[160];
        int written = 0;
        int sampleCount = srcCount < 12 ? srcCount : 12;
        for (int i = 0; i < sampleCount && written < (int)sizeof(sample) - 8; ++i) {
            int idx = indices ? indices[i] : i;
            int n = std::snprintf(sample + written, sizeof(sample) - written,
                                  "%s%d", i ? "," : "", idx);
            if (n <= 0)
                break;
            written += n;
        }
        sample[sizeof(sample) - 1] = '\0';
        CK_LOG_FMT("FFPipeline", "  first indices: %s", sample);
    }
}

void CKFFDebugState::LogPositionTSamples(const float *viewport, const VxDrawPrimitiveData *data) const {
    if (!viewport || !data || !data->PositionPtr) return;

    CK_LOG_FMT("FFPipeline",
               "  PositionT viewport sx=%.8f sy=%.8f ox=%.3f oy=%.3f posStride=%u color=%s uv=%s",
               viewport[0], viewport[1], viewport[2], viewport[3],
               data->PositionStride,
               data->ColorPtr ? "yes" : "no",
               data->TexCoordPtr ? "yes" : "no");

    int samples = data->VertexCount < 4 ? data->VertexCount : 4;
    for (int i = 0; i < samples; ++i) {
        const float *p = (const float *)((const CKBYTE *)data->PositionPtr + i * data->PositionStride);
        float rhw = fabsf(p[3]) > 0.000001f ? p[3] : 1.0f;
        float clipW = 1.0f / rhw;
        float ndcX = p[0] * viewport[0] + viewport[2];
        float ndcY = p[1] * viewport[1] + viewport[3];
        float ndcZ = p[2];
        CKDWORD color = 0;
        if (data->ColorPtr)
            memcpy(&color, (const CKBYTE *)data->ColorPtr + i * data->ColorStride, 4);
        CK_LOG_FMT("FFPipeline",
                   "  pt%d screen=(%.3f %.3f %.3f %.6f) ndc=(%.3f %.3f %.3f) clip=(%.3f %.3f %.3f %.3f) color=0x%08X",
                   i, p[0], p[1], p[2], p[3], ndcX, ndcY, ndcZ,
                   ndcX * clipW, ndcY * clipW, ndcZ * clipW, clipW, color);
    }
}

const char *CKFFDebugState::PrimitiveName(VXPRIMITIVETYPE type) const {
    switch (type) {
    case VX_POINTLIST: return "POINTLIST";
    case VX_LINELIST: return "LINELIST";
    case VX_LINESTRIP: return "LINESTRIP";
    case VX_TRIANGLELIST: return "TRIANGLELIST";
    case VX_TRIANGLESTRIP: return "TRIANGLESTRIP";
    case VX_TRIANGLEFAN: return "TRIANGLEFAN";
    default: return "UNKNOWN";
    }
}
