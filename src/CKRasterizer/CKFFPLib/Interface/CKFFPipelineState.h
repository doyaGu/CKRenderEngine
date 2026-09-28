#ifndef CKFFPIPELINESTATE_H
#define CKFFPIPELINESTATE_H

#include "CKRasterizerContextEnums.h"

// Complete fixed-function pipeline state for one draw.
struct CKFFPipelineState {
    CKDrawState State;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKBOOL ScissorEnabled;
    CKBOOL DepthClipEnabled;
    CKRECT Scissor;
    float PointSize;

    CKFFPipelineState()
        : StencilRef(0), StencilReadMask(0xFF), StencilWriteMask(0xFF),
          ScissorEnabled(FALSE), DepthClipEnabled(TRUE), PointSize(1.0f) {
        State.Lo = CKRST_STATE_DEFAULT_LO;
        State.Mid = CKRST_STATE_DEFAULT_MID;
        State.Hi = 0;
        Scissor.left = Scissor.top = Scissor.right = Scissor.bottom = 0;
    }
};

#endif // CKFFPIPELINESTATE_H
