#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cstdio>

static int g_DrawLogCount = 0;
static int g_Real3DDrawLogCount = 0;
static int g_Real3DViewLogCount = 0;
static int g_PositionTDrawLogCount = 0;
static int g_Opaque3DDrawSerial = 0;
static int g_Transparent3DDrawSerial = 0;
static int g_3DContractLogCount = 0;

static void LogMatrixRows(const char *label, const VxMatrix &m) {
    CK_LOG_FMT("FFPipeline", "  %s row0: %.6f %.6f %.6f %.6f", label, m[0][0], m[0][1], m[0][2], m[0][3]);
    CK_LOG_FMT("FFPipeline", "  %s row1: %.6f %.6f %.6f %.6f", label, m[1][0], m[1][1], m[1][2], m[1][3]);
    CK_LOG_FMT("FFPipeline", "  %s row2: %.6f %.6f %.6f %.6f", label, m[2][0], m[2][1], m[2][2], m[2][3]);
    CK_LOG_FMT("FFPipeline", "  %s row3: %.6f %.6f %.6f %.6f", label, m[3][0], m[3][1], m[3][2], m[3][3]);
}

static void MulMatrix(VxMatrix &dst, const VxMatrix &a, const VxMatrix &b) {
    VxMatrix tmp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp[r][c] = a[r][0] * b[0][c] +
                        a[r][1] * b[1][c] +
                        a[r][2] * b[2][c] +
                        a[r][3] * b[3][c];
        }
    }
    dst = tmp;
}

static void TransposeMatrix(VxMatrix &dst, const VxMatrix &src) {
    VxMatrix tmp;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            tmp[r][c] = src[c][r];
        }
    }
    dst = tmp;
}

static CKDWORD BaseTextureArg(CKDWORD arg) {
    return arg & ~(CKRST_TA_COMPLEMENT | CKRST_TA_ALPHAREPLICATE);
}

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

struct DebugDrawRange {
    int Exact;
    int Start;
    int End;
};

static int DebugDrawLogLimit() {
    static const int value = EnvIntRaw("CK2_3D_DEBUG_DRAW_LOG_LIMIT", 0);
    return value;
}

static int DebugReal3DLogLimit() {
    static const int value = EnvIntRaw("CK2_3D_DEBUG_REAL3D_LOG_LIMIT", 0);
    return value;
}

static int Debug3DContractLogLimit() {
    static const int value = EnvIntRaw("CK2_3D_DEBUG_3D_CONTRACT_LOG_LIMIT", 0);
    return value;
}

static int DebugPositionTLogLimit() {
    static const int value = EnvIntRaw("CK2_3D_DEBUG_POSITIONT_LOG_LIMIT", 0);
    return value;
}

static bool DebugDrawSerialPerFrame() {
    static const bool enabled = EnvEnabledRaw("CK2_3D_DEBUG_DRAW_SERIAL_PER_FRAME");
    return enabled;
}

static bool DebugSkipPositionTDraws() {
    static const bool enabled = EnvEnabledRaw("CK2_3D_DEBUG_SKIP_POSITIONT_DRAWS");
    return enabled;
}

static bool DebugSkip3DDraws() {
    static const bool enabled = EnvEnabledRaw("CK2_3D_DEBUG_SKIP_3D_DRAWS");
    return enabled;
}

static bool DebugForceUnlit() {
    static const bool enabled = EnvEnabledRaw("CK2_3D_DEBUG_FORCE_UNLIT");
    return enabled;
}

static bool DebugDisableFog() {
    static const bool enabled = EnvEnabledRaw("CK2_3D_DEBUG_DISABLE_FOG");
    return enabled;
}

static const DebugDrawRange &Opaque3DDebugDrawRange() {
    static const DebugDrawRange value = {
        EnvIntRaw("CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_EXACT", -1),
        EnvIntRaw("CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_START", 0),
        EnvIntRaw("CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_END", INT_MAX)
    };
    return value;
}

static const DebugDrawRange &Transparent3DDebugDrawRange() {
    static const DebugDrawRange value = {
        EnvIntRaw("CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_EXACT", -1),
        EnvIntRaw("CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_START", 0),
        EnvIntRaw("CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_END", INT_MAX)
    };
    return value;
}

static bool ShouldSkipDebugView(CKRenderView view) {
    static const bool skipOpaque3D = EnvEnabledRaw("CK2_3D_DEBUG_SKIP_OPAQUE3D_DRAWS");
    static const bool skipTransparent3D = EnvEnabledRaw("CK2_3D_DEBUG_SKIP_TRANSPARENT3D_DRAWS");
    return (view == CKRP_VIEW_OPAQUE3D && skipOpaque3D) ||
           (view == CKRP_VIEW_TRANSPARENT && skipTransparent3D);
}

static int NextDebugDrawSerial(CKRenderView view) {
    if (view == CKRP_VIEW_OPAQUE3D)
        return g_Opaque3DDrawSerial++;
    if (view == CKRP_VIEW_TRANSPARENT)
        return g_Transparent3DDrawSerial++;
    return -1;
}

static bool ShouldSkipDebugDrawSerial(CKRenderView view, int drawSerial) {
    if (drawSerial < 0)
        return false;

    const DebugDrawRange *range = nullptr;
    if (view == CKRP_VIEW_OPAQUE3D) {
        range = &Opaque3DDebugDrawRange();
    } else if (view == CKRP_VIEW_TRANSPARENT) {
        range = &Transparent3DDebugDrawRange();
    } else {
        return false;
    }

    if (range->Exact >= 0)
        return drawSerial != range->Exact;
    return drawSerial < range->Start || drawSerial > range->End;
}

static const char *PrimitiveName(VXPRIMITIVETYPE type) {
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

CKDWORD CKFFLegacyTextureBlendToColorOp(CKDWORD blend) {
    switch (blend & VXTEXTUREBLEND_MASK) {
    case VXTEXTUREBLEND_DECAL:
    case VXTEXTUREBLEND_COPY:
    case VXTEXTUREBLEND_DECALMASK:
        return CKRST_TOP_SELECTARG1;
    case VXTEXTUREBLEND_DECALALPHA:
        return CKRST_TOP_BLENDTEXTUREALPHA;
    case VXTEXTUREBLEND_ADD:
        return CKRST_TOP_ADD;
    case VXTEXTUREBLEND_DOTPRODUCT3:
        return CKRST_TOP_DOTPRODUCT3;
    case VXTEXTUREBLEND_MODULATE:
    case VXTEXTUREBLEND_MODULATEALPHA:
    case VXTEXTUREBLEND_MODULATEMASK:
    default:
        return CKRST_TOP_MODULATE;
    }
}

CKDWORD CKFFLegacyTextureBlendToAlphaOp(CKDWORD blend) {
    switch (blend & VXTEXTUREBLEND_MASK) {
    case VXTEXTUREBLEND_DECALALPHA:
        return CKRST_TOP_SELECTARG2;
    case VXTEXTUREBLEND_MODULATEALPHA:
        return CKRST_TOP_MODULATE;
    case VXTEXTUREBLEND_DECAL:
    case VXTEXTUREBLEND_COPY:
    case VXTEXTUREBLEND_DECALMASK:
    case VXTEXTUREBLEND_MODULATE:
    case VXTEXTUREBLEND_MODULATEMASK:
    case VXTEXTUREBLEND_ADD:
    case VXTEXTUREBLEND_DOTPRODUCT3:
    default:
        return CKRST_TOP_SELECTARG1;
    }
}

static CKDWORD GetStageColorOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_OP];
    if (op != 0)
        return op;
    if (!hasTexture)
        return CKRST_TOP_DISABLE;
    return CKFFLegacyTextureBlendToColorOp(stage[CKRST_TSS_TEXTUREMAPBLEND]);
}

static CKDWORD GetStageAlphaOp(const CKDWORD *stage, bool stageActive, bool hasTexture) {
    if (!stageActive)
        return CKRST_TOP_DISABLE;

    CKDWORD op = stage[CKRST_TSS_AOP];
    if (op != 0)
        return op;
    return hasTexture ? CKFFLegacyTextureBlendToAlphaOp(stage[CKRST_TSS_TEXTUREMAPBLEND]) : CKRST_TOP_DISABLE;
}

static CKDWORD GetStageColorArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_ARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKRST_TA_TEXTURE : CKRST_TA_DIFFUSE;
}

static CKDWORD GetStageColorArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_ARG2];
    return arg != 0 ? arg : CKRST_TA_CURRENT;
}

static CKDWORD GetStageAlphaArg1(const CKDWORD *stage, bool hasTexture) {
    CKDWORD arg = stage[CKRST_TSS_AARG1];
    if (arg != 0)
        return arg;
    return hasTexture ? CKRST_TA_TEXTURE : CKRST_TA_DIFFUSE;
}

static CKDWORD GetStageAlphaArg2(const CKDWORD *stage) {
    CKDWORD arg = stage[CKRST_TSS_AARG2];
    return arg != 0 ? arg : CKRST_TA_CURRENT;
}

static int GetActiveTextureCountFromDPFlags(CKDWORD dpFlags) {
    int count = (int)CKRST_DP_STAGEFLAGS(dpFlags);
    if (count < 0) count = 0;
    if (count > CKFF_MAX_TEXTURE_STAGES) count = CKFF_MAX_TEXTURE_STAGES;
    return count;
}

static CKDWORD ResolveMaterialSource(CKBOOL lighting, CKBOOL colorVertex, CKBOOL fromVertex,
                                     CKDWORD dpFlags, CKDWORD streamFlag,
                                     CKFFMaterialSource vertexSource) {
    (void)lighting;
    if (!colorVertex || !fromVertex || ((dpFlags & streamFlag) == 0))
        return CKFF_MS_MATERIAL;
    return vertexSource;
}

static float EncodeShaderLightType(VXLIGHT_TYPE type) {
    switch (type) {
    case VX_LIGHTDIREC: return 0.0f;
    case VX_LIGHTSPOT:  return 2.0f;
    case VX_LIGHTPOINT:
    default:            return 1.0f;
    }
}

static void LogVertexClipSamples(const VxMatrix &world, const VxMatrix &view, const VxMatrix &proj,
                                 const VxDrawPrimitiveData *data) {
    if (!data || !data->PositionPtr) return;

    VxMatrix viewProj, mvp;
    MulMatrix(viewProj, view, proj);
    MulMatrix(mvp, world, viewProj);
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

static void LogPrimitiveIndexContract(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                                      const VxDrawPrimitiveData *data) {
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

static void LogPositionTSamples(const float *viewport, const VxDrawPrimitiveData *data) {
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

CKFixedFunctionPipeline::CKFixedFunctionPipeline()
    : m_Context(nullptr), m_ActiveLightCount(0), m_CurrentActiveTextureCount(0),
      m_CurrentLightingEnabled(false), m_DirtyFlags(CKFF_DIRTY_ALL) {
    Vx3DMatrixIdentity(m_World);
    Vx3DMatrixIdentity(m_View);
    Vx3DMatrixIdentity(m_Projection);
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++)
        Vx3DMatrixIdentity(m_TexMatrix[i]);
    memset(&m_Material, 0, sizeof(m_Material));
    m_Material.Diffuse[0] = m_Material.Diffuse[1] = m_Material.Diffuse[2] = m_Material.Diffuse[3] = 1.0f;
    m_Material.Ambient[0] = m_Material.Ambient[1] = m_Material.Ambient[2] = 1.0f;
    m_Material.Ambient[3] = 1.0f;
    memset(m_Lights, 0, sizeof(m_Lights));
    memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
    memset(m_TextureHandles, 0, sizeof(m_TextureHandles));
    memset(m_StageStates, 0, sizeof(m_StageStates));
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
    dst.Position[3] = EncodeShaderLightType(light->Type);

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

void CKFixedFunctionPipeline::BeginDebugFrame() {
    if (!DebugDrawSerialPerFrame())
        return;

    g_Opaque3DDrawSerial = 0;
    g_Transparent3DDrawSerial = 0;
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
    const int debugDrawSerial = NextDebugDrawSerial(view);
    const int drawLogLimit = DebugDrawLogLimit();

    if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
        CK_LOG_FMT("FFPipeline", "DrawPrimitive: view=%d serial=%d verts=%d indices=%d flags=0x%X",
                   (int)view, debugDrawSerial, data->VertexCount, indexCount, data->Flags);
        ++g_DrawLogCount;
    }

    if (ShouldSkipDebugView(view) ||
        ShouldSkipDebugDrawSerial(view, debugDrawSerial) ||
        (positionT && DebugSkipPositionTDraws()) ||
        (!positionT && DebugSkip3DDraws())) {
        if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
            CK_LOG_FMT("FFPipeline",
                       "DrawPrimitive skipped by env: view=%d serial=%d type=%d verts=%d indices=%d flags=0x%X fmt=0x%X positionT=%d",
                       (int)view, debugDrawSerial, (int)type, data->VertexCount, indexCount,
                       data->Flags, formatFlags, positionT ? 1 : 0);
            ++g_DrawLogCount;
        }
        return;
    }

    // Prepare transient geometry
    if (!m_TransientGeometry.Prepare(encoder, type, indices, indexCount, data)) {
        if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
            CK_LOG("FFPipeline", "DrawPrimitive: Prepare FAILED");
            g_DrawLogCount++;
        }
        return;
    }

    // Build shader key and get program
    m_CurrentActiveTextureCount = GetActiveTextureCountFromDPFlags(data->Flags);
    CKFFShaderKey key = BuildCurrentKey(data->Flags, formatFlags);
    CKDWORD program = m_ShaderCache.GetProgram(key);
    if (program == 0) {
        if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
            CK_LOG("FFPipeline", "DrawPrimitive: program == 0!");
            g_DrawLogCount++;
        }
        return;
    }

    const int real3DLogLimit = DebugReal3DLogLimit();
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        real3DLogLimit > 0 && g_Real3DDrawLogCount < real3DLogLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(formatFlags);
        CK_LOG_FMT("FFPipeline",
                   "Real3D DrawPrimitive #%d: serial=%d view=%d type=%d(%s) verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u tex0=%u lightingRS=%u activeLights=%d program=%u",
                   g_Real3DDrawLogCount, debugDrawSerial, (int)view, (int)type, PrimitiveName(type), data->VertexCount, indexCount,
                   data->Flags, formatFlags, stride, m_TextureHandles[0],
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING), m_ActiveLightCount, program);
        CK_LOG_FMT("FFPipeline",
                   "  key VS: normal=%d color0=%d color1=%d tex0=%d lighting=%d lightCount=%u FSstage0=%u",
                   key.VS.GetHasNormal() ? 1 : 0, key.VS.GetHasColor0() ? 1 : 0,
                   key.VS.GetHasColor1() ? 1 : 0, key.VS.GetHasTexCoord0() ? 1 : 0,
                   key.VS.GetLightingEnabled() ? 1 : 0, key.VS.GetLightCount(),
                   key.FS.GetStageColorOp(0));
        LogMatrixRows("World", m_World);
        LogMatrixRows("View", m_View);
        LogMatrixRows("Proj", m_Projection);
        LogPrimitiveIndexContract(type, indices, indexCount, data);
        LogVertexClipSamples(m_World, m_View, m_Projection, data);
        g_Real3DDrawLogCount++;
    }
    const int contractLimit = Debug3DContractLogLimit();
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        contractLimit > 0 && g_3DContractLogCount < contractLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(formatFlags);
        CK_LOG_FMT("FFPipeline",
                   "3D contract #%d: serial=%d view=%d path=DrawPrimitive type=%d(%s) verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u keyLighting=%d texCount=%d stage0C=%u/%u/%u stage0A=%u/%u/%u tex0=%u alpha=%u/%u/%u blend=%u/%u/%u z=%u/%u/%u cull=%u",
                   g_3DContractLogCount, debugDrawSerial, (int)view, (int)type, PrimitiveName(type),
                   data->VertexCount, indexCount, data->Flags, formatFlags, stride,
                   key.VS.GetLightingEnabled() ? 1 : 0, m_CurrentActiveTextureCount,
                   key.FS.GetStageColorOp(0),
                   GetStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0),
                   GetStageColorArg2(m_StageStates[0]),
                   GetStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0),
                   GetStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0),
                   GetStageAlphaArg2(m_StageStates[0]),
                   m_TextureHandles[0],
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_SRCBLEND),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_DESTBLEND),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZWRITEENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZFUNC),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_CULLMODE));
        LogPrimitiveIndexContract(type, indices, indexCount, data);
        LogVertexClipSamples(m_World, m_View, m_Projection, data);
        ++g_3DContractLogCount;
    }
    const int positionTLogLimit = DebugPositionTLogLimit();
    if ((formatFlags & CKFF_VF_POSITIONT) &&
        positionTLogLimit > 0 && g_PositionTDrawLogCount < positionTLogLimit) {
        CKDWORD stride = CKVertexLayoutCache::ComputeStride(formatFlags);
        CK_LOG_FMT("FFPipeline",
                   "PositionT DrawPrimitive #%d: view=%d type=%d verts=%d indices=%d flags=0x%X fmt=0x%X stride=%u tex0=%u program=%u stage0=%u/%u/%u",
                   g_PositionTDrawLogCount, (int)view, (int)type, data->VertexCount, indexCount,
                   data->Flags, formatFlags, stride, m_TextureHandles[0], program,
                   key.FS.GetStageColorOp(0), GetStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0),
                   GetStageColorArg2(m_StageStates[0]));
        LogPositionTSamples(m_Viewport, data);
        g_PositionTDrawLogCount++;
    }
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        real3DLogLimit > 0 && g_Real3DViewLogCount < real3DLogLimit &&
        (fabsf(m_View[3][0]) > 0.001f || fabsf(m_View[3][1]) > 0.001f || fabsf(m_View[3][2]) > 0.001f)) {
        CK_LOG_FMT("FFPipeline", "Real3D non-identity view #%d: view=%d verts=%d indices=%d flags=0x%X program=%u",
                   g_Real3DViewLogCount, (int)view, data->VertexCount, indexCount, data->Flags, program);
        LogMatrixRows("World", m_World);
        LogMatrixRows("View", m_View);
        LogMatrixRows("Proj", m_Projection);
        LogVertexClipSamples(m_World, m_View, m_Projection, data);
        g_Real3DViewLogCount++;
    }

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(
        (type == VX_TRIANGLEFAN || type == VX_TRIANGLESTRIP) ? VX_TRIANGLELIST : type);
    encoder->SetState(drawState);

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

    const int drawLogLimit = DebugDrawLogLimit();
    if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
        CK_LOG_FMT("FFPipeline", "DrawVertexBuffer: view=%d vb=%u ib=%u verts=%u indices=%u layout=%u",
                   (int)view, vb, ib, vertexCount, indexCount, vertexLayout);
        CK_LOG_FMT("FFPipeline", "  World row0: %.3f %.3f %.3f %.3f",
                   m_World[0][0], m_World[0][1], m_World[0][2], m_World[0][3]);
        CK_LOG_FMT("FFPipeline", "  World row1: %.3f %.3f %.3f %.3f",
                   m_World[1][0], m_World[1][1], m_World[1][2], m_World[1][3]);
        CK_LOG_FMT("FFPipeline", "  World row2: %.3f %.3f %.3f %.3f",
                   m_World[2][0], m_World[2][1], m_World[2][2], m_World[2][3]);
        CK_LOG_FMT("FFPipeline", "  World row3: %.3f %.3f %.3f %.3f",
                   m_World[3][0], m_World[3][1], m_World[3][2], m_World[3][3]);
        g_DrawLogCount++;
    }

    // Build shader key from the actual mesh vertex format.
    const bool positionT = (formatFlags & CKFF_VF_POSITIONT) != 0;
    const int debugDrawSerial = NextDebugDrawSerial(view);
    if (ShouldSkipDebugView(view) ||
        ShouldSkipDebugDrawSerial(view, debugDrawSerial) ||
        (positionT && DebugSkipPositionTDraws()) ||
        (!positionT && DebugSkip3DDraws())) {
        if (drawLogLimit > 0 && g_DrawLogCount < drawLogLimit) {
            CK_LOG_FMT("FFPipeline",
                       "DrawVertexBuffer skipped by env: view=%d serial=%d type=%d vb=%u ib=%u verts=%u indices=%u dp=0x%X fmt=0x%X positionT=%d",
                       (int)view, debugDrawSerial, (int)type, vb, ib, vertexCount, indexCount,
                       dpFlags, formatFlags, positionT ? 1 : 0);
            ++g_DrawLogCount;
        }
        return;
    }

    m_CurrentActiveTextureCount = GetActiveTextureCountFromDPFlags(dpFlags);
    CKFFShaderKey key = BuildCurrentKey(dpFlags, formatFlags);
    CKDWORD program = m_ShaderCache.GetProgram(key);
    if (program == 0) return;

    const int real3DLogLimit = DebugReal3DLogLimit();
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        real3DLogLimit > 0 && g_Real3DDrawLogCount < real3DLogLimit) {
        CK_LOG_FMT("FFPipeline",
                   "Real3D DrawVertexBuffer #%d: serial=%d view=%d type=%d(%s) vb=%u ib=%u base=%u verts=%u start=%u indices=%u dp=0x%X fmt=0x%X layout=%u tex0=%u lightingRS=%u activeLights=%d program=%u",
                   g_Real3DDrawLogCount, debugDrawSerial, (int)view, (int)type, PrimitiveName(type), vb, ib, baseVertex, vertexCount,
                   startIndex, indexCount, dpFlags, formatFlags, vertexLayout, m_TextureHandles[0],
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING), m_ActiveLightCount, program);
        CK_LOG_FMT("FFPipeline",
                   "  key VS: normal=%d color0=%d color1=%d tex0=%d lighting=%d lightCount=%u FSstage0=%u",
                   key.VS.GetHasNormal() ? 1 : 0, key.VS.GetHasColor0() ? 1 : 0,
                   key.VS.GetHasColor1() ? 1 : 0, key.VS.GetHasTexCoord0() ? 1 : 0,
                   key.VS.GetLightingEnabled() ? 1 : 0, key.VS.GetLightCount(),
                   key.FS.GetStageColorOp(0));
        LogMatrixRows("World", m_World);
        LogMatrixRows("View", m_View);
        LogMatrixRows("Proj", m_Projection);
        g_Real3DDrawLogCount++;
    }
    const int contractLimit = Debug3DContractLogLimit();
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        contractLimit > 0 && g_3DContractLogCount < contractLimit) {
        CK_LOG_FMT("FFPipeline",
                   "3D contract #%d: serial=%d view=%d path=DrawVertexBuffer type=%d(%s) vb=%u ib=%u base=%u verts=%u start=%u indices=%u dp=0x%X fmt=0x%X layout=%u keyLighting=%d texCount=%d stage0C=%u/%u/%u stage0A=%u/%u/%u tex0=%u alpha=%u/%u/%u blend=%u/%u/%u z=%u/%u/%u cull=%u",
                   g_3DContractLogCount, debugDrawSerial, (int)view, (int)type, PrimitiveName(type),
                   vb, ib, baseVertex, vertexCount, startIndex, indexCount, dpFlags, formatFlags, vertexLayout,
                   key.VS.GetLightingEnabled() ? 1 : 0, m_CurrentActiveTextureCount,
                   key.FS.GetStageColorOp(0),
                   GetStageColorArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0),
                   GetStageColorArg2(m_StageStates[0]),
                   GetStageAlphaOp(m_StageStates[0], m_CurrentActiveTextureCount > 0, m_TextureHandles[0] != 0),
                   GetStageAlphaArg1(m_StageStates[0], m_CurrentActiveTextureCount > 0 && m_TextureHandles[0] != 0),
                   GetStageAlphaArg2(m_StageStates[0]),
                   m_TextureHandles[0],
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHABLENDENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_SRCBLEND),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_DESTBLEND),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZWRITEENABLE),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_ZFUNC),
                   m_DrawStateCache.GetRenderState(VXRENDERSTATE_CULLMODE));
        LogMatrixRows("World", m_World);
        LogMatrixRows("View", m_View);
        LogMatrixRows("Proj", m_Projection);
        ++g_3DContractLogCount;
    }
    if ((view == CKRP_VIEW_OPAQUE3D || view == CKRP_VIEW_TRANSPARENT) &&
        real3DLogLimit > 0 && g_Real3DViewLogCount < real3DLogLimit &&
        (fabsf(m_View[3][0]) > 0.001f || fabsf(m_View[3][1]) > 0.001f || fabsf(m_View[3][2]) > 0.001f)) {
        CK_LOG_FMT("FFPipeline", "Real3D VB non-identity view #%d: view=%d vb=%u ib=%u verts=%u indices=%u layout=%u program=%u",
                   g_Real3DViewLogCount, (int)view, vb, ib, vertexCount, indexCount, vertexLayout, program);
        LogMatrixRows("World", m_World);
        LogMatrixRows("View", m_View);
        LogMatrixRows("Proj", m_Projection);
        g_Real3DViewLogCount++;
    }

    // Upload uniforms
    UploadUniforms(encoder);

    // Set world transform
    CKDWORD transformIdx = m_Context->AllocTransform(&m_World, 1);
    encoder->SetTransform(transformIdx, 1);

    // Set draw state
    CKDrawState drawState = m_DrawStateCache.BuildDrawState(type);
    encoder->SetState(drawState);

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

CKFFShaderKey CKFixedFunctionPipeline::BuildCurrentKey(CKDWORD dpFlags, CKDWORD formatFlags) {
    CKFFShaderKey key;

    const bool hasFormat = formatFlags != 0;
    const bool positionT = hasFormat ? ((formatFlags & CKFF_VF_POSITIONT) != 0) : ((dpFlags & CKRST_DP_TRANSFORM) == 0);

    // Vertex key
    key.VS.SetHasPosition(!positionT);
    key.VS.SetHasPositionT(positionT);
    key.VS.SetHasNormal(hasFormat ? ((formatFlags & CKFF_VF_NORMAL) != 0) : ((dpFlags & CKRST_DP_LIGHT) != 0));
    key.VS.SetHasColor0(hasFormat ? ((formatFlags & CKFF_VF_COLOR0) != 0) : ((dpFlags & CKRST_DP_DIFFUSE) != 0));
    key.VS.SetHasColor1(hasFormat ? ((formatFlags & CKFF_VF_COLOR1) != 0) : ((dpFlags & CKRST_DP_SPECULAR) != 0));
    key.VS.SetHasTexCoord0(hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD0) != 0) : (m_CurrentActiveTextureCount > 0));
    key.VS.SetHasTexCoord1(hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD1) != 0) : (m_CurrentActiveTextureCount > 1));
    key.VS.SetHasTexCoord2(hasFormat ? ((formatFlags & CKFF_VF_TEXCOORD2) != 0) : (m_CurrentActiveTextureCount > 2));

    CKBOOL lighting = m_DrawStateCache.GetRenderState(VXRENDERSTATE_LIGHTING);
    CKBOOL specular = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE);
    CKBOOL normalize = m_DrawStateCache.GetRenderState(VXRENDERSTATE_NORMALIZENORMALS);

    if (DebugForceUnlit())
        lighting = FALSE;

    key.VS.SetLightingEnabled(!positionT && lighting && key.VS.GetHasNormal());
    m_CurrentLightingEnabled = key.VS.GetLightingEnabled();
    key.VS.SetSpecularEnabled(specular != 0);
    key.VS.SetNormalizeNormals(normalize != 0);
    key.VS.SetLightCount(key.VS.GetLightingEnabled() ? m_ActiveLightCount : 0);

    const CKBOOL colorVertex = m_DrawStateCache.GetRenderState(VXRENDERSTATE_COLORVERTEX);
    const CKDWORD diffuseSource = ResolveMaterialSource(
        key.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD ambientSource = ResolveMaterialSource(
        key.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);
    const CKDWORD specularSource = ResolveMaterialSource(
        key.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX),
        dpFlags, CKRST_DP_SPECULAR, CKFF_MS_COLOR1);
    const CKDWORD emissiveSource = ResolveMaterialSource(
        key.VS.GetLightingEnabled(), colorVertex,
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_EMISSIVEFROMVERTEX),
        dpFlags, CKRST_DP_DIFFUSE, CKFF_MS_COLOR0);

    key.VS.SetDiffuseSource(diffuseSource);
    key.VS.SetAmbientSource(ambientSource);
    key.VS.SetSpecularSource(specularSource);
    key.VS.SetEmissiveSource(emissiveSource);
    m_MaterialSource[0] = (float)diffuseSource;
    m_MaterialSource[1] = (float)ambientSource;
    m_MaterialSource[2] = (float)specularSource;
    m_MaterialSource[3] = (float)emissiveSource;

    // Fog
    CKBOOL fogEnable = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE);
    if (DebugDisableFog())
        fogEnable = FALSE;
    if (fogEnable) {
        CKDWORD fogMode = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE);
        key.VS.SetFogMode(fogMode);
    }

    // Fragment key mirrors the active fixed-function texture-stage contract.
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        const CKDWORD colorOp = GetStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        const CKDWORD alphaOp = GetStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        key.FS.SetStageColorOp(stage, colorOp);
        key.FS.SetStageColorArg1(stage, BaseTextureArg(GetStageColorArg1(m_StageStates[stage], hasTexture)));
        key.FS.SetStageColorArg2(stage, BaseTextureArg(GetStageColorArg2(m_StageStates[stage])));
        key.FS.SetStageAlphaOp(stage, alphaOp);

        if (colorOp == CKRST_TOP_DISABLE)
            break;
    }

    key.FS.SetSpecularAdd(specular != 0);
    key.FS.SetFogEnabled(fogEnable != 0);

    CKBOOL alphaTest = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE);
    if (alphaTest) {
        key.FS.SetAlphaTestEnabled(true);
        key.FS.SetAlphaFunc(m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC));
    }

    return key;
}

void CKFixedFunctionPipeline::UploadUniforms(CKRasterizerEncoder *encoder) {
    if (!encoder) return;

    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    VxMatrix modelView;
    VxMatrix normalMatrix;
    VxMatrix viewProj;
    VxMatrix modelViewProj;
    MulMatrix(modelView, m_World, m_View);
    Vx3DInverseMatrix(normalMatrix, modelView);
    TransposeMatrix(normalMatrix, normalMatrix);
    MulMatrix(viewProj, m_View, m_Projection);
    MulMatrix(modelViewProj, m_World, viewProj);
    encoder->SetUniform(u.u_ckModelViewProj, &modelViewProj, 1);
    encoder->SetUniform(u.u_ckModelView, &modelView, 1);
    encoder->SetUniform(u.u_ckNormalMatrix, &normalMatrix, 1);

    // bgfx uniform bindings are draw state. Since draws submit with
    // CKRST_DISCARD_ALL, upload the current fixed-function constants for every
    // draw instead of relying on dirty flags across submissions.
    CKFFLightData viewLights[CKFF_MAX_LIGHTS];
    int packed = 0;
    for (int i = 0; m_CurrentLightingEnabled && i < CKFF_MAX_LIGHTS && packed < m_ActiveLightCount; i++) {
        if (!m_LightEnabled[i]) continue;

        viewLights[packed] = m_Lights[i];

        // Transform position to view space
        VxVector pos(m_Lights[i].Position[0], m_Lights[i].Position[1], m_Lights[i].Position[2]);
        VxVector viewPos;
        Vx3DMultiplyMatrixVector(&viewPos, m_View, &pos);
        viewLights[packed].Position[0] = viewPos.x;
        viewLights[packed].Position[1] = viewPos.y;
        viewLights[packed].Position[2] = viewPos.z;

        // Transform direction to view space (rotation only)
        VxVector dir(m_Lights[i].Direction[0], m_Lights[i].Direction[1], m_Lights[i].Direction[2]);
        VxVector viewDir;
        Vx3DRotateVector(&viewDir, m_View, &dir);
        viewLights[packed].Direction[0] = viewDir.x;
        viewLights[packed].Direction[1] = viewDir.y;
        viewLights[packed].Direction[2] = viewDir.z;

        packed++;
    }

    if (packed > 0) {
        encoder->SetUniform(u.u_lights, viewLights, packed * 7);
    }

    // Light params: x=count, y/z/w = global ambient RGB
    CKDWORD ambientColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_AMBIENT);
    float lightParams[4];
    lightParams[0] = m_CurrentLightingEnabled ? (float)packed : -1.0f;
    lightParams[1] = ((ambientColor >> 16) & 0xFF) / 255.0f;
    lightParams[2] = ((ambientColor >> 8) & 0xFF) / 255.0f;
    lightParams[3] = (ambientColor & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_lightParams, lightParams, 1);

    encoder->SetUniform(u.u_material, &m_Material, 5);
    encoder->SetUniform(u.u_ffParams, m_MaterialSource, 1);
    float lightModelParams[4] = {
        m_DrawStateCache.GetRenderState(VXRENDERSTATE_LOCALVIEWER) ? 1.0f : 0.0f,
        0.0f, 0.0f, 0.0f
    };
    encoder->SetUniform(u.u_lightModelParams, lightModelParams, 1);

    float fogParams[4];
    CKDWORD fs = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGSTART);
    CKDWORD fe = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGEND);
    CKDWORD fd = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGDENSITY);
    memcpy(&fogParams[0], &fs, sizeof(float));
    memcpy(&fogParams[1], &fe, sizeof(float));
    memcpy(&fogParams[2], &fd, sizeof(float));
    fogParams[3] = (m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGENABLE) &&
                    !DebugDisableFog())
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGVERTEXMODE)
        : 0.0f;
    encoder->SetUniform(u.u_fogParams, fogParams, 1);

    CKDWORD fogColor = m_DrawStateCache.GetRenderState(VXRENDERSTATE_FOGCOLOR);
    float fogColorF[4];
    fogColorF[0] = ((fogColor >> 16) & 0xFF) / 255.0f;
    fogColorF[1] = ((fogColor >> 8) & 0xFF) / 255.0f;
    fogColorF[2] = (fogColor & 0xFF) / 255.0f;
    fogColorF[3] = ((fogColor >> 24) & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_fogColor, fogColorF, 1);

    CKDWORD tf = m_DrawStateCache.GetRenderState(VXRENDERSTATE_TEXTUREFACTOR);
    float texFactor[4];
    texFactor[0] = ((tf >> 16) & 0xFF) / 255.0f;
    texFactor[1] = ((tf >> 8) & 0xFF) / 255.0f;
    texFactor[2] = (tf & 0xFF) / 255.0f;
    texFactor[3] = ((tf >> 24) & 0xFF) / 255.0f;
    encoder->SetUniform(u.u_texFactor, texFactor, 1);

    float alphaParams[4];
    alphaParams[0] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAREF) / 255.0f;
    alphaParams[1] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHATESTENABLE)
        ? (float)m_DrawStateCache.GetRenderState(VXRENDERSTATE_ALPHAFUNC)
        : 0.0f;
    alphaParams[2] = m_DrawStateCache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) ? 1.0f : 0.0f;
    alphaParams[3] = 0.0f;
    encoder->SetUniform(u.u_alphaParams, alphaParams, 1);

    encoder->SetUniform(u.u_viewport, m_Viewport, 1);

    float stageParams[CKFF_MAX_TEXTURE_STAGES * 2][4];
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        const bool stageActive = stage < m_CurrentActiveTextureCount;
        const bool hasTexture = stageActive && m_TextureHandles[stage] != 0;
        stageParams[stage * 2 + 0][0] = (float)GetStageColorOp(m_StageStates[stage], stageActive, hasTexture);
        stageParams[stage * 2 + 0][1] = (float)GetStageColorArg1(m_StageStates[stage], hasTexture);
        stageParams[stage * 2 + 0][2] = (float)GetStageColorArg2(m_StageStates[stage]);
        stageParams[stage * 2 + 0][3] = hasTexture ? 1.0f : 0.0f;
        stageParams[stage * 2 + 1][0] = (float)GetStageAlphaOp(m_StageStates[stage], stageActive, hasTexture);
        stageParams[stage * 2 + 1][1] = (float)GetStageAlphaArg1(m_StageStates[stage], hasTexture);
        stageParams[stage * 2 + 1][2] = (float)GetStageAlphaArg2(m_StageStates[stage]);
        stageParams[stage * 2 + 1][3] = 0.0f;
    }
    encoder->SetUniform(u.u_stageParams, stageParams, CKFF_MAX_TEXTURE_STAGES * 2);

    m_DirtyFlags = 0;
}

void CKFixedFunctionPipeline::BindTextures(CKRasterizerEncoder *encoder) {
    if (!encoder) return;
    const CKFFUniformHandles &u = m_ShaderCache.GetUniforms();

    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES && i < m_CurrentActiveTextureCount; i++) {
        if (m_TextureHandles[i]) {
            CKSamplerDesc sampler = BuildSamplerDesc(i);
            encoder->SetTexture(i, u.s_texture[i], m_TextureHandles[i], &sampler);
        }
    }
}

CKSamplerDesc CKFixedFunctionPipeline::BuildSamplerDesc(int stage) const {
    CKSamplerDesc desc;
    memset(&desc, 0, sizeof(desc));

    CKDWORD mag = m_StageStates[stage][CKRST_TSS_MAGFILTER];
    CKDWORD min = m_StageStates[stage][CKRST_TSS_MINFILTER];
    CKDWORD addr = m_StageStates[stage][CKRST_TSS_ADDRESS];
    CKDWORD addrU = m_StageStates[stage][CKRST_TSS_ADDRESSU];
    CKDWORD addrV = m_StageStates[stage][CKRST_TSS_ADDRESSV];

    // Filter mode translation (VXTEXTUREFILTER → CK_FILTER_MODE)
    // VXTEXTUREFILTER: 1=NEAREST, 2=LINEAR, 3=MIPNEAREST, 4=MIPLINEAR, 5=LINEARMIPNEAREST, 6=LINEARMIPLINEAR
    desc.MagFilter = (mag == VXTEXTUREFILTER_NEAREST) ? CKRST_FILTER_NEAREST : CKRST_FILTER_LINEAR;
    desc.MinFilter = (min == VXTEXTUREFILTER_NEAREST) ? CKRST_FILTER_NEAREST : CKRST_FILTER_LINEAR;
    desc.MipFilter = CKRST_FILTER_LINEAR; // Default mip

    // Address mode translation
    CK_ADDRESS_MODE translateAddr = CKRST_ADDRESS_WRAP;
    if (addr == VXTEXTURE_ADDRESSCLAMP) translateAddr = CKRST_ADDRESS_CLAMP;
    else if (addr == VXTEXTURE_ADDRESSMIRROR) translateAddr = CKRST_ADDRESS_MIRROR;
    else if (addr == VXTEXTURE_ADDRESSBORDER) translateAddr = CKRST_ADDRESS_BORDER;

    desc.AddressU = (addrU != 0) ? (CK_ADDRESS_MODE)addrU : translateAddr;
    desc.AddressV = (addrV != 0) ? (CK_ADDRESS_MODE)addrV : translateAddr;
    desc.AddressW = translateAddr;

    desc.BorderColor = m_StageStates[stage][CKRST_TSS_BORDERCOLOR];
    desc.CompareFunc = CKRST_COMPARE_NONE;

    return desc;
}

float CKFixedFunctionPipeline::ComputeDepthKey() const {
    // Depth key = distance from camera (view-space Z of the world origin)
    float z = m_World[3][0] * m_View[0][2] +
              m_World[3][1] * m_View[1][2] +
              m_World[3][2] * m_View[2][2] +
              m_View[3][2];
    return z;
}
