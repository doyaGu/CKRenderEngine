#ifndef CKDRAWSTATECACHE_H
#define CKDRAWSTATECACHE_H

#include "CKTypes.h"
#include "CKRasterizerEnums.h"

#define CKFF_RS_COUNT 256

enum CKDrawStateDirty : CKDWORD {
    CKFF_DIRTY_BLEND     = 0x01,
    CKFF_DIRTY_DEPTH     = 0x02,
    CKFF_DIRTY_RASTER    = 0x04,
    CKFF_DIRTY_STENCIL   = 0x08,
    CKFF_DIRTY_COLORMASK = 0x10,
};

class CKDrawStateCache {
public:
    CKDrawStateCache();

    void SetRenderState(VXRENDERSTATETYPE state, CKDWORD value);
    CKDWORD GetRenderState(VXRENDERSTATETYPE state) const;

    void SetColorWriteMask(CKBOOL r, CKBOOL g, CKBOOL b, CKBOOL a);
    void SetColorWriteMask(CKDWORD mask);
    CKDWORD GetColorWriteMask() const;

    CKDrawState BuildDrawState(VXPRIMITIVETYPE topology);
    CKDWORD GetBuildCacheHits() const { return m_BuildCacheHits; }
    CKDWORD GetBuildRebuilds() const { return m_BuildRebuilds; }
    void ResetBuildStats();

    void Reset();

private:
    CKDWORD m_States[CKFF_RS_COUNT];
    CKDWORD m_DirtyMask;
    CKDrawState m_CachedState;
    VXPRIMITIVETYPE m_LastTopology;
    CKDWORD m_ColorWriteMask;
    CKDWORD m_BuildCacheHits;
    CKDWORD m_BuildRebuilds;

    void SetDefaults();
};

#endif // CKDRAWSTATECACHE_H
