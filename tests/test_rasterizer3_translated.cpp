// Translation core (CKTranslatedRasterizer / Driver / Context) on top of the
// recording backend from FFPRecordingHarness.h: state/query semantics,
// resource Lock/Unlock shadows, ordered passes, targets and shutdown.

#include <stdio.h>
#include <string.h>

#include "CKTranslatedRasterizerInternal.h"
#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

namespace {

struct Fixture {
    // World is declared last so the members above are plain aliases into it.
    CKRasterizer *Rasterizer;
    CKTranslatedDriver *Driver;
    CKTranslatedContext *Context;
    FFPRecordingBackend *Backend;
    CKFixedFunctionPipeline *FFP;

    Fixture() : Rasterizer(NULL), Driver(NULL), Context(NULL), Backend(NULL), FFP(NULL)
    {
        TestCheck(World.CreateContext(64, 64), "translated context over the recording backend");
        Rasterizer = World.Rasterizer;
        Driver = World.Driver;
        Context = World.Context;
        Backend = World.Backend;
        FFP = Context ? Context->GetFFPipelineForTests() : NULL;
        TestCheck(Backend != NULL && FFP != NULL, "test accessors");
    }

    FFPTranslatedWorld World;
};

CKDWORD Diag(CKTranslatedContext *ctx, CKRST_DIAGNOSTIC kind)
{
    return ctx->GetStats()->Diagnostics[kind];
}

CKDWORD MakeTexture(Fixture &f, int size, CKDWORD extraFlags)
{
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = size;
    desc.Format.Height = size;
    desc.MipMapCount = 1;
    desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | extraFlags;
    CKDWORD handle = 0;
    TestCheck(f.Context->CreateTexture(&desc, &handle) && handle != 0, "CreateTexture");
    return handle;
}

const CKDWORD kVertexFormat = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_STAGES0;

CKDWORD MakeVertexBuffer(Fixture &f, CKDWORD count)
{
    CKVertexBufferDesc desc;
    desc.m_VertexFormat = kVertexFormat;
    desc.m_MaxVertexCount = count;
    desc.m_CurrentVCount = count;
    CKDWORD handle = 0;
    TestCheck(f.Context->CreateVertexBuffer(&desc, NULL, &handle) && handle != 0, "CreateVertexBuffer");
    const CKDWORD stride = CKRSTGetVertexSize(kVertexFormat, desc.m_TexcoordDims);
    TestCheck(stride == 12 + 12 + 4 + 8, "canonical stride of position/normal/diffuse/uv");
    float *vertices = (float *)f.Context->LockVertexBuffer(handle, 0, count, CKRST_LOCK_DEFAULT);
    TestCheck(vertices != NULL, "LockVertexBuffer");
    if (vertices) {
        memset(vertices, 0, stride * count);
        for (CKDWORD i = 0; i < count; ++i)
            vertices[i * (stride / 4)] = (float)i;
        TestCheck(f.Context->UnlockVertexBuffer(handle), "UnlockVertexBuffer");
    }
    return handle;
}

CKDWORD MakeIndexBuffer(Fixture &f, CKDWORD count)
{
    CKIndexBufferDesc desc;
    desc.m_MaxIndexCount = count;
    desc.m_CurrentICount = count;
    CKDWORD handle = 0;
    TestCheck(f.Context->CreateIndexBuffer(&desc, NULL, &handle) && handle != 0, "CreateIndexBuffer");
    CKWORD *indices = (CKWORD *)f.Context->LockIndexBuffer(handle, 0, count, CKRST_LOCK_DEFAULT);
    TestCheck(indices != NULL, "LockIndexBuffer");
    if (indices) {
        for (CKDWORD i = 0; i < count; ++i)
            indices[i] = (CKWORD)(i % 4);
        TestCheck(f.Context->UnlockIndexBuffer(handle), "UnlockIndexBuffer");
    }
    return handle;
}

// --- Tests -----------------------------------------------------------------

void TestLifecycle()
{
    Fixture f;
    TestCheck(f.Driver->m_Desc == "Recording backend", "driver description synced from the backend driver");
    TestCheck(f.Driver->m_3DCaps.MaxNumberTextureStage == 8, "3D caps synced from the backend driver");
    TestCheck(f.Driver->GetBackendDriver() != NULL, "backend driver reachable");
    TestCheck(f.Context->m_Width == 64 && f.Context->m_Height == 64, "context size from the backend");
    TestCheck(f.Context->GetDeviceStatus() == CK_OK, "device status");
    TestCheck(f.Context->IsIdle(), "idle after Create");
    CKRasterizerCapsDesc caps;
    TestCheck(f.Context->GetCaps(&caps), "GetCaps");
    TestCheck(caps.MaxTextureStages >= 1 && caps.MaxTextureStages <= CKRST_MAX_TEXTURE_STAGES, "texture stage cap");
    TestCheck(caps.MaxLights == CKRST_MAX_LIGHTS && caps.MaxUserClipPlanes == CKRST_MAX_USER_CLIP_PLANES, "fixed caps");
    TestCheck(f.Driver->m_Contexts.Size() == 1, "driver tracks its context");
}

void TestDefaultsReachThePipeline()
{
    Fixture f;
    CKDWORD value = 0;
    // Create() resets only the contract-visible mirror; the explicit call
    // pushes the v1 defaults into the pipeline.
    TestCheck(f.FFP->GetRenderState(VXRENDERSTATE_CULLMODE) == VXCULL_CCW, "pipeline keeps its own defaults after Create");
    f.Context->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
    f.Context->InitDefaultRenderStatesValue();
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_CULLMODE, &value) && value == VXCULL_CCW, "CULLMODE default");
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_ZENABLE, &value) && value == 1, "ZENABLE default");
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_COLORWRITEENABLE, &value) && value == CKRST_COLORWRITE_ALL,
              "COLORWRITEENABLE default");
    TestCheck(f.FFP->GetRenderState(VXRENDERSTATE_CULLMODE) == VXCULL_CCW, "FFP received CULLMODE default");
    TestCheck(f.FFP->GetColorWriteMask() == CKRST_COLORWRITE_ALL, "FFP received color write default");
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == CKRST_TOP_MODULATE, "stage 0 OP");
    TestCheck(f.Context->GetTextureStageState(1, CKRST_TSS_OP, &value) && value == CKRST_TOP_DISABLE, "stage 1 OP");
    TestCheck(f.Context->GetTextureStageState(3, CKRST_TSS_TEXCOORDINDEX, &value) && value == 3, "TEXCOORDINDEX");
    TestCheck(f.FFP->GetTexture(0) == 0, "InitDefaultRenderStatesValue unbinds textures in the pipeline");
}

void TestStateRoundTrip()
{
    Fixture f;
    CKDWORD value = 0;
    TestCheck(f.Context->SetRenderState(VXRENDERSTATE_FOGCOLOR, 0x12345678u), "SetRenderState");
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_FOGCOLOR, &value) && value == 0x12345678u, "verbatim get");
    TestCheck(f.FFP->GetRenderState(VXRENDERSTATE_FOGCOLOR) == 0x12345678u, "FFP forwarded");
    TestCheck(f.Context->SetRenderState(VXRENDERSTATE_CULLMODE, 77u), "any value of a valid type is accepted");
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_CULLMODE, &value) && value == 77u, "odd value returned verbatim");

    TestCheck(f.Context->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, 0x5u), "COLORWRITEENABLE");
    TestCheck(f.Context->GetRenderState(VXRENDERSTATE_COLORWRITEENABLE, &value) && value == 0x5u, "color write get");
    TestCheck(f.FFP->GetColorWriteMask() == 0x5u, "FFP color write mask");

    TestCheck(f.Context->SetTextureStageState(2, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP), "TSS ADDRESS");
    TestCheck(f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSU, &value) && value == VXTEXTURE_ADDRESSCLAMP, "ADDRESSU fan out");
    TestCheck(f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESSV, &value) && value == VXTEXTURE_ADDRESSCLAMP, "ADDRESSV fan out");
    TestCheck(f.Context->GetTextureStageState(2, CKRST_TSS_ADDRESW, &value) && value == VXTEXTURE_ADDRESSCLAMP, "ADDRESSW fan out");
    TestCheck(f.FFP->GetTextureStageState(2, CKRST_TSS_ADDRESSV) == VXTEXTURE_ADDRESSCLAMP, "FFP ADDRESSV");

    const CKDWORD before = Diag(f.Context, CKRST_DIAG_INVALID_RENDER_STATE);
    TestCheck(!f.Context->SetRenderState(VXRENDERSTATE_MAXSTATE, 1), "out-of-range render state rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_RENDER_STATE) == before + 1, "INVALID_RENDER_STATE counted");
    TestCheck(!f.Context->SetTextureStageState(8, CKRST_TSS_OP, 1), "stage 8 rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_STAGE_INDEX) == 1, "INVALID_STAGE_INDEX counted");
    TestCheck(!f.Context->SetTextureStageState(0, (CKRST_TEXTURESTAGESTATETYPE)0, 1), "TSS type 0 rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_STAGE_STATE) == 1, "INVALID_STAGE_STATE counted");
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == CKRST_TOP_MODULATE,
              "state unchanged after rejected calls");
}

void TestRepeatedCompoundTextureStates()
{
    Fixture f;
    CKDWORD value = 0;
    for (int reset = 0; reset < 2; ++reset) {
        f.Context->InitDefaultRenderStatesValue();
        for (int repeat = 0; repeat < 2; ++repeat) {
            f.Context->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
            TestCheck(f.FFP->GetTextureStageState(0, CKRST_TSS_OP) == CKRST_TOP_MODULATE &&
                          f.FFP->IsTextureStageStateSet(0, CKRST_TSS_OP),
                      "writing the initial query default must reach FFP after every reset");
            f.Context->SetTextureStageState(0, CKRST_TSS_ARG1, 0);
            TestCheck(!f.FFP->IsTextureStageStateSet(0, CKRST_TSS_ARG1),
                      "repeated zero combine arguments must remain implicit");
            f.Context->SetTextureStageState(0, CKRST_TSS_MINFILTER, 0);
            TestCheck(f.FFP->GetTextureStageState(0, CKRST_TSS_MINFILTER) == 0 &&
                          f.FFP->IsTextureStageStateSet(0, CKRST_TSS_MINFILTER),
                      "repeated zero sampler values must remain explicit");
        }
    }
    f.Context->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    f.Context->SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSWRAP);
    f.Context->SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSCLAMP);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_ADDRESSU, &value) &&
                  value == VXTEXTURE_ADDRESSCLAMP &&
                  f.FFP->GetTextureStageState(0, CKRST_TSS_ADDRESSU) == value,
              "repeated ADDRESS must replace independently changed axes");

    f.Context->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    f.Context->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    f.Context->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == 0 &&
                  f.FFP->GetTextureStageState(0, CKRST_TSS_OP) == 0 &&
                  !f.FFP->IsTextureStageStateSet(0, CKRST_TSS_OP),
              "repeated TEXTUREMAPBLEND must clear newer explicit combine state for queries and rendering");

    const CKDWORD stageBlend = (VXBLEND_DESTCOLOR << 4) | VXBLEND_ZERO;
    f.Context->SetTextureStageState(0, CKRST_TSS_STAGEBLEND, stageBlend);
    f.Context->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    f.Context->SetTextureStageState(0, CKRST_TSS_STAGEBLEND, stageBlend);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) &&
                  value == CKRST_TOP_MODULATE &&
                  f.FFP->GetTextureStageState(0, CKRST_TSS_OP) == value &&
                  f.FFP->IsTextureStageStateSet(0, CKRST_TSS_OP),
              "repeated STAGEBLEND must reapply its derived combine state for queries and rendering");
}

void TestSemanticTextureStageReset()
{
    Fixture f;
    const CKDWORD texture = MakeTexture(f, 2, 0);
    CKFFTextureStageSnapshot initial[CKRST_MAX_TEXTURE_STAGES];
    for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
        f.Context->SetTexture(texture, stage);
        for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state)
            f.Context->SetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)state, 0x1000u + state);
        VxMatrix matrix;
        matrix.SetIdentity();
        matrix[3][0] = float(stage + 1);
        f.Context->SetTransformMatrix(VXMATRIX_TEXTURE(stage), matrix);
        f.FFP->SaveTextureStage(stage, initial[stage]);
    }
    for (int first : {0, 2, 7, 8}) {
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
            f.FFP->RestoreTextureStage(stage, initial[stage]);
        TestCheck(f.Context->ResetTextureStages(first, CKRST_MAX_TEXTURE_STAGES - first), "valid range reset");
        CKFFTextureStageSnapshot actual[CKRST_MAX_TEXTURE_STAGES];
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            f.FFP->SaveTextureStage(stage, actual[stage]);
            f.FFP->RestoreTextureStage(stage, initial[stage]);
        }
        // Behavioral reference for the old CK_3D operation. Production must
        // never expand the range into this sequence of virtual calls.
        for (int stage = first; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            f.Context->SetTexture(0, stage);
            for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state)
                f.Context->SetTextureStageState(stage, (CKRST_TEXTURESTAGESTATETYPE)state,
                                               state == CKRST_TSS_TEXCOORDINDEX ? stage : 0);
            VxMatrix matrix;
            matrix.SetIdentity();
            f.Context->SetTransformMatrix(VXMATRIX_TEXTURE(stage), matrix);
        }
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            CKFFTextureStageSnapshot expected;
            f.FFP->SaveTextureStage(stage, expected);
            TestCheck(actual[stage].Texture == expected.Texture && actual[stage].TextureFlags == expected.TextureFlags &&
                          actual[stage].StateSetMask == expected.StateSetMask &&
                          actual[stage].StateQueryMask == expected.StateQueryMask &&
                          memcmp(actual[stage].States, expected.States, sizeof(expected.States)) == 0 &&
                          memcmp(&actual[stage].TextureMatrix, &expected.TextureMatrix, sizeof(VxMatrix)) == 0,
                      "range reset preserves lower stages and matches complete reset semantics");
        }
    }
    f.Context->SetTextureStageState(7, CKRST_TSS_CONSTANT, 0xFFAABBCCu);
    TestCheck(!f.Context->ResetTextureStages(-1, 1) && !f.Context->ResetTextureStages(7, 2) &&
                  !f.Context->ResetTextureStages(0, -1) && !f.Context->ResetTextureStages(1, 0x7FFFFFFF),
              "invalid ranges fail before any mutation, including overflowing lengths");
    CKDWORD value = 0;
    TestCheck(f.Context->GetTextureStageState(7, CKRST_TSS_CONSTANT, &value) && value == 0xFFAABBCCu,
              "invalid reset preserves state");
}

void TestTextureStageQueriesFollowSavedState()
{
    Fixture f;
    CKDWORD value = 0;
    CKFFTextureStageSnapshot initial, cleared;
    f.FFP->SaveTextureStage(0, initial);
    f.Context->SetTextureStageState(0, CKRST_TSS_OP, 0);
    f.FFP->SaveTextureStage(0, cleared);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == 0,
              "a cleared unresolved value overrides the initial query default");
    f.FFP->RestoreTextureStage(0, initial);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == CKRST_TOP_MODULATE,
              "save/restore preserves initial query defaults");
    f.FFP->RestoreTextureStage(0, cleared);
    TestCheck(f.Context->GetTextureStageState(0, CKRST_TSS_OP, &value) && value == 0 &&
                  !f.FFP->IsTextureStageStateSet(0, CKRST_TSS_OP),
              "restore preserves explicit/unset distinction and public query together");
}

void TestAuthoritativeStateSnapshot()
{
    Fixture f;
    CKDWORD initial = 0, value = 0;
    f.Context->GetRenderState(VXRENDERSTATE_LIGHTING, &initial);
    const CKDWORD effective = f.FFP->GetRenderState(VXRENDERSTATE_LIGHTING);
    const CKViewportData viewport = f.FFP->GetViewport();
    CKFFStateGuard guard(*f.FFP);
    f.Context->SetRenderState(VXRENDERSTATE_LIGHTING, effective);
    f.Context->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, 0xFFFF0001u);
    CKViewportData temporary; temporary.ViewWidth = 3; temporary.ViewHeight = 5;
    f.Context->SetViewport(&temporary);
    CKMaterialData material = {}; material.SpecularPower = 19.0f;
    f.Context->SetMaterial(&material);
    guard.Restore();
    f.Context->GetRenderState(VXRENDERSTATE_LIGHTING, &value);
    TestCheck(value == initial && f.FFP->GetRenderState(VXRENDERSTATE_LIGHTING) == effective,
              "internal drawing restores query presence and effective defaults together");
    TestCheck(f.Context->GetViewportForTests().ViewWidth == viewport.ViewWidth &&
                  f.Context->GetMaterialForTests().SpecularPower == 0.0f,
              "viewport and material queries share the restored FFP state");
    f.Context->SetRenderState(VXRENDERSTATE_COLORWRITEENABLE, 0xFFFF0001u);
    f.Context->GetRenderState(VXRENDERSTATE_COLORWRITEENABLE, &value);
    TestCheck(value == 0xFFFF0001u && f.FFP->GetColorWriteMask() == CKRST_COLORWRITE_RED,
              "raw color write state is queryable while native state uses channel bits");
}

void TestMatricesLightsClipPlanes()
{
    Fixture f;
    VxMatrix m;
    Vx3DMatrixIdentity(m);
    m[3][0] = 5.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLD, m), "WORLD");
    TestCheck(f.FFP->GetWorldMatrix()[3][0] == 5.0f, "FFP world matrix");
    m[3][0] = 7.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_VIEW, m), "VIEW");
    TestCheck(f.FFP->GetViewMatrix()[3][0] == 7.0f, "FFP view matrix");
    m[3][0] = 9.0f;
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_PROJECTION, m), "PROJECTION");
    TestCheck(f.FFP->GetProjectionMatrix()[3][0] == 9.0f, "FFP projection matrix");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_TEXTURE(3), m), "TEXTURE3");
    TestCheck(f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(3), m), "WORLDMATRIX(3)");
    TestCheck(!f.Context->SetTransformMatrix(VXMATRIX_WORLDMATRIX(4), m), "WORLDMATRIX(4) rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_MATRIX_TYPE) == 1, "INVALID_MATRIX_TYPE counted");

    CKLightData light;
    memset(&light, 0, sizeof(light));
    light.Type = VX_LIGHTPOINT;
    light.Range = 100.0f;
    TestCheck(f.Context->SetLight(2, &light) && f.Context->EnableLight(2, TRUE), "light 2");
    TestCheck(!f.Context->SetLight(CKRST_MAX_LIGHTS, &light), "light 8 rejected");
    TestCheck(!f.Context->EnableLight(CKRST_MAX_LIGHTS, TRUE), "enable light 8 rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_LIGHT_INDEX) == 2, "INVALID_LIGHT_INDEX counted twice");

    VxPlane plane(VxVector(0.0f, 1.0f, 0.0f), 3.0f);
    VxPlane read;
    TestCheck(f.Context->SetUserClipPlane(2, plane) && f.Context->GetUserClipPlane(2, read), "clip plane 2");
    TestCheck(read.m_Normal.y == 1.0f && read.m_D == 3.0f, "clip plane round trip");
    TestCheck(!f.Context->SetUserClipPlane(CKRST_MAX_USER_CLIP_PLANES, plane), "clip plane 6 rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_CLIP_PLANE_INDEX) == 1, "INVALID_CLIP_PLANE_INDEX counted");

    CKMaterialData material;
    memset(&material, 0, sizeof(material));
    material.SpecularPower = 4.0f;
    TestCheck(f.Context->SetMaterial(&material), "material");
    CKViewportData viewport;
    viewport.ViewWidth = 32;
    viewport.ViewHeight = 16;
    TestCheck(f.Context->SetViewport(&viewport), "viewport");
}

void TestTextures()
{
    Fixture f;
    const CKDWORD created = f.Backend->CreatedTextureCount;
    const CKDWORD texture = MakeTexture(f, 32, 0);
    TestCheck(f.Backend->CreatedTextureCount == created + 1, "backend texture created");
    CKTextureDesc desc;
    TestCheck(f.Context->GetTextureDesc(texture, &desc) && desc.Format.Width == 32, "GetTextureDesc");

    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 32;
    image.Height = 32;
    image.BytesPerLine = 32 * 4;
    CKBYTE pixels[32 * 32 * 4];
    memset(pixels, 0x80, sizeof(pixels));
    image.Image = pixels;
    TestCheck(f.Context->LoadTexture(texture, image, 0, CKRST_CUBEFACE_XPOS, NULL), "LoadTexture level 0");
    TestCheck(f.Backend->UpdatedTextureCount == 1, "backend texture updated");
    TestCheck(!f.Context->LoadTexture(texture, image, 1, CKRST_CUBEFACE_XPOS, NULL), "missing mip level rejected");
    TestCheck(!f.Context->LoadTexture(texture, image, 0, CKRST_CUBEFACE_YNEG, NULL), "face on a 2D texture rejected");
    TestCheck(!f.Context->LoadTexture(0xDEAD, image, 0, CKRST_CUBEFACE_XPOS, NULL), "unknown handle rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_HANDLE) == 1, "REJECT_INVALID_HANDLE counted");

    TestCheck(f.Context->SetTexture(texture, 0), "SetTexture stage 0");
    TestCheck(f.FFP->GetTexture(0) == texture, "FFP texture bound");
    TestCheck(!f.Context->SetTexture(0xDEAD, 1), "unknown texture handle rejected");
    TestCheck(!f.Context->SetTexture(texture, CKRST_MAX_TEXTURE_STAGES), "stage 8 rejected");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_TEXTURE) == 1, "one live texture");
    TestCheck(f.Context->DeleteObject(texture, CKRST_OBJ_TEXTURE), "DeleteObject");
    TestCheck(f.FFP->GetTexture(0) == 0, "deleted texture unbound from the FFP");
    TestCheck(!f.Context->DeleteObject(texture, CKRST_OBJ_TEXTURE), "double delete rejected");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "no live resources");
}

void TestBuffers()
{
    Fixture f;
    const CKDWORD vb = MakeVertexBuffer(f, 4);
    const CKDWORD ib = MakeIndexBuffer(f, 6);
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_VERTEXBUFFER) == 1, "one live VB");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_INDEXBUFFER) == 1, "one live IB");

    void *first = f.Context->LockVertexBuffer(vb, 1, 2, CKRST_LOCK_DEFAULT);
    TestCheck(first != NULL, "partial lock");
    TestCheck(f.Context->LockVertexBuffer(vb, 0, 1, CKRST_LOCK_DEFAULT) == NULL, "double lock rejected");
    TestCheck(f.Context->UnlockVertexBuffer(vb), "unlock partial");
    TestCheck(!f.Context->UnlockVertexBuffer(vb), "unlock without lock rejected");
    TestCheck(f.Context->LockVertexBuffer(vb, 3, 2, CKRST_LOCK_DEFAULT) == NULL, "lock past the end rejected");
    TestCheck(f.Context->LockIndexBuffer(ib, 0, 7, CKRST_LOCK_DEFAULT) == NULL, "index lock past the end rejected");
    TestCheck(f.Context->LockVertexBuffer(0xDEAD, 0, 1, CKRST_LOCK_DEFAULT) == NULL, "unknown VB rejected");

    TestCheck(f.Context->FlushObjects(CKRST_OBJ_VERTEXBUFFER), "FlushObjects(VB)");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_VERTEXBUFFER) == 0, "VBs flushed");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_INDEXBUFFER) == 1, "IB untouched by the VB flush");
    TestCheck(f.Context->DeleteObject(ib, CKRST_OBJ_INDEXBUFFER), "delete IB");
}

void TestFrameFlowAndDraws()
{
    Fixture f;
    const CKDWORD vb = MakeVertexBuffer(f, 4);
    const CKDWORD ib = MakeIndexBuffer(f, 6);
    f.Backend->PassClears.clear();

    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH, 0xFF204060, 1.0f, 0, 0, NULL), "Clear");
    TestCheck(f.Backend->PassClears.size() == 1 && f.Backend->PassClears[0].Color == 0xFF204060, "backend clear recorded");
    TestCheck(f.Context->BeginScene(), "BeginScene");
    TestCheck(!f.Context->BeginScene(), "nested BeginScene rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_SCENE_STATE) == 1, "REJECT_SCENE_STATE counted");

    const CKDWORD drawsBefore = f.Backend->Log.DrawCount;
    f.Context->SetDebugMarker((CKSTRING)"quad");
    const CKBOOL firstDraw = f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 4, 0, 6);
    if (!firstDraw)
        printf("  FFP reject reason %d, diagnostics reject/unsupported %u/%u\n",
               (int)f.FFP->GetLastDrawRejectReason(),
               (unsigned)Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER),
               (unsigned)Diag(f.Context, CKRST_DIAG_REJECT_UNSUPPORTED_STATE));
    TestCheck(firstDraw, "DrawPrimitiveVBIB");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore + 1, "one backend draw");
    const CKDWORD firstPass = f.Backend->Log.DrawPasses[drawsBefore];

    const CKDWORD passesBefore = f.Context->GetPassCountForTests();
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_STENCIL, 0, 1.0f, 0x7, 0, NULL), "mid-scene stencil clear");
    TestCheck(f.Context->GetPassCountForTests() == passesBefore + 2, "stencil clear splits the scene pass");
    // Every pass configures its clear (scene passes with flags 0); the
    // stencil clear must be there with its own flags and value.
    bool stencilClearRecorded = false;
    for (size_t i = 0; i < f.Backend->PassClears.size(); ++i) {
        if (f.Backend->PassClears[i].Flags == CKRST_CTXCLEAR_STENCIL && f.Backend->PassClears[i].Stencil == 0x7)
            stencilClearRecorded = true;
    }
    TestCheck(stencilClearRecorded, "stencil clear recorded on its own pass");

    TestCheck(f.Context->DrawPrimitiveVB(VX_TRIANGLESTRIP, vb, 0, 4, NULL, 0), "DrawPrimitiveVB non-indexed");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore + 2, "second backend draw");
    TestCheck(f.Backend->Log.DrawPasses[drawsBefore + 1] > firstPass, "later draw lands on a later pass");

    CKWORD cpuIndices[6] = {0, 1, 2, 2, 1, 3};
    TestCheck(f.Context->DrawPrimitiveVB(VX_TRIANGLELIST, vb, 0, 4, cpuIndices, 6), "DrawPrimitiveVB with indices");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore + 3, "third backend draw");

    float positions[3][3] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    CKDWORD colors[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    VxDrawPrimitiveData dp;
    memset(&dp, 0, sizeof(dp));
    dp.VertexCount = 3;
    dp.Flags = CKRST_DP_TRANSFORM | CKRST_DP_DIFFUSE;
    dp.PositionPtr = positions;
    dp.PositionStride = sizeof(positions[0]);
    dp.ColorPtr = colors;
    dp.ColorStride = sizeof(colors[0]);
    TestCheck(f.Context->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &dp), "DrawPrimitive (software path)");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore + 4, "fourth backend draw");

    TestCheck(!f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, 0xDEAD, ib, 0, 4, 0, 6), "unknown VB rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_HANDLE) == 1, "REJECT_INVALID_HANDLE counted");
    TestCheck(!f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 4, 0, 2), "too few indices rejected");
    TestCheck(!f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 4, 3, 6), "index range past the IB rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_REJECT_INVALID_PARAMETER) == 2, "REJECT_INVALID_PARAMETER counted twice");

    TestCheck(!f.Context->BackToFront(FALSE), "present inside the scene rejected");
    TestCheck(f.Context->EndScene(), "EndScene");
    TestCheck(!f.Context->EndScene(), "EndScene twice rejected");
    const CKDWORD frames = f.Backend->FrameSerial;
    TestCheck(f.Context->BackToFront(FALSE), "BackToFront");
    TestCheck(f.Backend->FrameSerial == frames + 1, "backend Present() called once");
    const CKRenderStats *stats = f.Context->GetStats();
    TestCheck(stats->FrameNumber == 1, "frame counter");
    TestCheck(stats->DrawCalls == 4 && stats->Primitives == 2 + 2 + 2 + 1, "draw and primitive counters");
    TestCheck(stats->Passes == 4 && stats->Clears == 2,
              "pass and clear counters (clear/scene, stencil clear, scene, present)");
    TestCheck(f.Context->IsIdle(), "idle after present");

    // The next frame starts again at pass 0 and does not leak scratch buffers.
    TestCheck(f.Context->BeginScene() && f.Context->DrawPrimitiveVBIB(VX_TRIANGLELIST, vb, ib, 0, 4, 0, 6),
              "second frame draw");
    TestCheck(f.Backend->Log.DrawPasses[f.Backend->Log.DrawCount - 1] <= firstPass, "passes restart per frame");
    TestCheck(f.Context->EndScene() && f.Context->BackToFront(TRUE), "second frame presented");
}

void TestRenderTargets()
{
    Fixture f;
    const CKDWORD plain = MakeTexture(f, 32, 0);
    const CKDWORD target = MakeTexture(f, 64, CKRST_TEXTURE_RENDERTARGET);
    TestCheck(!f.Context->SetTargetTexture(plain, 32, 32, CKRST_CUBEFACE_XPOS), "non render-target texture rejected");
    TestCheck(Diag(f.Context, CKRST_DIAG_INVALID_TARGET) == 1, "INVALID_TARGET counted");
    TestCheck(!f.Context->SetTargetTexture(target, 64, 64, CKRST_CUBEFACE_YNEG), "cube face on a 2D target rejected");
    TestCheck(!f.Context->SetTargetTexture(target, 32, 32, CKRST_CUBEFACE_XPOS), "size mismatch rejected");
    TestCheck(f.Context->SetTargetTexture(target, 64, 64, CKRST_CUBEFACE_XPOS), "SetTargetTexture");
    TestCheck(f.Context->GetTargetForTests() == target, "target recorded");
    TestCheck(f.FFP->IsRenderTargetActive(), "pipeline told about the bound target");

    TestCheck(!f.Context->BeginOverlayPhase(), "overlay phase refused while a texture is the target");
    TestCheck(Diag(f.Context, CKRST_DIAG_OVERLAY_ON_TARGET) == 1, "OVERLAY_ON_TARGET counted");

    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_COLOR, 0, 1.0f, 0, 0, NULL) && f.Context->BeginScene(), "scene on the target");
    TestCheck(!f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "target change inside the scene rejected");
    TestCheck(f.Context->EndScene() && f.Context->BackToFront(FALSE), "present the target frame");
    TestCheck(f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "back to the backbuffer");
    TestCheck(f.Context->GetTargetForTests() == 0, "target cleared");
    TestCheck(!f.FFP->IsRenderTargetActive(), "pipeline told about the released target");

    TestCheck(f.Context->SetTargetTexture(target, 64, 64, CKRST_CUBEFACE_XPOS), "target again");
    TestCheck(f.Context->DeleteObject(target, CKRST_OBJ_TEXTURE), "deleting the target texture");
    TestCheck(f.Context->GetTargetForTests() == 0, "deleting the target texture releases the target");
}

void TestOverlayPhase()
{
    Fixture f;
    TestCheck(f.Context->BeginScene(), "BeginScene");
    TestCheck(!f.Context->BeginOverlayPhase(), "overlay inside the scene rejected");
    TestCheck(f.Context->EndScene(), "EndScene");
    const CKDWORD passes = f.Context->GetPassCountForTests();
    const CKDWORD drawsBefore = f.Backend->Log.DrawCount;
    TestCheck(f.Context->BeginOverlayPhase(), "BeginOverlayPhase");
    TestCheck(f.Context->GetPassCountForTests() == passes, "native-size overlay continues the scene pass");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore, "identity resolve skipped");
    TestCheck(f.Context->BackToFront(FALSE), "present");
    TestCheck(f.Backend->Log.DrawCount == drawsBefore + 1, "present blit submitted");

    CKRasterizerOptions options;
    options.RenderScale = 0.5f;
    TestCheck(f.Context->SetOptions(&options), "SetOptions(RenderScale 0.5)");
    TestCheck(f.Context->BeginScene() && f.Context->EndScene(), "scaled scene");
    const CKDWORD scaledPasses = f.Context->GetPassCountForTests();
    const CKDWORD draws = f.Backend->Log.DrawCount;
    TestCheck(f.Context->BeginOverlayPhase(), "overlay after a scaled scene");
    // The resolve opens the native-target pass and overlay draws continue in it.
    TestCheck(f.Context->GetPassCountForTests() == scaledPasses + 1, "combined resolve and overlay pass");
    TestCheck(f.Backend->Log.DrawCount == draws + 1, "resolve submitted");
    TestCheck(f.Context->BackToFront(FALSE), "present scaled frame");
}

// Spec 4.4: engine rectangles are window pixels; passes, clears and the
// viewport scissor are scaled to the scene target, the overlay is not.
void TestRenderScaleCoordinates()
{
    Fixture f;
    CKRasterizerOptions options;
    options.RenderScale = 0.5f;
    TestCheck(f.Context->SetOptions(&options), "SetOptions(RenderScale 0.5)");

    // The fixture window is 64x64: the scene target is 32x32.
    CKRECT clearRect = {8, 8, 40, 24};
    TestCheck(f.Context->Clear(CKRST_CTXCLEAR_COLOR, 0, 1.0f, 0, 1, &clearRect), "clear rect");
    TestCheck(!f.Backend->PassClears.empty(), "clear recorded");
    if (!f.Backend->PassClears.empty()) {
        const CKRECT &r = f.Backend->PassClears.back().Rect;
        TestCheck(r.left == 4 && r.top == 4 && r.right == 20 && r.bottom == 12,
                  "clear rectangles are scaled to the scene target");
    }
    TestCheck(f.Context->BeginScene(), "BeginScene");
    CKViewportData viewport;
    viewport.ViewX = 8;
    viewport.ViewY = 4;
    viewport.ViewWidth = 32;
    viewport.ViewHeight = 16;
    viewport.ViewZMin = 0.0f;
    viewport.ViewZMax = 1.0f;
    TestCheck(f.Context->SetViewport(&viewport), "viewport");
    CKRECT scissor;
    TestCheck(f.FFP->GetViewportScissor(&scissor) && scissor.left == 4 && scissor.top == 2 &&
                  scissor.right == 20 && scissor.bottom == 10,
              "the scene viewport scissor is scaled to the scene target");
    const float *remap = f.FFP->GetViewportRemap();
    TestCheck(remap[0] == 0.5f && remap[1] == 0.25f && remap[2] == -0.25f && remap[3] == 0.625f,
              "the viewport remap uses window pixels");
    TestCheck(f.Context->EndScene(), "EndScene");
    TestCheck(f.Context->BeginOverlayPhase(), "BeginOverlayPhase");
    TestCheck(f.FFP->GetViewportScissor(&scissor) && scissor.left == 8 && scissor.top == 4 &&
                  scissor.right == 40 && scissor.bottom == 20,
              "the overlay viewport scissor is not scaled");
    TestCheck(f.Context->BackToFront(FALSE), "present");

    // A texture target has no scaling at all.
    const CKDWORD target = MakeTexture(f, 64, CKRST_TEXTURE_RENDERTARGET);
    TestCheck(f.Context->SetTargetTexture(target, 64, 64, CKRST_CUBEFACE_XPOS), "SetTargetTexture");
    viewport.ViewX = 0;
    viewport.ViewY = 0;
    viewport.ViewWidth = 32;
    viewport.ViewHeight = 64;
    TestCheck(f.Context->SetViewport(&viewport), "target viewport");
    TestCheck(f.FFP->GetViewportScissor(&scissor) && scissor.right == 32 && scissor.bottom == 64,
              "target viewports use texture pixels");
    TestCheck(f.Context->SetTargetTexture(0, 0, 0, CKRST_CUBEFACE_XPOS), "release target");
}

void TestShutdown()
{
    Fixture f;
    const CKDWORD vb = MakeVertexBuffer(f, 4);
    (void)vb;
    TestCheck(f.Context->BeginScene(), "BeginScene");
    TestCheck(f.Context->BeginShutdown(), "BeginShutdown inside a scene ends it");
    TestCheck(f.Context->IsIdle(), "idle after shutdown");
    TestCheck(!f.Context->BeginScene(), "no scene after shutdown");
    TestCheck(f.Context->GetLiveResourceCountForTests(CKRST_OBJ_ALL) == 0, "resources dropped");
    TestCheck(f.Driver->DestroyContext(f.Context), "DestroyContext");
    f.Context = NULL;
    TestCheck(f.Driver->m_Contexts.Size() == 0, "context removed from the driver");
}

void TestPresentFailurePropagation()
{
    Fixture f;
    CKRasterizerOptions options;
    options.RenderScale = 0.5f;
    TestCheck(f.Context->SetOptions(&options), "scaled frame keeps the composite pass");
    TestCheck(f.Context->BeginScene() && f.Context->EndScene(), "frame for composite failure");
    f.Backend->Log.DrawError = CKERR_INVALIDOPERATION;
    f.Backend->Log.DrawErrorAt = f.Backend->Log.DrawCount + 1;
    TestCheck(!f.Context->BackToFront(FALSE), "composite failure reaches BackToFront");

    f.Backend->Log.DrawError = CK_OK;
    f.Backend->Log.DrawErrorAt = 0;
    TestCheck(f.Context->BeginScene() && f.Context->EndScene(), "frame for present-blit failure");
    f.Backend->Log.DrawError = CKERR_INVALIDOPERATION;
    f.Backend->Log.DrawErrorAt = f.Backend->Log.DrawCount + 2;
    TestCheck(!f.Context->BackToFront(FALSE), "internal present failure reaches BackToFront");
    f.Backend->Log.DrawError = CK_OK;
    f.Backend->Log.DrawErrorAt = 0;
}

void TestRasterizerOwnsShaderSelectionAndCatalogFailure()
{
    FFPTranslatedWorld world;
    FFPRecordingDriver *provider = world.BackendDriver();
    provider->FailShaderCatalog = TRUE;
    TestCheck(!world.CreateContext(64, 64) && world.Context &&
                  world.Context->GetDeviceStatus() != CK_OK,
              "missing rasterizer artifacts fail creation and roll the device back");
    TestCheck(provider->ShaderTargetQueries == 1 && provider->ShaderCatalogQueries == 1,
              "the rasterizer supplies device targets then resolves a catalog");

    provider->FailShaderCatalog = FALSE;
    TestCheck(world.Context->Create(NULL, 0, 0, 64, 64, 32, FALSE, 60, 24, 8),
              "the same context can retry after its catalog becomes available");
    TestCheck(provider->ShaderTargetQueries == 2 && provider->ShaderCatalogQueries == 2,
              "retry supplies a fresh shader selection exactly once");
    TestCheck(world.Context->BeginScene() && world.Context->EndScene() && world.Context->BackToFront(FALSE),
              "the retained catalog supports lazy presentation shader creation");
    TestCheck(provider->ShaderTargetQueries == 2 && provider->ShaderCatalogQueries == 2,
              "frame submission and lazy program creation never rediscover artifacts");
}

void TestDestroyContextPreservesFailedShutdown()
{
    Fixture f;
    f.Backend->ForceNotIdle = TRUE;
    f.World.BackendDriver()->ForceDestroyBusy = TRUE;
    TestCheck(!f.Driver->DestroyContext(f.Context), "DestroyContext rejects a failed shutdown");
    TestCheck(f.Driver->m_Contexts.Size() == 1 && f.Context->GetDeviceStatus() == CK_OK,
              "failed shutdown keeps the context and backend alive");

    f.Backend->ForceNotIdle = FALSE;
    f.World.BackendDriver()->ForceDestroyBusy = FALSE;
    TestCheck(f.Driver->DestroyContext(f.Context), "DestroyContext succeeds after the backend becomes idle");
    f.Context = NULL;
    f.World.Context = NULL;
    f.World.Backend = NULL;
}

} // namespace

int main()
{
    TestFramework framework;
    framework.Run("lifecycle", TestLifecycle);
    framework.Run("defaults reach the pipeline", TestDefaultsReachThePipeline);
    framework.Run("state round trip", TestStateRoundTrip);
    framework.Run("repeated compound texture states", TestRepeatedCompoundTextureStates);
    framework.Run("semantic texture stage reset", TestSemanticTextureStageReset);
    framework.Run("texture stage query snapshots", TestTextureStageQueriesFollowSavedState);
    framework.Run("authoritative state snapshot", TestAuthoritativeStateSnapshot);
    framework.Run("matrices, lights, clip planes", TestMatricesLightsClipPlanes);
    framework.Run("textures", TestTextures);
    framework.Run("buffers", TestBuffers);
    framework.Run("frame flow and draws", TestFrameFlowAndDraws);
    framework.Run("render targets", TestRenderTargets);
    framework.Run("overlay phase", TestOverlayPhase);
    framework.Run("RenderScale coordinates", TestRenderScaleCoordinates);
    framework.Run("present failures propagate", TestPresentFailurePropagation);
    framework.Run("rasterizer owns shader selection and catalog failures", TestRasterizerOwnsShaderSelectionAndCatalogFailure);
    framework.Run("failed shutdown preserves context", TestDestroyContextPreservesFailedShutdown);
    framework.Run("shutdown", TestShutdown);
    return framework.ExitCode();
}
