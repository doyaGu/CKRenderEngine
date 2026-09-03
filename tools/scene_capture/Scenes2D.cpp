// 2D scenes: background / foreground 2D entities, sprites, sprite text, and
// the present_* variants that only change CK2_3D.ini presentation options.

#include "SceneUtil.h"

namespace {

void FillSpritePixels(CKSprite *sprite, int w, int h)
{
    CKBYTE *pixels = sprite->LockSurfacePtr(0);
    if (!pixels)
        return;
    const int pitch = sprite->GetBytesPerLine();
    for (int y = 0; y < h; ++y) {
        CKDWORD *row = reinterpret_cast<CKDWORD *>(pixels + (size_t)y * pitch);
        for (int x = 0; x < w; ++x) {
            // Diagonal stripes with a transparent circle in the middle.
            const float cx = (float)x - (float)w * 0.5f;
            const float cy = (float)y - (float)h * 0.5f;
            const bool hole = cx * cx + cy * cy < (float)(w * w) * 0.06f;
            const bool stripe = ((x + y) / 8) & 1;
            row[x] = hole ? 0x00000000 : (stripe ? 0xFFFFD040 : 0xFF2050C0);
        }
    }
    sprite->ReleaseSurfacePtr(0);
}

bool BuildSpritesText(SceneContext &sc)
{
    const float w = (float)sc.Width;
    const float h = (float)sc.Height;
    SceneSetBackgroundColor(sc, 0xFF000000);
    SceneSetAmbient(sc, 0xFF404040);

    // Background 2D entity: full-window gradient behind the 3D scene.
    CKTexture *gradient = SceneCreateGradientTexture(sc, "bg", 128, 128, 0xFF102040, 0xFF402010, 0xFF80A0C0);
    CKMaterial *bgMat = SceneCreateMaterial(sc, "bg", VxColor(1.0f, 1.0f, 1.0f, 1.0f), gradient);
    SceneCreate2dQuad(sc, "background", VxRect(0.0f, 0.0f, w, h), bgMat, true);

    // 3D content so the layering order is visible.
    CKMaterial *red = SceneCreateMaterial(sc, "red", VxColor(0.9f, 0.2f, 0.1f, 1.0f));
    SceneCreateEntity(sc, "box", SceneCreateBoxMesh(sc, "box", VxVector(3.0f, 3.0f, 3.0f), red), VxVector(0.0f, 0.0f, 0.0f));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 1.0f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.5f, -1.0f, 0.4f), 100.0f);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(4.0f, 3.0f, -7.0f), VxVector(0.0f, 0.0f, 0.0f), 45.0f);

    // Foreground 2D entity with alpha (checker with transparent cells).
    CKTexture *checker = SceneCreateCheckerTexture(sc, "fgchecker", 64, 64, 8, 0xFFFFFFFF, 0x40FF00FF);
    CKMaterial *fgMat = SceneCreateMaterial(sc, "fg", VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
    fgMat->EnableAlphaBlend(TRUE);
    fgMat->SetSourceBlend(VXBLEND_SRCALPHA);
    fgMat->SetDestBlend(VXBLEND_INVSRCALPHA);
    fgMat->SetTextureBlendMode(VXTEXTUREBLEND_MODULATEALPHA);
    SceneCreate2dQuad(sc, "foreground", VxRect(w - 176.0f, 16.0f, w - 16.0f, 176.0f), fgMat, false);

    // Sprite drawn from CPU pixels (sprite path, not a material quad).
    CKSprite *sprite = static_cast<CKSprite *>(sc.Context->CreateObject(CKCID_SPRITE, (CKSTRING)"sprite", CK_OBJECTCREATION_NONAMECHECK));
    if (!sprite || !sprite->Create(96, 96, 32, 0))
        return false;
    FillSpritePixels(sprite, 96, 96);
    sprite->SetTransparent(TRUE);
    sprite->SetTransparentColor(0x00000000);
    sprite->SetHomogeneousCoordinates(FALSE);
    Vx2DVector spritePos(24.0f, 24.0f);
    sprite->SetPosition(spritePos);
    sc.RenderContext->AddObject(sprite);

    // Sprite text: exercises the text raster path; glyph shapes depend on the
    // platform font, so its area is masked when comparing across machines.
    CKSpriteText *text = static_cast<CKSpriteText *>(sc.Context->CreateObject(CKCID_SPRITETEXT, (CKSTRING)"text", CK_OBJECTCREATION_NONAMECHECK));
    if (!text || !text->Create(400, 48, 32, 0))
        return false;
    text->SetFont((CKSTRING)"Arial", 24, 700, FALSE, FALSE);
    text->SetTextColor(0xFFFFFFFF);
    text->SetBackgroundColor(0x00000000);
    text->SetAlign(CKSPRITETEXT_LEFT);
    text->SetText((CKSTRING)"Ballanced 0123456789 CK2_3D");
    text->SetHomogeneousCoordinates(FALSE);
    Vx2DVector textPos(20.0f, h - 72.0f);
    text->SetPosition(textPos);
    sc.RenderContext->AddObject(text);
    return sc.MainCamera != NULL;
}

} // namespace

static const char *const kPresentRenderScale =
    "<CK2_3D>\n    RenderScale = 0.5\n</CK2_3D>\n";
static const char *const kPresentFxaa =
    "<CK2_3D>\n    FXAA = 1\n</CK2_3D>\n";
static const char *const kPresentSharpness =
    "<CK2_3D>\n    Sharpness = 1.0\n</CK2_3D>\n";
static const char *const kPresentMsaa =
    "<CK2_3D>\n    Antialias = 4\n</CK2_3D>\n";

const SceneDef g_Scenes2D[] = {
    {"sprites_text", "Background + foreground 2D entities, a sprite and sprite text over a lit box", BuildSpritesText, NULL, NULL, true, 4, 0.97f, NULL},
    {"present_renderscale", "sprites_text with RenderScale = 0.5 (foreground 2D must stay native resolution)", BuildSpritesText, NULL, NULL, false, 4, 0.97f, kPresentRenderScale},
    {"present_fxaa", "sprites_text with FXAA = 1", BuildSpritesText, NULL, NULL, false, 4, 0.97f, kPresentFxaa},
    {"present_sharpness", "sprites_text with Sharpness = 1.0", BuildSpritesText, NULL, NULL, false, 4, 0.97f, kPresentSharpness},
    {"present_msaa", "sprites_text with Antialias = 4", BuildSpritesText, NULL, NULL, false, 4, 0.97f, kPresentMsaa},
};
const int g_Scenes2DCount = (int)(sizeof(g_Scenes2D) / sizeof(g_Scenes2D[0]));
