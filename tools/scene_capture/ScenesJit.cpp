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

bool BuildComposite3D(SceneContext &sc) {
    g_Objects.clear();
    SceneSetBackgroundColor(sc, 0xff284058);
    SceneSetAmbient(sc, 0xff505050);
    CKTexture *checker = SceneCreateCheckerTexture(sc, "checker", 128, 128, 16, 0xffc0b080, 0xff304858);
    CKMaterial *ground = SceneCreateMaterial(sc, "ground", VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "floor", 30, 30, 8, 8, ground), VxVector(0, 0, 0));
    for (unsigned i = 0; i < 12; ++i) {
        char name[32]; snprintf(name, sizeof(name), "object-%u", i);
        CKMaterial *material = SceneCreateMaterial(sc, name, VxColor(0.3f + 0.2f * (i % 3), 0.8f, 0.9f, 1.0f), checker);
        CKMesh *mesh = i % 2 ? SceneCreateSphereMesh(sc, name, 1.2f, 12, 16, material)
                            : SceneCreateBoxMesh(sc, name, VxVector(2, 2, 2), material);
        if (!mesh) return false;
        // Alternate genuine prelit and lit meshes in the same render list.
        if (i % 3 != 0) mesh->SetLitMode(VX_PRELITMESH);
        CK3dEntity *entity = SceneCreateEntity(sc, name, mesh, VxVector(0, 1.3f, 0));
        if (!entity) return false;
        g_Objects.push_back(entity);
    }
    CKMaterial *glass = Panel(sc, "glass", VxColor(0.3f, 0.75f, 1.0f, 0.3f));
    glass->SetTwoSided(TRUE);
    SceneCreateEntity(sc, "glass", SceneCreateQuadMesh(sc, "glass", 13, 4, glass), VxVector(0, 2.3f, -2));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.8f, 0.8f, 1.0f, 1.0f), VxVector(0, 10, 0), VxVector(-0.5f, -1, 0.3f), 100);
    SceneCreateLight(sc, "point", VX_LIGHTPOINT, VxColor(1.0f, 0.3f, 0.1f, 1.0f), VxVector(4, 6, -3), VxVector(0, -1, 0), 20);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0, 8, -17), VxVector(0, 1, 1), 55);
    SceneCreate2dQuad(sc, "HUD", VxRect(18, 18, 225, 46), Panel(sc, "HUD", VxColor(0.1f, 0.7f, 0.3f, 0.75f)), false);
    return sc.MainCamera != NULL;
}

void MoveComposite3D(SceneContext &sc) {
    const float phase = float(sc.FrameIndex) * 0.02f;
    for (unsigned i = 0; i < g_Objects.size(); ++i) {
        const float angle = phase + float(i) * 0.4f;
        VxMatrix matrix; Vx3DMatrixIdentity(matrix);
        matrix[0][0] = matrix[2][2] = std::cos(angle);
        matrix[0][2] = std::sin(angle); matrix[2][0] = -std::sin(angle);
        matrix[3][0] = -6.0f + float(i % 4) * 4.0f;
        matrix[3][1] = 1.5f + 0.4f * std::sin(phase + float(i));
        matrix[3][2] = -3.0f + float(i / 4) * 4.0f;
        g_Objects[i]->SetWorldMatrix(matrix);
    }
    const VxVector eye(2.0f * std::sin(phase * 0.5f), 8, -17), target(0, 1, 1);
    sc.MainCamera->SetPosition(&eye); sc.MainCamera->LookAt(&target);
}

bool BuildDynamicLighting(SceneContext &sc) {
    g_Lights.clear();
    SceneSetBackgroundColor(sc, 0xff243040);
    SceneSetAmbient(sc, 0xff282020);
    CKMaterial *floor = SceneCreateMaterial(sc, "floor", VxColor(0.6f, 0.6f, 0.6f, 1.0f));
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "floor", 26, 22, 8, 1, floor), VxVector(0, 0, 0));
    for (unsigned i = 0; i < 4; ++i) {
        char name[32]; snprintf(name, sizeof(name), "lit-sphere-%u", i);
        CKMaterial *material = SceneCreateMaterial(sc, name, VxColor(0.35f + i * 0.15f, 0.6f, 0.75f, 1.0f));
        if (!material) return false;
        material->SetSpecular(VxColor(0.9f, 0.9f, 0.9f, 1.0f));
        material->SetPower(i == 0 ? 0.0f : float(1 << (i + 2)));
        if (i == 0) material->SetEmissive(VxColor(0.08f, 0.02f, 0.01f, 1.0f));
        SceneCreateEntity(sc, name, SceneCreateSphereMesh(sc, name, 1.5f, 20, 28, material),
                          VxVector(-6.0f + i * 4.0f, 1.5f, 0));
    }
    for (unsigned i = 0; i < 8; ++i) {
        char name[32]; snprintf(name, sizeof(name), "light-%u", i);
        const VXLIGHT_TYPE type = i == 0 ? VX_LIGHTDIREC : i % 2 ? VX_LIGHTPOINT : VX_LIGHTSPOT;
        CKLight *light = SceneCreateLight(sc, name, type,
            VxColor(0.1f + 0.1f * (i % 3), 0.1f + 0.12f * ((i + 1) % 3), 0.3f, 1.0f),
            VxVector(float(i) - 4, 6, -3), VxVector(0, -1, 0.4f), 30);
        if (!light) return false;
        light->SetLinearAttenuation(0.04f);
        light->SetQuadraticAttenuation(0.005f);
        g_Lights.push_back(light);
    }
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(1, 9, -18), VxVector(0, 1, 0), 55);
    return sc.MainCamera != NULL;
}

void MoveDynamicLighting(SceneContext &sc) {
    // Checkpoints 1/5/30/120 exercise ambient only, the inline first light,
    // all eight lights, and a changed subset. Motion is deterministic.
    const unsigned count = sc.FrameIndex < 3 ? 0 : sc.FrameIndex < 15 ? 1 : sc.FrameIndex < 60 ? 8 : 3;
    const float phase = float(sc.FrameIndex) * 0.02f;
    for (unsigned i = 0; i < g_Lights.size(); ++i) {
        CKLight *light = g_Lights[i];
        light->Active(i < count);
        const VxVector position(7 * std::sin(phase + i), 5.5f, -4 + 4 * std::cos(phase + i));
        const VxVector target(float(i % 4) * 4 - 6, 0, 0);
        light->SetPosition(&position);
        light->LookAt(&target);
    }
}
}

const SceneDef g_ScenesJit[] = {
    {"composite_2d", "Animated cards, overlapping alpha panels and sprite text through CK2 scene traversal", BuildComposite2D, MoveComposite2D, NULL, false, 2, 1.0f, NULL},
    {"composite_3d", "Moving camera, lit and prelit meshes, occlusion, transparent glass and 2D HUD", BuildComposite3D, MoveComposite3D, NULL, false, 2, 1.0f, NULL},
    {"lighting_dynamic", "Lit spheres with 0/1/8/3 moving directional, point and spot lights", BuildDynamicLighting, MoveDynamicLighting, NULL, false, 2, 1.0f, NULL},
};
const int g_ScenesJitCount = sizeof(g_ScenesJit) / sizeof(g_ScenesJit[0]);
