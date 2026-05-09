#ifndef CKFFDEBUG_H
#define CKFFDEBUG_H

#include "CKRenderConfig.h"
#include "VxMath.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFStateDesc.h"
#include "CKDrawStateCache.h"
#include "CKRenderPipeline.h"

struct VxDrawPrimitiveData;

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
