// Render-to-texture and back buffer copy scenes: TextureRender (2D and cube
// faces), DumpToMemory + CopyToVideo.

#include <string.h>
#include <vector>
#include <cmath>

#include "SceneUtil.h"
#include "ImageIO.h"

namespace {

// --- rtt_2d / rtt_cube ----------------------------------------------------------

struct RttState {
    CKTexture *Target = NULL;
    CKCamera *RttCamera = NULL;
    CK3dEntity *Spinner = NULL;
    CKMaterial *Mirror = NULL;
    bool Cube = false;
};
RttState g_Rtt;

bool BuildRttWorld(SceneContext &sc, bool cube)
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
    if (!g_Rtt.Target || !g_Rtt.Target->Create(size, size, 32, 0))
        return false;
    if (cube) {
        g_Rtt.Target->SetSlotCount(6);
        for (int face = 1; face < 6; ++face) {
            if (!g_Rtt.Target->Create(size, size, 32, face))
                return false;
        }
        g_Rtt.Target->SetCubeMap(TRUE);
    }
    g_Rtt.Target->SetDesiredVideoFormat(_32_ARGB8888);

    // RTT camera at the origin; per-face orientation set in PreFrame.
    // Keep the display geometry at x=100 outside the capture frustum. A far
    // plane of 100 intersects the reflective sphere on +X and samples the
    // attachment while writing it; that feedback has no portable oracle.
    g_Rtt.RttCamera = SceneCreateCamera(sc, "rttcamera", VxVector(0.0f, 0.0f, 0.0f), VxVector(0.0f, 0.0f, 8.0f), cube ? 90.0f : 60.0f, 0.5f, cube ? 50.0f : 100.0f);
    if (!g_Rtt.RttCamera)
        return false;
    if (cube)
        g_Rtt.RttCamera->SetAspectRatio(size, size);
    return true;
}

bool BuildRtt2D(SceneContext &sc)
{
    if (!BuildRttWorld(sc, false))
        return false;
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

const CKDWORD kCubeFaceColors[6] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF,
                                  0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF};

bool BuildRttCubeImpl(SceneContext &sc, bool solidFaces)
{
    if (!BuildRttWorld(sc, true))
        return false;
    if (solidFaces) {
        for (int face = 0; face < 6; ++face) {
            CKBYTE *pixels = g_Rtt.Target->LockSurfacePtr(face);
            if (!pixels)
                return false;
            for (int pixel = 0; pixel < 128 * 128; ++pixel)
                reinterpret_cast<CKDWORD *>(pixels)[pixel] = kCubeFaceColors[face];
            g_Rtt.Target->ReleaseSurfacePtr(face);
        }
    }
    // Main view: a reflective sphere far away sampling the cube map.
    CKMaterial *mirror = SceneCreateMaterial(sc, "cubemirror", VxColor(1.0f, 1.0f, 1.0f, 1.0f), g_Rtt.Target);
    mirror->SetEmissive(VxColor(0.2f, 0.2f, 0.2f, 1.0f));
    if (solidFaces) {
        mirror->SetDiffuse(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        mirror->SetAmbient(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
        mirror->SetEmissive(VxColor(1.0f, 1.0f, 1.0f, 1.0f));
        mirror->SetTextureMinMode(VXTEXTUREFILTER_LINEAR);
        mirror->SetTextureMagMode(VXTEXTUREFILTER_LINEAR);
    }
    mirror->SetEffect(VXEFFECT_TEXGEN);
    CKParameter *parameter = mirror->GetEffectParameter();
    if (sc.Verbose) {
        CKDWORD mode = ~0u;
        if (parameter)
            parameter->GetValue(&mode);
        printf("rtt_cube: default texgen=%u\n", (unsigned)mode);
    }
    // The original SDK defaults this parameter to NONE; our engine defaults
    // to 2D REFLECT. Explicitly select the same 3-coordinate cube reflection
    // mode on both stacks so this scene actually validates cube RTT.
    const CKDWORD mode = VXEFFECT_TGCUBEMAP_REFLECT;
    if (!parameter || parameter->SetValue(&mode, sizeof(mode)) != CK_OK)
        return false;
    if (sc.Verbose)
        printf("rtt_cube: selected texgen=%u\n", (unsigned)mode);
    g_Rtt.Mirror = mirror;
    g_Rtt.Spinner = SceneCreateEntity(sc, "cubesphere", SceneCreateSphereMesh(sc, "cubesphere", 3.0f, 24, 32, mirror), VxVector(100.0f, 0.0f, 0.0f));
    SceneCreateLight(sc, "sun2", VX_LIGHTDIREC, VxColor(1.0f, 1.0f, 1.0f, 1.0f), VxVector(100.0f, 10.0f, -5.0f), VxVector(0.0f, -1.0f, 0.5f), 100.0f);
    sc.MainCamera = SceneCreateCamera(sc, "camera", VxVector(100.0f, 2.0f, -10.0f), VxVector(100.0f, 0.0f, 0.0f), 50.0f);
    return sc.MainCamera != NULL && g_Rtt.Target != NULL;
}

bool BuildRttCube(SceneContext &sc) { return BuildRttCubeImpl(sc, false); }
bool BuildCubeFaceFilter(SceneContext &sc) { return BuildRttCubeImpl(sc, true); }

bool BuildCubeFilterDynamic(SceneContext &sc)
{
    return BuildCubeFaceFilter(sc) && g_Rtt.Target->UseMipmap(TRUE);
}

bool BuildCubeMipDynamic(SceneContext &sc)
{
    if (!BuildCubeFilterDynamic(sc))
        return false;
    // Fine checks disappear in distant mips; the border stays the face color.
    for (int face = 0; face < 6; ++face) {
        CKBYTE *pixels = g_Rtt.Target->LockSurfacePtr(face);
        if (!pixels)
            return false;
        for (int y = 4; y < 124; ++y) for (int x = 4; x < 124; ++x) {
            if ((x ^ y) & 1)
                reinterpret_cast<CKDWORD *>(pixels)[y * 128 + x] = 0xFF000000;
        }
        g_Rtt.Target->ReleaseSurfacePtr(face);
    }
    return true;
}

void MoveCubeFilter(SceneContext &sc)
{
    // Each checkpoint exercises a distinct sampler state, including both
    // mixed min/mag directions. Motion is deterministic between checkpoints.
    const int phase = sc.FrameIndex < 4 ? 0 : sc.FrameIndex < 29 ? 1 : sc.FrameIndex < 59 ? 2 : sc.FrameIndex < 89 ? 3 : 4;
    const VXTEXTURE_FILTERMODE minFilters[] = {VXTEXTUREFILTER_MIPNEAREST, VXTEXTUREFILTER_LINEARMIPLINEAR,
        VXTEXTUREFILTER_MIPNEAREST, VXTEXTUREFILTER_LINEARMIPLINEAR, VXTEXTUREFILTER_ANISOTROPIC};
    const VXTEXTURE_FILTERMODE magFilters[] = {VXTEXTUREFILTER_NEAREST, VXTEXTUREFILTER_NEAREST,
        VXTEXTUREFILTER_LINEAR, VXTEXTUREFILTER_LINEAR, VXTEXTUREFILTER_LINEAR};
    g_Rtt.Mirror->SetTextureMinMode(minFilters[phase]);
    g_Rtt.Mirror->SetTextureMagMode(magFilters[phase]);
    sc.RenderContext->SetTextureStageState(CKRST_TSS_MAXANISOTROPY, phase == 4 ? 8 : 1);
    const float angle = float(sc.FrameIndex) * 0.027f;
    const float distance = 12.0f + 5.0f * std::sin(angle);
    const VxVector target(100, 0, 0);
    const VxVector eye(100.0f + 2.0f * std::sin(angle * 0.7f), 2.0f, -distance);
    sc.MainCamera->SetPosition(&eye);
    sc.MainCamera->LookAt(&target);
    VxMatrix matrix; Vx3DMatrixIdentity(matrix);
    matrix[0][0] = matrix[2][2] = std::cos(angle);
    matrix[0][2] = std::sin(angle); matrix[2][0] = -std::sin(angle);
    matrix[3][0] = 100.0f;
    g_Rtt.Spinner->SetWorldMatrix(matrix);
    if (sc.FrameIndex == 0 || sc.FrameIndex == 4 || sc.FrameIndex == 29 || sc.FrameIndex == 59 || sc.FrameIndex == 89)
        printf("cube_filter: phase=%d min=%d mag=%d anisotropy=%d mips=%d\n", phase,
               int(minFilters[phase]), int(magFilters[phase]), phase == 4 ? 8 : 1, g_Rtt.Target->GetMipmapCount());
}

bool ValidateCubeFaceFilter(SceneContext &sc, const RgbaImage &image)
{
    int faces[6] = {};
    int mixed = 0;
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4) {
        const CKDWORD rgb = (CKDWORD(image.Pixels[pixel]) << 16) |
                            (CKDWORD(image.Pixels[pixel + 1]) << 8) |
                            CKDWORD(image.Pixels[pixel + 2]);
        if (rgb == 0x203040)
            continue;
        int face = 0;
        for (; face < 6 && rgb != (kCubeFaceColors[face] & 0xFFFFFF); ++face) {}
        if (face == 6)
            ++mixed;
        else
            ++faces[face];
    }
    printf("cube_face_filter: mixed=%d faces=%d,%d,%d,%d,%d,%d\n",
           mixed, faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]);
    if (mixed != 0) {
        sc.Error = "cube face filtering mixed adjacent face colors";
        return false;
    }
    for (int count : faces) {
        if (count < 100) {
            sc.Error = "cube face filtering did not display every face";
            return false;
        }
    }
    return true;
}

void RttPreFrame(SceneContext &sc)
{
    if (!g_Rtt.Target || !g_Rtt.RttCamera) {
        sc.Error = "RTT target or camera is missing";
        return;
    }
    CKRenderContext *rc = sc.RenderContext;
    const CK_RENDER_FLAGS flags = (CK_RENDER_FLAGS)(CK_RENDER_DEFAULTSETTINGS & ~CK_RENDER_DOBACKTOFRONT);
    rc->AttachViewpointToCamera(g_Rtt.RttCamera);
    if (!g_Rtt.Cube) {
        VxVector target(0.0f, 0.0f, 8.0f);
        g_Rtt.RttCamera->LookAt(&target);
        if (rc->SetRenderTarget(g_Rtt.Target, 0)) {
            if (rc->Render(flags) != CK_OK)
                sc.Error = "rtt_2d: rendering the target failed";
            if (sc.Verbose && sc.FrameIndex == 0) {
                VxRect view;
                rc->GetViewRect(view);
                const VxMatrix &projection = rc->GetProjectionTransformationMatrix();
                printf("rtt_2d: view=(%.0f %.0f %.0f %.0f) projection=(%.6f %.6f)\n",
                       view.left, view.top, view.right, view.bottom, projection[0][0], projection[1][1]);
            }
            if (!rc->SetRenderTarget(NULL, 0))
                sc.Error = "rtt_2d: releasing the target failed";
        } else
            sc.Error = "rtt_2d: binding the target failed";
    } else {
        // D3D cube face conventions: +X -X +Y -Y +Z -Z with matching up vectors.
        const VxVector dirs[6] = {VxVector(1, 0, 0), VxVector(-1, 0, 0), VxVector(0, 1, 0), VxVector(0, -1, 0), VxVector(0, 0, 1), VxVector(0, 0, -1)};
        const VxVector ups[6] = {VxVector(0, 1, 0), VxVector(0, 1, 0), VxVector(0, 0, -1), VxVector(0, 0, 1), VxVector(0, 1, 0), VxVector(0, 1, 0)};
        for (int face = 0; face < 6; ++face) {
            VxVector origin(0.0f, 0.0f, 0.0f);
            g_Rtt.RttCamera->SetPosition(&origin);
            g_Rtt.RttCamera->SetOrientation(&dirs[face], &ups[face]);
            if (rc->SetRenderTarget(g_Rtt.Target, face)) {
                if (rc->Render(flags) != CK_OK)
                    sc.Error = "rtt_cube: rendering face " + std::to_string(face) + " failed";
                if (sc.Verbose && sc.FrameIndex == 0) {
                    VxRect view;
                    rc->GetViewRect(view);
                    const VxMatrix &projection = rc->GetProjectionTransformationMatrix();
                    printf("rtt_cube face=%d: view=(%.0f %.0f %.0f %.0f) projection=(%.6f %.6f)\n",
                           face, view.left, view.top, view.right, view.bottom, projection[0][0], projection[1][1]);
                }
                if (!rc->SetRenderTarget(NULL, 0))
                    sc.Error = "rtt_cube: releasing face " + std::to_string(face) + " failed";
            } else
                sc.Error = "rtt_cube: binding face " + std::to_string(face) + " failed";
            if (!sc.Error.empty())
                break;
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

bool ValidateDumpCopy(SceneContext &sc, const RgbaImage &image)
{
#ifdef CKRE_SCENE_CAPTURE_VIRTOOLS_SDK
    // Capture the unmodified DX8 result, including its unsupported paste.
    // This is a visual reference, not the required CopyToVideo behavior.
    return true;
#else
    RgbaImage source;
    if (!g_DumpCopy.Valid || !ConvertVxImageToRgba(g_DumpCopy.Desc, source, sc.Error)) {
        sc.Error = "dump_copy: no valid source readback for the final paste";
        return false;
    }
    const int left = (int)g_DumpCopy.Dest.left;
    const int top = (int)g_DumpCopy.Dest.top;
    if (!image.Valid() || left < 0 || top < 0 || left + source.Width > image.Width || top + source.Height > image.Height) {
        sc.Error = "dump_copy: destination lies outside the captured frame";
        return false;
    }
    for (int y = 0; y < source.Height; ++y)
        for (int x = 0; x < source.Width; ++x)
            for (int channel = 0; channel < 3; ++channel)
                if (source.Row(y)[x * 4 + channel] != image.Row(top + y)[(left + x) * 4 + channel]) {
                    sc.Error = "dump_copy: pasted RGB differs from the source at " + std::to_string(x) + "," + std::to_string(y);
                    return false;
                }
    printf("dump_copy: all %d pasted pixels match the previous source readback exactly\n", source.Width * source.Height);
    return true;
#endif
}

} // namespace

const SceneDef g_ScenesRtt[] = {
    {"rtt_2d", "TextureRender into a 2D texture shown on a quad", BuildRtt2D, RttPreFrame, NULL, true, 6, 0.97f, NULL},
    {"rtt_cube", "TextureRender into six cube faces sampled with reflection texgen", BuildRttCube, RttPreFrame, NULL, true, 8, 0.95f, NULL},
    {"cube_face_filter", "Six solid cube faces reflected by a 3D sphere with face-local linear filtering", BuildCubeFaceFilter, NULL, NULL, true, 2, 1.0f, NULL, 0, ValidateCubeFaceFilter},
    {"cube_filter_dynamic", "Moving reflective sphere with mip and min/mag/anisotropic filter transitions", BuildCubeFilterDynamic, MoveCubeFilter, NULL, true, 2, 1.0f, NULL, 90, ValidateCubeFaceFilter, true},
    {"cube_mip_dynamic", "Moving reflective sphere with fine checks, mip transitions and changing filters", BuildCubeMipDynamic, MoveCubeFilter, NULL, true, 2, 1.0f, NULL, 90},
    {"dump_copy", "DumpToMemory of a region, CopyToVideo into another region next frame", BuildDumpCopy, NULL, DumpCopyPostFrame, true, 4, 0.98f, NULL, 2, ValidateDumpCopy},
};
const int g_ScenesRttCount = (int)(sizeof(g_ScenesRtt) / sizeof(g_ScenesRtt[0]));
