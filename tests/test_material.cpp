#include <stdio.h>
#include <stdlib.h>

#include "CKContext.h"
#include "CKDependencies.h"
#include "CKGlobals.h"
#include "CKRenderManager.h"
#include "CKStateChunk.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;

namespace {

void AddDriverTextureFormat(FFPDiagnosticDriver &driver, VX_PIXELFORMAT format) {
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(format, desc.Format);
    driver.m_TextureFormats.PushBack(desc);
}

struct MaterialTestWorld {
    MaterialTestWorld()
        : context(nullptr),
          renderManager(nullptr),
          renderContext(nullptr),
          rasterizer(&driver) {
        TestCheck(CKCreateContext(&context, nullptr, 0, 0) == CK_OK && context,
                  "CKCreateContext failed");

        renderManager = static_cast<RCKRenderManager *>(context->GetRenderManager());
        if (!renderManager)
            renderManager = new RCKRenderManager(context);
        TestCheck(renderManager != nullptr, "RCKRenderManager creation failed");

        AddDriverTextureFormat(driver, _32_ARGB8888);
        AddDriverTextureFormat(driver, _16_RGB565);

        renderContext = new RCKRenderContext(context);
        renderContext->m_RasterizerContext = &rasterizer;
        renderContext->m_RasterizerDriver = &driver;
    }

    ~MaterialTestWorld() {
        if (renderContext) {
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
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext rasterizer;
};

void FillTextureSlot(RCKTexture &texture, int slot, CKDWORD seed) {
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr(slot));
    TestCheck(pixels != nullptr, "texture surface must lock");
    const int count = texture.GetWidth() * texture.GetHeight();
    for (int i = 0; i < count; ++i)
        pixels[i] = seed + (CKDWORD)i;
    texture.ReleaseSurfacePtr(slot);
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
    matrix.Identity();
    matrix[3][0] = 3.25f;
    renderContext->SetTextureMatrix(matrix, stage);
    renderContext->m_FFPipeline.SetTextureStageState(
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
    world.rasterizer.FailUpdateTexture = TRUE;

    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, 0),
              "SetAsCurrent must fail when its texture upload fails");
    TestCheck(world.renderContext->m_FFPipeline.GetTexture(0) == 0,
              "failed material texture upload must not leave a texture bound");
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

    CKFFTextureStageSnapshot stage;
    world.renderContext->m_FFPipeline.SaveTextureStage(1, stage);
    TestCheck((stage.TextureFlags & CKRST_TEXTURE_CUBEMAP) != 0,
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
    world.rasterizer.FailUpdateTexture = TRUE;

    TestCheck(!material.SetAsCurrent(world.renderContext, TRUE, 0),
              "multi-texture material setup must fail when a secondary texture upload fails");
    TestCheck(world.renderContext->m_FFPipeline.GetTexture(1) == 0,
              "failed secondary texture upload must not leave the stage bound");
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

    CKStateChunk *chunk = material.Save(nullptr, CK_STATESAVE_MATERIALONLY);
    TestCheck(chunk != nullptr, "material Save failed");
    chunk->StartRead();
    TestCheck(material.Load(chunk, nullptr) == CK_OK, "material Load failed");
    DeleteCKStateChunk(chunk);

    TestCheck(material.GetEffectParameter() == effectParameter,
              "loading material state must not destroy its referenced effect parameter");
}

void SetEffectInitializesAndReleasesParameters() {
    MaterialTestWorld world;
    RCKMaterial material(world.context, "EffectLifecycle");

    material.SetEffect(VXEFFECT_TEXGEN);
    CKParameter *texGenParameter = material.GetEffectParameter();
    TestCheck(texGenParameter != nullptr,
              "setting an effect with a parameter type must create a parameter");

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
    CKFFTextureStageSnapshot stage;
    world.renderContext->m_FFPipeline.SaveTextureStage(0, stage);
    TestCheck(stage.TextureMatrix[3][0] == 3.25f,
              "SKIPTEXMAT callback matrix must survive material texture setup");
    TestCheck(world.renderContext->m_FFPipeline.GetTextureStageState(
                  0, CKRST_TSS_TEXTURETRANSFORMFLAGS) == CKRST_TTF_COUNT2,
              "SKIPTEXMAT callback transform flags must survive material texture setup");
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
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
    CKBuildClassHierarchyTable();

    TestFramework tests;
    tests.Run("Depth-writing alpha-test cutouts are not alpha transparent",
              &DepthWritingAlphaTestCutoutsAreNotAlphaTransparent);
    tests.Run("Texture slot bounds are checked",
              &TextureSlotBoundsAreChecked);
    tests.Run("SetAsCurrent propagates texture upload failure",
              &SetAsCurrentPropagatesTextureUploadFailure);
    tests.Run("Channel texture binding preserves texture flags",
              &ChannelTextureBindingPreservesTextureFlags);
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
    tests.Run("ZWrite changes invalidate transparency classification",
              &ZWriteChangesInvalidateTransparencyClassification);

    const int exitCode = tests.ExitCode();
    TestCheck(CKShutdown() == CK_OK, "CKShutdown failed");
    return exitCode;
}
