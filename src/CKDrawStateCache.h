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

    CKDrawState BuildDrawState(VXPRIMITIVETYPE topology);

    void Reset();

private:
    CKDWORD m_States[CKFF_RS_COUNT];
    CKDWORD m_DirtyMask;
    CKDrawState m_CachedState;
    VXPRIMITIVETYPE m_LastTopology;

    void SetDefaults();
};

#endif // CKDRAWSTATECACHE_H
