#ifndef RCKRASTERIZERCONTEXT_H
#define RCKRASTERIZERCONTEXT_H

#include "CKRasterizer.h"

class RCKRasterizerContext : public CKRasterizerContext {
public:
    // Stub implementation
    void ClearRenderStateDefaultValue(VXRENDERSTATETYPE state) {}
    void SetRenderStateFlag(VXRENDERSTATETYPE state, CKBOOL value) {}
};

#endif // RCKRASTERIZERCONTEXT_H
