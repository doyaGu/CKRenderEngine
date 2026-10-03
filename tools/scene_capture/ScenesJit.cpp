// Composite scenes use CK objects, materials, scene traversal and persistent
// meshes. Motion depends only on the frame index, so separate JIT modes can
// compare the same checkpoints regardless of compilation timing.
#include "SceneUtil.h"
#include <cmath>
#include <cstring>
#include <vector>
#ifndef CKRE_SCENE_CAPTURE_VIRTOOLS_SDK
#include "../../include/CKRasterizer.h"
#endif

namespace {
std::vector<CK2dEntity *> g_Cards;
std::vector<CKMaterial *> g_CardMaterials;
std::vector<CK3dEntity *> g_Objects;
std::vector<CKLight *> g_Lights;

CKMaterial *Panel(SceneContext &sc, const char *name, const VxColor &color, CKTexture *texture = NULL) {
    CKMaterial *material = SceneCreateMaterial(sc, name, color, texture);
    if (!material) return NULL;
    material->EnableAlphaBlend(TRUE);
    material->SetSourceBlend(VXBLEND_SRCALPHA);
    material->SetDestBlend(VXBLEND_INVSRCALPHA);
    material->EnableZWrite(FALSE);
    return material;
}

bool BuildComposite2D(SceneContext &sc) {
    g_Cards.clear(); g_CardMaterials.clear();
    SceneSetBackgroundColor(sc, 0xff101828);
    CKTexture *gradient = SceneCreateGradientTexture(sc, "background", 128, 128, 0xff182848, 0xff403060, 0xff508090);
    SceneCreate2dQuad(sc, "background", VxRect(0, 0, float(sc.Width), float(sc.Height)),
        SceneCreateMaterial(sc, "background", VxColor(1.0f, 1.0f, 1.0f, 1.0f), gradient), true);
    CKTexture *icons = SceneCreateCutoutTexture(sc, "icons", 64, 64);
    for (unsigned i = 0; i < 12; ++i) {
        char name[32]; snprintf(name, sizeof(name), "card-%u", i);
        CKMaterial *material = Panel(sc, name, VxColor(0.3f + 0.15f * (i % 4), 0.65f, 1.0f, 0.85f), icons);
        CK2dEntity *entity = SceneCreate2dQuad(sc, name, VxRect(0, 0, 100, 85), material, false);
        if (!material || !entity) return false;
        g_Cards.push_back(entity); g_CardMaterials.push_back(material);
    }
    // The overlay crosses several moving, partially transparent cards.
    SceneCreate2dQuad(sc, "overlay", VxRect(12, float(sc.Height - 115), float(sc.Width - 12), float(sc.Height - 12)),
        Panel(sc, "overlay", VxColor(0.08f, 0.12f, 0.2f, 0.65f)), false);
    CKSpriteText *text = static_cast<CKSpriteText *>(sc.Context->CreateObject(CKCID_SPRITETEXT, (CKSTRING)"caption", CK_OBJECTCREATION_NONAMECHECK));
    if (!text || !text->Create(550, 48, 32, 0)) return false;
    text->SetDesiredVideoFormat(_32_ARGB8888);
    text->SetFont((CKSTRING)"Arial", 22, 700, FALSE, FALSE);
    text->SetTextColor(0xffffffff); text->SetBackgroundColor(0);
    text->SetText((CKSTRING)"2D / alpha / text");
    text->SetAlign(CKSPRITETEXT_LEFT);
    text->SetHomogeneousCoordinates(FALSE);
    text->SetPosition(Vx2DVector(24.0f, float(sc.Height - 80)));
    SceneAddRenderObject(sc, text);
    return true;
}

void MoveComposite2D(SceneContext &sc) {
    const float phase = float(sc.FrameIndex) * 0.025f;
    for (unsigned i = 0; i < g_Cards.size(); ++i) {
        const float x = 30.0f + (sc.Width - 90.0f) * float(i % 4) / 4.0f + 18.0f * std::sin(phase + i);
        const float y = 30.0f + 105.0f * float(i / 4) + 12.0f * std::cos(phase * 0.7f + i);
        g_Cards[i]->SetRect(VxRect(x, y, x + 125, y + 115));
        g_CardMaterials[i]->SetDiffuse(VxColor(0.3f + 0.15f * (i % 4), 0.65f, 1.0f,
            0.55f + 0.3f * std::sin(phase + float(i))));
    }
}
}

const SceneDef g_ScenesJit[] = {
    {"composite_2d", "Animated cards, overlapping alpha panels and sprite text through CK2 scene traversal", BuildComposite2D, MoveComposite2D, NULL, false, 2, 1.0f, NULL},
};
const int g_ScenesJitCount = sizeof(g_ScenesJit) / sizeof(g_ScenesJit[0]);
