#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKBitmapReader.h"
#include "RCKSprite.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DerivedSpritePayload : public RCKSprite {
public:
    explicit DerivedSpritePayload(CKContext *c) : RCKSprite(c) {}
    CK_CLASSID GetClassID() override { return CKCID_SPRITETEXT; }
};
class SpriteSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void RectWire(CKStateChunk &c, float l, float t, float r, float b) { c.WriteFloat(l); c.WriteFloat(t); c.WriteFloat(r); c.WriteFloat(b); }
    static void Rect(const VxRect &r, float l, float t, float right, float bottom) {
        EXPECT_FLOAT_EQ(l, r.left); EXPECT_FLOAT_EQ(t, r.top); EXPECT_FLOAT_EQ(right, r.right); EXPECT_FLOAT_EQ(bottom, r.bottom);
    }
    static void Packed(CKStateChunk &c, int version = 7) {
        c.StartWrite(); c.SetDataVersion(version); c.WriteIdentifier(CK_STATESAVE_2DENTITYONLY);
        c.WriteDword(CK_2DENTITY_RESERVED3 | CK_2DENTITY_RESERVED0); RectWire(c, 7, 9, 57, 79); RectWire(c, .5f, 0, 1.5f, 2);
    }
    static void RawPixels(CKStateChunk &c) {
        // Literal 2.1 raw planar bitmap payload. No bitmap Save/WriteRawBitmap creates this input.
        c.WriteIdentifier(0x800000); c.WriteInt(1); c.WriteInt(32); c.WriteInt(2); c.WriteInt(2);
        c.WriteDword(A_MASK); c.WriteDword(R_MASK); c.WriteDword(G_MASK); c.WriteDword(B_MASK); c.WriteInt(0);
        CKBYTE blue[] = {9,13,1,5}, green[] = {10,14,2,6}, red[] = {11,15,3,7}, alpha[] = {12,16,4,8};
        c.WriteBuffer(4, blue); c.WriteBuffer(4, green); c.WriteBuffer(4, red); c.WriteBuffer(4, alpha);
    }
    static CKERROR Load(RCKSprite &s, CKStateChunk &c, CKFile *f = nullptr) { c.CloseChunk(); return s.Load(&c, f); }
    static CKBitmapProperties Format(int tag) { CKBitmapProperties p; p.m_ReaderGuid = CKGUID(0x78000030, tag); p.m_Ext = CKFileExtension("sw8"); return p; }
};
}
TEST_F(SpriteSerializationTest, NullLoadLeavesCurrentPixelsAndScreenStateUntouched) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xffabcdef)); s.SetRect(VxRect(3, 5, 13, 25));
    EXPECT_EQ(CKERR_INVALIDPARAMETER, s.Load(nullptr, nullptr)); EXPECT_EQ(0xffabcdefu, s.GetPixel(1, 1)); VxRect r; s.GetRect(r); Rect(r, 3, 5, 13, 25);
}
TEST_F(SpriteSerializationTest, FilelessSaveSelectsTransparentAndCurrentSlotIdentifiersIndependently) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.Create(2, 2, 32, 1)); ASSERT_TRUE(s.SetCurrentSlot(1)); s.SetTransparent(TRUE); s.SetTransparentColor(0x123456);
    for (int bits = 0; bits < 4; ++bits) {
        CKDWORD mask = ((bits & 1) ? CK_STATESAVE_SPRITETRANSPARENT : 0) | ((bits & 2) ? CK_STATESAVE_SPRITECURRENTIMAGE : 0);
        CKStateChunk *c = s.Save(nullptr, mask); ASSERT_NE(nullptr, c); EXPECT_EQ(CKCID_SPRITE, c->GetChunkClassID()); EXPECT_EQ(-1, c->GetCurrentPos()); c->StartRead();
        EXPECT_EQ((bits & 1) != 0, c->SeekIdentifier(CK_STATESAVE_SPRITETRANSPARENT) != FALSE);
        if (bits & 1) { EXPECT_EQ(0x123456u, c->ReadDword()); EXPECT_EQ(2u, c->ReadDword()); }
        EXPECT_EQ((bits & 2) != 0, c->SeekIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE) != FALSE); if (bits & 2) EXPECT_EQ(1, c->ReadInt());
        EXPECT_FALSE(c->SeekIdentifier(0x800000)); EXPECT_FALSE(c->SeekIdentifier(CK_STATESAVE_SPRITEFORMAT)); DeleteCKStateChunk(c);
    }
}
TEST_F(SpriteSerializationTest, FilelessLoadDecodesLiteralTransparencyAndCurrentSlot) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.Create(2, 2, 32, 1)); CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c);
    c.WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT); c.WriteDword(0x345678); c.WriteDword(1); c.WriteIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE); c.WriteInt(1);
    ASSERT_EQ(CK_OK, Load(s, c)); EXPECT_TRUE(s.IsTransparent()); EXPECT_EQ(0x345678u, s.GetTransparentColor()); EXPECT_EQ(1, s.GetCurrentSlot());
    VxRect r; s.GetRect(r); Rect(r, 7, 9, 57, 79); s.GetSourceRect(r); Rect(r, .5f, 0, 1.5f, 2);
}
TEST_F(SpriteSerializationTest, FilelessLoadIgnoresPersistentBitmapAndFormatIdentifiers) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff123456)); s.SetSaveOptions(CKTEXTURE_EXTERNAL);
    CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c); RawPixels(c); c.WriteIdentifier(CK_STATESAVE_SPRITEFORMAT); c.WriteDword(CKTEXTURE_RAWDATA); c.WriteBuffer(0, nullptr);
    ASSERT_EQ(CK_OK, Load(s, c)); EXPECT_EQ(3, s.GetWidth()); EXPECT_EQ(0xff123456u, s.GetPixel(1, 1)); EXPECT_EQ(CKTEXTURE_EXTERNAL, s.GetSaveOptions());
}
TEST_F(SpriteSerializationTest, FileLoadDecodesLiteralRawPlanesAndPreservesSerializedSourceCrop) {
    RCKSprite s(Context); CKFile file(Context); CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c); RawPixels(c);
    c.WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT); c.WriteDword(0x123456); c.WriteDword(1);
    ASSERT_EQ(CK_OK, Load(s, c, &file)); EXPECT_EQ(2, s.GetWidth()); EXPECT_EQ(2, s.GetHeight());
    EXPECT_EQ(0x04030201u, s.GetPixel(0, 0)); EXPECT_EQ(0x08070605u, s.GetPixel(1, 0));
    EXPECT_EQ(0x0c0b0a09u, s.GetPixel(0, 1)); EXPECT_EQ(0x100f0e0du, s.GetPixel(1, 1));
    EXPECT_TRUE(s.IsTransparent()); EXPECT_EQ(0x123456u, s.GetTransparentColor()); VxRect r; s.GetRect(r); Rect(r, 7, 9, 57, 79); s.GetSourceRect(r); Rect(r, .5f, 0, 1.5f, 2);
}
TEST_F(SpriteSerializationTest, FileSaveEmitsBitmapAndFormatEvenWithZeroSaveMask) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xffabcdef)); s.SetSaveOptions(CKTEXTURE_RAWDATA); s.SetTransparentColor(0x123456);
    CKFile file(Context); CKStateChunk *c = s.Save(&file, 0); ASSERT_NE(nullptr, c); c->StartRead(); ASSERT_TRUE(c->SeekIdentifier(0x800000)); EXPECT_EQ(1, c->ReadInt());
    VxImageDescEx desc; CKBYTE *pixels = c->ReadRawBitmap(desc); ASSERT_NE(nullptr, pixels); EXPECT_EQ(2, desc.Width); EXPECT_EQ(2, desc.Height);
    EXPECT_EQ(0xffabcdefu, reinterpret_cast<CKDWORD *>(pixels)[1]); delete[] pixels;
    ASSERT_TRUE(c->SeekIdentifier(CK_STATESAVE_SPRITETRANSPARENT)); EXPECT_EQ(0x123456u, c->ReadDword()); EXPECT_EQ(0u, c->ReadDword());
    ASSERT_TRUE(c->SeekIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE)); EXPECT_EQ(0, c->ReadInt()); ASSERT_TRUE(c->SeekIdentifier(CK_STATESAVE_SPRITEFORMAT));
    EXPECT_EQ(static_cast<CKDWORD>(CKTEXTURE_RAWDATA), c->ReadDword()); void *buffer = nullptr; EXPECT_EQ(0, c->ReadBuffer(&buffer)); EXPECT_EQ(nullptr, buffer); DeleteCKStateChunk(c);
}
TEST_F(SpriteSerializationTest, SharedFilelessLoadUsesSourceMetadataAfterIndependentFieldsAndDeepCopiesPixels) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(2, 2, 32)); ASSERT_TRUE(source.SetPixel(1, 1, 0xff123456));
    source.SetTransparent(FALSE); source.SetTransparentColor(0xabcdef); source.SetPickThreshold(81); CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c);
    c.WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT); c.WriteDword(0x123456); c.WriteDword(1);
    c.WriteIdentifier(CK_STATESAVE_SPRITESHARED); c.WriteObject(&source); ASSERT_EQ(CK_OK, Load(destination, c));
    EXPECT_FALSE(destination.IsTransparent()); EXPECT_EQ(0xabcdefu, destination.GetTransparentColor()); EXPECT_EQ(81, destination.GetPickThreshold());
    EXPECT_EQ(0xff123456u, destination.GetPixel(1, 1)); EXPECT_NE(source.LockSurfacePtr(), destination.LockSurfacePtr());
}
TEST_F(SpriteSerializationTest, SharedFileLoadTakesPrecedenceOverIndependentBitmapAndTransparency) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.SetPixel(2, 1, 0xff123456)); source.SetTransparent(FALSE);
    CKFile file(Context); CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c); c.WriteIdentifier(CK_STATESAVE_SPRITESHARED); c.WriteObject(&source);
    RawPixels(c); c.WriteIdentifier(CK_STATESAVE_SPRITETRANSPARENT); c.WriteDword(0x123456); c.WriteDword(1);
    ASSERT_EQ(CK_OK, Load(destination, c, &file)); EXPECT_EQ(3, destination.GetWidth()); EXPECT_FALSE(destination.IsTransparent()); EXPECT_EQ(0xff123456u, destination.GetPixel(2, 1));
}
TEST_F(SpriteSerializationTest, SharedSelfLoadPreservesLivePixelsAndOwnedBuffer) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff123456)); CKBYTE *pixels = s.LockSurfacePtr();
    CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c); c.WriteIdentifier(CK_STATESAVE_SPRITESHARED); c.WriteObject(&s);
    ASSERT_EQ(CK_OK, Load(s, c)); EXPECT_EQ(pixels, s.LockSurfacePtr()); EXPECT_EQ(0xff123456u, s.GetPixel(1, 1));
}
TEST_F(SpriteSerializationTest, SharedNullReferenceKeepsDestinationPixelsAndAcceptsOtherFields) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(2, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff123456));
    CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c); c.WriteIdentifier(CK_STATESAVE_SPRITESHARED); c.WriteObject(nullptr);
    ASSERT_EQ(CK_OK, Load(s, c)); EXPECT_EQ(0xff123456u, s.GetPixel(1, 1)); VxRect r; s.GetRect(r); Rect(r, 7, 9, 57, 79);
}
TEST_F(SpriteSerializationTest, VersionSixKeepsOldFormatWhileVersionSevenInstallsIndependentProperties) {
    for (int version = 6; version <= 7; ++version) {
        RCKSprite s(Context); CKBitmapProperties old = Format(1), fresh = Format(2); s.SetSaveFormat(&old); CKFile file(Context);
        CKStateChunk c(CKCID_SPRITE, nullptr); Packed(c, version); c.WriteIdentifier(CK_STATESAVE_SPRITEFORMAT); c.WriteDword(CKTEXTURE_EXTERNAL); c.WriteBuffer(fresh.m_Size, &fresh);
        ASSERT_EQ(CK_OK, Load(s, c, &file)); EXPECT_EQ(CKTEXTURE_EXTERNAL, s.GetSaveOptions()); ASSERT_NE(nullptr, s.GetSaveFormat());
        EXPECT_EQ(version == 6 ? old.m_ReaderGuid : fresh.m_ReaderGuid, s.GetSaveFormat()->m_ReaderGuid); EXPECT_NE(&fresh, s.GetSaveFormat());
    }
}
TEST_F(SpriteSerializationTest, DerivedSpriteSaveLeavesPayloadOpenForSubclassIdentifier) {
    DerivedSpritePayload s(Context); CKStateChunk *c = s.Save(nullptr, 0); ASSERT_NE(nullptr, c); EXPECT_GE(c->GetCurrentPos(), 0);
    c->WriteIdentifier(0x78787878); c->WriteInt(71); c->CloseChunk(); c->StartRead(); ASSERT_TRUE(c->SeekIdentifier(0x78787878)); EXPECT_EQ(71, c->ReadInt()); DeleteCKStateChunk(c);
}
TEST_F(SpriteSerializationTest, PreSaveDelegatesMaterialAndChildSelectionToEntityBase) {
    RCKSprite s(Context), child(Context); RCKMaterial material(Context); s.SetMaterial(&material); child.SetParent(reinterpret_cast<CK2dEntity *>(&s));
    CKFile file(Context); file.m_IndexByClassId.Resize(80); s.PreSave(&file, CK_STATESAVE_2DENTITYMATERIAL | CK_STATESAVE_2DENTITYHIERARCHY);
    EXPECT_TRUE(file.IsObjectToBeSaved(material.GetID())); EXPECT_TRUE(file.IsObjectToBeSaved(child.GetID()));
    child.SetParent(nullptr);
}
