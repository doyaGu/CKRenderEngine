#include <stdio.h>

#include "CKDrawStateCache.h"
#include "CKRasterizer.h"
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

CKDWORD BlendSrcAlpha(CKDWORD lo) {
    return (lo >> 24) & 0xF;
}

CKDWORD BlendDstAlpha(CKDWORD lo) {
    return (lo >> 28) & 0xF;
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

void DrawStateBuilderExpandsBothSourceAlphaBlendModes() {
    CKDrawStateBuilder builder;
    CKDrawState state = builder.Blend(VXBLEND_BOTHSRCALPHA, VXBLEND_ZERO).Build();

    TestCheck(BlendSrc(state.Lo) == VXBLEND_SRCALPHA,
              "BOTHSRCALPHA source must expand to SRCALPHA");
    TestCheck(BlendDst(state.Lo) == VXBLEND_INVSRCALPHA,
              "BOTHSRCALPHA destination must expand to INVSRCALPHA");

    CKDrawStateBuilder inverseBuilder;
    state = inverseBuilder.Blend(VXBLEND_BOTHINVSRCALPHA, VXBLEND_ZERO).Build();

    TestCheck(BlendSrc(state.Lo) == VXBLEND_INVSRCALPHA,
              "BOTHINVSRCALPHA source must expand to INVSRCALPHA");
    TestCheck(BlendDst(state.Lo) == VXBLEND_SRCALPHA,
              "BOTHINVSRCALPHA destination must expand to SRCALPHA");
}

void DrawStateBuilderExpandsSeparateBothSourceAlphaBlendModes() {
    CKDrawStateBuilder builder;
    CKDrawState state = builder
        .BlendSeparate(VXBLEND_BOTHSRCALPHA, VXBLEND_ZERO,
                       VXBLEND_BOTHINVSRCALPHA, VXBLEND_ZERO)
        .Build();

    TestCheck(BlendSrc(state.Lo) == VXBLEND_SRCALPHA,
              "Separate color BOTHSRCALPHA source must expand to SRCALPHA");
    TestCheck(BlendDst(state.Lo) == VXBLEND_INVSRCALPHA,
              "Separate color BOTHSRCALPHA destination must expand to INVSRCALPHA");
    TestCheck(BlendSrcAlpha(state.Lo) == VXBLEND_INVSRCALPHA,
              "Separate alpha BOTHINVSRCALPHA source must expand to INVSRCALPHA");
    TestCheck(BlendDstAlpha(state.Lo) == VXBLEND_SRCALPHA,
              "Separate alpha BOTHINVSRCALPHA destination must expand to SRCALPHA");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Blend state keeps explicit source-color destination",
              &BlendStateKeepsExplicitSourceColorDestination);
    tests.Run("Blend operation is encoded in draw state",
              &BlendOperationIsEncodedInDrawState);
    tests.Run("Draw state builder expands both-source-alpha blend modes",
              &DrawStateBuilderExpandsBothSourceAlphaBlendModes);
    tests.Run("Draw state builder expands separate both-source-alpha blend modes",
              &DrawStateBuilderExpandsSeparateBothSourceAlphaBlendModes);
    return tests.ExitCode();
}
