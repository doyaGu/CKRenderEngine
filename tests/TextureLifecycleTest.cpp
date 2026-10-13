#include <gtest/gtest.h>
#include <cstdint>
#include "CKAll.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class TextureLifecycleProbe : public RCKTexture {
public:
    explicit TextureLifecycleProbe(CKContext *context) : RCKTexture(context) {}
    int FreeCalls = 0; CKBOOL FreeResult = TRUE;
    CKBOOL FreeVideoMemory() override { ++FreeCalls; if (FreeResult) m_InVideoMemory = FALSE; return FreeResult; }
    void Resident() { m_InVideoMemory = TRUE; }
    static void RecordLayout(RCKTexture &texture) {
        const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(&texture);
        std::size_t previous = sizeof(CKTexture);
#define FIELD(field) { auto member = &TextureLifecycleProbe::field; \
        const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&(texture.*member)); \
        const std::size_t offset = address - begin; EXPECT_GE(offset, previous); \
        EXPECT_LE(offset + sizeof(texture.*member), sizeof(RCKTexture)); \
        EXPECT_EQ(0u, address % alignof(decltype(texture.*member))); \
        ::testing::Test::RecordProperty(#field "_offset", static_cast<int>(offset)); \
        ::testing::Test::RecordProperty(#field "_size", static_cast<int>(sizeof(texture.*member))); \
        previous = offset + sizeof(texture.*member); }
        FIELD(m_DesiredVideoFormat); FIELD(m_RasterizerContext); FIELD(m_MipMapLevel); FIELD(m_ObjectIndex);
        FIELD(m_MipMaps); FIELD(m_InVideoMemory); FIELD(m_TextureFlags); FIELD(m_CachedMipMapCount); FIELD(m_VideoFormat);
#undef FIELD
        ::testing::Test::RecordProperty("SDLSize", static_cast<int>(sizeof(RCKTexture)));
        ::testing::Test::RecordProperty("SDLAlignment", static_cast<int>(alignof(RCKTexture)));
        ::testing::Test::RecordProperty("SDLBaseSize", static_cast<int>(sizeof(CKTexture)));
        ::testing::Test::RecordProperty("SDLPointerSize", static_cast<int>(sizeof(void *)));
        ::testing::Test::RecordProperty("m_ClassID_value", static_cast<int>(m_ClassID));
        ::testing::Test::RecordProperty("m_ClassID_size", static_cast<int>(sizeof(m_ClassID)));
        EXPECT_EQ(nullptr, texture.*(&TextureLifecycleProbe::m_RasterizerContext));
        EXPECT_EQ(nullptr, texture.*(&TextureLifecycleProbe::m_MipMaps));
        EXPECT_EQ(0u, texture.*(&TextureLifecycleProbe::m_MipMapLevel));
        EXPECT_EQ(0u, texture.*(&TextureLifecycleProbe::m_ObjectIndex));
        EXPECT_FALSE(texture.*(&TextureLifecycleProbe::m_InVideoMemory));
        EXPECT_EQ(0u, texture.*(&TextureLifecycleProbe::m_TextureFlags));
        EXPECT_EQ(0u, texture.*(&TextureLifecycleProbe::m_CachedMipMapCount));
        const VxImageDescEx &desc = texture.*(&TextureLifecycleProbe::m_VideoFormat);
        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&desc);
        for (std::size_t i = 0; i < sizeof(desc); ++i) EXPECT_EQ(0u, bytes[i]);
    }
};
class TextureLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}

TEST_F(TextureLifecycleTest, RegisteredDefaultsManagerFormatAndActualPlatformLayout) {
    Manager->m_TextureVideoFormat.Value = _16_RGB565;
    RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE)); ASSERT_NE(nullptr, texture);
    EXPECT_EQ(31, texture->GetClassID()); EXPECT_EQ(31, RCKTexture::m_ClassID);
    EXPECT_TRUE(CKIsChildClassOf(texture, CKCID_BEOBJECT)); EXPECT_EQ(texture, Context->GetObject(texture->GetID()));
    EXPECT_EQ(_16_RGB565, texture->GetDesiredVideoFormat()); EXPECT_EQ(-1, texture->GetWidth()); EXPECT_EQ(-1, texture->GetHeight());
    EXPECT_EQ(0, texture->GetSlotCount()); EXPECT_EQ(0, texture->GetRstTextureIndex()); EXPECT_FALSE(texture->IsInVideoMemory());
    TextureLifecycleProbe::RecordLayout(*texture);
}
TEST_F(TextureLifecycleTest, NamedConstructionCopiesNameAndKeepsManagerSnapshot) {
    char name[] = "Named texture"; Manager->m_TextureVideoFormat.Value = _16_RGB565; RCKTexture first(Context, name);
    Manager->m_TextureVideoFormat.Value = _32_ARGB8888; RCKTexture second(Context); name[0] = 'X';
    EXPECT_STREQ("Named texture", first.GetName()); EXPECT_EQ(nullptr, second.GetName());
    EXPECT_EQ(_16_RGB565, first.GetDesiredVideoFormat()); EXPECT_EQ(_32_ARGB8888, second.GetDesiredVideoFormat());
}
TEST_F(TextureLifecycleTest, RegistrationPublishesNativeTextureGuidAndCurrentObjectOption) {
    CKClassDesc *desc = CKGetClassDesc(CKCID_TEXTURE); ASSERT_NE(nullptr, desc);
    desc->Parameter = CKGUID(); desc->DefaultOptions = 0; RCKTexture::Register();
    EXPECT_STREQ("Texture", RCKTexture::GetClassName()); EXPECT_STREQ("Texture", desc->NameFct());
    EXPECT_EQ(CKPGUID_TEXTURE, desc->Parameter); EXPECT_EQ(358295664u, desc->Parameter.d1); EXPECT_EQ(406223352u, desc->Parameter.d2);
    EXPECT_EQ(CK_GENERALOPTIONS_CANUSECURRENTOBJECT, desc->DefaultOptions);
}
TEST_F(TextureLifecycleTest, OwnDependencyMetadataIsEmptyForEveryModeAndIndex) {
    CKClassDesc *desc = CKGetClassDesc(CKCID_TEXTURE); ASSERT_NE(nullptr, desc);
    const int modes[] = {-1, 0, 1, 2, 3, 4, 5, 100};
    for (int mode : modes) { EXPECT_EQ(0, RCKTexture::GetDependenciesCount(mode)); EXPECT_EQ(0, desc->DependsCountFct(mode));
        for (int index = -1; index <= 2; ++index) { EXPECT_EQ(nullptr, RCKTexture::GetDependencies(index, mode)); EXPECT_EQ(nullptr, desc->DependsFct(index, mode)); } }
}
TEST_F(TextureLifecycleTest, SDKFactoryVirtualDestructionReleasesOwnedPixelsAndMipmapImages) {
    CKTexture *api = RCKTexture::CreateInstance(Context); ASSERT_NE(nullptr, api); const CK_ID id = api->GetID();
    EXPECT_EQ(31, api->GetClassID()); ASSERT_TRUE(api->Create(4, 4, 16, 0)); ASSERT_TRUE(api->SetUserMipMapMode(TRUE));
    VxImageDescEx mip; ASSERT_TRUE(api->GetUserMipMapLevel(0, mip)); ASSERT_NE(nullptr, mip.Image); mip.Image[0] = 137;
    ASSERT_TRUE(api->SetPixel(3, 3, 0xff123456u)); delete static_cast<CKObject *>(api);
    EXPECT_EQ(nullptr, Context->GetObject(id)); RCKTexture survivor(Context); EXPECT_EQ(31, survivor.GetClassID());
}
TEST_F(TextureLifecycleTest, MemoryOccupationIncludesPortableTailAndArgbPixelsInEverySlot) {
    RCKTexture texture(Context);
    const int tail = static_cast<int>(sizeof(RCKTexture) - sizeof(CKBeObject));
    EXPECT_EQ(texture.CKBeObject::GetMemoryOccupation() + tail, texture.GetMemoryOccupation());
    ASSERT_TRUE(texture.Create(3, 2, 16, 0)); ASSERT_TRUE(texture.Create(3, 2, 24, 1));
    EXPECT_EQ(texture.CKBeObject::GetMemoryOccupation() + tail + 3 * 2 * 2 * 4, texture.GetMemoryOccupation());
    texture.SetName("Owned texture name"); EXPECT_EQ(texture.CKBeObject::GetMemoryOccupation() + tail + 48, texture.GetMemoryOccupation());
}
TEST_F(TextureLifecycleTest, CreateStoresArgbPixelsRegardlessOfRequestedBppAndKeepsSlotsIndependent) {
    RCKTexture texture(Context); ASSERT_TRUE(texture.Create(3, 2, 16, 0)); VxImageDescEx desc; ASSERT_TRUE(texture.GetSystemTextureDesc(desc)); EXPECT_EQ(32, desc.BitsPerPixel);
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) EXPECT_EQ(0xff000000u, texture.GetPixel(x, y, 0));
    ASSERT_TRUE(texture.SetPixel(1, 1, 0xff123456u, 0)); ASSERT_TRUE(texture.Create(3, 2, 24, 1));
    EXPECT_EQ(0xff123456u, texture.GetPixel(1, 1, 0)); EXPECT_EQ(0xff000000u, texture.GetPixel(1, 1, 1));
    EXPECT_FALSE(texture.Create(4, 2, 32, 2)); EXPECT_EQ(2, texture.GetSlotCount()); EXPECT_EQ(3, texture.GetWidth());
    EXPECT_EQ(0xff123456u, texture.GetPixel(1, 1, 0)); EXPECT_FALSE(texture.Create(3, 2, 32, -1));
}
TEST_F(TextureLifecycleTest, SameSizeCreatePreservesActualUserMipPixelsAndVideoObject) {
    TextureLifecycleProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx first; ASSERT_TRUE(texture.GetUserMipMapLevel(0, first)); first.Image[0] = 197; texture.Resident();
    ASSERT_TRUE(texture.Create(4, 4, 32, 0)); VxImageDescEx after; ASSERT_TRUE(texture.GetUserMipMapLevel(0, after));
    EXPECT_EQ(first.Image, after.Image); EXPECT_EQ(197u, after.Image[0]); EXPECT_EQ(0, texture.FreeCalls);
}
TEST_F(TextureLifecycleTest, FailedCreateDoesNotReleaseVideoObjectOrUserMipPixels) {
    TextureLifecycleProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx first; ASSERT_TRUE(texture.GetUserMipMapLevel(0, first)); first.Image[0] = 83; texture.Resident();
    EXPECT_FALSE(texture.Create(4, 4, 32, -1)); EXPECT_EQ(0, texture.FreeCalls);
    VxImageDescEx after; ASSERT_TRUE(texture.GetUserMipMapLevel(0, after)); EXPECT_EQ(first.Image, after.Image); EXPECT_EQ(83u, after.Image[0]);
}
TEST_F(TextureLifecycleTest, ResizingCreateCallsVirtualReleaseAndPropagatesFailureAfterCpuResize) {
    TextureLifecycleProbe texture(Context); ASSERT_TRUE(texture.Create(3, 2, 32, 0)); texture.Resident(); texture.FreeResult = FALSE;
    EXPECT_FALSE(texture.Create(5, 3, 32, 0)); EXPECT_EQ(1, texture.FreeCalls); EXPECT_EQ(5, texture.GetWidth()); EXPECT_EQ(3, texture.GetHeight());
    EXPECT_EQ(0xff000000u, texture.GetPixel(4, 2));
}
TEST_F(TextureLifecycleTest, SuccessfulResizeDropsUserMipChainWhoseDimensionsNoLongerMatchImage) {
    TextureLifecycleProbe texture(Context); ASSERT_TRUE(texture.Create(4, 4, 32, 0)); ASSERT_TRUE(texture.SetUserMipMapMode(TRUE));
    VxImageDescEx before; ASSERT_TRUE(texture.GetUserMipMapLevel(0, before)); before.Image[0] = 251; texture.Resident();
    ASSERT_TRUE(texture.Create(8, 4, 32, 0)); EXPECT_EQ(8, texture.GetWidth()); EXPECT_EQ(4, texture.GetHeight());
    EXPECT_EQ(1, texture.FreeCalls); VxImageDescEx after; EXPECT_FALSE(texture.GetUserMipMapLevel(0, after));
    EXPECT_EQ(0xff000000u, texture.GetPixel(7, 3));
}
