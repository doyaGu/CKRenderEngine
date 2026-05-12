#include <stdio.h>

#include "CKDrawStateCache.h"
#include "TestTriangleMultiset.h"

namespace {

CKDWORD BlendSrc(CKDWORD lo) {
    return (lo >> 16) & 0xF;
}

CKDWORD BlendDst(CKDWORD lo) {
    return (lo >> 20) & 0xF;
}

CKDWORD BlendEquation(CKDWORD mid) {
    return mid & 0x7;
}

void BlendStateKeepsExplicitSourceColorDestination() {
    CKDrawStateCache cache;

    cache.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    cache.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ONE);
    cache.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_SRCCOLOR);

    CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);

    TestCheck(BlendSrc(state.Lo) == VXBLEND_ONE,
              "Source blend factor must remain VXBLEND_ONE");
    TestCheck(BlendDst(state.Lo) == VXBLEND_SRCCOLOR,
              "Destination blend factor must remain VXBLEND_SRCCOLOR");
}

void BlendOperationIsEncodedInDrawState() {
    CKDrawStateCache cache;

    cache.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    cache.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
    cache.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
    cache.SetRenderState(VXRENDERSTATE_BLENDOP, VXBLENDOP_SUBTRACT);

    CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);

    TestCheck(BlendEquation(state.Mid) == VXBLENDOP_SUBTRACT,
              "Blend operation must be encoded in the draw state");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Blend state keeps explicit source-color destination",
              &BlendStateKeepsExplicitSourceColorDestination);
    tests.Run("Blend operation is encoded in draw state",
              &BlendOperationIsEncodedInDrawState);
    return tests.ExitCode();
}
