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
        // Equal ZOrder entries have no stable order across the original and
        // current runtime's sorts. Define the intended alpha composition.
        entity->SetZOrder(int(i) + 1);
        g_Cards.push_back(entity); g_CardMaterials.push_back(material);
    }
    // The overlay crosses several moving, partially transparent cards.
    CK2dEntity *overlay = SceneCreate2dQuad(sc, "overlay", VxRect(12, float(sc.Height - 115), float(sc.Width - 12), float(sc.Height - 12)),
        Panel(sc, "overlay", VxColor(0.08f, 0.12f, 0.2f, 0.65f)), false);
    if (!overlay) return false;
    overlay->SetZOrder(13);
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
    text->SetZOrder(14);
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

#ifndef CKRE_SCENE_CAPTURE_VIRTOOLS_SDK
// The extended tween streams are a Ballanced API. The surrounding world,
// materials, camera, lighting and render callback use ordinary CK2 objects.
struct TweenVertex {
    VxVector Position, Normal, Target, TargetNormal;
    Vx2DVector UV;
    CKDWORD Color;
};
std::vector<TweenVertex> g_TweenVertices;
std::vector<CKWORD> g_TweenIndices;
CKMaterial *g_TweenMaterials[6] = {};

void DrawTweenScene(CKRenderContext *rc, void *argument) {
    SceneContext &sc = *static_cast<SceneContext *>(argument);
    const float frame = float(sc.FrameIndex);
    const float factor = frame <= 4 ? frame / 4 : frame <= 29 ? 1 - (frame - 4) * 0.65f / 25
        : 0.35f + (frame - 29) * 0.85f / 90;
    CKDWORD factorBits;
    std::memcpy(&factorBits, &factor, sizeof(factor));
    for (unsigned object = 0; object < 6; ++object) {
        const bool lit = object < 3;
        const unsigned streams = object % 3 + 1;
        rc->SetCurrentMaterial(g_TweenMaterials[object], lit);
        rc->SetState(VXRENDERSTATE_LIGHTING, lit);
        rc->SetState(VXRENDERSTATE_NORMALIZENORMALS, TRUE);
        rc->SetState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
        rc->SetState(VXRENDERSTATE_TWEENFACTOR, factorBits);
        VxMatrix world; Vx3DMatrixIdentity(world);
        const float angle = frame * 0.003f;
        world[0][0] = world[2][2] = std::cos(angle);
        world[0][2] = std::sin(angle); world[2][0] = -std::sin(angle);
        world[3][0] = float(object % 3) * 4.5f - 4.5f;
        world[3][1] = 2.3f; world[3][2] = lit ? 2.8f : -3.0f;
        rc->SetWorldTransformationMatrix(world);
        TweenVertex &v = g_TweenVertices.front();
        VxDrawPrimitiveData data = {};
        data.VertexCount = int(g_TweenVertices.size());
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_DIFFUSE | CKRST_DP_STAGES0 | CKRST_DP_TWEEN;
        if (lit) data.Flags |= CKRST_DP_LIGHT;
        data.PositionPtr = &v.Position; data.NormalPtr = &v.Normal;
        data.ColorPtr = &v.Color; data.TexCoordPtr = &v.UV;
        data.PositionStride = data.NormalStride = data.ColorStride = data.TexCoordStride = sizeof(v);
        if (streams & 1) { data.TweenPositionPtr = &v.Target; data.TweenPositionStride = sizeof(v); }
        if (streams & 2) { data.TweenNormalPtr = &v.TargetNormal; data.TweenNormalStride = sizeof(v); }
        if (!rc->DrawPrimitive(VX_TRIANGLELIST, g_TweenIndices.data(), int(g_TweenIndices.size()), &data))
            sc.Error = "tween scene draw failed";
    }
    rc->SetState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_DISABLE);
    rc->SetState(VXRENDERSTATE_LIGHTING, TRUE);
}

bool BuildTweenScene(SceneContext &sc) {
    g_TweenVertices.clear(); g_TweenIndices.clear();
    SceneSetBackgroundColor(sc, 0xff283848);
    SceneSetAmbient(sc, 0xff383838);
    CKTexture *checker = SceneCreateCheckerTexture(sc, "tween-checker", 128, 128, 16, 0xffe0c878, 0xff405870);
    CKMaterial *floor = SceneCreateMaterial(sc, "floor", VxColor(0.6f, 0.65f, 0.7f, 1.0f));
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "floor", 24, 22, 6, 1, floor), VxVector(0, 0, 0));
    CKMaterial *occluder = SceneCreateMaterial(sc, "occluder", VxColor(0.2f, 0.35f, 0.45f, 1.0f));
    SceneCreateEntity(sc, "occluder", SceneCreateBoxMesh(sc, "occluder", VxVector(11, 0.8f, 0.8f), occluder), VxVector(0, 0.8f, -4.2f));
    for (unsigned i = 0; i < 6; ++i) {
        char name[32]; snprintf(name, sizeof(name), "tween-%u", i);
        g_TweenMaterials[i] = SceneCreateMaterial(sc, name, VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
        if (!g_TweenMaterials[i]) return false;
        g_TweenMaterials[i]->SetSpecular(VxColor(0.7f, 0.7f, 0.7f, 1.0f));
        g_TweenMaterials[i]->SetPower(16.0f);
    }
    const unsigned rings = 20, segments = 28;
    for (unsigned r = 0; r <= rings; ++r) for (unsigned s = 0; s <= segments; ++s) {
        const float phi = 3.14159265359f * float(r) / rings, theta = 6.28318530718f * float(s) / segments;
        TweenVertex v;
        v.Normal = VxVector(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
        v.Position = v.Normal * 1.4f;
        v.Target = VxVector(v.Position.x * 0.65f, v.Position.y * 1.5f, v.Position.z * 0.85f);
        v.TargetNormal = VxVector(v.Normal.x / 0.65f, v.Normal.y / 1.5f, v.Normal.z / 0.85f);
        v.TargetNormal.Normalize();
        v.UV = Vx2DVector(float(s) / segments, float(r) / rings); v.Color = 0xffffffff;
        g_TweenVertices.push_back(v);
        if (r < rings && s < segments) {
            const CKWORD a = CKWORD(r * (segments + 1) + s), b = a + 1, c = a + segments + 1, d = c + 1;
            for (CKWORD index : {a, b, d, a, d, c}) g_TweenIndices.push_back(index);
        }
    }
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.9f, 0.8f, 0.7f, 1.0f), VxVector(0, 8, -5), VxVector(0.4f, -1, 0.5f), 100);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0, 10, -20), VxVector(0, 1.4f, 0), 50);
    sc.RenderContext->SetFogMode(VXFOG_LINEAR);
    sc.RenderContext->SetFogStart(16); sc.RenderContext->SetFogEnd(40); sc.RenderContext->SetFogColor(0xff283848);
    sc.RenderContext->AddPostRenderCallBack(DrawTweenScene, &sc, FALSE, TRUE);
    return sc.MainCamera != NULL;
}

struct SkinVertex { float PositionWeights[7]; VxVector Normal, Target, TargetNormal; Vx2DVector UV; CKDWORD Color; };
std::vector<SkinVertex> g_SkinVertices[4];
std::vector<CKWORD> g_SkinIndices;
CKMaterial *g_SkinMaterial = NULL;
CKMaterial *g_ClipHudMaterial = NULL;
bool g_SkinClip = false;

void DrawSkinScene(CKRenderContext *rc, void *argument) {
    SceneContext &sc = *static_cast<SceneContext *>(argument);
    CKRasterizerContext *rasterizer = rc->GetRasterizerContext();
    if (!rasterizer) { sc.Error = "skin scene has no rasterizer"; return; }
    const CKDWORD clipMask = sc.FrameIndex < 3 ? 0 : sc.FrameIndex < 20 ? 36 : sc.FrameIndex < 60 ? 63 : 25;
    const auto setPlanes = [&](const float planes[6][4]) {
        for (unsigned i = 0; i < 6; ++i) {
            VxPlane plane; plane.m_Normal = VxVector(planes[i][0], planes[i][1], planes[i][2]); plane.m_D = planes[i][3];
            if (!rasterizer->SetUserClipPlane(i, plane)) sc.Error = "clip plane upload failed";
        }
        rc->SetState(VXRENDERSTATE_CLIPPLANEENABLE, clipMask);
    };
    for (unsigned object = 0; object < 8; ++object) {
        const unsigned count = object % 4;
        const bool indexed = sc.FrameIndex >= 3 && sc.FrameIndex < 60 && (!g_SkinClip || count >= 2);
        // The public packed layout carries indices after a weight field,
        // even when blend state uses only the implicit weight.
        const unsigned storedWeights = indexed && count == 0 ? 1 : count;
        const bool lit = object < 4;
        rc->SetCurrentMaterial(g_SkinMaterial, lit);
        rc->SetState(VXRENDERSTATE_NORMALIZENORMALS, TRUE);
        rc->SetState(VXRENDERSTATE_VERTEXBLEND, g_SkinClip && count < 2 ?
            (count ? VXVBLEND_TWEENING : VXVBLEND_DISABLE) : (count ? count : VXVBLEND_0WEIGHTS));
        if (g_SkinClip) {
            const float center = float(count) * 5 - 7.5f, z = lit ? 3.5f : -4;
            const float planes[6][4] = {{1,0,0,2.5f-center}, {-1,0,0,center+2.5f},
                {0,1,0,-1.5f}, {0,-1,0,4.5f + 0.3f*std::sin(float(sc.FrameIndex)*0.03f)},
                {0,0,1,0.4f-z}, {0,0,-1,z+0.4f}};
            setPlanes(planes);
        }
        rc->SetState(VXRENDERSTATE_INDEXVBLENDENABLE, indexed);
        VxMatrix world; Vx3DMatrixIdentity(world);
        rc->SetWorldTransformationMatrix(world);
        for (unsigned slot = 0; slot < 4; ++slot) {
            const float angle = 0.3f * std::sin(float(sc.FrameIndex) * 0.025f + slot * 0.5f) * float(slot + 1);
            VxMatrix bone; Vx3DMatrixIdentity(bone);
            bone[0][0] = bone[1][1] = std::cos(angle);
            bone[0][1] = std::sin(angle); bone[1][0] = -std::sin(angle);
            const float pivot = float(slot) * 1.5f;
            bone[3][0] = float(count) * 5.0f - 7.5f + pivot * std::sin(angle);
            bone[3][1] = 0.4f + pivot * (1 - std::cos(angle)); bone[3][2] = lit ? 3.5f : -4.0f;
            if (!rasterizer->SetTransformMatrix(VXMATRIX_WORLDMATRIX(slot), bone)) sc.Error = "skin palette upload failed";
        }
        SkinVertex &v = g_SkinVertices[count].front();
        VxDrawPrimitiveData data = {};
        data.VertexCount = int(g_SkinVertices[count].size());
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_DIFFUSE | CKRST_DP_STAGES0 | CKRST_DP_WEIGHT(storedWeights) |
                     (lit ? CKRST_DP_LIGHT : 0) | (indexed ? CKRST_DP_MATRIXPAL : 0);
        data.PositionPtr = v.PositionWeights; data.NormalPtr = &v.Normal; data.TexCoordPtr = &v.UV; data.ColorPtr = &v.Color;
        data.PositionStride = data.NormalStride = data.TexCoordStride = data.ColorStride = sizeof(v);
        if (g_SkinClip && count == 1) {
            const float factor = 0.35f + 0.25f*std::sin(float(sc.FrameIndex)*0.03f);
            CKDWORD bits; std::memcpy(&bits, &factor, sizeof(bits));
            rc->SetState(VXRENDERSTATE_TWEENFACTOR, bits);
            data.Flags |= CKRST_DP_TWEEN;
            data.TweenPositionPtr = &v.Target; data.TweenNormalPtr = &v.TargetNormal;
            data.TweenPositionStride = data.TweenNormalStride = sizeof(v);
        }
        if (!rc->DrawPrimitive(VX_TRIANGLELIST, g_SkinIndices.data(), int(g_SkinIndices.size()), &data))
            sc.Error = "skin scene draw failed";
    }
    rc->SetState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_DISABLE);
    rc->SetState(VXRENDERSTATE_INDEXVBLENDENABLE, FALSE);
    VxMatrix identity; Vx3DMatrixIdentity(identity);
    rc->SetWorldTransformationMatrix(identity);
    for (unsigned slot = 0; slot < 4; ++slot) rasterizer->SetTransformMatrix(VXMATRIX_WORLDMATRIX(slot), identity);
    if (g_SkinClip) {
        rc->SetCurrentMaterial(g_ClipHudMaterial, FALSE);
        rc->SetState(VXRENDERSTATE_ZENABLE, FALSE);
        const float width = float(sc.Width), height = float(sc.Height);
        const float planes[6][4] = {{1,0,0,-width*0.35f}, {-1,0,0,width*0.65f},
            {0,1,0,-height+45}, {0,-1,0,height-25}, {0,0,1,0}, {0,0,-1,1}};
        setPlanes(planes);
        float positions[4][4] = {{30,height-48,0.5f,1}, {width-30,height-48,0.5f,1},
                                 {width-30,height-22,0.5f,1}, {30,height-22,0.5f,1}};
        CKDWORD colors[4] = {0xffe09048,0xffe09048,0xffe09048,0xffe09048};
        CKWORD indices[] = {0,1,2,0,2,3};
        VxDrawPrimitiveData data = {}; data.VertexCount = 4; data.Flags = CKRST_DP_CL_VCT;
        data.PositionPtr = positions; data.PositionStride = sizeof(positions[0]);
        data.ColorPtr = colors; data.ColorStride = sizeof(colors[0]);
        if (!rc->DrawPrimitive(VX_TRIANGLELIST, indices, 6, &data)) sc.Error = "clipped POSITIONT overlay failed";
        rc->SetState(VXRENDERSTATE_ZENABLE, TRUE);
        rc->SetState(VXRENDERSTATE_CLIPPLANEENABLE, 0);
    }
}

bool BuildSkinScene(SceneContext &sc) {
    g_SkinClip = false;
    g_SkinIndices.clear();
    for (auto &vertices : g_SkinVertices) vertices.clear();
    SceneSetBackgroundColor(sc, 0xff283848); SceneSetAmbient(sc, 0xff383838);
    CKTexture *checker = SceneCreateCheckerTexture(sc, "skin-checker", 128, 128, 16, 0xffe0b868, 0xff406880);
    g_SkinMaterial = SceneCreateMaterial(sc, "skin", VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
    if (!g_SkinMaterial) return false;
    g_SkinMaterial->SetSpecular(VxColor(0.65f, 0.65f, 0.65f, 1.0f)); g_SkinMaterial->SetPower(16);
    CKMaterial *floor = SceneCreateMaterial(sc, "floor", VxColor(0.6f, 0.65f, 0.7f, 1.0f));
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "floor", 28, 24, 6, 1, floor), VxVector(0, 0, 0));
    const unsigned rings = 24, segments = 20;
    for (unsigned r = 0; r <= rings; ++r) for (unsigned s = 0; s <= segments; ++s) {
        const float t = float(r) / rings, a = 6.28318530718f * float(s) / segments;
        for (unsigned count = 0; count < 4; ++count) {
            SkinVertex v = {};
            v.PositionWeights[0] = std::cos(a) * 0.6f; v.PositionWeights[1] = t * 6; v.PositionWeights[2] = std::sin(a) * 0.6f;
            if (count == 1) v.PositionWeights[3] = 1 - t;
            if (count == 2) { v.PositionWeights[3] = (1 - t) * (1 - t); v.PositionWeights[4] = 2 * t * (1 - t); }
            if (count == 3) {
                v.PositionWeights[3] = (1 - t) * (1 - t) * (1 - t);
                v.PositionWeights[4] = 3 * t * (1 - t) * (1 - t); v.PositionWeights[5] = 3 * t * t * (1 - t);
            }
            const CKDWORD indices = 0x00010203u; // reverse the palette to distinguish indexed and sequential draws
            std::memcpy(v.PositionWeights + 3 + (count ? count : 1), &indices, sizeof(indices));
            v.Normal = VxVector(std::cos(a), 0, std::sin(a)); v.UV = Vx2DVector(float(s) / segments, t * 2); v.Color = 0xffffffff;
            v.Target = VxVector(v.PositionWeights[0] + 0.6f*std::sin(t*3.14159265f), t*5.5f, v.PositionWeights[2]);
            v.TargetNormal = VxVector(v.Normal.x, -0.3f*std::cos(t*3.14159265f)*v.Normal.x, v.Normal.z);
            g_SkinVertices[count].push_back(v);
        }
        if (r < rings && s < segments) {
            const CKWORD a0 = CKWORD(r * (segments + 1) + s), b = a0 + 1, c = a0 + segments + 1, d = c + 1;
            for (CKWORD index : {a0, d, b, a0, c, d}) g_SkinIndices.push_back(index);
        }
    }
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.9f, 0.8f, 0.7f, 1.0f), VxVector(0, 8, -5), VxVector(0.4f, -1, 0.5f), 100);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0, 13, -27), VxVector(0, 2.5f, 0), 50);
    sc.RenderContext->SetFogMode(VXFOG_LINEAR); sc.RenderContext->SetFogStart(20); sc.RenderContext->SetFogEnd(50);
    sc.RenderContext->SetFogColor(0xff283848);
    sc.RenderContext->AddPostRenderCallBack(DrawSkinScene, &sc, FALSE, TRUE);
    return sc.MainCamera != NULL;
}
bool BuildClipScene(SceneContext &sc) {
    if (!BuildSkinScene(sc)) return false;
    g_ClipHudMaterial = SceneCreateMaterial(sc, "clip-overlay", VxColor(1,1,1,1));
    g_SkinClip = true;
    return g_ClipHudMaterial != NULL;
}
#endif
}

const SceneDef g_ScenesJit[] = {
    {"composite_2d", "Animated cards, overlapping alpha panels and sprite text through CK2 scene traversal", BuildComposite2D, MoveComposite2D, NULL, false, 2, 1.0f, NULL},
    {"composite_3d", "Moving camera, lit and prelit meshes, occlusion, transparent glass and 2D HUD", BuildComposite3D, MoveComposite3D, NULL, false, 2, 1.0f, NULL},
    {"lighting_dynamic", "Lit spheres with 0/1/8/3 moving directional, point and spot lights", BuildDynamicLighting, MoveDynamicLighting, NULL, false, 2, 1.0f, NULL},
#ifndef CKRE_SCENE_CAPTURE_VIRTOOLS_SDK
    {"tween_3d", "Six textured lit/prelit morphs with position/normal streams, fog and depth occlusion", BuildTweenScene, NULL, NULL, false, 2, 1.0f, NULL},
    {"clipping_3d", "Six dynamic user planes over lit/prelit ordinary, tweened and skinned meshes plus a clipped 2D overlay", BuildClipScene, NULL, NULL, false, 2, 1.0f, NULL},
    {"skinning_3d", "Eight lit/prelit skinned tubes with 0-3 weights and changing palette/index state", BuildSkinScene, NULL, NULL, false, 2, 1.0f, NULL},
#endif
};
const int g_ScenesJitCount = sizeof(g_ScenesJit) / sizeof(g_ScenesJit[0]);
