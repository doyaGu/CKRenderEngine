#include <stdio.h>
#include <stdlib.h>

#include "CKContext.h"
#include "CKDependencies.h"
#include "CKGlobals.h"
#include "CKParameter.h"
#include "CKRenderManager.h"
#include "CKStateChunk.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKTexture.h"
#include "FFPRecordingContext.h"
#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;

namespace {

void AddDriverTextureFormat(FFPRecordingDriver &driver, VX_PIXELFORMAT format) {
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(format, desc.Format);
    driver.AddTextureFormat(desc);
}

struct MaterialTestWorld {
    MaterialTestWorld()
        : context(nullptr),
          renderManager(nullptr),
          renderContext(nullptr),
          rasterizer(nullptr) {
        TestCheck(CKCreateContext(&context, nullptr, 0, 0) == CK_OK && context,
                  "CKCreateContext failed");

        renderManager = static_cast<RCKRenderManager *>(context->GetRenderManager());
        if (!renderManager)
            renderManager = new RCKRenderManager(context);
        TestCheck(renderManager != nullptr, "RCKRenderManager creation failed");

        AddDriverTextureFormat(*translated.BackendDriver(), _32_ARGB8888);
        AddDriverTextureFormat(*translated.BackendDriver(), _16_RGB565);

        TestCheck(translated.CreateContext(64, 64), "recording Context creation failed");
        rasterizer = translated.Backend;

        renderContext = new RCKRenderContext(context);
        renderContext->m_RasterizerContext = translated.Context;
        renderContext->m_RasterizerDriver = translated.Driver;
        TestCheck(renderManager->RegisterRasterizerContext(
                      translated.Context, renderContext->m_DriverIndex),
                  "rasterizer context registration failed");
    }

    ~MaterialTestWorld() {
        if (renderContext) {
            renderManager->ForgetRasterizerContext(
                renderContext->m_RasterizerContext);
            renderContext->m_RasterizerContext = nullptr;
            renderContext->m_RasterizerDriver = nullptr;
            delete renderContext;
            renderContext = nullptr;
        }
        if (context) {
            CKCloseContext(context);
            context = nullptr;
        }
    }

    CKContext *context;
    RCKRenderManager *renderManager;
    RCKRenderContext *renderContext;
    FFPRecordingWorld translated;
    FFPRecordingBackend *rasterizer;
};

void FillTextureSlot(RCKTexture &texture, int slot, CKDWORD seed) {
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr(slot));
    TestCheck(pixels != nullptr, "texture surface must lock");
    const int count = texture.GetWidth() * texture.GetHeight();
    for (int i = 0; i < count; ++i)
        pixels[i] = seed + (CKDWORD)i;
    texture.ReleaseSurfacePtr(slot);
}

void DrawMeshOnce(RCKMesh &mesh, RCKRenderContext *context,
                  FFPRecordingWorld &translated) {
    TestCheck(translated.Context->BeginScene(), "mesh BeginScene failed");
    TestCheck(mesh.DefaultRender(context, nullptr), "mesh draw failed");
    TestCheck(translated.Context->EndScene(), "mesh EndScene failed");
}

class HardwareMeshForTest : public RCKMesh {
public:
    HardwareMeshForTest(CKContext *context, CKSTRING name) : RCKMesh(context, name) {}

    void CorruptFirstPrimitiveIndex(CKWORD value) {
        TestCheck(m_MaterialGroups.Size() > 0 && m_MaterialGroups[0] &&
                      m_MaterialGroups[0]->m_Primitives.Size() > 0 &&
                      m_MaterialGroups[0]->m_Primitives[0].m_Indices.Size() > 0,
                  "mesh test requires one optimized primitive");
        m_MaterialGroups[0]->m_Primitives[0].m_Indices[0] = value;
        m_MaterialGroups[0]->m_Primitives[0].m_IndexBufferOffset = -1;
    }
};

void CheckResetTextureStage(CKRasterizerContext &context, int stage) {
    CKDWORD value = ~0u;
    TestCheck(context.GetTexture(stage, &value) && value == 0,
              "unused material stage must have no texture");
    for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state) {
        const CKDWORD expected = state == CKRST_TSS_TEXCOORDINDEX ? CKDWORD(stage) : 0;
        TestCheck(context.GetTextureStageState(stage, CKRST_TEXTURESTAGESTATETYPE(state), &value) &&
                      value == expected,
                  "unused material stage must expose the complete not-set state");
    }
    VxMatrix matrix, identity;
    identity.SetIdentity();
    TestCheck(context.GetTransformMatrix(VXMATRIX_TYPE(VXMATRIX_TEXTURE0 + stage), matrix) &&
                  memcmp(&matrix, &identity, sizeof(matrix)) == 0,
              "unused material stage must restore the identity texture transform");
}

void MaterialBindingClearsTailAndPreservesLowerStages() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "StageResetMaterial");
    RCKTexture texture(world.context, "StageResetTexture");
    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTextureSlot(texture, 0, 0xFF112233u);
    CKRasterizerContext &context = *world.translated.Context;
    for (int current : {0, 2}) {
        CKDWORD previousTextures[CKRST_MAX_TEXTURE_STAGES] = {};
        CKDWORD previousStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE] = {};
        VxMatrix previousMatrices[CKRST_MAX_TEXTURE_STAGES];
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, stage), "seed stage texture");
            // Valid state IDs retain arbitrary values at the rasterizer boundary.
            for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state)
                context.SetTextureStageState(stage, CKRST_TEXTURESTAGESTATETYPE(state), 0x1000u + state);
            previousMatrices[stage].SetIdentity();
            previousMatrices[stage][3][0] = float(stage + 1);
            context.SetTransformMatrix(VXMATRIX_TYPE(VXMATRIX_TEXTURE0 + stage), previousMatrices[stage]);
            context.GetTexture(stage, &previousTextures[stage]);
            for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state)
                context.GetTextureStageState(stage, CKRST_TEXTURESTAGESTATETYPE(state), &previousStates[stage][state]);
        }
        material.SetTexture(current, &texture);
        TestCheck(material.SetAsCurrent(world.renderContext, TRUE, current), "apply material to selected stage");
        for (int stage = 0; stage < current; ++stage) {
            CKDWORD value = 0;
            TestCheck(context.GetTexture(stage, &value) && value == previousTextures[stage],
                      "material on a later stage must preserve earlier texture bindings");
            for (CKDWORD state = CKRST_TSS_OP; state < CKRST_TSS_MAXSTATE; ++state)
                TestCheck(context.GetTextureStageState(stage, CKRST_TEXTURESTAGESTATETYPE(state), &value) &&
                              value == previousStates[stage][state],
                          "material on a later stage must preserve earlier stage states");
            VxMatrix matrix;
            context.GetTransformMatrix(VXMATRIX_TYPE(VXMATRIX_TEXTURE0 + stage), matrix);
            TestCheck(memcmp(&matrix, &previousMatrices[stage], sizeof(matrix)) == 0,
                      "material on a later stage must preserve earlier texture matrices");
        }
        for (int stage = current + 1; stage < CKRST_MAX_TEXTURE_STAGES; ++stage)
            CheckResetTextureStage(context, stage);
        CKDWORD bound = 0;
        TestCheck(context.GetTexture(current, &bound) && bound == previousTextures[current],
                  "material must bind its own texture after resetting the chain");
    }
}

VX_EFFECTCALLBACK_RETVAL OwnedTextureStagesCallback(CKRenderContext *context, CKMaterial *, int, void *) {
    context->SetTextureStageState(CKRST_TSS_CONSTANT, 0xFFAABBCCu, 7);
    return VXEFFECTRETVAL_SKIPALLTEX;
}

void MaterialPreservesCallbackOwnedTextureStages() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CallbackTexturesMaterial");
    VxEffectDescription effect;
    effect.Summary = "OwnedTextureStages";
    effect.SetCallback = &OwnedTextureStagesCallback;
    material.SetEffect(VX_EFFECT(world.renderManager->AddEffect(effect)));
    world.translated.Context->SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF112233u);
    TestCheck(material.SetAsCurrent(world.renderContext, TRUE, 0), "apply callback-owned material");
    CKDWORD value = 0;
    TestCheck(world.translated.Context->GetTextureStageState(0, CKRST_TSS_BORDERCOLOR, &value) && value == 0xFF112233u,
              "SKIPALLTEX must preserve the current texture stage");
    TestCheck(world.translated.Context->GetTextureStageState(7, CKRST_TSS_CONSTANT, &value) && value == 0xFFAABBCCu,
              "SKIPALLTEX must preserve later stages written by the effect callback");
}

VX_EFFECTCALLBACK_RETVAL CustomEffectCallback(CKRenderContext *, CKMaterial *, int, void *argument) {
    int *calls = static_cast<int *>(argument);
    if (calls)
        ++(*calls);
    return VXEFFECTRETVAL_SKIPNONE;
}

VX_EFFECTCALLBACK_RETVAL TextureMatrixEffectCallback(CKRenderContext *context,
                                                     CKMaterial *, int stage,
                                                     void *) {
    RCKRenderContext *renderContext = static_cast<RCKRenderContext *>(context);
    VxMatrix matrix;
    matrix.SetIdentity();
    matrix[3][0] = 3.25f;
    renderContext->SetTextureMatrix(matrix, stage);
    renderContext->m_RasterizerContext->SetTextureStageState(
        stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    return VXEFFECTRETVAL_SKIPTEXMAT;
}

void DepthWritingAlphaTestCutoutsAreNotAlphaTransparent() {
    CKContext context(nullptr, 0, 0);
    RCKMaterial material(&context, "CutoutMaterial");

    material.EnableAlphaBlend(TRUE);
    material.SetSourceBlend(VXBLEND_SRCALPHA);
    material.SetDestBlend(VXBLEND_INVSRCALPHA);
    material.EnableAlphaTest(TRUE);
    material.EnableZWrite(TRUE);

    TestCheck(!material.IsAlphaTransparent(),
              "Depth-writing alpha-test cutouts must not be sorted with true alpha-blend objects");

    material.EnableZWrite(FALSE);
    TestCheck(material.IsAlphaTransparent(),
              "Non-depth-writing alpha-blend materials must still be sorted as transparent");
}

void TextureSlotBoundsAreChecked() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "SlotBounds");
    RCKTexture texture(world.context, "SlotTexture");

    material.SetTexture(0, &texture);
    material.SetTexture(-1, reinterpret_cast<CKTexture *>(static_cast<uintptr_t>(0x11111111u)));
    material.SetTexture(4, reinterpret_cast<CKTexture *>(static_cast<uintptr_t>(0x22222222u)));

    TestCheck(material.GetTexture(0) == &texture,
              "invalid SetTexture indices must not modify slot 0");
    TestCheck(material.GetTexture(-1) == nullptr,
              "negative texture slot lookup must return null");
    TestCheck(material.GetTexture(4) == nullptr,
              "out-of-range texture slot lookup must return null");
    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, -1),
              "negative texture stage must be rejected");
    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, CKFF_MAX_TEXTURE_STAGES),
              "out-of-range texture stage must be rejected");
}

void SetAsCurrentPropagatesTextureUploadFailure() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "UploadFailureMaterial");
    RCKTexture texture(world.context, "UploadFailureTexture");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTextureSlot(texture, 0, 0xFF112233u);
    material.SetTexture(0, &texture);
    world.translated.Context->SetTextureStageState(7, CKRST_TSS_CONSTANT, 0xFFABCDEFu);
    world.rasterizer->FailUpdateTexture = TRUE;

    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, 0),
              "SetAsCurrent must fail when its texture upload fails");
    CKDWORD bound = 0xFFFFFFFFu;
    TestCheck(world.renderContext->m_RasterizerContext->GetTexture(0, &bound) && bound == 0,
              "failed material texture upload must not leave a texture bound");
    CheckResetTextureStage(*world.translated.Context, 7);
}

void ChannelTextureBindingPreservesTextureFlags() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CubeChannelMaterial");
    RCKTexture cube(world.context, "CubeChannelTexture");

    TestCheck(cube.Create(2, 2, 32, 0), "cube slot 0 Create failed");
    cube.SetCubeMap(TRUE);
    FillTextureSlot(cube, 0, 0xFF000100u);
    for (int i = 1; i < 6; ++i) {
        TestCheck(cube.Create(2, 2, 32, i), "cube face Create failed");
        FillTextureSlot(cube, i, 0xFF000100u + (CKDWORD)i * 0x100u);
    }

    material.SetTexture(0, &cube);
    TestCheck(material.BindTextureSlotToStage(world.renderContext, 0, 1),
              "BindTextureSlotToStage should bind cubemap texture");

    CKDWORD bound = 0;
    CKTextureDesc desc;
    TestCheck(world.renderContext->m_RasterizerContext->GetTexture(1, &bound) && bound != 0 &&
              world.renderContext->m_RasterizerContext->GetTextureDesc(bound, &desc) &&
              (desc.Flags & CKRST_TEXTURE_CUBEMAP) != 0,
              "channel texture binding must preserve cubemap texture flags");
}

void MultiTextureEffectPropagatesSecondaryUploadFailure() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "SecondaryUploadFailureMaterial");
    RCKTexture baseTexture(world.context, "BaseTexture");
    RCKTexture detailTexture(world.context, "DetailTexture");

    TestCheck(baseTexture.Create(2, 2, 32, 0), "base texture Create failed");
    TestCheck(detailTexture.Create(2, 2, 32, 0), "detail texture Create failed");
    FillTextureSlot(baseTexture, 0, 0xFF010203u);
    FillTextureSlot(detailTexture, 0, 0xFF102030u);

    TestCheck(baseTexture.SetAsCurrent(world.renderContext, FALSE, 0),
              "test setup should upload the base texture before inducing failures");
    material.SetTexture(0, &baseTexture);
    material.SetTexture(1, &detailTexture);
    material.SetEffect(VXEFFECT_2TEXTURES);
    world.rasterizer->FailUpdateTexture = TRUE;

    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, 0),
              "multi-texture material setup must fail when a secondary texture upload fails");
    CKDWORD bound = 0xFFFFFFFFu;
    TestCheck(world.renderContext->m_RasterizerContext->GetTexture(1, &bound) && bound == 0,
              "failed secondary texture upload must not leave the stage bound");
}

void MeshAdditionalPassPreservesMonoPassChannels() {
    MaterialTestWorld world;
    RCKMaterial base(world.context, "Base"), lightmap(world.context, "Lightmap"), glow(world.context, "Glow");
    RCKTexture baseTexture(world.context, "BaseTexture"), lightmapTexture(world.context, "LightmapTexture"), glowTexture(world.context, "GlowTexture");
    RCKMaterial *materials[] = {&base, &lightmap, &glow};
    RCKTexture *textures[] = {&baseTexture, &lightmapTexture, &glowTexture};
    for (int i = 0; i < 3; ++i) {
        TestCheck(textures[i]->Create(2, 2, 32, 0), "channel texture creation failed");
        FillTextureSlot(*textures[i], 0, 0xFF808080u);
        materials[i]->SetTexture(0, textures[i]);
    }
    RCKMesh mesh(world.context, "MixedChannels");
    TestCheck(mesh.SetVertexCount(3) && mesh.SetFaceCount(1), "mesh creation failed");
    VxVector positions[] = {VxVector(-0.5f, -0.5f, 0.5f), VxVector(0.5f, -0.5f, 0.5f), VxVector(0.0f, 0.5f, 0.5f)};
    VxVector normal(0, 0, -1);
    for (int i = 0; i < 3; ++i) {
        mesh.SetVertexPosition(i, &positions[i]);
        mesh.SetVertexNormal(i, &normal);
    }
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceMaterial(0, &base);
    const int modulate = mesh.AddChannel(&lightmap, TRUE);
    const int additive = mesh.AddChannel(&glow, TRUE);
    mesh.SetChannelSourceBlend(modulate, VXBLEND_ZERO);
    mesh.SetChannelDestBlend(modulate, VXBLEND_SRCCOLOR);
    mesh.SetChannelSourceBlend(additive, VXBLEND_ONE);
    mesh.SetChannelDestBlend(additive, VXBLEND_ONE);
    mesh.SetFlags(mesh.GetFlags() | VXMESH_RENDERCHANNELS);
    TestCheck(world.translated.Context->BeginScene(), "BeginScene failed");
    TestCheck(mesh.DefaultRender(world.renderContext, nullptr), "mesh draw failed");
    TestCheck(world.rasterizer->Log.DrawCount == 2, "base and additive must each draw");
    int lightmapDraws = 0;
    for (const auto &binding : world.rasterizer->Log.TextureBindings)
        if (binding.Stage == 1 && binding.Texture)
            ++lightmapDraws;
    TestCheck(lightmapDraws == 2, "DX8 keeps the mono-pass lightmap active during the additional channel draw");
    CKDWORD texture = 0xFFFFFFFFu;
    world.translated.Context->GetTexture(1, &texture);
    TestCheck(texture == 0, "mesh completion must retire its additional stages");
    world.translated.Context->EndScene();
}

void MeshBuffersAreIndependentPerContext() {
    MaterialTestWorld world;
    FFPRecordingWorld second;
    AddDriverTextureFormat(*second.BackendDriver(), _32_ARGB8888);
    TestCheck(second.CreateContext(64, 64),
              "second recording Context creation failed");

    RCKRenderContext secondContext(world.context);
    secondContext.m_RasterizerContext = second.Context;
    secondContext.m_RasterizerDriver = second.Driver;
    TestCheck(world.renderManager->RegisterRasterizerContext(
                  second.Context, secondContext.m_DriverIndex),
              "second rasterizer context registration failed");
    world.renderContext->SetFullViewport(
        &world.renderContext->m_ViewportData, 64, 64);
    secondContext.SetFullViewport(&secondContext.m_ViewportData, 64, 64);

    const CKDWORD firstDeletesBeforeMesh =
        world.rasterizer->DeletedObjectCount;
    const CKDWORD secondDeletesBeforeMesh =
        second.Backend->DeletedObjectCount;
    {
        RCKMaterial material(world.context, "TwoContextMeshMaterial");
        RCKMesh mesh(world.context, "TwoContextMesh");
        TestCheck(mesh.SetVertexCount(3) && mesh.SetFaceCount(1),
                  "mesh creation failed");
        VxVector positions[] = {
            VxVector(-0.5f, -0.5f, 0.5f),
            VxVector(0.5f, -0.5f, 0.5f),
            VxVector(0.0f, 0.5f, 0.5f),
        };
        VxVector normal(0.0f, 0.0f, -1.0f);
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexPosition(i, &positions[i]);
            mesh.SetVertexNormal(i, &normal);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFaceMaterial(0, &material);

        const CKDWORD firstCreates = world.rasterizer->CreatedBufferCount;
        const CKDWORD firstUpdates = world.rasterizer->UpdatedBufferCount;
        for (int i = 0; i < 4; ++i)
            DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount == firstCreates + 2 &&
                      world.rasterizer->UpdatedBufferCount == firstUpdates + 2,
                  "first context should create and upload one mesh VB and IB");
        TestCheck(world.rasterizer->Log.VertexBufferSetCount != 0 &&
                      world.rasterizer->Log.IndexBufferSetCount != 0,
                  "the warmed mesh should draw through its hardware buffers");

        const CKDWORD secondCreates = second.Backend->CreatedBufferCount;
        const CKDWORD secondUpdates = second.Backend->UpdatedBufferCount;
        DrawMeshOnce(mesh, &secondContext, second);
        TestCheck(second.Backend->CreatedBufferCount == secondCreates + 2 &&
                      second.Backend->UpdatedBufferCount == secondUpdates + 2,
                  "second context should create and upload its own mesh VB and IB");
        TestCheck(world.rasterizer->DeletedObjectCount ==
                      firstDeletesBeforeMesh,
                  "using a second context must not delete the first mesh buffers");

        const CKDWORD firstCreatesBeforeReturn =
            world.rasterizer->CreatedBufferCount;
        const CKDWORD firstUpdatesBeforeReturn =
            world.rasterizer->UpdatedBufferCount;
        DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount ==
                      firstCreatesBeforeReturn,
                  "switching back must not recreate the first context's mesh buffers");
        TestCheck(world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeReturn,
                  "switching back must not re-upload the first context's mesh buffers");

        positions[0].x -= 0.1f;
        mesh.SetVertexPosition(0, &positions[0]);
        const CKDWORD firstCreatesBeforeChange =
            world.rasterizer->CreatedBufferCount;
        const CKDWORD firstUpdatesBeforeChange =
            world.rasterizer->UpdatedBufferCount;
        const CKDWORD firstDeletesBeforeChange =
            world.rasterizer->DeletedObjectCount;
        for (int i = 0; i < 4; ++i)
            DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount ==
                      firstCreatesBeforeChange,
                  "a vertex edit must not recreate the first context's handles");
        TestCheck(world.rasterizer->DeletedObjectCount ==
                      firstDeletesBeforeChange,
                  "a vertex edit must not delete the first context's handles");
        TestCheck(world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeChange + 1,
                  "a vertex edit should upload only the first context's VB");
        TestCheck(world.rasterizer->LastBufferUpdateKind ==
                      CKRST_BUFFER_VERTEX &&
                      world.rasterizer->LastBufferUpdateMode ==
                      CKRST_BUFFER_UPDATE_DISCARD,
                  "mesh vertex refresh should use the discard update path");

        const CKDWORD secondCreatesBeforeChange =
            second.Backend->CreatedBufferCount;
        const CKDWORD secondUpdatesBeforeChange =
            second.Backend->UpdatedBufferCount;
        const CKDWORD secondDeletesBeforeChange =
            second.Backend->DeletedObjectCount;
        DrawMeshOnce(mesh, &secondContext, second);
        TestCheck(second.Backend->CreatedBufferCount ==
                      secondCreatesBeforeChange &&
                      second.Backend->UpdatedBufferCount ==
                      secondUpdatesBeforeChange + 1 &&
                      second.Backend->DeletedObjectCount ==
                      secondDeletesBeforeChange,
                  "the same vertex edit should refresh the second VB once");

        mesh.SetFaceVertexIndex(0, 0, 2, 1);
        const CKDWORD firstUpdatesBeforeIndices =
            world.rasterizer->UpdatedBufferCount;
        for (int i = 0; i < 4; ++i)
            DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeIndices + 1,
                  "changed indices should upload only the first context's IB");
        TestCheck(world.rasterizer->LastBufferUpdateKind ==
                      CKRST_BUFFER_INDEX &&
                      world.rasterizer->LastBufferUpdateMode ==
                      CKRST_BUFFER_UPDATE_DISCARD,
                  "changed indices should use the discard update path");

        const CKDWORD secondUpdatesBeforeIndices =
            second.Backend->UpdatedBufferCount;
        DrawMeshOnce(mesh, &secondContext, second);
        TestCheck(second.Backend->UpdatedBufferCount ==
                      secondUpdatesBeforeIndices + 1 &&
                      second.Backend->LastBufferUpdateKind ==
                      CKRST_BUFFER_INDEX,
                  "changed indices should refresh the second context's IB once");

        const CKDWORD firstCreatesBeforeWrap =
            world.rasterizer->CreatedBufferCount;
        const CKDWORD firstUpdatesBeforeWrap =
            world.rasterizer->UpdatedBufferCount;
        const CKDWORD firstDeletesBeforeWrap =
            world.rasterizer->DeletedObjectCount;
        mesh.SetWrapMode(VXTEXTUREWRAP_U);
        DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount ==
                      firstCreatesBeforeWrap &&
                      world.rasterizer->DeletedObjectCount ==
                      firstDeletesBeforeWrap &&
                      world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeWrap + 2,
                  "enabling wrap should refresh the existing VB and IB handles");

        mesh.SetWrapMode((VXTEXTURE_WRAPMODE)0);
        DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount ==
                      firstCreatesBeforeWrap &&
                      world.rasterizer->DeletedObjectCount ==
                      firstDeletesBeforeWrap &&
                      world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeWrap + 4,
                  "disabling wrap should restore the existing VB and IB handles");

        world.renderManager->ForgetRasterizerContext(second.Context);
        const CKDWORD firstCreatesBeforeForget =
            world.rasterizer->CreatedBufferCount;
        const CKDWORD firstUpdatesBeforeForget =
            world.rasterizer->UpdatedBufferCount;
        DrawMeshOnce(mesh, world.renderContext, world.translated);
        TestCheck(world.rasterizer->CreatedBufferCount ==
                      firstCreatesBeforeForget &&
                      world.rasterizer->UpdatedBufferCount ==
                      firstUpdatesBeforeForget,
                  "forgetting the second context must preserve the first mesh buffers");
    }

    TestCheck(world.rasterizer->DeletedObjectCount ==
                  firstDeletesBeforeMesh + 2 &&
                  second.Backend->DeletedObjectCount ==
                  secondDeletesBeforeMesh,
              "mesh destruction should delete only buffers on the live context");

    secondContext.m_RasterizerContext = nullptr;
    secondContext.m_RasterizerDriver = nullptr;
}

void MeshPartialDeletionRetriesOnlyTheFailedBuffer() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "PartialDeleteMaterial");
    RCKMesh mesh(world.context, "PartialDeleteMesh");
    TestCheck(mesh.SetVertexCount(3) && mesh.SetFaceCount(1), "mesh creation failed");
    VxVector positions[] = {
        VxVector(-0.5f, -0.5f, 0.5f),
        VxVector(0.5f, -0.5f, 0.5f),
        VxVector(0.0f, 0.5f, 0.5f),
    };
    VxVector normal(0.0f, 0.0f, -1.0f);
    for (int i = 0; i < 3; ++i) {
        mesh.SetVertexPosition(i, &positions[i]);
        mesh.SetVertexNormal(i, &normal);
    }
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceMaterial(0, &material);
    for (int i = 0; i < 4; ++i)
        DrawMeshOnce(mesh, world.renderContext, world.translated);

    const CKDWORD deletesBeforeFailure = world.rasterizer->DeletedObjectCount;
    world.rasterizer->FailDestroyObjectAfter = 2;
    TestCheck(!world.renderManager->DeleteMeshBuffers(&mesh),
              "mesh deletion must report a partially failed buffer pair");
    TestCheck(world.rasterizer->DeletedObjectCount == deletesBeforeFailure + 2,
              "mesh deletion must attempt both buffers before reporting failure");

    TestCheck(world.renderManager->DeleteMeshBuffers(&mesh),
              "mesh deletion must retry the retained buffer");
    TestCheck(world.rasterizer->DeletedObjectCount == deletesBeforeFailure + 3,
              "mesh retry must delete only the previously failed buffer");
}

void WrapAwareHardwareMeshRejectsOutOfRangeIndices() {
    MaterialTestWorld world;
    HardwareMeshForTest mesh(world.context, "InvalidWrapIndexMesh");
    TestCheck(mesh.SetVertexCount(3) && mesh.SetFaceCount(1), "mesh creation failed");
    VxVector positions[] = {
        VxVector(-0.5f, -0.5f, 0.5f),
        VxVector(0.5f, -0.5f, 0.5f),
        VxVector(0.0f, 0.5f, 0.5f),
    };
    VxVector normal(0.0f, 0.0f, -1.0f);
    for (int i = 0; i < 3; ++i) {
        mesh.SetVertexPosition(i, &positions[i]);
        mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexTextureCoordinates(i, (float)i, 0.0f, -1);
    }
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    DrawMeshOnce(mesh, world.renderContext, world.translated);
    mesh.CorruptFirstPrimitiveIndex(3);
    mesh.SetWrapMode(VXTEXTUREWRAP_U);

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGE(0);
    data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
    data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
    data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
    data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride);
    data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);

    const CKDWORD creates = world.rasterizer->CreatedBufferCount;
    const CKDWORD updates = world.rasterizer->UpdatedBufferCount;
    TestCheck(!mesh.CheckHWVertexBuffer(world.renderContext, world.translated.Context, &data),
              "wrap-aware hardware packing must reject an out-of-range source index");
    TestCheck(world.rasterizer->CreatedBufferCount == creates && world.rasterizer->UpdatedBufferCount == updates,
              "invalid wrap-aware indices must be rejected before allocating or uploading buffers");
}

void ForgettingLastContextDetachesSelectedMeshBuffers() {
    MaterialTestWorld world;
    world.renderContext->SetFullViewport(
        &world.renderContext->m_ViewportData, 64, 64);

    const CKDWORD deletesBeforeMesh =
        world.rasterizer->DeletedObjectCount;
    {
        RCKMaterial material(world.context, "ForgottenContextMaterial");
        RCKMesh mesh(world.context, "ForgottenContextMesh");
        TestCheck(mesh.SetVertexCount(3) && mesh.SetFaceCount(1),
                  "mesh creation failed");
        VxVector positions[] = {
            VxVector(-0.5f, -0.5f, 0.5f),
            VxVector(0.5f, -0.5f, 0.5f),
            VxVector(0.0f, 0.5f, 0.5f),
        };
        VxVector normal(0.0f, 0.0f, -1.0f);
        for (int i = 0; i < 3; ++i) {
            mesh.SetVertexPosition(i, &positions[i]);
            mesh.SetVertexNormal(i, &normal);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 2);
        mesh.SetFaceMaterial(0, &material);

        for (int i = 0; i < 4; ++i)
            DrawMeshOnce(mesh, world.renderContext, world.translated);

        world.renderManager->ForgetRasterizerContext(
            world.translated.Context);
    }

    TestCheck(world.rasterizer->DeletedObjectCount == deletesBeforeMesh,
              "a mesh must not delete buffers through a forgotten Context");
}

void AdditionalTexturesSaveWithoutEffect() {
    MaterialTestWorld world;
    RCKMaterial source(world.context, "MultiTextureSource");
    RCKMaterial loaded(world.context, "MultiTextureLoaded");
    RCKTexture texture1(world.context, "TextureSlot1");

    source.SetTexture(1, &texture1);

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_MATERIALONLY);
    TestCheck(chunk != nullptr, "material Save failed");
    chunk->StartRead();
    TestCheck(loaded.Load(chunk, nullptr) == CK_OK, "material Load failed");
    DeleteCKStateChunk(chunk);

    TestCheck(loaded.GetTexture(1) == &texture1,
              "additional material textures must survive save/load even without an effect");
}

void LoadPreservesUnspecifiedEffectState() {
    MaterialTestWorld world;
    RCKMaterial source(world.context, "NoEffectSource");
    RCKMaterial loaded(world.context, "EffectLoaded");

    loaded.SetEffect(VXEFFECT_TEXGEN);
    CKParameter *effectParameter = loaded.GetEffectParameter();
    TestCheck(loaded.GetEffect() == VXEFFECT_TEXGEN,
              "test setup should assign a material effect");
    TestCheck(effectParameter != nullptr,
              "test setup should create an effect parameter");

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_MATERIALONLY);
    TestCheck(chunk != nullptr, "material Save failed");
    chunk->StartRead();
    TestCheck(loaded.Load(chunk, nullptr) == CK_OK, "material Load failed");
    DeleteCKStateChunk(chunk);

    TestCheck(loaded.GetEffect() == VXEFFECT_TEXGEN,
              "loading partial material state must preserve unspecified effect bits");
    TestCheck(loaded.GetEffectParameter() == effectParameter,
              "loading partial material state must preserve an unspecified effect parameter");
}

void LoadPreservesReferencedEffectParameter() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "EffectMaterial");

    material.SetEffect(VXEFFECT_TEXGEN);
    CKParameter *effectParameter = material.GetEffectParameter();
    TestCheck(effectParameter != nullptr,
              "test setup should create an effect parameter");
    CK_ID effectParameterId = effectParameter->GetID();

    CKStateChunk *chunk = material.Save(nullptr, CK_STATESAVE_MATERIALONLY);
    TestCheck(chunk != nullptr, "material Save failed");
    chunk->StartRead();
    TestCheck(material.Load(chunk, nullptr) == CK_OK, "material Load failed");
    DeleteCKStateChunk(chunk);

    TestCheck(world.context->GetObject(effectParameterId) == effectParameter,
              "loading material state must not destroy its referenced effect parameter");
}

void SetEffectInitializesAndReleasesParameters() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "EffectLifecycle");

    material.SetEffect(VXEFFECT_TEXGEN);
    CKParameter *texGenParameter = material.GetEffectParameter();
    TestCheck(texGenParameter != nullptr,
              "setting an effect with a parameter type must create a parameter");
    CKDWORD texGenMode = VXEFFECT_TGNONE;
    TestCheck(texGenParameter && texGenParameter->GetValue(&texGenMode, TRUE) == CK_OK &&
                  texGenMode == VXEFFECT_TGREFLECT,
              "TexGen effect parameters must initialize to Reflect");

    VxEffectDescription callbackOnlyEffect;
    callbackOnlyEffect.Summary = "CallbackOnly";
    callbackOnlyEffect.SetCallback = &CustomEffectCallback;
    int effectIndex = world.renderManager->AddEffect(callbackOnlyEffect);
    material.SetEffect(static_cast<VX_EFFECT>(effectIndex));

    TestCheck(material.GetEffect() == static_cast<VX_EFFECT>(effectIndex),
              "custom effect index should be stored on the material");
    TestCheck(material.GetEffectParameter() == nullptr,
              "switching to an effect without parameter type must release the old parameter");
}

void CustomEffectCallbackRunsWhenMaterialIsCurrent() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CustomEffectMaterial");
    int calls = 0;

    VxEffectDescription customEffect;
    customEffect.Summary = "CustomCallback";
    customEffect.SetCallback = &CustomEffectCallback;
    customEffect.CallbackArg = &calls;
    int effectIndex = world.renderManager->AddEffect(customEffect);

    material.SetEffect(static_cast<VX_EFFECT>(effectIndex));
    TestCheck(material.SetAsCurrent(world.renderContext, TRUE, 0),
              "SetAsCurrent should succeed for callback-only custom effects");
    TestCheck(calls == 1,
              "registered material effect callback must run when the material is set current");
}

void CustomEffectTextureMatrixSurvivesMaterialSetup() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CustomTextureMatrixMaterial");

    VxEffectDescription customEffect;
    customEffect.Summary = "CustomTextureMatrix";
    customEffect.SetCallback = &TextureMatrixEffectCallback;
    int effectIndex = world.renderManager->AddEffect(customEffect);

    material.SetEffect(static_cast<VX_EFFECT>(effectIndex));
    TestCheck(material.SetAsCurrent(world.renderContext, TRUE, 0),
              "SetAsCurrent should preserve callback-owned texture matrices");
    VxMatrix textureMatrix;
    Vx3DMatrixIdentity(textureMatrix);
    CKDWORD transformFlags = 0;
    TestCheck(world.renderContext->m_RasterizerContext->GetTransformMatrix(VXMATRIX_TEXTURE0, textureMatrix) &&
                  textureMatrix[3][0] == 3.25f,
              "SKIPTEXMAT callback matrix must survive material texture setup");
    TestCheck(world.renderContext->m_RasterizerContext->GetTextureStageState(
                  0, CKRST_TSS_TEXTURETRANSFORMFLAGS, &transformFlags) && transformFlags == CKRST_TTF_COUNT2,
              "SKIPTEXMAT callback transform flags must survive material texture setup");
}

void CubeTexGenPreservesThreeCoordinates() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CubeTexGen");

    TestCheck(material.TexGenEffect(world.renderContext, VXEFFECT_TGREFLECT, nullptr, 0) != 0,
              "reflection TexGen setup must succeed");
    CKDWORD texcoordIndex = 0;
    CKDWORD flags = CKRST_TTF_NONE;
    TestCheck(world.renderContext->m_RasterizerContext->GetTextureStageState(
                  0, CKRST_TSS_TEXCOORDINDEX, &texcoordIndex) &&
                  CKRSTTexcoordIndex(texcoordIndex) == 0 &&
                  CKRSTTexcoordGeneration(texcoordIndex) == CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR,
              "reflection TexGen must select the camera-space reflection vector");
    TestCheck(world.renderContext->m_RasterizerContext->GetTextureStageState(
                  0, CKRST_TSS_TEXTURETRANSFORMFLAGS, &flags) && flags == CKRST_TTF_COUNT2,
              "two-dimensional reflection TexGen must output two coordinates");
    VxMatrix textureMatrix;
    Vx3DMatrixIdentity(textureMatrix);
    TestCheck(world.renderContext->m_RasterizerContext->GetTransformMatrix(VXMATRIX_TEXTURE0, textureMatrix) &&
                  textureMatrix[0][0] == 0.4f && textureMatrix[1][1] == -0.4f &&
                  textureMatrix[2][2] == 0.4f && textureMatrix[3][0] == 0.5f &&
                  textureMatrix[3][1] == 0.5f,
              "reflection TexGen must install the D3D8-compatible 2D mapping matrix");

    const VX_EFFECTTEXGEN effects[] = {
        VXEFFECT_TGCUBEMAP_REFLECT,
        VXEFFECT_TGCUBEMAP_NORMALS,
        VXEFFECT_TGCUBEMAP_SKYMAP,
        VXEFFECT_TGCUBEMAP_POSITIONS,
    };

    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i) {
        TestCheck(material.TexGenEffect(world.renderContext, effects[i], nullptr, 0) != 0,
                  "cubemap TexGen setup must succeed");
        CKDWORD flags = CKRST_TTF_NONE;
        TestCheck(world.renderContext->m_RasterizerContext->GetTextureStageState(
                      0, CKRST_TSS_TEXTURETRANSFORMFLAGS, &flags) && flags == CKRST_TTF_COUNT3,
                  "cubemap TexGen must preserve its three-dimensional direction");
    }

    TestCheck(material.TexGenEffect(world.renderContext, VXEFFECT_TGPLANAR, nullptr, 0) != 0,
              "planar TexGen setup must succeed");
    flags = CKRST_TTF_NONE;
    TestCheck(world.renderContext->m_RasterizerContext->GetTextureStageState(
                  0, CKRST_TSS_TEXTURETRANSFORMFLAGS, &flags) && flags == CKRST_TTF_COUNT2,
              "planar TexGen remains two-dimensional");
}

void CubeReflectionRemovesCameraRotation() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "CubeReflection");
    VxMatrix view;
    view.SetIdentity();
    view[1][1] = 0.8f; view[1][2] = -0.6f;
    view[2][1] = 0.6f; view[2][2] = 0.8f;
    view[3][0] = -100.0f; view[3][2] = 10.0f;
    world.renderContext->SetViewTransformationMatrix(view);
    TestCheck(material.TexGenEffect(world.renderContext, VXEFFECT_TGCUBEMAP_REFLECT, nullptr, 0) != 0,
              "cube reflection setup succeeds");
    VxMatrix texture;
    TestCheck(world.renderContext->m_RasterizerContext->GetTransformMatrix(VXMATRIX_TEXTURE0, texture),
              "cube reflection installs a texture matrix");
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            TestCheck(fabsf(texture[row][col] - view[col][row]) < 0.00001f,
                      "cube reflection removes the camera rotation as on CKDX8Rasterizer");
    TestCheck(texture[3][0] == 0 && texture[3][1] == 0 && texture[3][2] == 0,
              "reflection directions must not include camera translation");
}

void ZWriteChangesInvalidateTransparencyClassification() {
    CKContext context(nullptr, 0, 0);
    RCKMaterial material(&context, "ZWriteTransparency");

    material.EnableAlphaBlend(TRUE);
    material.SetDestBlend(VXBLEND_INVSRCALPHA);
    material.EnableAlphaTest(TRUE);
    material.EnableZWrite(TRUE);
    g_UpdateTransparency = FALSE;

    material.EnableZWrite(FALSE);
    TestCheck(g_UpdateTransparency,
              "ZWrite changes must invalidate transparency classification when alpha-test sorting depends on it");
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();
    TestCheck(CKStartUp() == CK_OK, "CKStartUp failed");
    CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
    CKBuildClassHierarchyTable();

    TestFramework tests;
    tests.Run("Depth-writing alpha-test cutouts are not alpha transparent",
              &DepthWritingAlphaTestCutoutsAreNotAlphaTransparent);
    tests.Run("Texture slot bounds are checked",
              &TextureSlotBoundsAreChecked);
    tests.Run("Material binding clears tail and preserves lower stages",
              &MaterialBindingClearsTailAndPreservesLowerStages);
    tests.Run("Material preserves callback-owned texture stages",
              &MaterialPreservesCallbackOwnedTextureStages);
    tests.Run("SetAsCurrent propagates texture upload failure",
              &SetAsCurrentPropagatesTextureUploadFailure);
    tests.Run("Channel texture binding preserves texture flags",
              &ChannelTextureBindingPreservesTextureFlags);
    tests.Run("Mesh additional pass preserves mono-pass channels", &MeshAdditionalPassPreservesMonoPassChannels);
    tests.Run("Mesh buffers are independent per context",
              &MeshBuffersAreIndependentPerContext);
    tests.Run("Mesh partial deletion retries only the failed buffer",
              &MeshPartialDeletionRetriesOnlyTheFailedBuffer);
    tests.Run("Wrap-aware hardware mesh rejects out-of-range indices",
              &WrapAwareHardwareMeshRejectsOutOfRangeIndices);
    tests.Run("Forgetting the last context detaches selected mesh buffers",
              &ForgettingLastContextDetachesSelectedMeshBuffers);
    tests.Run("Multi-texture effect propagates secondary upload failure",
              &MultiTextureEffectPropagatesSecondaryUploadFailure);
    tests.Run("Additional textures save without effect",
              &AdditionalTexturesSaveWithoutEffect);
    tests.Run("Load preserves unspecified effect state",
              &LoadPreservesUnspecifiedEffectState);
    tests.Run("Load preserves a referenced effect parameter",
              &LoadPreservesReferencedEffectParameter);
    tests.Run("SetEffect initializes and releases parameters",
              &SetEffectInitializesAndReleasesParameters);
    tests.Run("Custom effect callback runs when material is current",
              &CustomEffectCallbackRunsWhenMaterialIsCurrent);
    tests.Run("Custom effect texture matrix survives material setup",
              &CustomEffectTextureMatrixSurvivesMaterialSetup);
    tests.Run("Cubemap TexGen preserves three coordinates",
              &CubeTexGenPreservesThreeCoordinates);
    tests.Run("Cube reflection removes camera rotation", &CubeReflectionRemovesCameraRotation);
    tests.Run("ZWrite changes invalidate transparency classification",
              &ZWriteChangesInvalidateTransparencyClassification);

    const int exitCode = tests.ExitCode();
    TestCheck(CKShutdown() == CK_OK, "CKShutdown failed");
    return exitCode;
}
