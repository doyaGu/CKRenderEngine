// Stencil scenes reproducing the ShadowStencil (mid-frame stencil clear) and
// PlanarShadow (stencil write mask) building block state sequences with the
// public CKRenderContext API.

#include "SceneUtil.h"

namespace {

void SetupStencilBaseStates(CKRenderContext *rc)
{
    rc->SetState(VXRENDERSTATE_LIGHTING, FALSE);
    rc->SetState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    rc->SetState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    rc->SetState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    rc->SetState(VXRENDERSTATE_ZENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_FOGENABLE, FALSE);
    rc->SetTexture(NULL);
}

void DrawStencilTagQuad(CKRenderContext *rc, float x0, float y0, float x1, float y1)
{
    // Writes stencil without touching color: ZERO / ONE blend keeps the color.
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_SRCBLEND, VXBLEND_ZERO);
    rc->SetState(VXRENDERSTATE_DESTBLEND, VXBLEND_ONE);
    SceneDrawScreenQuad(rc, x0, y0, x1, y1, 0xFFFFFFFF, 0.1f);
}

void DrawDarkenQuad(CKRenderContext *rc, float x0, float y0, float x1, float y1)
{
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
    rc->SetState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
    SceneDrawScreenQuad(rc, x0, y0, x1, y1, 0xA0000000, 0.1f);
}

void BuildStencilWorld(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF405060);
    SceneSetAmbient(sc, 0xFF404040);
    CKMaterial *ground = SceneCreateMaterial(sc, "ground", VxColor(1.0f, 1.0f, 1.0f, 1.0f),
                                             SceneCreateCheckerTexture(sc, "checker", 128, 128, 16, 0xFFE0E0E0, 0xFF707070));
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "plane", 30.0f, 30.0f, 6, 6.0f, ground), VxVector(0.0f, 0.0f, 0.0f));
    CKMaterial *red = SceneCreateMaterial(sc, "red", VxColor(0.9f, 0.2f, 0.1f, 1.0f));
    SceneCreateEntity(sc, "box", SceneCreateBoxMesh(sc, "box", VxVector(3.0f, 3.0f, 3.0f), red), VxVector(-3.0f, 1.5f, 1.0f));
    CKMaterial *green = SceneCreateMaterial(sc, "green", VxColor(0.2f, 0.8f, 0.3f, 1.0f));
    SceneCreateEntity(sc, "sphere", SceneCreateSphereMesh(sc, "sphere", 1.8f, 16, 24, green), VxVector(3.5f, 1.8f, -1.0f));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 0.95f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.5f, -1.0f, 0.3f), 100.0f);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0.0f, 8.0f, -15.0f), VxVector(0.0f, 1.0f, 0.0f), 50.0f);
}

// --- stencil_clear_midframe ----------------------------------------------------
// Frame: (opaque 3D) -> tag the whole screen (stencil INCR) -> Clear(STENCIL)
// -> tag the left half (INCR) -> darken where stencil != 0.
// Correct result: only the left half is darkened. Without the mid-frame clear
// the whole screen would be darkened.

struct ClearState {
    int Width, Height;
};
ClearState g_Clear;

void StencilClearCallback(CKRenderContext *rc, void *)
{
    const float w = (float)g_Clear.Width;
    const float h = (float)g_Clear.Height;
    SetupStencilBaseStates(rc);
    rc->SetState(VXRENDERSTATE_STENCILENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_STENCILMASK, 0xFF);
    rc->SetState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);
    rc->SetState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    rc->SetState(VXRENDERSTATE_STENCILREF, 0);
    rc->SetState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    rc->SetState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    rc->SetState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_INCR);

    // 1. Tag everything.
    DrawStencilTagQuad(rc, 0.0f, 0.0f, w, h);
    // 2. Mid-frame stencil clear (ShadowStencil does exactly this between
    //    casters).
    rc->Clear(CK_RENDER_CLEARSTENCIL, 0);
    // 3. Tag the left half again.
    DrawStencilTagQuad(rc, 0.0f, 0.0f, w * 0.5f, h);
    // 4. Darken where stencil != 0.
    rc->SetState(VXRENDERSTATE_STENCILFUNC, VXCMP_NOTEQUAL);
    rc->SetState(VXRENDERSTATE_STENCILREF, 0);
    rc->SetState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
    DrawDarkenQuad(rc, 0.0f, 0.0f, w, h);

    rc->SetState(VXRENDERSTATE_STENCILENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_ZENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_LIGHTING, TRUE);
}

bool BuildStencilClearMidframe(SceneContext &sc)
{
    g_Clear.Width = sc.Width;
    g_Clear.Height = sc.Height;
    BuildStencilWorld(sc);
    sc.RenderContext->AddPostRenderCallBack(StencilClearCallback, NULL, FALSE, TRUE);
    return sc.MainCamera != NULL;
}

// --- stencil_write_mask ---------------------------------------------------------
// PlanarShadow pattern: each shadow owns one stencil bit through the write
// mask. Quad A tags bit 3 over the left 2/3, quad B tags bit 1 over the right
// 2/3 (they overlap in the middle third). The darkening pass reads only bit 3.
// Correct result: left 2/3 darkened. If the write mask is ignored (0xFF
// written), quad B also sets bit 3 and the whole width is darkened.

ClearState g_Mask;

void StencilWriteMaskCallback(CKRenderContext *rc, void *)
{
    const float w = (float)g_Mask.Width;
    const float h = (float)g_Mask.Height;
    SetupStencilBaseStates(rc);
    rc->SetState(VXRENDERSTATE_STENCILENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    rc->SetState(VXRENDERSTATE_STENCILREF, 0xFF);
    rc->SetState(VXRENDERSTATE_STENCILMASK, 0x00);
    rc->SetState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    rc->SetState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    rc->SetState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);

    rc->SetState(VXRENDERSTATE_STENCILWRITEMASK, 1 << 3);
    DrawStencilTagQuad(rc, 0.0f, 0.0f, w * 2.0f / 3.0f, h);
    rc->SetState(VXRENDERSTATE_STENCILWRITEMASK, 1 << 1);
    DrawStencilTagQuad(rc, w / 3.0f, 0.0f, w, h);

    // Read bit 3 only.
    rc->SetState(VXRENDERSTATE_STENCILWRITEMASK, 0x00);
    rc->SetState(VXRENDERSTATE_STENCILMASK, 1 << 3);
    rc->SetState(VXRENDERSTATE_STENCILREF, 1 << 3);
    rc->SetState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    rc->SetState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);
    DrawDarkenQuad(rc, 0.0f, 0.0f, w, h);

    rc->SetState(VXRENDERSTATE_STENCILENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_STENCILMASK, 0xFFFFFFFF);
    rc->SetState(VXRENDERSTATE_STENCILWRITEMASK, 0xFFFFFFFF);
    rc->SetState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    rc->SetState(VXRENDERSTATE_ZENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    rc->SetState(VXRENDERSTATE_LIGHTING, TRUE);
}

bool BuildStencilWriteMask(SceneContext &sc)
{
    g_Mask.Width = sc.Width;
    g_Mask.Height = sc.Height;
    BuildStencilWorld(sc);
    sc.RenderContext->AddPostRenderCallBack(StencilWriteMaskCallback, NULL, FALSE, TRUE);
    return sc.MainCamera != NULL;
}

void ClearSubViewportCallback(CKRenderContext *rc, void *argument)
{
    SceneContext &sc = *static_cast<SceneContext *>(argument);
    VxRect original;
    rc->GetViewRect(original);
    VxRect smaller(sc.Width * 0.25f, sc.Height * 0.25f,
                   sc.Width * 0.75f, sc.Height * 0.75f);
    CKMaterial *background = rc->GetBackgroundMaterial();
    if (!background) {
        sc.Error = "clear_subviewport: missing background material";
        return;
    }
    const VxColor color = background->GetDiffuse();
    background->SetDiffuse(VxColor(0.0f, 1.0f, 0.0f, 1.0f));
    rc->SetViewRect(smaller);
    if (rc->Clear(CK_RENDER_CLEARBACK, 0) != CK_OK)
        sc.Error = "clear_subviewport: Clear failed";
    rc->SetViewRect(original);
    background->SetDiffuse(color);
}

bool BuildClearSubViewport(SceneContext &sc)
{
    BuildStencilWorld(sc);
    sc.RenderContext->AddPostRenderCallBack(ClearSubViewportCallback, &sc, FALSE, TRUE);
    return sc.MainCamera != NULL;
}

} // namespace

const SceneDef g_ScenesStencil[] = {
    {"stencil_clear_midframe", "ShadowStencil pattern: stencil tag, Clear(STENCIL) mid-frame, tag, darken where != 0", BuildStencilClearMidframe, NULL, NULL, true, 4, 0.98f, NULL},
    {"stencil_write_mask", "PlanarShadow pattern: per-bit stencil write masks (1 << bit)", BuildStencilWriteMask, NULL, NULL, true, 4, 0.98f, NULL},
    {"clear_subviewport", "Clear color after SetViewRect to the center quarter of the target", BuildClearSubViewport, NULL, NULL, true, 4, 0.98f, NULL},
};
const int g_ScenesStencilCount = (int)(sizeof(g_ScenesStencil) / sizeof(g_ScenesStencil[0]));
