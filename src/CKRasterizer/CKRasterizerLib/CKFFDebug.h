#ifndef CKFFDEBUG_H
#define CKFFDEBUG_H

#include "CKRenderConfig.h"
#include "VxMath.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFConstants.h"
#include "CKFFStateDesc.h"
#include "CKDrawStateCache.h"
#include "CKRenderPipeline.h"

struct VxDrawPrimitiveData;

inline const char *CKFFUniformDebugName(const CKFFUniformHandles &u, CKDWORD uniform)
{
    if (uniform == u.u_ffMatrices) return "u_ffMatrices";
    if (uniform == u.u_vertexBlendMatrices) return "u_vertexBlendMatrices";
    if (uniform == u.u_ffDrawParams) return "u_ffDrawParams";
    if (uniform == u.u_lights) return "u_lights";
    if (uniform == u.u_texMatrix) return "u_texMatrix";
    if (uniform == u.u_bumpEnv) return "u_bumpEnv";
    if (uniform == u.u_viewport) return "u_viewport";
    if (uniform == u.u_stageParams) return "u_stageParams";
    if (uniform == u.u_ffSpec) return "u_ffSpec";
    if (uniform == u.u_clipPlanes) return "u_clipPlanes";
    if (uniform == u.u_clipParams) return "u_clipParams";
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        if (uniform == u.s_texture[i]) return "s_texture";
        if (uniform == u.s_textureCube[i]) return "s_textureCube";
        if (uniform == u.s_textureVolume[i]) return "s_textureVolume";
    }
    return "unknown";
}

inline CKDWORD CKFFUniformDebugSlot(const CKFFUniformHandles &u, CKDWORD uniform)
{
    if (uniform == u.u_ffMatrices) return 1;
    if (uniform == u.u_vertexBlendMatrices) return 2;
    if (uniform == u.u_ffDrawParams) return 3;
    if (uniform == u.u_lights) return 6;
    if (uniform == u.u_texMatrix) return 11;
    if (uniform == u.u_bumpEnv) return 20;
    if (uniform == u.u_viewport) return 21;
    if (uniform == u.u_stageParams) return 22;
    if (uniform == u.u_ffSpec) return 23;
    if (uniform == u.u_clipPlanes) return 24;
    if (uniform == u.u_clipParams) return 25;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; ++i) {
        if (uniform == u.s_texture[i]) return 32 + (CKDWORD)i;
        if (uniform == u.s_textureCube[i]) return 40 + (CKDWORD)i;
        if (uniform == u.s_textureVolume[i]) return 48 + (CKDWORD)i;
    }
    return 0;
}

#if CKRE_ENABLE_FFP_DIAGNOSTICS

struct CKFFDebugConfig {
    int DrawLogLimit;
    int Real3DLogLimit;
    int Contract3DLogLimit;
    int PositionTLogLimit;
    bool DrawSerialPerFrame;

    static const CKFFDebugConfig &Get();
};

#else

struct CKFFDebugConfig {
    int DrawLogLimit = 0;
    int Real3DLogLimit = 0;
    int Contract3DLogLimit = 0;
    int PositionTLogLimit = 0;
    bool DrawSerialPerFrame = false;

    static const CKFFDebugConfig &Get() { static CKFFDebugConfig c; return c; }
};

#endif

struct CKFFDrawDebugStage {
    CKDWORD ColorOp;
    CKDWORD ColorArg1;
    CKDWORD ColorArg2;
    CKDWORD AlphaOp;
    CKDWORD AlphaArg1;
    CKDWORD AlphaArg2;
    CKDWORD Texture;
};

struct CKFFDrawDebugInfo {
    CKRenderView View;
    VXPRIMITIVETYPE Type;
    CKWORD *Indices;
    int IndexCount;
    const VxDrawPrimitiveData *Data;
    CKDWORD FormatFlags;
    CKDWORD Program;
    int DrawSerial;
    int ActiveTextureCount;
    int ActiveLightCount;
    const CKFFStateDesc *StateDesc;
    const CKDrawStateCache *DrawState;
    CKFFDrawDebugStage Stage0;
    const VxMatrix *World;
    const VxMatrix *ViewMatrix;
    const VxMatrix *Projection;
    const float *Viewport;

    CKDWORD VertexBuffer;
    CKDWORD IndexBuffer;
    CKDWORD BaseVertex;
    CKDWORD VertexCount;
    CKDWORD StartIndex;
    CKDWORD PersistentIndexCount;
    CKDWORD DPFlags;
    CKDWORD VertexLayout;
};

#if CKRE_ENABLE_FFP_DIAGNOSTICS

class CKFFDebugState {
public:
    CKFFDebugState();

    void BeginFrame();
    bool AnyLoggingEnabled() const;
    int NextDrawSerial(CKRenderView view);

    void LogDrawPrimitiveHeader(const CKFFDrawDebugInfo &info);
    void LogDrawPrimitivePrepareFailed();
    void LogDrawPrimitiveProgramMissing();
    void LogDrawPrimitiveDetails(const CKFFDrawDebugInfo &info);

    void LogDrawVertexBufferHeader(const CKFFDrawDebugInfo &info);
    void LogDrawVertexBufferDetails(const CKFFDrawDebugInfo &info);

private:
    bool Is3DView(CKRenderView view) const;
    bool HasNonIdentityViewTranslation(const VxMatrix &view) const;

    void LogMatrixRows(const char *label, const VxMatrix &m) const;
    void LogVertexClipSamples(const VxMatrix &world, const VxMatrix &view,
                              const VxMatrix &proj, const VxDrawPrimitiveData *data) const;
    void LogPrimitiveIndexContract(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                                   const VxDrawPrimitiveData *data) const;
    void LogPositionTSamples(const float *viewport, const VxDrawPrimitiveData *data) const;
    const char *PrimitiveName(VXPRIMITIVETYPE type) const;
    const char *VertexBlendName(CKDWORD vertexBlend) const;

    int m_DrawLogCount;
    int m_Real3DDrawLogCount;
    int m_Real3DViewLogCount;
    int m_PositionTDrawLogCount;
    int m_Opaque3DDrawSerial;
    int m_Transparent3DDrawSerial;
    int m_3DContractLogCount;
};

#else

class CKFFDebugState {
public:
    CKFFDebugState() {}
    void BeginFrame() {}
    bool AnyLoggingEnabled() const { return false; }
    int NextDrawSerial(CKRenderView) { return -1; }
    void LogDrawPrimitiveHeader(const CKFFDrawDebugInfo &) {}
    void LogDrawPrimitivePrepareFailed() {}
    void LogDrawPrimitiveProgramMissing() {}
    void LogDrawPrimitiveDetails(const CKFFDrawDebugInfo &) {}
    void LogDrawVertexBufferHeader(const CKFFDrawDebugInfo &) {}
    void LogDrawVertexBufferDetails(const CKFFDrawDebugInfo &) {}
};

#endif

#endif // CKFFDEBUG_H
