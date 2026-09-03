// Render-to-texture and back buffer copy scenes: TextureRender (2D and cube
// faces), DumpToMemory + CopyToVideo.

#include <string.h>
#include <vector>

#include "SceneUtil.h"

namespace {

// --- rtt_2d / rtt_cube ----------------------------------------------------------

struct RttState {
    CKTexture *Target = NULL;
    CKCamera *RttCamera = NULL;
    CK3dEntity *Spinner = NULL;
    bool Cube = false;
};
RttState g_Rtt;

void BuildRttWorld(SceneContext &sc, bool cube)
{
    g_Rtt = RttState();
    g_Rtt.Cube = cube;
    SceneSetBackgroundColor(sc, 0xFF203040);
    SceneSetAmbient(sc, 0xFF404040);

    // World seen by the RTT camera: coloured boxes around the origin.
    const VxColor colors[4] = {VxColor(0.9f, 0.2f, 0.2f, 1.0f), VxColor(0.2f, 0.9f, 0.2f, 1.0f), VxColor(0.2f, 0.3f, 0.9f, 1.0f), VxColor(0.9f, 0.9f, 0.2f, 1.0f)};
    const VxVector positions[4] = {VxVector(8.0f, 0.0f, 0.0f), VxVector(-8.0f, 0.0f, 0.0f), VxVector(0.0f, 0.0f, 8.0f), VxVector(0.0f, 0.0f, -8.0f)};
    for (int i = 0; i < 4; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "rttbox%d", i);
        CKMaterial *mat = SceneCreateMaterial(sc, name, colors[i]);
        SceneCreateEntity(sc, name, SceneCreateBoxMesh(sc, name, VxVector(3.0f, 3.0f, 3.0f), mat), positions[i]);
    }
    CKMaterial *floorMat = SceneCreateMaterial(sc, "rttfloor", VxColor(0.6f, 0.6f, 0.6f, 1.0f),
                                               SceneCreateCheckerTexture(sc, "rttchecker", 64, 64, 8, 0xFFFFFFFF, 0xFF606060));
    SceneCreateEntity(sc, "rttfloor", SceneCreatePlaneMesh(sc, "rttplane", 30.0f, 30.0f, 4, 4.0f, floorMat), VxVector(0.0f, -3.0f, 0.0f));
    CKMaterial *ceilMat = SceneCreateMaterial(sc, "rttceil", VxColor(0.3f, 0.5f, 0.8f, 1.0f));
    SceneCreateEntity(sc, "rttceil", SceneCreateBoxMesh(sc, "rttceilbox", VxVector(6.0f, 1.0f, 6.0f), ceilMat), VxVector(0.0f, 7.0f, 0.0f));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 1.0f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.3f, -1.0f, 0.5f), 100.0f);

    // Render target texture.
    g_Rtt.Target = static_cast<CKTexture *>(sc.Context->CreateObject(CKCID_TEXTURE, (CKSTRING)"rtt", CK_OBJECTCREATION_NONAMECHECK));
    const int size = cube ? 128 : 256;
    g_Rtt.Target->Create(size, size, 32, 0);
    if (cube) {
        g_Rtt.Target->SetSlotCount(6);
        for (int face = 1; face < 6; ++face)
            g_Rtt.Target->Create(size, size, 32, face);
        g_Rtt.Target->SetCubeMap(TRUE);
    }
    g_Rtt.Target->SetDesiredVideoFormat(_32_ARGB8888);

    // RTT camera at the origin; per-face orientation set in PreFrame.
    g_Rtt.RttCamera = SceneCreateCamera(sc, "rttcamera", VxVector(0.0f, 0.0f, 0.0f), VxVector(0.0f, 0.0f, 8.0f), cube ? 90.0f : 60.0f, 0.5f, 100.0f);
    if (cube)
        g_Rtt.RttCamera->SetAspectRatio(size, size);
}

bool BuildRtt2D(SceneContext &sc)
{
    BuildRttWorld(sc, false);
    // Main view: a screen sitting far away from the RTT world showing the texture.
    CKMaterial *screenMat = SceneCreateMaterial(sc, "screen", VxColor(1.0f, 1.0f, 1.0f, 1.0f), g_Rtt.Target);
    screenMat->SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP);
    screenMat->SetEmissive(VxColor(1.0f, 1.0f, 1.0f, 1.0f)); // unlit display
    SceneCreateEntity(sc, "screen", SceneCreateQuadMesh(sc, "screen", 8.0f, 8.0f, screenMat), VxVector(100.0f, 0.0f, 0.0f));
    CKMaterial *frameMat = SceneCreateMaterial(sc, "frame", VxColor(0.8f, 0.8f, 0.8f, 1.0f));
    SceneCreateEntity(sc, "frame", SceneCreateBoxMesh(sc, "frame", VxVector(9.0f, 9.0f, 0.5f), frameMat), VxVector(100.0f, 0.0f, 0.6f));
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(100.0f, 1.0f, -11.0f), VxVector(100.0f, 0.0f, 0.0f), 50.0f);
    return sc.MainCamera != NULL && g_Rtt.Target != NULL;
}

bool BuildRttCube(SceneContext &sc)
{
    BuildRttWorld(sc, true);
    // Main view: a reflective sphere far away sampling the cube map.
    CKMaterial *mirror = SceneCreateMaterial(sc, "cubemirror", VxColor(1.0f, 1.0f, 1.0f, 1.0f), g_Rtt.Target);
    mirror->SetEmissive(VxColor(0.2f, 0.2f, 0.2f, 1.0f));
    mirror->SetEffect(VXEFFECT_TEXGEN);
    SceneCreateEntity(sc, "cubesphere", SceneCreateSphereMesh(sc, "cubesphere", 3.0f, 24, 32, mirror), VxVector(100.0f, 0.0f, 0.0f));
    SceneCreateLight(sc, "sun2", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 1.0f, 1.0f), VxVector(100.0f, 10.0f, -5.0f), VxVector(0.0f, -1.0f, 0.5f), 100.0f);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(100.0f, 2.0f, -10.0f), VxVector(100.0f, 0.0f, 0.0f), 50.0f);
    return sc.MainCamera != NULL && g_Rtt.Target != NULL;
}

void RttPreFrame(SceneContext &sc)
{
    if (!g_Rtt.Target || !g_Rtt.RttCamera)
        return;
    CKRenderContext *rc = sc.RenderContext;
    const CK_RENDER_FLAGS flags = (CK_RENDER_FLAGS)(CK_RENDER_DEFAULTSETTINGS & ~CK_RENDER_DOBACKTOFRONT);
    rc->AttachViewpointToCamera(g_Rtt.RttCamera);
    if (!g_Rtt.Cube) {
        VxVector target(0.0f, 0.0f, 8.0f);
        g_Rtt.RttCamera->LookAt(&target);
        if (rc->SetRenderTarget(g_Rtt.Target, 0)) {
            rc->Render(flags);
            rc->SetRenderTarget(NULL, 0);
        }
    } else {
        // D3D cube face conventions: +X -X +Y -Y +Z -Z with matching up vectors.
        const VxVector dirs[6] = {VxVector(1, 0, 0), VxVector(-1, 0, 0), VxVector(0, 1, 0), VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(0, 0, -1)};
        const VxVector ups[6] = {VxVector(0, 1, 0), VxVector(0, 1, 0), VxVector(0, 0, -1), VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(0, 1, 0)};
        for (int face = 0; face < 6; ++face) {
            VxVector origin(0.0f, 0.0f, 0.0f);
            g_Rtt.RttCamera->SetPosition(&origin);
            g_Rtt.RttCamera->SetOrientation(&dirs[face], &ups[face]);
            if (rc->SetRenderTarget(g_Rtt.Target, face)) {
                rc->Render(flags);
                rc->SetRenderTarget(NULL, 0);
            }
        }
    }
    rc->AttachViewpointToCamera(sc.MainCamera);
}

// --- dump_copy ---------------------------------------------------------------------

struct DumpCopyState {
    std::vector<CKBYTE> Pixels;
    VxImageDescEx Desc;
    bool Valid = false;
    VxRect Source;
    VxRect Dest;
};
DumpCopyState g_DumpCopy;

void DumpCopyPostRender(CKRenderContext *rc, void *)
{
    if (!g_DumpCopy.Valid)
        return;
    // Paste last frame's source region into the destination region (inside
    // the frame, after the 3D scene): CopyToVideo path.
    // The original engine's CopyToVideo returns 0 here (checked with the
    // retail DLLs in every callback position and outside the frame), so the
    // oracle frame shows no pasted region; ours pastes the dumped pixels.
    rc->CopyToVideo(&g_DumpCopy.Dest, VXBUFFER_BACKBUFFER, g_DumpCopy.Desc);
}

bool BuildDumpCopy(SceneContext &sc)
{
    g_DumpCopy = DumpCopyState();
    SceneSetBackgroundColor(sc, 0xFF203040);
    SceneSetAmbient(sc, 0xFF303030);
    CKMaterial *ground = SceneCreateMaterial(sc, "ground", VxColor(1.0f, 1.0f, 1.0f, 1.0f),
                                             SceneCreateCheckerTexture(sc, "checker", 128, 128, 16, 0xFFC8C8C8, 0xFF404040));
    SceneCreateEntity(sc, "floor", SceneCreatePlaneMesh(sc, "plane", 30.0f, 30.0f, 6, 6.0f, ground), VxVector(0.0f, 0.0f, 0.0f));
    CKMaterial *red = SceneCreateMaterial(sc, "red", VxColor(0.9f, 0.15f, 0.1f, 1.0f));
    SceneCreateEntity(sc, "box", SceneCreateBoxMesh(sc, "box", VxVector(3.0f, 3.0f, 3.0f), red), VxVector(-3.0f, 1.5f, 0.0f));
    CKMaterial *blue = SceneCreateMaterial(sc, "blue", VxColor(0.2f, 0.3f, 0.9f, 1.0f));
    SceneCreateEntity(sc, "sphere", SceneCreateSphereMesh(sc, "sphere", 1.8f, 16, 24, blue), VxVector(3.0f, 1.8f, -1.0f));
    SceneCreateLight(sc, "sun", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 0.95f, 1.0f), VxVector(0.0f, 10.0f, 0.0f), VxVector(-0.5f, -1.0f, 0.3f), 100.0f);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(0.0f, 7.0f, -14.0f), VxVector(0.0f, 1.0f, 0.0f), 50.0f);

    const float w = (float)sc.Width;
    const float h = (float)sc.Height;
    // Source: the left-middle quarter (contains the red box). Destination:
    // top-right corner, same size.
    g_DumpCopy.Source = VxRect(w * 0.125f, h * 0.35f, w * 0.125f + w * 0.3f, h * 0.35f + h * 0.3f);
    g_DumpCopy.Dest = VxRect(w - w * 0.3f - 8.0f, 8.0f, w - 8.0f, 8.0f + h * 0.3f);
    sc.RenderContext->AddPostRenderCallBack(DumpCopyPostRender, NULL, FALSE, FALSE);
    return sc.MainCamera != NULL;
}

void DumpCopyPostFrame(SceneContext &sc)
{
    // Outside the frame: read back the whole frame (the same call the tool
    // uses for its capture), then crop the source region for the next frame.
    if (sc.FrameIndex >= sc.FrameCount - 1)
        return; // the final frame is captured by the tool itself
    CKRenderContext *rc = sc.RenderContext;
    VxImageDescEx full;
    memset(&full, 0, sizeof(full));
    full.Size = sizeof(full);
    const int size = rc->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, full);
    if (size <= 0)
        return;
    std::vector<CKBYTE> fullPixels((size_t)size, 0);
    full.Image = fullPixels.data();
    if (rc->DumpToMemory(NULL, VXBUFFER_BACKBUFFER, full) <= 0)
        return;
    full.Image = fullPixels.data();
    const int bytesPerPixel = (full.BitsPerPixel + 7) / 8;
    const int pitch = full.BytesPerLine > 0 ? full.BytesPerLine : full.Width * bytesPerPixel;
    const int left = (int)g_DumpCopy.Source.left;
    const int top = (int)g_DumpCopy.Source.top;
    const int width = (int)(g_DumpCopy.Source.right - g_DumpCopy.Source.left);
    const int height = (int)(g_DumpCopy.Source.bottom - g_DumpCopy.Source.top);
    if (left < 0 || top < 0 || width <= 0 || height <= 0 || left + width > full.Width || top + height > full.Height)
        return;

    VxImageDescEx crop = full;
    crop.Width = width;
    crop.Height = height;
    crop.BytesPerLine = width * bytesPerPixel;
    g_DumpCopy.Pixels.assign((size_t)crop.BytesPerLine * height, 0);
    for (int y = 0; y < height; ++y)
        memcpy(&g_DumpCopy.Pixels[(size_t)y * crop.BytesPerLine],
               fullPixels.data() + (size_t)(top + y) * pitch + (size_t)left * bytesPerPixel,
               (size_t)crop.BytesPerLine);
    crop.Image = g_DumpCopy.Pixels.data();
    g_DumpCopy.Desc = crop;
    g_DumpCopy.Valid = true;
}

} // namespace

const SceneDef g_ScenesRtt[] = {
    {"rtt_2d", "TextureRender into a 2D texture shown on a quad", BuildRtt2D, RttPreFrame, NULL, true, 6, 0.97f, NULL},
    {"rtt_cube", "TextureRender into six cube faces sampled with reflection texgen", BuildRttCube, RttPreFrame, NULL, true, 8, 0.95f, NULL},
    {"dump_copy", "DumpToMemory of a region, CopyToVideo into another region next frame", BuildDumpCopy, NULL, DumpCopyPostFrame, true, 4, 0.98f, NULL, 2},
};
const int g_ScenesRttCount = (int)(sizeof(g_ScenesRtt) / sizeof(g_ScenesRtt[0]));
