#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "CKDependencies.h"
#include "CKBitmapReader.h"
#include "RCKSprite.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SpriteCopyProbe : public RCKSprite {
public:
    static CKBitmapData &Bitmap(RCKSprite &o) { return o.*(&SpriteCopyProbe::m_BitmapData); }
};
class SpriteDependencyMap : public CKDependenciesContext {
public:
    explicit SpriteDependencyMap(CKContext *c) : CKDependenciesContext(c) {}
    void Map(CKObject &from, CKObject &to) { m_MapID.Insert(from.GetID(), to.GetID()); }
};
class SpriteCopyTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &o, CKDWORD bits = 3) { o.Resize(80); for (int i = 0; i < 80; ++i) o[i] = 0;
        o.m_Flags = CK_DEPENDENCIES_CUSTOM; o[CKCID_2DENTITY] = bits; }
    static void Rect(const VxRect &r, float l, float t, float right, float bottom) {
        EXPECT_FLOAT_EQ(l, r.left); EXPECT_FLOAT_EQ(t, r.top); EXPECT_FLOAT_EQ(right, r.right); EXPECT_FLOAT_EQ(bottom, r.bottom);
    }
    static CKBitmapProperties Format() { CKBitmapProperties p; p.m_ReaderGuid = CKGUID(0x78000010, 7); p.m_Ext = CKFileExtension("cp8");
        p.m_Format.Width = 3; p.m_Format.Height = 2; p.m_Format.BitsPerPixel = 32; return p; }
};
}
TEST_F(SpriteCopyTest, BitmapCopyOwnsIndependentPixelsForEverySlotAndPreservesSource) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32, 0)); ASSERT_TRUE(source.Create(3, 2, 32, 1));
    ASSERT_TRUE(source.SetPixel(0, 0, 0xff123456, 0)); ASSERT_TRUE(source.SetPixel(2, 1, 0xffabcdef, 1)); destination.CopySpriteData(&source);
    ASSERT_EQ(2, destination.GetSlotCount()); EXPECT_EQ(0xff123456u, destination.GetPixel(0, 0, 0)); EXPECT_EQ(0xffabcdefu, destination.GetPixel(2, 1, 1));
    EXPECT_NE(source.LockSurfacePtr(0), destination.LockSurfacePtr(0)); EXPECT_NE(source.LockSurfacePtr(1), destination.LockSurfacePtr(1));
    ASSERT_TRUE(destination.SetPixel(0, 0, 0xff765432, 0)); EXPECT_EQ(0xff123456u, source.GetPixel(0, 0, 0));
}
TEST_F(SpriteCopyTest, BitmapCopyPreservesFilenameAfterAllocatingDestinationSlots) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.Create(3, 2, 32, 1));
    char first[] = "first-source.bmp", second[] = "second-source.bmp"; ASSERT_TRUE(source.SetSlotFileName(0, first)); ASSERT_TRUE(source.SetSlotFileName(1, second));
    destination.CopySpriteData(&source); EXPECT_STREQ(first, destination.GetSlotFileName(0)); EXPECT_STREQ(second, destination.GetSlotFileName(1));
    EXPECT_STREQ(first, source.GetSlotFileName(0));
}
TEST_F(SpriteCopyTest, BitmapCopyCopiesPersistentMetadataAndDesiredFormatWithoutChangingEntityRect) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.Create(3, 2, 32, 1)); ASSERT_TRUE(source.SetCurrentSlot(1));
    source.SetTransparent(TRUE); source.SetTransparentColor(0x123456); source.SetPickThreshold(123); source.SetSaveOptions(CKTEXTURE_EXTERNAL); source.SetDesiredVideoFormat(_16_RGB565);
    destination.SetRect(VxRect(7, 9, 107, 209)); VxRect crop(1, 0, 2, 1); destination.SetSourceRect(crop); destination.CopySpriteData(&source);
    EXPECT_EQ(1, destination.GetCurrentSlot()); EXPECT_TRUE(destination.IsTransparent()); EXPECT_EQ(0x123456u, destination.GetTransparentColor());
    EXPECT_EQ(123, destination.GetPickThreshold()); EXPECT_EQ(CKTEXTURE_EXTERNAL, destination.GetSaveOptions()); EXPECT_EQ(_16_RGB565, destination.GetDesiredVideoFormat());
    VxRect r; destination.GetRect(r); Rect(r, 7, 9, 107, 209); destination.GetSourceRect(r); Rect(r, 1, 0, 2, 1);
}
TEST_F(SpriteCopyTest, BitmapSelfCopyPreservesLiveBufferIdentityPixelsAndFilename) {
    RCKSprite source(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.SetPixel(1, 1, 0xff718293));
    char name[] = "self-source.bmp"; ASSERT_TRUE(source.SetSlotFileName(0, name)); CKBYTE *buffer = source.LockSurfacePtr(0);
    source.CopySpriteData(&source); EXPECT_EQ(buffer, source.LockSurfacePtr(0)); EXPECT_EQ(0xff718293u, source.GetPixel(1, 1)); EXPECT_STREQ(name, source.GetSlotFileName(0));
}
TEST_F(SpriteCopyTest, BitmapCopyEmptySourceSlotClearsPreviouslyResidentDestinationPixels) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.SetSlotCount(2));
    char emptyName[] = "missing-source.bmp"; ASSERT_TRUE(source.SetSlotFileName(1, emptyName)); ASSERT_EQ(nullptr, source.LockSurfacePtr(1));
    ASSERT_TRUE(destination.Create(3, 2, 32)); ASSERT_TRUE(destination.Create(3, 2, 32, 1)); ASSERT_TRUE(destination.SetPixel(2, 1, 0xffabcdef, 1));
    destination.CopySpriteData(&source); EXPECT_EQ(nullptr, destination.LockSurfacePtr(1)); EXPECT_STREQ(emptyName, destination.GetSlotFileName(1));
}
TEST_F(SpriteCopyTest, BitmapCopyReplacesDimensionsAndDropsTrailingSlots) {
    RCKSprite source(Context), destination(Context); ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.SetPixel(2, 1, 0xff112233));
    ASSERT_TRUE(destination.Create(2, 4, 32)); ASSERT_TRUE(destination.Create(2, 4, 32, 1)); destination.CopySpriteData(&source);
    EXPECT_EQ(3, destination.GetWidth()); EXPECT_EQ(2, destination.GetHeight()); EXPECT_EQ(1, destination.GetSlotCount()); EXPECT_EQ(0xff112233u, destination.GetPixel(2, 1));
}
TEST_F(SpriteCopyTest, BitmapCopyDeepCopiesPropertiesAndPlainSourceClearsOldOwnedProperties) {
    RCKSprite destination(Context); CKBitmapProperties p = Format(); destination.SetSaveFormat(&p);
    {
        RCKSprite source(Context); ASSERT_TRUE(source.Create(3, 2, 32)); source.SetSaveFormat(&p); destination.CopySpriteData(&source);
        ASSERT_NE(nullptr, destination.GetSaveFormat()); EXPECT_NE(source.GetSaveFormat(), destination.GetSaveFormat());
        EXPECT_EQ(p.m_ReaderGuid, destination.GetSaveFormat()->m_ReaderGuid);
    }
    ASSERT_NE(nullptr, destination.GetSaveFormat()); EXPECT_EQ(p.m_ReaderGuid, destination.GetSaveFormat()->m_ReaderGuid);
    RCKSprite plain(Context); ASSERT_TRUE(plain.Create(3, 2, 32)); destination.CopySpriteData(&plain); EXPECT_EQ(nullptr, destination.GetSaveFormat());
}
TEST_F(SpriteCopyTest, NullBitmapCopyKeepsDestinationPixelsAndMetadata) {
    RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff89abcd)); s.SetPickThreshold(71);
    s.CopySpriteData(nullptr); EXPECT_EQ(0xff89abcdu, s.GetPixel(1, 1)); EXPECT_EQ(71, s.GetPickThreshold()); EXPECT_EQ(1, s.GetSlotCount());
}
TEST_F(SpriteCopyTest, CopyCombinesBaseHierarchyAndIndependentBitmapOwnership) {
    RCK2dEntity parent(Context); RCKSprite source(Context), destination(Context); RCKMaterial material(Context);
    ASSERT_TRUE(source.Create(3, 2, 32)); ASSERT_TRUE(source.SetPixel(2, 1, 0xff112233)); source.SetRect(VxRect(7, 9, 17, 29)); source.SetZOrder(-27); source.SetMaterial(&material);
    source.SetParent(reinterpret_cast<CK2dEntity *>(&parent)); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); VxRect r; destination.GetRect(r); Rect(r, 7, 9, 17, 29);
    EXPECT_EQ(-27, destination.GetZOrder()); EXPECT_EQ(&material, destination.GetMaterial()); EXPECT_EQ(reinterpret_cast<CK2dEntity *>(&parent), destination.GetParent());
    EXPECT_EQ(2, parent.GetChildrenCount()); EXPECT_EQ(0xff112233u, destination.GetPixel(2, 1)); EXPECT_NE(source.LockSurfacePtr(), destination.LockSurfacePtr());
    destination.SetParent(nullptr); source.SetParent(nullptr); dependencies.StopDependencies();
}
TEST_F(SpriteCopyTest, CopySelfPreservesPixelsAndSingleHierarchyEntry) {
    RCK2dEntity parent(Context); RCKSprite s(Context); ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff718293));
    s.SetParent(reinterpret_cast<CK2dEntity *>(&parent)); CKBYTE *buffer = s.LockSurfacePtr(); CKDependencies options; Options(options);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, s.Copy(s, dependencies));
    EXPECT_EQ(buffer, s.LockSurfacePtr()); EXPECT_EQ(0xff718293u, s.GetPixel(1, 1)); EXPECT_EQ(1, parent.GetChildrenCount()); s.SetParent(nullptr); dependencies.StopDependencies();
}
TEST_F(SpriteCopyTest, PrepareDependenciesIncludesInheritedReferencesAndPropagatesSaveExclusion) {
    RCKSprite s(Context), child(Context); RCKMaterial material(Context); s.SetMaterial(&material); child.SetParent(reinterpret_cast<CK2dEntity *>(&s));
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, s.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(s.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(child.GetID())); dependencies.StopDependencies();
    CKDependenciesContext excluded(Context); excluded.StartDependencies(&options); excluded.SetOperationMode(CK_DEPENDENCIES_SAVE); s.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, s.PrepareDependencies(excluded)); EXPECT_FALSE(excluded.IsDependenciesHere(s.GetID())); excluded.StopDependencies(); child.SetParent(nullptr);
}
TEST_F(SpriteCopyTest, RemapDependenciesRemapsInheritedMaterialAndParentWhileKeepingBitmap) {
    RCK2dEntity parent(Context), other(Context); RCKMaterial material(Context), remapped(Context); RCKSprite s(Context);
    ASSERT_TRUE(s.Create(3, 2, 32)); ASSERT_TRUE(s.SetPixel(1, 1, 0xff123456)); s.SetMaterial(&material); s.SetParent(reinterpret_cast<CK2dEntity *>(&parent));
    CKDependencies options; Options(options); SpriteDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(parent, other); dependencies.Map(material, remapped);
    ASSERT_EQ(CK_OK, s.RemapDependencies(dependencies)); EXPECT_EQ(&remapped, s.GetMaterial()); EXPECT_EQ(reinterpret_cast<CK2dEntity *>(&other), s.GetParent());
    EXPECT_EQ(0xff123456u, s.GetPixel(1, 1)); s.SetParent(nullptr); dependencies.StopDependencies();
}
