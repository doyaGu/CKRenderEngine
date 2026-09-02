// 3D fixed-function scenes: lighting, alpha test, alpha blend, material
// channels, fog modes, texgen environment mapping.

#include "SceneUtil.h"

namespace {

// Shared ground + camera used by several scenes so they differ only in the
// feature under test.
void BuildGroundAndCamera(SceneContext &sc, float cameraHeight = 9.0f, float cameraDistance = -16.0f)
{
    CKTexture *checker = SceneCreateCheckerTexture(sc, "checker", 128, 128, 16, 0xFFC8C8C8, 0xFF404040);
    CKMaterial *ground = SceneCreateMaterial(sc, "ground", VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
    CKMesh *plane = SceneCreatePlaneMesh(sc, "plane", 30.0f, 30.0f, 6, 6.0f, ground);
    SceneCreateEntity(sc, "floor", plane, VxVector(0.0f, 0.0f, 0.0f));
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0.0f, cameraHeight, cameraDistance), VxVector(0.0f, 1.0f, 0.0f), 50.0f);
}

void BuildKeyLights(SceneContext &sc)
{
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.8f, 0.8f, 0.75f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.5f, -1.0f, 0.3f), 100.0f);
    SceneCreateLight(sc, "point", VX_LIGHTPOINT, VxColor(0.2f, 1.0f, 0.2f, 1.0f), VxVector(6.0f, 4.0f, -3.0f), VxVector(0.0f, -1.0f, 0.0f), 12.0f);
    SceneCreateLight(sc, "spot", VX_LIGHTSPOT, VxColor(0.3f, 0.4f, 1.0f, 1.0f), VxVector(-4.0f, 8.0f, 2.0f), VxVector(0.0f, -1.0f, 0.0f), 20.0f);
}

// --- opaque_lit ---------------------------------------------------------------

bool BuildOpaqueLit(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF304050);
    SceneSetAmbient(sc, 0xFF202020);
    BuildGroundAndCamera(sc);

    CKMaterial *red = SceneCreateMaterial(sc, "red", VxColor(0.9f, 0.15f, 0.1f, 1.0f));
    SceneCreateEntity(sc, "box", SceneCreateBoxMesh(sc, "box", VxVector(3.0f, 3.0f, 3.0f), red), VxVector(-4.0f, 1.5f, 2.0f));

    CKMaterial *white = SceneCreateMaterial(sc, "white", VxColor(0.9f, 0.9f, 0.9f, 1.0f));
    white->SetSpecular(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
    white->SetPower(32.0f);
    SceneCreateEntity(sc, "sphere", SceneCreateSphereMesh(sc, "sphere", 2.0f, 16, 24, white), VxVector(3.0f, 2.0f, 0.0f));

    CKMaterial *yellow = SceneCreateMaterial(sc, "yellow", VxColor(0.95f, 0.85f, 0.2f, 1.0f));
    yellow->SetEmissive(VxColor(0.1f, 0.1f, 0.0f, 1.0f));
    SceneCreateEntity(sc, "smallbox", SceneCreateBoxMesh(sc, "smallbox", VxVector(1.5f, 1.5f, 1.5f), yellow), VxVector(0.5f, 0.75f, -4.0f));

    BuildKeyLights(sc);
    return sc.MainCamera != NULL;
}

// --- cutout -------------------------------------------------------------------

bool BuildCutout(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF203040);
    SceneSetAmbient(sc, 0xFF606060);
    BuildGroundAndCamera(sc, 6.0f, -14.0f);
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 1.0f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(0.2f, -1.0f, 0.5f), 100.0f);

    CKMaterial *back = SceneCreateMaterial(sc, "back", VxColor(0.2f, 0.7f, 0.3f, 1.0f));
    SceneCreateEntity(sc, "backbox", SceneCreateBoxMesh(sc, "backbox", VxVector(6.0f, 4.0f, 1.0f), back), VxVector(0.0f, 2.0f, 6.0f));

    CKTexture *cutout = SceneCreateCutoutTexture(sc, "cutout", 128, 128);
    CKMaterial *mat = SceneCreateMaterial(sc, "cutout", VxColor(1.0f, 1.0f, 1.0f, 1.0f), cutout);
    mat->SetTextureBlendMode(VXTEXTUREBLEND_MODULATEALPHA);
    mat->EnableAlphaTest(TRUE);
    mat->SetAlphaFunc(VXCMP_GREATEREQUAL);
    mat->SetAlphaRef(128);
    mat->SetTwoSided(TRUE);
    mat->SetTextureMinMode(VXTEXTUREFILTER_NEAREST);
    mat->SetTextureMagMode(VXTEXTUREFILTER_NEAREST);
    CKMesh *quad = SceneCreateQuadMesh(sc, "quad", 8.0f, 8.0f, mat);
    SceneCreateEntity(sc, "cutoutquad", quad, VxVector(0.0f, 4.0f, 0.0f));
    return sc.MainCamera != NULL;
}

// --- alpha_blend --------------------------------------------------------------

bool BuildAlphaBlend(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF202028);
    SceneSetAmbient(sc, 0xFFFFFFFF);
    BuildGroundAndCamera(sc, 6.0f, -12.0f);

    struct Quad {
        const char *Name;
        VxColor Color;
        VXBLEND_MODE Src, Dst;
        VxVector Pos;
        bool Ramp;
    };
    const Quad quads[] = {
        {"blend_srcalpha", VxColor(1.0f, 0.2f, 0.2f, 0.5f), VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA, VxVector(-3.0f, 3.0f, 2.0f), false},
        {"blend_add", VxColor(0.2f, 0.4f, 1.0f, 1.0f), VXBLEND_ONE, VXBLEND_ONE, VxVector(0.0f, 3.5f, 0.0f), false},
        {"blend_modulate", VxColor(1.0f, 1.0f, 1.0f, 1.0f), VXBLEND_ZERO, VXBLEND_SRCCOLOR, VxVector(3.0f, 3.0f, -2.0f), true},
        {"blend_ramp", VxColor(1.0f, 1.0f, 1.0f, 1.0f), VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA, VxVector(0.0f, 1.5f, -4.0f), true},
    };
    for (size_t i = 0; i < sizeof(quads) / sizeof(quads[0]); ++i) {
        const Quad &q = quads[i];
        CKTexture *tex = q.Ramp ? SceneCreateAlphaRampTexture(sc, q.Name, 64, 16, i == 2 ? 0x00FFC040 : 0x0040C0FF) : NULL;
        CKMaterial *mat = SceneCreateMaterial(sc, q.Name, q.Color, tex);
        mat->EnableAlphaBlend(TRUE);
        mat->SetSourceBlend(q.Src);
        mat->SetDestBlend(q.Dst);
        mat->EnableZWrite(FALSE);
        mat->SetTwoSided(TRUE);
        if (tex)
            mat->SetTextureBlendMode(VXTEXTUREBLEND_MODULATEALPHA);
        SceneCreateEntity(sc, q.Name, SceneCreateQuadMesh(sc, q.Name, 5.0f, 4.0f, mat), q.Pos);
    }
    // Opaque reference object behind the quads.
    CKMaterial *solid = SceneCreateMaterial(sc, "solid", VxColor(0.9f, 0.9f, 0.2f, 1.0f));
    SceneCreateEntity(sc, "pillar", SceneCreateBoxMesh(sc, "pillar", VxVector(1.0f, 6.0f, 1.0f), solid), VxVector(-1.0f, 3.0f, 5.0f));
    return sc.MainCamera != NULL;
}

// --- material_channels --------------------------------------------------------

bool BuildMaterialChannels(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF283038);
    SceneSetAmbient(sc, 0xFF808080);
    BuildGroundAndCamera(sc, 7.0f, -13.0f);
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.9f, 0.9f, 0.9f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.3f, -1.0f, 0.4f), 100.0f);

    CKTexture *base = SceneCreateCheckerTexture(sc, "base", 64, 64, 8, 0xFFFFFFFF, 0xFF8080FF);
    CKMaterial *baseMat = SceneCreateMaterial(sc, "base", VxColor(1.0f, 1.0f, 1.0f, 1.0f), base);
    CKMesh *box = SceneCreateBoxMesh(sc, "channelbox", VxVector(5.0f, 5.0f, 5.0f), baseMat, 2.0f);
    CKMesh *sphere = SceneCreateSphereMesh(sc, "channelsphere", 2.5f, 16, 24, baseMat);

    // Channel 1: gradient "lightmap" modulated (ZERO / SRCCOLOR).
    CKTexture *lightmap = SceneCreateGradientTexture(sc, "lightmap", 64, 64, 0xFF202020, 0xFFFFFFFF, 0xFFFFFF80);
    CKMaterial *lightmapMat = SceneCreateMaterial(sc, "lightmap", VxColor(1.0f, 1.0f, 1.0f, 1.0f), lightmap);
    // Channel 2: additive glow spots (ONE / ONE).
    CKTexture *glow = SceneCreateCheckerTexture(sc, "glow", 64, 64, 32, 0xFF000000, 0xFF603000);
    CKMaterial *glowMat = SceneCreateMaterial(sc, "glow", VxColor(1.0f, 1.0f, 1.0f, 1.0f), glow);

    CKMesh *meshes[2] = {box, sphere};
    for (int m = 0; m < 2; ++m) {
        CKMesh *mesh = meshes[m];
        const int c1 = mesh->AddChannel(lightmapMat, TRUE);
        const int c2 = mesh->AddChannel(glowMat, TRUE);
        if (c1 < 0 || c2 < 0)
            return false;
        mesh->SetChannelSourceBlend(c1, VXBLEND_ZERO);
        mesh->SetChannelDestBlend(c1, VXBLEND_SRCCOLOR);
        mesh->SetChannelSourceBlend(c2, VXBLEND_ONE);
        mesh->SetChannelDestBlend(c2, VXBLEND_ONE);
        // Give the channels their own UV mapping so the layers are visibly distinct.
        const int vertexCount = mesh->GetVertexCount();
        for (int i = 0; i < vertexCount; ++i) {
            float u = 0.0f, v = 0.0f;
            mesh->GetVertexTextureCoordinates(i, &u, &v);
            mesh->SetVertexTextureCoordinates(i, u * 0.5f, v * 0.5f, c1);
            mesh->SetVertexTextureCoordinates(i, u * 3.0f + 0.25f, v * 3.0f, c2);
        }
    }
    SceneCreateEntity(sc, "channelbox", box, VxVector(-3.5f, 2.5f, 1.0f));
    SceneCreateEntity(sc, "channelsphere", sphere, VxVector(3.5f, 2.5f, -1.0f));
    return sc.MainCamera != NULL;
}

// --- fog_* ----------------------------------------------------------------------

bool BuildFogScene(SceneContext &sc, VXFOG_MODE mode)
{
    const CKDWORD fogColor = 0xFF6080A0;
    SceneSetBackgroundColor(sc, fogColor);
    SceneSetAmbient(sc, 0xFF404040);
    CKTexture *checker = SceneCreateCheckerTexture(sc, "checker", 128, 128, 16, 0xFFC8C8C8, 0xFF404040);
    CKMaterial *ground = SceneCreateMaterial(sc, "ground", VxColor(1.0f, 1.0f, 1.0f, 1.0f), checker);
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "plane", 24.0f, 80.0f, 8, 8.0f, ground), VxVector(0.0f, 0.0f, 25.0f));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(0.9f, 0.9f, 0.85f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.4f, -1.0f, 0.2f), 100.0f);

    const VxColor colors[3] = {VxColor(0.9f, 0.2f, 0.2f, 1.0f), VxColor(0.2f, 0.9f, 0.2f, 1.0f), VxColor(0.2f, 0.3f, 0.9f, 1.0f)};
    for (int i = 0; i < 8; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "fogbox%d", i);
        CKMaterial *mat = SceneCreateMaterial(sc, name, colors[i % 3]);
        const float x = (i & 1) ? 4.0f : -4.0f;
        SceneCreateEntity(sc, name, SceneCreateBoxMesh(sc, name, VxVector(2.5f, 3.0f + (float)i * 0.5f, 2.5f), mat),
                          VxVector(x, 1.5f + (float)i * 0.25f, -2.0f + (float)i * 7.0f));
    }
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0.0f, 4.0f, -12.0f), VxVector(0.0f, 2.0f, 20.0f), 55.0f, 0.5f, 120.0f);

    CKRenderContext *rc = sc.RenderContext;
    rc->SetFogMode(mode);
    rc->SetFogColor(fogColor);
    rc->SetFogStart(6.0f);
    rc->SetFogEnd(45.0f);
    rc->SetFogDensity(mode == VXFOG_EXP2 ? 0.035f : 0.05f);
    return sc.MainCamera != NULL;
}

bool BuildFogLinear(SceneContext &sc) { return BuildFogScene(sc, VXFOG_LINEAR); }
bool BuildFogExp(SceneContext &sc) { return BuildFogScene(sc, VXFOG_EXP); }
bool BuildFogExp2(SceneContext &sc) { return BuildFogScene(sc, VXFOG_EXP2); }

// --- texgen_envmap ------------------------------------------------------------

bool BuildTexgenEnvmap(SceneContext &sc)
{
    SceneSetBackgroundColor(sc, 0xFF182028);
    SceneSetAmbient(sc, 0xFF303030);
    BuildGroundAndCamera(sc, 5.0f, -12.0f);
    BuildKeyLights(sc);

    // Environment: strongly structured so the reflection mapping is obvious.
    CKTexture *env = SceneCreateGradientTexture(sc, "env", 128, 128, 0xFF2040FF, 0xFFFF8020, 0xFFFFFFFF);
    CKMaterial *envMat = SceneCreateMaterial(sc, "envmat", VxColor(0.9f, 0.9f, 0.9f, 1.0f), env);
    envMat->SetSpecular(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
    envMat->SetPower(24.0f);
    envMat->SetEffect(VXEFFECT_TEXGEN); // defaults to the reflection generator
    SceneCreateEntity(sc, "mirrorball", SceneCreateSphereMesh(sc, "mirrorball", 2.5f, 24, 32, envMat), VxVector(0.0f, 2.5f, 0.0f));

    CKMaterial *envBox = SceneCreateMaterial(sc, "envbox", VxColor(1.0f, 1.0f, 1.0f, 1.0f), env);
    envBox->SetEffect(VXEFFECT_TEXGEN);
    SceneCreateEntity(sc, "mirrorbox", SceneCreateBoxMesh(sc, "mirrorbox", VxVector(2.5f, 2.5f, 2.5f), envBox), VxVector(-5.0f, 1.25f, 2.0f));

    CKMaterial *plain = SceneCreateMaterial(sc, "plain", VxColor(0.7f, 0.2f, 0.6f, 1.0f));
    SceneCreateEntity(sc, "plainbox", SceneCreateBoxMesh(sc, "plainbox", VxVector(2.0f, 4.0f, 2.0f), plain), VxVector(5.0f, 2.0f, 3.0f));
    return sc.MainCamera != NULL;
}

} // namespace

const SceneDef g_Scenes3D[] = {
    {"opaque_lit", "Textured floor, box, specular sphere; directional + point + spot lights", BuildOpaqueLit, NULL, NULL, true, 4, 0.98f, NULL},
    {"cutout", "Alpha-tested cutout quad in front of an opaque box", BuildCutout, NULL, NULL, true, 4, 0.98f, NULL},
    {"alpha_blend", "SRCALPHA, additive and modulate blended quads over a floor", BuildAlphaBlend, NULL, NULL, true, 4, 0.98f, NULL},
    {"material_channels", "Mesh channels with ZERO/SRCCOLOR and ONE/ONE blends (STAGEBLEND)", BuildMaterialChannels, NULL, NULL, true, 4, 0.98f, NULL},
    {"fog_linear", "Receding boxes with linear fog", BuildFogLinear, NULL, NULL, true, 6, 0.97f, NULL},
    {"fog_exp", "Receding boxes with exponential fog", BuildFogExp, NULL, NULL, true, 6, 0.97f, NULL},
    {"fog_exp2", "Receding boxes with squared exponential fog", BuildFogExp2, NULL, NULL, true, 6, 0.97f, NULL},
    {"texgen_envmap", "Reflection texgen (VXEFFECT_TEXGEN) on a sphere and a box", BuildTexgenEnvmap, NULL, NULL, true, 6, 0.97f, NULL},
};
const int g_Scenes3DCount = (int)(sizeof(g_Scenes3D) / sizeof(g_Scenes3D[0]));
