#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CKContext.h"
#include "CKDependencies.h"
#include "CKGlobals.h"
#include "CKStateChunk.h"
#include "CKRenderedScene.h"
#include "RCKCamera.h"
#include "RCK2dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKTexture.h"
#include "RCKSprite.h"
#include "RCKVertexBuffer.h"
#include "FFPRecordingContext.h"
#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

void AddDriverTextureFormat(FFPRecordingDriver &driver, VX_PIXELFORMAT format) {
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(format, desc.Format);
    driver.AddTextureFormat(desc);
}

struct TextureTestWorld {
    TextureTestWorld()
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
        AddDriverTextureFormat(*translated.BackendDriver(), _16_ARGB4444);

        TestCheck(translated.CreateContext(64, 64), "recording Context creation failed");
        rasterizer = translated.Backend;

        renderContext = new RCKRenderContext(context);
        renderContext->m_RasterizerContext = translated.Context;
        renderContext->m_RasterizerDriver = translated.Driver;
        TestCheck(renderManager->RegisterRasterizerContext(
                      translated.Context, renderContext->m_DriverIndex),
                  "rasterizer context registration failed");
        renderContext->m_WindowSettings.m_Rect.left = 0;
        renderContext->m_WindowSettings.m_Rect.top = 0;
        renderContext->m_WindowSettings.m_Rect.right = 64;
        renderContext->m_WindowSettings.m_Rect.bottom = 64;
        renderContext->m_WindowSettings.m_Bpp = 32;
        renderContext->m_WindowSettings.m_Zbpp = 24;
        renderContext->m_WindowSettings.m_StencilBpp = 8;
        renderContext->m_Settings = renderContext->m_WindowSettings;
    }

    ~TextureTestWorld() {
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

void FillTexture(RCKTexture &texture, CKDWORD seed) {
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr());
    TestCheck(pixels != nullptr, "texture surface must lock");
    const int count = texture.GetWidth() * texture.GetHeight();
    for (int i = 0; i < count; ++i)
        pixels[i] = seed + (CKDWORD)i;
    texture.ReleaseSurfacePtr();
}

void FillSprite(RCKSprite &sprite, CKDWORD seed) {
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(sprite.LockSurfacePtr());
    TestCheck(pixels != nullptr, "sprite surface must lock");
    const int count = sprite.GetWidth() * sprite.GetHeight();
    for (int i = 0; i < count; ++i)
        pixels[i] = seed + (CKDWORD)i;
    TestCheck(sprite.ReleaseSurfacePtr(), "sprite surface must release");
}

void DrawSprite(RCKSprite &sprite, RCKRenderContext &context,
                FFPRecordingWorld &translated) {
    TestCheck(translated.Context->BeginScene(), "begin sprite scene failed");
    TestCheck(sprite.Draw(&context) == CK_OK, "sprite draw failed");
    TestCheck(translated.Context->EndScene(), "end sprite scene failed");
}

const CKRST_DPFLAGS StandaloneVertexFormat =
    static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_DIFFUSE);

void FillVertexBuffer(CKVertexBuffer *buffer, RCKRenderContext *context,
                      CKLOCKFLAGS flags, float offset) {
    VxDrawPrimitiveData *data = buffer->Lock(context, 0, 3, flags);
    TestCheck(data != nullptr, "vertex buffer lock failed");

    const VxVector positions[3] = {
        VxVector(-0.5f + offset, -0.5f, 0.0f),
        VxVector(0.5f + offset, -0.5f, 0.0f),
        VxVector(offset, 0.5f, 0.0f),
    };
    for (int i = 0; i < 3; ++i) {
        *reinterpret_cast<VxVector *>(
            static_cast<CKBYTE *>(data->PositionPtr) +
            i * data->PositionStride) = positions[i];
        *reinterpret_cast<CKDWORD *>(
            static_cast<CKBYTE *>(data->ColorPtr) +
            i * data->ColorStride) = 0xFFFFFFFFu;
    }
    buffer->Unlock(context);
}

void DrawVertexBuffer(CKVertexBuffer *buffer, RCKRenderContext &context,
                      FFPRecordingWorld &translated) {
    TestCheck(translated.Context->BeginScene(),
              "begin vertex buffer scene failed");
    TestCheck(buffer->Draw(&context, VX_TRIANGLELIST, nullptr, 0, 0, 3),
              "vertex buffer draw failed");
    TestCheck(translated.Context->EndScene(),
              "end vertex buffer scene failed");
}

VxVector ReadPackedPosition(const FFPRecordingBackend &backend, CKDWORD vertexIndex, CKDWORD nativeStride) {
    VxVector position;
    const CKBYTE *source = backend.LastBufferUpdateData.data() + vertexIndex * nativeStride;
    memcpy(&position, source, sizeof(position));
    return position;
}

void StandardTextureUploadPreservesSourceFormat() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "SourceFormat");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF102030u);
    texture.SetDesiredVideoFormat(_16_RGB565);

    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "SystemToVideoMemory should succeed");
    TestCheck(world.rasterizer->CreatedTextureCount == 1,
              "texture should be created once");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer->LastTextureDesc.Format) == _32_ARGB8888,
              "regular textures should be created with the source image format");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer->LastTextureUpdateDesc) == _32_ARGB8888,
              "Restore upload should preserve the source image format");
    TestCheck(world.rasterizer->LastTextureUpdateDesc.BytesPerLine == 4 * 4,
              "source-format texture upload pitch should remain one row");
    TestCheck(texture.GetVideoPixelFormat() == _32_ARGB8888,
              "reported video format should match the created source format");
}

void SetAsCurrentFailureDoesNotBindOrClearRestoreFlag() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "FailCreateUpload");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF405060u);
    world.rasterizer->FailUpdateTexture = TRUE;

    TestCheck(!texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "SetAsCurrent should fail when upload fails");
    TestCheck(!texture.IsInVideoMemory(),
              "failed upload must not leave the texture marked in video memory");
    TestCheck(texture.ToRestore(),
              "failed upload must keep the restore flag set");
    CKDWORD bound = 0xFFFFFFFFu;
    TestCheck(world.renderContext->m_RasterizerContext->GetTexture(0, &bound) && bound == 0,
              "failed upload must not bind the texture stage");
}

void RestoreFailureKeepsDirtyFlag() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "FailRestore");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF607080u);
    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "initial upload should succeed");
    TestCheck(!texture.ToRestore(), "successful upload should clear restore flag");

    FillTexture(texture, 0xFF708090u);
    world.rasterizer->FailUpdateTexture = TRUE;
    TestCheck(!texture.Restore(FALSE), "Restore should report update failure");
    TestCheck(texture.ToRestore(), "failed Restore must keep restore flag set");
}

void TextureReplacementStopsWhenDeleteFails() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "FailTextureDelete");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF203040u);
    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "initial texture upload failed");
    const CKDWORD createdBeforeFailure = world.rasterizer->CreatedTextureCount;
    const CKDWORD deletedBeforeFailure = world.rasterizer->DeletedObjectCount;

    world.rasterizer->FailDestroyObject = TRUE;
    TestCheck(!texture.SystemToVideoMemory(world.renderContext, FALSE),
              "texture replacement must report deletion failure");
    TestCheck(world.rasterizer->CreatedTextureCount == createdBeforeFailure,
              "texture replacement must not create after deletion failure");
    TestCheck(world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 1,
              "texture replacement must attempt the failed deletion once");
    world.rasterizer->FailDestroyObject = FALSE;
    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "texture replacement must retry after deletion recovers");
    TestCheck(world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 2 &&
                  world.rasterizer->CreatedTextureCount == createdBeforeFailure + 1,
              "texture retry must delete the retained handle before creating its replacement");
}

void SpriteReplacementStopsWhenDeleteFails() {
    TextureTestWorld world;
    RCKSprite sprite(world.context, "FailSpriteDelete");

    TestCheck(sprite.Create(2, 2, 32, 0), "sprite Create failed");
    FillSprite(sprite, 0xFF506070u);
    TestCheck(sprite.SystemToVideoMemory(world.renderContext, FALSE),
              "initial sprite upload failed");
    const CKDWORD createdBeforeFailure = world.rasterizer->CreatedTextureCount;
    const CKDWORD deletedBeforeFailure = world.rasterizer->DeletedObjectCount;

    world.rasterizer->FailDestroyObject = TRUE;
    TestCheck(!sprite.SystemToVideoMemory(world.renderContext, FALSE),
              "sprite replacement must report deletion failure");
    TestCheck(world.rasterizer->CreatedTextureCount == createdBeforeFailure,
              "sprite replacement must not create after deletion failure");
    TestCheck(world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 1,
              "sprite replacement must attempt the failed deletion once");
    world.rasterizer->FailDestroyObject = FALSE;
    TestCheck(sprite.SystemToVideoMemory(world.renderContext, FALSE),
              "sprite replacement must retry after deletion recovers");
    TestCheck(world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 2 &&
                  world.rasterizer->CreatedTextureCount == createdBeforeFailure + 1,
              "sprite retry must delete the retained handle before creating its replacement");
}

void TexturePartialDeletionRetriesOnlyTheFailedContext() {
    TextureTestWorld world;
    FFPRecordingWorld second;
    AddDriverTextureFormat(*second.BackendDriver(), _32_ARGB8888);
    TestCheck(second.CreateContext(64, 64), "second recording Context creation failed");

    RCKRenderContext secondContext(world.context);
    secondContext.m_RasterizerContext = second.Context;
    secondContext.m_RasterizerDriver = second.Driver;
    TestCheck(world.renderManager->RegisterRasterizerContext(second.Context, secondContext.m_DriverIndex),
              "second rasterizer context registration failed");

    RCKTexture texture(world.context, "PartialTextureDelete");
    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF304050u);
    TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0), "first context upload failed");
    TestCheck(texture.SetAsCurrent(&secondContext, FALSE, 0), "second context upload failed");

    const CKDWORD firstDeletes = world.rasterizer->DeletedObjectCount;
    const CKDWORD secondDeletes = second.Backend->DeletedObjectCount;
    world.rasterizer->FailDestroyObject = TRUE;
    TestCheck(!texture.FreeVideoMemory(), "partial deletion must report the failed context");
    TestCheck(texture.IsInVideoMemory(), "partial deletion must retain the failed context handle");
    TestCheck(world.rasterizer->DeletedObjectCount == firstDeletes + 1 &&
                  second.Backend->DeletedObjectCount == secondDeletes + 1,
              "partial deletion must attempt every context once");

    world.rasterizer->FailDestroyObject = FALSE;
    TestCheck(texture.FreeVideoMemory(), "partial deletion must retry after recovery");
    TestCheck(!texture.IsInVideoMemory(), "successful retry must clear the final texture handle");
    TestCheck(world.rasterizer->DeletedObjectCount == firstDeletes + 2 &&
                  second.Backend->DeletedObjectCount == secondDeletes + 1,
              "retry must delete only the previously failed context handle");

    world.renderManager->ForgetRasterizerContext(second.Context);
    secondContext.m_RasterizerContext = nullptr;
    secondContext.m_RasterizerDriver = nullptr;
}

void VertexBufferReplacementRetriesFailedDeletion() {
    TextureTestWorld world;
    CKVertexBuffer *buffer = world.renderManager->CreateVertexBuffer();
    TestCheck(buffer != nullptr, "CreateVertexBuffer failed");
    TestCheck(buffer->Check(world.renderContext, 3, StandaloneVertexFormat, TRUE) == CK_VB_LOST,
              "new vertex buffer should request its initial contents");
    FillVertexBuffer(buffer, world.renderContext, CK_LOCK_DEFAULT, 0.0f);

    const CKDWORD createdBeforeFailure = world.rasterizer->CreatedBufferCount;
    const CKDWORD deletedBeforeFailure = world.rasterizer->DeletedObjectCount;
    world.rasterizer->FailDestroyObject = TRUE;
    TestCheck(buffer->Check(world.renderContext, 6, StandaloneVertexFormat, TRUE) == CK_VB_FAILED,
              "incompatible vertex buffer must stop when deletion fails");
    TestCheck(world.rasterizer->CreatedBufferCount == createdBeforeFailure &&
                  world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 1,
              "failed vertex buffer replacement must retain the old handle");

    world.rasterizer->FailDestroyObject = FALSE;
    TestCheck(buffer->Check(world.renderContext, 6, StandaloneVertexFormat, TRUE) == CK_VB_LOST,
              "vertex buffer replacement must retry after deletion recovers");
    TestCheck(world.rasterizer->DeletedObjectCount == deletedBeforeFailure + 2,
              "vertex buffer retry must delete the retained handle");
    FillVertexBuffer(buffer, world.renderContext, CK_LOCK_DEFAULT, 0.1f);
    TestCheck(world.rasterizer->CreatedBufferCount == createdBeforeFailure + 1,
              "vertex buffer retry must create exactly one replacement");
    buffer->Destroy();
}

void TextureConfigurationStaysUnchangedWhenDeleteFails() {
    {
        TextureTestWorld world;
        RCKTexture texture(world.context, "FailMipmapDelete");
        TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
        FillTexture(texture, 0xFF203040u);
        TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE), "initial texture upload failed");

        const int oldMipMapCount = texture.GetMipmapCount();
        world.rasterizer->FailDestroyObject = TRUE;
        TestCheck(!texture.UseMipmap(TRUE), "mipmap change must report deletion failure");
        TestCheck(texture.GetMipmapCount() == oldMipMapCount, "failed deletion must preserve the mipmap request");
        world.rasterizer->FailDestroyObject = FALSE;
    }

    {
        TextureTestWorld world;
        RCKTexture texture(world.context, "FailFormatDelete");
        TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
        FillTexture(texture, 0xFF506070u);
        TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE), "initial texture upload failed");

        const VX_PIXELFORMAT oldFormat = texture.GetDesiredVideoFormat();
        world.rasterizer->FailDestroyObject = TRUE;
        texture.SetDesiredVideoFormat(oldFormat == _16_RGB565 ? _32_ARGB8888 : _16_RGB565);
        TestCheck(texture.GetDesiredVideoFormat() == oldFormat, "failed deletion must preserve the desired texture format");
        world.rasterizer->FailDestroyObject = FALSE;
    }

    {
        TextureTestWorld world;
        RCKSprite sprite(world.context, "FailSpriteFormatDelete");
        TestCheck(sprite.Create(4, 4, 32, 0), "sprite Create failed");
        FillSprite(sprite, 0xFF8090A0u);
        TestCheck(sprite.SystemToVideoMemory(world.renderContext, FALSE), "initial sprite upload failed");

        const VX_PIXELFORMAT oldFormat = sprite.GetDesiredVideoFormat();
        world.rasterizer->FailDestroyObject = TRUE;
        sprite.SetDesiredVideoFormat(oldFormat == _16_RGB565 ? _32_ARGB8888 : _16_RGB565);
        TestCheck(sprite.GetDesiredVideoFormat() == oldFormat, "failed deletion must preserve the desired sprite format");
        world.rasterizer->FailDestroyObject = FALSE;
    }
}

void SystemToVideoMemoryRejectsUnregisteredRasterizerContext() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "MissingDriver");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF112233u);
    world.renderManager->ForgetRasterizerContext(world.translated.Context);

    TestCheck(!texture.SystemToVideoMemory(world.renderContext, FALSE),
              "SystemToVideoMemory should reject an unregistered rasterizer context");
    TestCheck(!texture.IsInVideoMemory(),
              "failed upload must not mark the texture as resident");
    TestCheck(world.renderManager->RegisterRasterizerContext(
                  world.translated.Context,
                  world.renderContext->m_DriverIndex),
              "rasterizer context re-registration failed");
}

void MipmapRequestsKeepLegacyBoolAndExplicitCounts() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "MipRequests");

    TestCheck(texture.UseMipmap(TRUE), "UseMipmap(TRUE) failed");
    TestCheck(texture.GetMipmapCount() == -1,
              "boolean TRUE should still request automatic mipmap generation");
    TestCheck(texture.UseMipmap(3), "UseMipmap(3) failed");
    TestCheck(texture.GetMipmapCount() == 3,
              "explicit mipmap counts greater than one should be preserved");
    TestCheck(texture.UseMipmap(FALSE), "UseMipmap(FALSE) failed");
    TestCheck(texture.GetMipmapCount() == 0,
              "FALSE should disable mipmaps");
}

void GeneratedAndUserMipmapsUseInitializedCreationModes() {
    TextureTestWorld world;
    RCKTexture generated(world.context, "GeneratedMips");

    TestCheck(generated.Create(4, 4, 32, 0), "generated texture Create failed");
    FillTexture(generated, 0xFF102030u);
    TestCheck(generated.UseMipmap(3), "generated UseMipmap failed");
    TestCheck(generated.SystemToVideoMemory(world.renderContext, FALSE),
              "generated mip texture upload failed");
    TestCheck(world.rasterizer->LastTextureDesc.MipMapCount == (CKDWORD)-1,
              "base-only system data must request generated mipmaps");

    RCKTexture user(world.context, "UserMips");
    TestCheck(user.Create(4, 4, 32, 0), "user texture Create failed");
    FillTexture(user, 0xFF405060u);
    TestCheck(user.SetUserMipMapMode(TRUE), "user mipmap mode failed");
    TestCheck(user.SystemToVideoMemory(world.renderContext, FALSE),
              "user mip texture upload failed");
    TestCheck(world.rasterizer->LastTextureDesc.MipMapCount == 3,
              "complete user mip data must request the explicit full chain");
}

void SavedAutomaticMipmapLoadsAsAutomaticRequest() {
    TextureTestWorld world;
    RCKTexture source(world.context, "SaveAutoMipSource");
    RCKTexture loaded(world.context, "SaveAutoMipLoaded");

    TestCheck(source.Create(2, 2, 32, 0), "source Create failed");
    TestCheck(source.UseMipmap(TRUE), "UseMipmap(TRUE) failed");

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_OLDTEXONLY);
    TestCheck(chunk != nullptr, "texture Save failed");
    chunk->StartRead();
    TestCheck(loaded.Load(chunk, nullptr) == CK_OK, "texture Load failed");
    DeleteCKStateChunk(chunk);

    TestCheck(loaded.GetMipmapCount() == -1,
              "saved automatic mipmap request should load as automatic, not byte value 255");
}

void FailedUserMipmapEnableHasNoSideEffects() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "BadUserMip");

    TestCheck(texture.Create(3, 4, 32, 0), "texture Create failed");
    TestCheck(texture.UseMipmap(3), "UseMipmap(3) failed");
    TestCheck(!texture.SetUserMipMapMode(TRUE),
              "non-power-of-two texture should reject user mipmaps");
    TestCheck(texture.GetMipmapCount() == 3,
              "failed user mipmap enable must not change mipmap request");
}

void FailedCreatePreservesUserMipmaps() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "CreateFailureKeepsMips");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    TestCheck(texture.SetUserMipMapMode(TRUE), "user mipmap setup failed");

    VxImageDescEx beforeMip;
    TestCheck(texture.GetUserMipMapLevel(0, beforeMip),
              "source user mipmap level missing");
    reinterpret_cast<CKDWORD *>(beforeMip.Image)[0] = 0x11223344u;

    TestCheck(!texture.Create(8, 8, 32, 1),
              "mismatched slot Create should fail");

    VxImageDescEx afterMip;
    TestCheck(texture.GetUserMipMapLevel(0, afterMip),
              "failed Create must preserve user mipmap levels");
    TestCheck(afterMip.Image == beforeMip.Image,
              "failed Create must not replace user mipmap storage");
    TestCheck(reinterpret_cast<CKDWORD *>(afterMip.Image)[0] == 0x11223344u,
              "failed Create must preserve user mipmap pixels");
    TestCheck(texture.GetMipmapCount() == -1,
              "failed Create must preserve automatic mipmap request");
    TestCheck(texture.GetWidth() == 4 && texture.GetHeight() == 4,
              "failed Create must preserve texture dimensions");
}

void SuccessfulCreateStillClearsUserMipmaps() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "CreateSuccessClearsMips");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    TestCheck(texture.SetUserMipMapMode(TRUE), "user mipmap setup failed");

    VxImageDescEx mipLevel;
    TestCheck(texture.GetUserMipMapLevel(0, mipLevel),
              "source user mipmap level missing");

    TestCheck(texture.Create(8, 8, 32, 0),
              "same-slot Create should succeed");
    TestCheck(!texture.GetUserMipMapLevel(0, mipLevel),
              "successful Create must still clear user mipmaps");
    TestCheck(texture.GetMipmapCount() == -1,
              "successful Create must preserve automatic mipmap request");
    TestCheck(texture.GetWidth() == 8 && texture.GetHeight() == 8,
              "successful Create must update texture dimensions");
}

void UserMipmapsHandleNonSquarePowerOfTwoTextures() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "NonSquareUserMip");

    TestCheck(texture.Create(4, 16, 32, 0), "texture Create failed");
    TestCheck(texture.SetUserMipMapMode(TRUE),
              "4x16 texture should allow user mipmaps");

    const int expected[][2] = {
        {2, 8},
        {1, 4},
        {1, 2},
        {1, 1},
    };
    for (int i = 0; i < 4; ++i) {
        VxImageDescEx level;
        TestCheck(texture.GetUserMipMapLevel(i, level),
                  "expected user mipmap level missing");
        TestCheck(level.Width == expected[i][0] && level.Height == expected[i][1],
                  "non-square mipmap level dimensions are wrong");
        TestCheck(level.BytesPerLine == level.Width * 4,
                  "mipmap pitch should track clamped width");
        TestCheck(level.Image != nullptr, "mipmap level image must be allocated");
    }

    VxImageDescEx extra;
    TestCheck(!texture.GetUserMipMapLevel(4, extra),
              "extra user mipmap level should not exist");
}

void UserMipmapsUploadUsingCreatedTextureFormat() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "UserMipUploadFormat");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF304050u);
    TestCheck(texture.SetUserMipMapMode(TRUE), "user mipmap setup failed");
    texture.SetDesiredVideoFormat(_16_RGB565);

    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "SystemToVideoMemory should upload user mipmaps");
    TestCheck(world.rasterizer->UpdatedTextureCount == 3,
              "base level and two user mip levels should be uploaded");
    TestCheck(world.rasterizer->LastUpdateMip == 2,
              "last upload should be the final user mip level");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer->LastTextureUpdateDesc) == _32_ARGB8888,
              "user mipmap levels should use the created texture format");
    TestCheck(world.rasterizer->LastTextureUpdateDesc.BytesPerLine == 1 * 4,
              "user mipmap upload pitch should remain one row");
    TestCheck(world.rasterizer->LastTextureUpdateDesc.Width == 1 &&
                  world.rasterizer->LastTextureUpdateDesc.Height == 1,
              "final user mip dimensions should be preserved during conversion");
}

void CopyPreservesUserMipmapsAndInvalidatesDestinationVideoMemory() {
    TextureTestWorld world;
    RCKTexture source(world.context, "SourceTexture");
    RCKTexture dest(world.context, "DestTexture");

    TestCheck(source.Create(4, 4, 32, 0), "source Create failed");
    FillTexture(source, 0xFF010000u);
    TestCheck(source.SetUserMipMapMode(TRUE), "source user mipmaps failed");
    VxImageDescEx sourceMip;
    TestCheck(source.GetUserMipMapLevel(0, sourceMip), "source mip level missing");
    reinterpret_cast<CKDWORD *>(sourceMip.Image)[0] = 0xAABBCCDDu;

    TestCheck(dest.Create(2, 2, 32, 0), "dest Create failed");
    FillTexture(dest, 0xFF020000u);
    TestCheck(dest.SystemToVideoMemory(world.renderContext, FALSE),
              "dest initial upload should succeed");
    const CKDWORD deletesBeforeCopy = world.rasterizer->DeletedObjectCount;

    CKDependenciesContext dependencies(world.context);
    TestCheck(dest.Copy(source, dependencies) == CK_OK, "texture Copy failed");

    TestCheck(world.rasterizer->DeletedObjectCount > deletesBeforeCopy,
              "copy should free stale destination video memory");
    TestCheck(!dest.IsInVideoMemory(), "copied texture should not keep stale video memory");
    TestCheck(dest.GetWidth() == 4 && dest.GetHeight() == 4,
              "copy should replace destination dimensions");

    VxImageDescEx destMip;
    TestCheck(dest.GetUserMipMapLevel(0, destMip), "copied mip level missing");
    TestCheck(destMip.Image != sourceMip.Image,
              "copy should deep-copy user mipmap storage");
    TestCheck(reinterpret_cast<CKDWORD *>(destMip.Image)[0] == 0xAABBCCDDu,
              "copy should preserve user mipmap pixels");
    TestCheck(dest.ToRestore(), "copied texture should be marked for restore");
}

void TextureObjectsAreIndependentPerContext() {
    TextureTestWorld world;
    FFPRecordingWorld second;
    AddDriverTextureFormat(*second.BackendDriver(), _32_ARGB8888);
    AddDriverTextureFormat(*second.BackendDriver(), _16_RGB565);
    AddDriverTextureFormat(*second.BackendDriver(), _16_ARGB4444);
    TestCheck(second.CreateContext(64, 64),
              "second recording Context creation failed");

    RCKRenderContext secondContext(world.context);
    secondContext.m_RasterizerContext = second.Context;
    secondContext.m_RasterizerDriver = second.Driver;
    TestCheck(world.renderManager->RegisterRasterizerContext(
                  second.Context, secondContext.m_DriverIndex),
              "second rasterizer context registration failed");

    RCKTexture texture(world.context, "TwoContexts");
    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF102030u);

    TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "first context upload failed");
    TestCheck(world.rasterizer->CreatedTextureCount == 1 &&
                  world.rasterizer->UpdatedTextureCount == 1,
              "first context should create and upload one texture object");

    TestCheck(texture.SetAsCurrent(&secondContext, FALSE, 0),
              "second context upload failed");
    TestCheck(second.Backend->CreatedTextureCount == 1 &&
                  second.Backend->UpdatedTextureCount == 1,
              "second context should create and upload its own texture object");
    TestCheck(world.rasterizer->DeletedObjectCount == 0,
              "using a second context must not delete the first texture object");

    TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "switching back to the first context failed");
    TestCheck(world.rasterizer->CreatedTextureCount == 1 &&
                  world.rasterizer->UpdatedTextureCount == 1 &&
                  second.Backend->CreatedTextureCount == 1 &&
                  second.Backend->UpdatedTextureCount == 1,
              "context switches must reuse both texture objects");

    FillTexture(texture, 0xFF405060u);
    TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "first context refresh failed");
    TestCheck(world.rasterizer->UpdatedTextureCount == 2,
              "changed pixels should refresh the first context once");
    TestCheck(texture.SetAsCurrent(&secondContext, FALSE, 0),
              "second context refresh failed");
    TestCheck(second.Backend->UpdatedTextureCount == 2,
              "changed pixels should refresh the second context once");

    world.renderManager->ForgetRasterizerContext(second.Context);
    TestCheck(texture.IsInVideoMemory(),
              "forgetting the active context must retain another live texture object");
    TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "forgetting the second context must preserve the first texture object");
    TestCheck(world.rasterizer->CreatedTextureCount == 1 &&
                  world.rasterizer->UpdatedTextureCount == 2,
              "forgetting another context must neither recreate nor upload this texture object");

    TestCheck(texture.FreeVideoMemory(),
              "freeing the remaining texture object failed");
    TestCheck(world.rasterizer->DeletedObjectCount == 1 &&
                  second.Backend->DeletedObjectCount == 0,
              "only the live context should receive an explicit texture delete");

    secondContext.m_RasterizerContext = nullptr;
    secondContext.m_RasterizerDriver = nullptr;
}

void SpriteObjectsAreIndependentPerContext() {
    TextureTestWorld world;
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
    world.renderContext->SetFullViewport(&world.renderContext->m_ViewportData, 64, 64);
    secondContext.SetFullViewport(&secondContext.m_ViewportData, 64, 64);

    RCKSprite sprite(world.context, "TwoContextSprite");
    TestCheck(sprite.Create(4, 4, 32, 0), "sprite Create failed");
    FillSprite(sprite, 0xFF102030u);

    DrawSprite(sprite, *world.renderContext, world.translated);
    const CKDWORD firstCreatedTextures = world.rasterizer->CreatedTextureCount;
    TestCheck(firstCreatedTextures >= 1,
              "first context should create a sprite texture");
    TestCheck(world.rasterizer->UpdatedTextureCount == 1,
              "first context should upload one sprite texture");

    DrawSprite(sprite, secondContext, second);
    const CKDWORD secondCreatedTextures = second.Backend->CreatedTextureCount;
    TestCheck(secondCreatedTextures >= 1,
              "second context should create its own sprite texture");
    TestCheck(second.Backend->UpdatedTextureCount == 1,
              "second context should upload its own sprite texture");
    TestCheck(world.rasterizer->DeletedObjectCount == 0,
              "using a second context must not delete the first sprite texture");

    DrawSprite(sprite, *world.renderContext, world.translated);
    TestCheck(world.rasterizer->CreatedTextureCount == firstCreatedTextures &&
                  world.rasterizer->UpdatedTextureCount == 1 &&
                  second.Backend->CreatedTextureCount == secondCreatedTextures &&
                  second.Backend->UpdatedTextureCount == 1,
              "sprite context switches must reuse both texture objects");

    FillSprite(sprite, 0xFF405060u);
    DrawSprite(sprite, *world.renderContext, world.translated);
    TestCheck(world.rasterizer->UpdatedTextureCount == 2,
              "changed sprite pixels should refresh the first context once");
    DrawSprite(sprite, secondContext, second);
    TestCheck(second.Backend->UpdatedTextureCount == 2,
              "changed sprite pixels should refresh the second context once");

    world.renderManager->ForgetRasterizerContext(second.Context);
    TestCheck(sprite.IsInVideoMemory(),
              "forgetting the active context must retain another live sprite texture");
    DrawSprite(sprite, *world.renderContext, world.translated);
    TestCheck(world.rasterizer->CreatedTextureCount == firstCreatedTextures &&
                  world.rasterizer->UpdatedTextureCount == 2,
              "forgetting another context must preserve the first sprite texture");

    const CKDWORD firstDeletes = world.rasterizer->DeletedObjectCount;
    const CKDWORD secondDeletes = second.Backend->DeletedObjectCount;
    TestCheck(sprite.FreeVideoMemory(),
              "freeing the remaining sprite texture failed");
    TestCheck(world.rasterizer->DeletedObjectCount == firstDeletes + 1 &&
                  second.Backend->DeletedObjectCount == secondDeletes,
              "only the live context should receive an explicit sprite delete");

    secondContext.m_RasterizerContext = nullptr;
    secondContext.m_RasterizerDriver = nullptr;
}

void ForgettingLastContextDetachesTextureObjects() {
    TextureTestWorld world;
    world.renderContext->SetFullViewport(
        &world.renderContext->m_ViewportData, 64, 64);

    CKDWORD deletesBeforeDestruction = 0;
    {
        RCKTexture texture(world.context, "LastContextTexture");
        TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
        FillTexture(texture, 0xFF102030u);
        TestCheck(texture.SetAsCurrent(world.renderContext, FALSE, 0),
                  "texture upload failed");

        RCKSprite sprite(world.context, "LastContextSprite");
        TestCheck(sprite.Create(4, 4, 32, 0), "sprite Create failed");
        FillSprite(sprite, 0xFF405060u);
        DrawSprite(sprite, *world.renderContext, world.translated);

        world.renderManager->ForgetRasterizerContext(world.translated.Context);
        TestCheck(!texture.IsInVideoMemory(),
                  "forgetting the last context must detach the texture object");
        TestCheck(!sprite.IsInVideoMemory(),
                  "forgetting the last context must detach the sprite object");
        deletesBeforeDestruction = world.rasterizer->DeletedObjectCount;
    }

    TestCheck(world.rasterizer->DeletedObjectCount == deletesBeforeDestruction,
              "detached objects must not delete through a forgotten context");
}

void SpriteUploadFailureDoesNotDraw() {
    TextureTestWorld world;
    RCKSprite sprite(world.context, "FailSpriteUpload");
    TestCheck(sprite.Create(4, 4, 32, 0), "sprite Create failed");
    FillSprite(sprite, 0xFF102030u);
    world.renderContext->SetFullViewport(
        &world.renderContext->m_ViewportData, 64, 64);
    world.rasterizer->FailUpdateTexture = TRUE;

    TestCheck(world.translated.Context->BeginScene(),
              "begin failed sprite scene failed");
    TestCheck(sprite.Draw(world.renderContext) == CKERR_INVALIDOPERATION,
              "sprite Draw should report its upload failure");
    TestCheck(world.translated.Context->EndScene(),
              "end failed sprite scene failed");
    TestCheck(!sprite.IsInVideoMemory(),
              "failed sprite upload must discard the incomplete texture object");
    TestCheck(sprite.ToRestore(),
              "failed sprite upload must preserve the dirty flag");
    TestCheck(world.rasterizer->Log.DrawCount == 0,
              "failed sprite upload must not submit a draw");
}

void VertexBufferObjectsAreIndependentPerContext() {
    TextureTestWorld world;
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

    CKVertexBuffer *buffer = world.renderManager->CreateVertexBuffer();
    TestCheck(buffer != nullptr, "CreateVertexBuffer failed");
    TestCheck(buffer->Check(world.renderContext, 3, StandaloneVertexFormat,
                            TRUE) == CK_VB_LOST,
              "new vertex buffer should request its initial contents");

    const CKDWORD firstCreates = world.rasterizer->CreatedBufferCount;
    FillVertexBuffer(buffer, world.renderContext, CK_LOCK_DEFAULT, 0.0f);
    TestCheck(world.rasterizer->CreatedBufferCount == firstCreates + 1 &&
                  world.rasterizer->UpdatedBufferCount == 1,
              "first context should create and upload one vertex buffer");
    DrawVertexBuffer(buffer, *world.renderContext, world.translated);

    const CKDWORD secondCreates = second.Backend->CreatedBufferCount;
    TestCheck(buffer->Check(&secondContext, 3, StandaloneVertexFormat,
                            TRUE) == CK_VB_OK,
              "existing CPU vertices should realize on a second context");
    TestCheck(second.Backend->CreatedBufferCount == secondCreates + 1 &&
                  second.Backend->UpdatedBufferCount == 1,
              "second context should create and upload its own vertex buffer");
    DrawVertexBuffer(buffer, secondContext, second);
    TestCheck(world.rasterizer->DeletedObjectCount == 0,
              "using a second context must not delete the first vertex buffer");

    TestCheck(buffer->Check(world.renderContext, 3, StandaloneVertexFormat,
                            TRUE) == CK_VB_OK,
              "switching back should find the first vertex buffer");
    DrawVertexBuffer(buffer, *world.renderContext, world.translated);
    TestCheck(world.rasterizer->CreatedBufferCount == firstCreates + 1 &&
                  world.rasterizer->UpdatedBufferCount == 1,
              "switching back must reuse the first vertex buffer");

    const CKDWORD deletesBeforeDiscard =
        world.rasterizer->DeletedObjectCount;
    FillVertexBuffer(buffer, world.renderContext, CK_LOCK_DISCARD, 0.1f);
    TestCheck(world.rasterizer->CreatedBufferCount == firstCreates + 1 &&
                  world.rasterizer->DeletedObjectCount ==
                      deletesBeforeDiscard,
              "discard must keep the Rasterizer Handle");
    TestCheck(world.rasterizer->LastBufferUpdateMode ==
                  CKRST_BUFFER_UPDATE_DISCARD,
              "discard must reach the concrete Context unchanged");

    TestCheck(buffer->Check(&secondContext, 3, StandaloneVertexFormat,
                            TRUE) == CK_VB_OK,
              "changed CPU vertices should refresh the second context");
    TestCheck(second.Backend->CreatedBufferCount == secondCreates + 1 &&
                  second.Backend->UpdatedBufferCount == 2,
              "refreshing another context must update its existing buffer");
    DrawVertexBuffer(buffer, secondContext, second);

    world.renderManager->ForgetRasterizerContext(second.Context);
    TestCheck(buffer->Check(world.renderContext, 3, StandaloneVertexFormat,
                            TRUE) == CK_VB_OK,
              "forgetting another context must preserve the first buffer");

    const CKDWORD firstDeletes = world.rasterizer->DeletedObjectCount;
    const CKDWORD secondDeletes = second.Backend->DeletedObjectCount;
    buffer->Destroy();
    TestCheck(world.rasterizer->DeletedObjectCount == firstDeletes + 1 &&
                  second.Backend->DeletedObjectCount == secondDeletes,
              "only the live context should receive an explicit buffer delete");

    secondContext.m_RasterizerContext = nullptr;
    secondContext.m_RasterizerDriver = nullptr;
}

void PartialVertexBufferDiscardUploadsCompleteCpuContents() {
    TextureTestWorld world;
    CKVertexBuffer *buffer = world.renderManager->CreateVertexBuffer();
    TestCheck(buffer != nullptr, "CreateVertexBuffer failed");
    TestCheck(buffer->Check(world.renderContext, 3, StandaloneVertexFormat, TRUE) == CK_VB_LOST,
              "new vertex buffer should request its initial contents");
    FillVertexBuffer(buffer, world.renderContext, CK_LOCK_DEFAULT, 0.0f);
    const CKDWORD fullUpdateSize = world.rasterizer->LastBufferUpdateSize;
    TestCheck(fullUpdateSize != 0 && fullUpdateSize % 3 == 0,
              "initial upload must contain three complete native vertices");

    VxDrawPrimitiveData *data = buffer->Lock(world.renderContext, 1, 1, CK_LOCK_DISCARD);
    TestCheck(data != nullptr, "partial discard lock failed");
    const VxVector replacement(2.0f, 3.0f, 4.0f);
    *static_cast<VxVector *>(data->PositionPtr) = replacement;
    *static_cast<CKDWORD *>(data->ColorPtr) = 0xFF102030u;
    buffer->Unlock(world.renderContext);

    TestCheck(world.rasterizer->LastBufferUpdateMode == CKRST_BUFFER_UPDATE_DISCARD,
              "partial discard must preserve the discard update mode");
    TestCheck(world.rasterizer->LastBufferUpdateOffset == 0,
              "partial discard must begin at the start of the native buffer");
    TestCheck(world.rasterizer->LastBufferUpdateSize == fullUpdateSize,
              "partial discard must upload the complete CPU vertex snapshot");

    const CKDWORD nativeStride = fullUpdateSize / 3;
    const VxVector first = ReadPackedPosition(*world.rasterizer, 0, nativeStride);
    const VxVector changed = ReadPackedPosition(*world.rasterizer, 1, nativeStride);
    const VxVector last = ReadPackedPosition(*world.rasterizer, 2, nativeStride);
    TestCheck(first.x == -0.5f && first.y == -0.5f && first.z == 0.0f,
              "partial discard must preserve the first staged vertex");
    TestCheck(changed.x == replacement.x && changed.y == replacement.y && changed.z == replacement.z,
              "partial discard must upload the modified staged vertex");
    TestCheck(last.x == 0.0f && last.y == 0.5f && last.z == 0.0f,
              "partial discard must preserve the last staged vertex");

    buffer->Destroy();
}

void RenderTargetPreservesCameraAspectRatio() {
    TextureTestWorld world;
    RCKTexture target(world.context, "CameraRatioTarget");
    RCKCamera camera(world.context, "CameraRatio");
    camera.SetAspectRatio(4, 3);
    TestCheck(target.Create(256, 256, 32, 0), "camera-ratio target creation");
    target.SetDesiredVideoFormat(_32_ARGB8888);
    world.renderContext->AttachViewpointToCamera(reinterpret_cast<CKCamera *>(&camera));
    TestCheck(world.renderContext->SetRenderTarget(&target, 0), "camera-ratio target binding");
    world.renderContext->m_RenderedScene->UpdateViewportSize(TRUE, CK_RENDER_USECAMERARATIO);
    VxRect view;
    world.renderContext->GetViewRect(view);
    // Original CKDX8Rasterizer capture: 4:3 camera on a 256-square target.
    TestCheck(view.left == 0.0f && view.top == 32.0f &&
                  view.right == 256.0f && view.bottom == 224.0f,
              "render targets must preserve the requested camera ratio like the original engine");
    TestCheck(world.renderContext->Clear(CK_RENDER_CLEARBACK, 0) == CK_OK,
              "engine clear with a letterboxed target");
    const FFPPassClearRecord &clear = world.rasterizer->PassClears.back();
    TestCheck(clear.Rect.left == 0 && clear.Rect.top == 0 &&
                  clear.Rect.right == 256 && clear.Rect.bottom == 256,
              "engine Clear must clear the full target, independently of the camera viewport");
    TestCheck(world.renderContext->SetRenderTarget(NULL, 0), "camera-ratio target release");
    TestCheck(world.renderContext->GetWidth() == 64 &&
                  world.renderContext->GetHeight() == 64,
              "target release restores the window dimensions");
    world.renderContext->DetachViewpointFromCamera();
}

void EnsureRenderTargetPreservesMipRequestAndUsesDesiredFormat() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "RenderTarget");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    TestCheck(texture.UseMipmap(3), "UseMipmap(3) failed");
    texture.SetDesiredVideoFormat(_16_RGB565);

    TestCheck(texture.EnsureRenderTarget(world.renderContext),
              "EnsureRenderTarget should create a render target texture");
    TestCheck(texture.GetMipmapCount() == 3,
              "render target creation must not overwrite the requested mipmap count");
    TestCheck((world.rasterizer->LastTextureDesc.Flags & CKRST_TEXTURE_RENDERTARGET) != 0,
              "render target flag should be passed to CreateTexture");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer->LastTextureDesc.Format) == _16_RGB565,
              "render target should use the desired video format");
    TestCheck(texture.GetVideoPixelFormat() == _16_RGB565,
              "reported render target video format should match the created format");
    TestCheck(world.rasterizer->UpdatedTextureCount == 0,
              "render target creation should not upload system-memory pixels");
}

void WindowResizeCommitsOnlyAfterBackendSuccess() {
    TextureTestWorld world;
    RCKRenderContext *rc = world.renderContext;
    TestCheck(rc->Resize(5, 6, 640, 480, 0) == CK_OK, "initial engine resize");
    const CKRECT previous = rc->m_Settings.m_Rect;
    const CKViewportData previousViewport = rc->m_ViewportData;
    rc->m_ProjectionUpdated = TRUE;
    world.rasterizer->FailResize = TRUE;
    TestCheck(rc->Resize(10, 20, 1920, 1080, 0) == CKERR_INVALIDOPERATION, "report backend resize failure");
    TestCheck(memcmp(&previous, &rc->m_Settings.m_Rect, sizeof(previous)) == 0 &&
              memcmp(&previousViewport, &rc->m_ViewportData, sizeof(previousViewport)) == 0 &&
              rc->m_ProjectionUpdated, "failed resize preserves settings, viewport and projection validity");
    world.rasterizer->FailResize = FALSE;
    TestCheck(rc->Resize(0, 0, 1920, 1080, VX_RESIZE_NOMOVE) == CK_OK, "Player NOMOVE resize succeeds");
    TestCheck(rc->GetWidth() == 1920 && rc->GetHeight() == 1080 &&
              rc->m_Settings.m_Rect.left == 5 && rc->m_Settings.m_Rect.top == 6,
              "Player resize preserves origin and updates dimensions");
    TestCheck(rc->m_ViewportData.ViewWidth == 1920 && rc->m_ViewportData.ViewHeight == 1080 &&
              !rc->m_ProjectionUpdated, "successful resize updates viewport and invalidates projection");
    TestCheck(rc->Resize(20, 30, -1, -1, VX_RESIZE_NOSIZE) == CK_OK, "move ignores size arguments");
    TestCheck(rc->GetWidth() == 1920 && rc->GetHeight() == 1080 &&
              rc->m_Settings.m_Rect.left == 20 && rc->m_Settings.m_Rect.top == 30,
              "moving preserves the window extent");
}

void SpriteUsesOraclePointSampling() {
    TextureTestWorld world;
    RCKSprite sprite(world.context, "PointSampledSprite");
    TestCheck(sprite.Create(16, 16, 32, 0), "create sprite pixels");
    sprite.SetDesiredVideoFormat(_32_ARGB8888);
    world.renderContext->SetFullViewport(&world.renderContext->m_ViewportData, 64, 64);
    TestCheck(world.translated.Context->BeginScene(), "begin sprite scene");
    TestCheck(sprite.Draw(world.renderContext) == CK_OK, "draw sprite");
    TestCheck(world.rasterizer->Log.DrawCount == 1, "sprite reaches the backend");
    TestCheck(!world.rasterizer->Log.TextureBindings.empty(), "sprite binds its texture");
    const CKSamplerDesc &sampler = world.rasterizer->Log.LastTextureSampler;
    TestCheck(sampler.MinFilter == CKRST_FILTER_NEAREST && sampler.MagFilter == CKRST_FILTER_NEAREST,
              "sprite uses the point min/mag filters recorded on CKDX8Rasterizer");
    TestCheck(world.translated.Context->EndScene(), "end sprite scene");
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();
    TestCheck(CKStartUp() == CK_OK, "CKStartUp failed");
    CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKCamera, CKCID_3DENTITY);
    CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
    CKBuildClassHierarchyTable();

    TestFramework tests;
    tests.Run("Standard texture upload preserves source format",
              &StandardTextureUploadPreservesSourceFormat);
    tests.Run("SetAsCurrent failure does not bind or clear restore flag",
              &SetAsCurrentFailureDoesNotBindOrClearRestoreFlag);
    tests.Run("Restore failure keeps dirty flag",
              &RestoreFailureKeepsDirtyFlag);
    tests.Run("Texture replacement stops when delete fails",
              &TextureReplacementStopsWhenDeleteFails);
    tests.Run("Sprite replacement stops when delete fails",
              &SpriteReplacementStopsWhenDeleteFails);
    tests.Run("Texture partial deletion retries only the failed context",
              &TexturePartialDeletionRetriesOnlyTheFailedContext);
    tests.Run("Vertex buffer replacement retries failed deletion",
              &VertexBufferReplacementRetriesFailedDeletion);
    tests.Run("Texture configuration stays unchanged when delete fails",
              &TextureConfigurationStaysUnchangedWhenDeleteFails);
    tests.Run("SystemToVideoMemory rejects unregistered rasterizer context",
              &SystemToVideoMemoryRejectsUnregisteredRasterizerContext);
    tests.Run("Mipmap requests keep legacy bool and explicit counts",
              &MipmapRequestsKeepLegacyBoolAndExplicitCounts);
    tests.Run("Generated and user mipmaps use initialized creation modes",
              &GeneratedAndUserMipmapsUseInitializedCreationModes);
    tests.Run("Saved automatic mipmap loads as automatic request",
              &SavedAutomaticMipmapLoadsAsAutomaticRequest);
    tests.Run("Failed user mipmap enable has no side effects",
              &FailedUserMipmapEnableHasNoSideEffects);
    tests.Run("Failed Create preserves user mipmaps",
              &FailedCreatePreservesUserMipmaps);
    tests.Run("Successful Create still clears user mipmaps",
              &SuccessfulCreateStillClearsUserMipmaps);
    tests.Run("User mipmaps handle non-square power-of-two textures",
              &UserMipmapsHandleNonSquarePowerOfTwoTextures);
    tests.Run("User mipmaps upload using created texture format",
              &UserMipmapsUploadUsingCreatedTextureFormat);
    tests.Run("Copy preserves user mipmaps and invalidates destination video memory",
              &CopyPreservesUserMipmapsAndInvalidatesDestinationVideoMemory);
    tests.Run("Texture objects are independent per context",
              &TextureObjectsAreIndependentPerContext);
    tests.Run("Sprite objects are independent per context",
              &SpriteObjectsAreIndependentPerContext);
    tests.Run("Forgetting the last context detaches texture objects",
              &ForgettingLastContextDetachesTextureObjects);
    tests.Run("Sprite upload failure does not draw",
              &SpriteUploadFailureDoesNotDraw);
    tests.Run("Vertex buffer objects are independent per context",
              &VertexBufferObjectsAreIndependentPerContext);
    tests.Run("Partial vertex buffer discard uploads complete CPU contents",
              &PartialVertexBufferDiscardUploadsCompleteCpuContents);
    tests.Run("EnsureRenderTarget preserves mip request and uses desired format",
              &EnsureRenderTargetPreservesMipRequestAndUsesDesiredFormat);
    tests.Run("RenderTarget preserves camera aspect ratio",
              &RenderTargetPreservesCameraAspectRatio);
    tests.Run("Sprite uses oracle point sampling", &SpriteUsesOraclePointSampling);
    tests.Run("Window resize commits only after backend success", &WindowResizeCommitsOnlyAfterBackendSuccess);

    const int exitCode = tests.ExitCode();
    TestCheck(CKShutdown() == CK_OK, "CKShutdown failed");
    return exitCode;
}
