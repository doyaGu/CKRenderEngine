#ifndef CKFFDEBUG_H
#define CKFFDEBUG_H

#include "VxMath.h"
#include "CKRenderEngineEnums.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "CKFFStateDesc.h"
#include "CKDrawStateCache.h"
#include "CKRenderPipeline.h"

struct VxDrawPrimitiveData;

struct CKFFDebugConfig {
    int DrawLogLimit;
    int Real3DLogLimit;
    int Contract3DLogLimit;
    int PositionTLogLimit;
    bool DrawSerialPerFrame;
    bool SkipPositionTDraws;
    bool Skip3DDraws;
    bool ForceUnlit;
    bool DisableFog;
    bool SkipOpaque3DDraws;
    bool SkipTransparent3DDraws;

    static const CKFFDebugConfig &Get();
};

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

class CKFFDebugState {
public:
    struct DrawRange {
        int Exact;
        int Start;
        int End;
    };

    CKFFDebugState();

    void BeginFrame();
    int NextDrawSerial(CKRenderView view);
    bool ShouldSkip(CKRenderView view, int drawSerial, bool positionT) const;
    bool ForceUnlit() const;
    bool DisableFog() const;

    void LogDrawPrimitiveHeader(const CKFFDrawDebugInfo &info);
    void LogDrawPrimitiveSkipped(const CKFFDrawDebugInfo &info, bool positionT);
    void LogDrawPrimitivePrepareFailed();
    void LogDrawPrimitiveProgramMissing();
    void LogDrawPrimitiveDetails(const CKFFDrawDebugInfo &info);

    void LogDrawVertexBufferHeader(const CKFFDrawDebugInfo &info);
    void LogDrawVertexBufferSkipped(const CKFFDrawDebugInfo &info, bool positionT);
    void LogDrawVertexBufferDetails(const CKFFDrawDebugInfo &info);

private:
    bool Is3DView(CKRenderView view) const;
    bool ShouldSkipDrawSerial(CKRenderView view, int drawSerial) const;
    const DrawRange &RangeForView(CKRenderView view) const;
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

#endif // CKFFDEBUG_H
