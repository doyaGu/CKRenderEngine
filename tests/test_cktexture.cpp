#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CKContext.h"
#include "CKDependencies.h"
#include "CKGlobals.h"
#include "CKStateChunk.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKTexture.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

void AddDriverTextureFormat(FFPDiagnosticDriver &driver, VX_PIXELFORMAT format) {
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(format, desc.Format);
    driver.m_TextureFormats.PushBack(desc);
}

struct TextureTestWorld {
    TextureTestWorld()
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
        AddDriverTextureFormat(driver, _16_ARGB4444);

        renderContext = new RCKRenderContext(context);
        renderContext->m_RasterizerContext = &rasterizer;
        renderContext->m_RasterizerDriver = &driver;
    }

    ~TextureTestWorld() {
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

void FillTexture(RCKTexture &texture, CKDWORD seed) {
    CKDWORD *pixels = reinterpret_cast<CKDWORD *>(texture.LockSurfacePtr());
    TestCheck(pixels != nullptr, "texture surface must lock");
    const int count = texture.GetWidth() * texture.GetHeight();
    for (int i = 0; i < count; ++i)
        pixels[i] = seed + (CKDWORD)i;
    texture.ReleaseSurfacePtr();
}

void StandardTextureUploadPreservesSourceFormat() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "SourceFormat");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF102030u);
    texture.SetDesiredVideoFormat(_16_RGB565);

    TestCheck(texture.SystemToVideoMemory(world.renderContext, FALSE),
              "SystemToVideoMemory should succeed");
    TestCheck(world.rasterizer.CreatedTextureCount == 1,
              "texture should be created once");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer.LastTextureDesc.Format) == _32_ARGB8888,
              "regular textures should be created with the source image format");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer.LastTextureUpdateDesc) == _32_ARGB8888,
              "Restore upload should preserve the source image format");
    TestCheck(world.rasterizer.LastTextureUpdateDesc.BytesPerLine == 4 * 4,
              "source-format texture upload pitch should remain one row");
    TestCheck(texture.GetVideoPixelFormat() == _32_ARGB8888,
              "reported video format should match the created source format");
}

void SetAsCurrentFailureDoesNotBindOrClearRestoreFlag() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "FailCreateUpload");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF405060u);
    world.rasterizer.FailUpdateTexture = TRUE;

    TestCheck(!texture.SetAsCurrent(world.renderContext, FALSE, 0),
              "SetAsCurrent should fail when upload fails");
    TestCheck(!texture.IsInVideoMemory(),
              "failed upload must not leave the texture marked in video memory");
    TestCheck(texture.ToRestore(),
              "failed upload must keep the restore flag set");
    TestCheck(world.renderContext->m_FFPipeline.GetTexture(0) == 0,
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
    world.rasterizer.FailUpdateTexture = TRUE;
    TestCheck(!texture.Restore(FALSE), "Restore should report update failure");
    TestCheck(texture.ToRestore(), "failed Restore must keep restore flag set");
}

void SystemToVideoMemoryRejectsMissingRasterizerDriver() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "MissingDriver");

    TestCheck(texture.Create(2, 2, 32, 0), "texture Create failed");
    FillTexture(texture, 0xFF112233u);
    world.rasterizer.m_Driver = nullptr;

    TestCheck(!texture.SystemToVideoMemory(world.renderContext, FALSE),
              "SystemToVideoMemory should reject a rasterizer context without a driver");
    TestCheck(!texture.IsInVideoMemory(),
              "failed upload must not mark the texture as resident");
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
    TestCheck(world.rasterizer.UpdatedTextureCount == 3,
              "base level and two user mip levels should be uploaded");
    TestCheck(world.rasterizer.LastUpdateMip == 2,
              "last upload should be the final user mip level");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer.LastTextureUpdateDesc) == _32_ARGB8888,
              "user mipmap levels should use the created texture format");
    TestCheck(world.rasterizer.LastTextureUpdateDesc.BytesPerLine == 1 * 4,
              "user mipmap upload pitch should remain one row");
    TestCheck(world.rasterizer.LastTextureUpdateDesc.Width == 1 &&
                  world.rasterizer.LastTextureUpdateDesc.Height == 1,
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
    const CKDWORD deletesBeforeCopy = world.rasterizer.DeletedObjectCount;

    CKDependenciesContext dependencies(world.context);
    TestCheck(dest.Copy(source, dependencies) == CK_OK, "texture Copy failed");

    TestCheck(world.rasterizer.DeletedObjectCount > deletesBeforeCopy,
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

void EnsureRenderTargetPreservesMipRequestAndUsesDesiredFormat() {
    TextureTestWorld world;
    RCKTexture texture(world.context, "RenderTarget");

    TestCheck(texture.Create(4, 4, 32, 0), "texture Create failed");
    TestCheck(texture.UseMipmap(3), "UseMipmap(3) failed");
    texture.SetDesiredVideoFormat(_16_RGB565);

    TestCheck(texture.EnsureRenderTarget(world.renderContext, FALSE),
              "EnsureRenderTarget should create a render target texture");
    TestCheck(texture.GetMipmapCount() == 3,
              "render target creation must not overwrite the requested mipmap count");
    TestCheck((world.rasterizer.LastTextureDesc.Flags & CKRST_TEXTURE_RENDERTARGET) != 0,
              "render target flag should be passed to CreateTexture");
    TestCheck(VxImageDesc2PixelFormat(world.rasterizer.LastTextureDesc.Format) == _16_RGB565,
              "render target should use the desired video format");
    TestCheck(texture.GetVideoPixelFormat() == _16_RGB565,
              "reported render target video format should match the created format");
    TestCheck(world.rasterizer.UpdatedTextureCount == 0,
              "render target creation should not upload system-memory pixels");
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();
    TestCheck(CKStartUp() == CK_OK, "CKStartUp failed");
    CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKRenderContext, CKCID_OBJECT);
    CKBuildClassHierarchyTable();

    TestFramework tests;
    tests.Run("Standard texture upload preserves source format",
              &StandardTextureUploadPreservesSourceFormat);
    tests.Run("SetAsCurrent failure does not bind or clear restore flag",
              &SetAsCurrentFailureDoesNotBindOrClearRestoreFlag);
    tests.Run("Restore failure keeps dirty flag",
              &RestoreFailureKeepsDirtyFlag);
    tests.Run("SystemToVideoMemory rejects missing rasterizer driver",
              &SystemToVideoMemoryRejectsMissingRasterizerDriver);
    tests.Run("Mipmap requests keep legacy bool and explicit counts",
              &MipmapRequestsKeepLegacyBoolAndExplicitCounts);
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
    tests.Run("EnsureRenderTarget preserves mip request and uses desired format",
              &EnsureRenderTargetPreservesMipRequestAndUsesDesiredFormat);

    const int exitCode = tests.ExitCode();
    TestCheck(CKShutdown() == CK_OK, "CKShutdown failed");
    return exitCode;
}
