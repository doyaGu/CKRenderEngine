#include "SceneUtil.h"

#include <math.h>
#include <string.h>
#include <vector>

namespace {

const float kPi = 3.14159265358979f;

template <class T>
T *CreateCK(SceneContext &sc, CK_CLASSID cid, const char *name)
{
    return static_cast<T *>(sc.Context->CreateObject(cid, (CKSTRING)name, CK_OBJECTCREATION_NONAMECHECK));
}

struct MeshBuilder {
    std::vector<VxVector> Positions;
    std::vector<VxVector> Normals;
    std::vector<float> U, V;
    std::vector<int> Faces;

    int AddVertex(const VxVector &p, const VxVector &n, float u, float v)
    {
        Positions.push_back(p);
        Normals.push_back(n);
        U.push_back(u);
        V.push_back(v);
        return (int)Positions.size() - 1;
    }

    void AddFace(int a, int b, int c)
    {
        Faces.push_back(a);
        Faces.push_back(b);
        Faces.push_back(c);
    }

    CKMesh *Commit(SceneContext &sc, const char *name, CKMaterial *mat)
    {
        CKMesh *mesh = CreateCK<CKMesh>(sc, CKCID_MESH, name);
        if (!mesh)
            return NULL;
        const int vertexCount = (int)Positions.size();
        const int faceCount = (int)Faces.size() / 3;
        mesh->SetVertexCount(vertexCount);
        mesh->SetFaceCount(faceCount);
        for (int i = 0; i < vertexCount; ++i) {
            mesh->SetVertexPosition(i, &Positions[i]);
            mesh->SetVertexNormal(i, &Normals[i]);
            mesh->SetVertexTextureCoordinates(i, U[i], V[i]);
            mesh->SetVertexColor(i, 0xFFFFFFFF);
            mesh->SetVertexSpecularColor(i, 0xFF000000);
        }
        for (int f = 0; f < faceCount; ++f) {
            mesh->SetFaceVertexIndex(f, Faces[f * 3], Faces[f * 3 + 1], Faces[f * 3 + 2]);
            mesh->SetFaceMaterial(f, mat);
        }
        mesh->BuildFaceNormals();
        mesh->ModifierVertexMove(FALSE, FALSE);
        return mesh;
    }
};

struct CheckerParams {
    int Cell;
    CKDWORD A, B;
};

void CheckerPixel(int x, int y, int, int, void *user, CKDWORD *argb)
{
    const CheckerParams *p = static_cast<const CheckerParams *>(user);
    const int cell = p->Cell > 0 ? p->Cell : 1;
    *argb = (((x / cell) + (y / cell)) & 1) ? p->B : p->A;
}

struct GradientParams {
    CKDWORD Left, Right, Top;
};

CKDWORD LerpColor(CKDWORD a, CKDWORD b, float t)
{
    CKDWORD out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = (float)((a >> shift) & 0xFF);
        const float cb = (float)((b >> shift) & 0xFF);
        const int c = (int)(ca + (cb - ca) * t + 0.5f);
        out |= (CKDWORD)(c < 0 ? 0 : (c > 255 ? 255 : c)) << shift;
    }
    return out;
}

void GradientPixel(int x, int y, int w, int h, void *user, CKDWORD *argb)
{
    const GradientParams *p = static_cast<const GradientParams *>(user);
    const float tx = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
    const float ty = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
    *argb = LerpColor(LerpColor(p->Left, p->Right, tx), p->Top, 1.0f - ty);
}

void CutoutPixel(int x, int y, int w, int h, void *, CKDWORD *argb)
{
    // Ring plus diagonal bars, hard alpha edges: exercises alpha test.
    const float cx = (float)x + 0.5f - (float)w * 0.5f;
    const float cy = (float)y + 0.5f - (float)h * 0.5f;
    const float r = sqrtf(cx * cx + cy * cy) / (float)w;
    const bool ring = r > 0.28f && r < 0.42f;
    const bool bar = ((x + y) / 12) % 3 == 0 && r < 0.2f;
    const CKDWORD rgb = ring ? 0x00E0A020 : 0x0020A0E0;
    *argb = (ring || bar) ? (0xFF000000 | rgb) : 0x00000000 | rgb;
}

struct RampParams {
    CKDWORD Rgb;
};

void RampPixel(int x, int, int w, int, void *user, CKDWORD *argb)
{
    const RampParams *p = static_cast<const RampParams *>(user);
    const CKDWORD a = w > 1 ? (CKDWORD)(255 * x / (w - 1)) : 255;
    *argb = (a << 24) | (p->Rgb & 0x00FFFFFF);
}

} // namespace

CKMaterial *SceneCreateMaterial(SceneContext &sc, const char *name, const VxColor &diffuse, CKTexture *texture)
{
    CKMaterial *mat = CreateCK<CKMaterial>(sc, CKCID_MATERIAL, name);
    if (!mat)
        return NULL;
    mat->SetDiffuse(diffuse);
    mat->SetAmbient(VxColor(diffuse.r * 0.5f, diffuse.g * 0.5f, diffuse.b * 0.5f, 1.0f));
    mat->SetSpecular(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
    mat->SetEmissive(VxColor(0.0f, 0.0f, 0.0f, 1.0f));
    mat->SetPower(0.0f);
    if (texture) {
        mat->SetTexture(0, texture);
        mat->SetTextureBlendMode(VXTEXTUREBLEND_MODULATEALPHA);
        mat->SetTextureMinMode(VXTEXTUREFILTER_LINEAR);
        mat->SetTextureMagMode(VXTEXTUREFILTER_LINEAR);
        mat->SetTextureAddressMode(VXTEXTURE_ADDRESSWRAP);
    }
    return mat;
}

CKTexture *SceneCreateTexture(SceneContext &sc, const char *name, int w, int h, TexturePixelFn fn, void *user)
{
    CKTexture *tex = CreateCK<CKTexture>(sc, CKCID_TEXTURE, name);
    if (!tex)
        return NULL;
    if (!tex->Create(w, h, 32, 0))
        return NULL;
    tex->SetDesiredVideoFormat(_32_ARGB8888);
    CKBYTE *pixels = tex->LockSurfacePtr(0);
    if (!pixels)
        return NULL;
    VxImageDescEx imageDesc;
    if (!tex->GetImageDesc(imageDesc) || imageDesc.BytesPerLine <= 0)
        return NULL;
    const int pitch = imageDesc.BytesPerLine;
    for (int y = 0; y < h; ++y) {
        CKDWORD *row = reinterpret_cast<CKDWORD *>(pixels + (size_t)y * pitch);
        for (int x = 0; x < w; ++x) {
            CKDWORD argb = 0xFFFF00FF;
            fn(x, y, w, h, user, &argb);
            row[x] = argb;
        }
    }
    tex->ReleaseSurfacePtr(0);
    return tex;
}

CKTexture *SceneCreateCheckerTexture(SceneContext &sc, const char *name, int w, int h, int cell, CKDWORD colorA, CKDWORD colorB)
{
    CheckerParams params = {cell, colorA, colorB};
    return SceneCreateTexture(sc, name, w, h, CheckerPixel, &params);
}

CKTexture *SceneCreateGradientTexture(SceneContext &sc, const char *name, int w, int h, CKDWORD left, CKDWORD right, CKDWORD top)
{
    GradientParams params = {left, right, top};
    return SceneCreateTexture(sc, name, w, h, GradientPixel, &params);
}

CKTexture *SceneCreateCutoutTexture(SceneContext &sc, const char *name, int w, int h)
{
    return SceneCreateTexture(sc, name, w, h, CutoutPixel, NULL);
}

CKTexture *SceneCreateAlphaRampTexture(SceneContext &sc, const char *name, int w, int h, CKDWORD rgb)
{
    RampParams params = {rgb};
    return SceneCreateTexture(sc, name, w, h, RampPixel, &params);
}

CKMesh *SceneCreateBoxMesh(SceneContext &sc, const char *name, const VxVector &size, CKMaterial *mat, float uvRepeat)
{
    MeshBuilder b;
    const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    struct Face {
        VxVector n, u, v;
    };
    const Face faces[6] = {
        {VxVector(0, 0, -1), VxVector(1, 0, 0), VxVector(0, 1, 0)},  // front (-Z)
        {VxVector(0, 0, 1), VxVector(-1, 0, 0), VxVector(0, 1, 0)},  // back (+Z)
        {VxVector(-1, 0, 0), VxVector(0, 0, -1), VxVector(0, 1, 0)}, // left
        {VxVector(1, 0, 0), VxVector(0, 0, 1), VxVector(0, 1, 0)},   // right
        {VxVector(0, 1, 0), VxVector(1, 0, 0), VxVector(0, 0, -1)},  // top
        {VxVector(0, -1, 0), VxVector(1, 0, 0), VxVector(0, 0, 1)},  // bottom
    };
    for (int f = 0; f < 6; ++f) {
        const Face &face = faces[f];
        const VxVector center(face.n.x * hx, face.n.y * hy, face.n.z * hz);
        const VxVector ext(hx, hy, hz);
        VxVector du(face.u.x * ext.x, face.u.y * ext.y, face.u.z * ext.z);
        VxVector dv(face.v.x * ext.x, face.v.y * ext.y, face.v.z * ext.z);
        const int i0 = b.AddVertex(VxVector(center.x - du.x - dv.x, center.y - du.y - dv.y, center.z - du.z - dv.z), face.n, 0.0f, uvRepeat);
        const int i1 = b.AddVertex(VxVector(center.x + du.x - dv.x, center.y + du.y - dv.y, center.z + du.z - dv.z), face.n, uvRepeat, uvRepeat);
        const int i2 = b.AddVertex(VxVector(center.x + du.x + dv.x, center.y + du.y + dv.y, center.z + du.z + dv.z), face.n, uvRepeat, 0.0f);
        const int i3 = b.AddVertex(VxVector(center.x - du.x + dv.x, center.y - du.y + dv.y, center.z - du.z + dv.z), face.n, 0.0f, 0.0f);
        // Clockwise when seen from outside (Virtools / D3D front face).
        b.AddFace(i0, i2, i1);
        b.AddFace(i0, i3, i2);
    }
    return b.Commit(sc, name, mat);
}

CKMesh *SceneCreatePlaneMesh(SceneContext &sc, const char *name, float width, float depth, int subdiv, float uvRepeat, CKMaterial *mat)
{
    MeshBuilder b;
    if (subdiv < 1)
        subdiv = 1;
    for (int z = 0; z <= subdiv; ++z) {
        for (int x = 0; x <= subdiv; ++x) {
            const float fx = (float)x / (float)subdiv;
            const float fz = (float)z / (float)subdiv;
            b.AddVertex(VxVector((fx - 0.5f) * width, 0.0f, (fz - 0.5f) * depth), VxVector(0, 1, 0),
                        fx * uvRepeat, (1.0f - fz) * uvRepeat);
        }
    }
    for (int z = 0; z < subdiv; ++z) {
        for (int x = 0; x < subdiv; ++x) {
            const int i0 = z * (subdiv + 1) + x;
            const int i1 = i0 + 1;
            const int i2 = i0 + subdiv + 1;
            const int i3 = i2 + 1;
            // Front face (D3D clockwise) points up (+Y).
            b.AddFace(i0, i3, i1);
            b.AddFace(i0, i2, i3);
        }
    }
    return b.Commit(sc, name, mat);
}

CKMesh *SceneCreateSphereMesh(SceneContext &sc, const char *name, float radius, int rings, int segments, CKMaterial *mat)
{
    MeshBuilder b;
    if (rings < 2) rings = 2;
    if (segments < 3) segments = 3;
    for (int r = 0; r <= rings; ++r) {
        const float phi = kPi * (float)r / (float)rings;
        const float y = cosf(phi);
        const float s = sinf(phi);
        for (int g = 0; g <= segments; ++g) {
            const float theta = 2.0f * kPi * (float)g / (float)segments;
            const VxVector n(s * cosf(theta), y, s * sinf(theta));
            b.AddVertex(VxVector(n.x * radius, n.y * radius, n.z * radius), n,
                        (float)g / (float)segments, (float)r / (float)rings);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int g = 0; g < segments; ++g) {
            const int i0 = r * (segments + 1) + g;
            const int i1 = i0 + 1;
            const int i2 = i0 + segments + 1;
            const int i3 = i2 + 1;
            b.AddFace(i0, i1, i3);
            b.AddFace(i0, i3, i2);
        }
    }
    return b.Commit(sc, name, mat);
}

CKMesh *SceneCreateQuadMesh(SceneContext &sc, const char *name, float width, float height, CKMaterial *mat)
{
    MeshBuilder b;
    const float hw = width * 0.5f, hh = height * 0.5f;
    const VxVector n(0, 0, -1);
    const int i0 = b.AddVertex(VxVector(-hw, -hh, 0), n, 0.0f, 1.0f);
    const int i1 = b.AddVertex(VxVector(hw, -hh, 0), n, 1.0f, 1.0f);
    const int i2 = b.AddVertex(VxVector(hw, hh, 0), n, 1.0f, 0.0f);
    const int i3 = b.AddVertex(VxVector(-hw, hh, 0), n, 0.0f, 0.0f);
    b.AddFace(i0, i2, i1);
    b.AddFace(i0, i3, i2);
    return b.Commit(sc, name, mat);
}

CK3dEntity *SceneCreateEntity(SceneContext &sc, const char *name, CKMesh *mesh, const VxVector &position)
{
    CK3dEntity *entity = CreateCK<CK3dEntity>(sc, CKCID_3DOBJECT, name);
    if (!entity)
        return NULL;
    if (mesh)
        entity->SetCurrentMesh(mesh);
    entity->SetPosition(&position);
    sc.RenderContext->AddObject(entity);
    return entity;
}

CKCamera *SceneCreateCamera(SceneContext &sc, const char *name, const VxVector &position, const VxVector &target,
                            float fovDegrees, float nearPlane, float farPlane)
{
    CKCamera *camera = CreateCK<CKCamera>(sc, CKCID_CAMERA, name);
    if (!camera)
        return NULL;
    camera->SetPosition(&position);
    camera->LookAt(&target);
    camera->SetFov(fovDegrees * kPi / 180.0f);
    camera->SetFrontPlane(nearPlane);
    camera->SetBackPlane(farPlane);
    camera->SetAspectRatio(sc.Width, sc.Height);
    sc.RenderContext->AddObject(camera);
    return camera;
}

CKLight *SceneCreateLight(SceneContext &sc, const char *name, VXLIGHT_TYPE type, const VxColor &color,
                          const VxVector &position, const VxVector &direction, float range)
{
    CKLight *light = CreateCK<CKLight>(sc, CKCID_LIGHT, name);
    if (!light)
        return NULL;
    light->SetType(type);
    light->SetColor(color);
    light->SetPosition(&position);
    VxVector target(position.x + direction.x, position.y + direction.y, position.z + direction.z);
    light->LookAt(&target);
    light->SetRange(range);
    light->SetConstantAttenuation(1.0f);
    light->SetLinearAttenuation(0.0f);
    light->SetQuadraticAttenuation(0.0f);
    if (type == VX_LIGHTSPOT) {
        light->SetHotSpot(20.0f * kPi / 180.0f);
        light->SetFallOff(45.0f * kPi / 180.0f);
        light->SetFallOffShape(1.0f);
    }
    light->SetSpecularFlag(TRUE);
    light->Active(TRUE);
    sc.RenderContext->AddObject(light);
    return light;
}

CK2dEntity *SceneCreate2dQuad(SceneContext &sc, const char *name, const VxRect &rect, CKMaterial *mat, bool background)
{
    CK2dEntity *entity = CreateCK<CK2dEntity>(sc, CKCID_2DENTITY, name);
    if (!entity)
        return NULL;
    entity->SetHomogeneousCoordinates(FALSE);
    entity->SetRect(rect);
    entity->SetMaterial(mat);
    entity->SetBackground(background ? TRUE : FALSE);
    sc.RenderContext->AddObject(entity);
    return entity;
}

void SceneSetBackgroundColor(SceneContext &sc, CKDWORD argb)
{
    CKMaterial *background = sc.RenderContext->GetBackgroundMaterial();
    if (!background)
        return;
    VxColor color;
    color.Set(argb);
    background->SetDiffuse(color);
}

void SceneSetAmbient(SceneContext &sc, CKDWORD argb)
{
    sc.RenderContext->SetAmbientLight(argb);
}

void SceneDrawScreenQuad(CKRenderContext *rc, float x0, float y0, float x1, float y1, CKDWORD argb, float z)
{
    VxDrawPrimitiveData *data = rc->GetDrawPrimitiveStructure((CKRST_DPFLAGS)(CKRST_DP_VC), 4);
    if (!data)
        return;
    VxVector4 *positions = static_cast<VxVector4 *>(data->PositionPtr);
    const CKDWORD posStride = data->PositionStride;
    const CKDWORD colStride = data->ColorStride;
    const float corners[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (int i = 0; i < 4; ++i) {
        VxVector4 *p = reinterpret_cast<VxVector4 *>(reinterpret_cast<CKBYTE *>(positions) + posStride * i);
        p->x = corners[i][0];
        p->y = corners[i][1];
        p->z = z;
        p->w = 1.0f;
        CKDWORD *c = reinterpret_cast<CKDWORD *>(reinterpret_cast<CKBYTE *>(data->ColorPtr) + colStride * i);
        *c = argb;
    }
    static CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    rc->SetTexture(NULL);
    rc->SetState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    rc->DrawPrimitive(VX_TRIANGLELIST, indices, 6, data);
}

float SceneFrameAngle(const SceneContext &sc, float degreesPerFrame)
{
    return (float)sc.FrameIndex * degreesPerFrame;
}
