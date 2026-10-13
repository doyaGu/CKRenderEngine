#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"
#include "RCKRenderObject.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class MaterialBatchTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Options(CKDependencies &options) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0; options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    }
    static void LiteralMaterialFlags(CKStateChunk &chunk, int version, CKDWORD low) {
        chunk.StartWrite(); chunk.SetDataVersion(version); chunk.WriteIdentifier(0x1000);
        if (version == 4) {
            const float colors[4][4] = {{.7f, .7f, .7f, 1}, {.3f, .3f, .3f, 1}, {.5f, .5f, .5f, 1}, {0, 0, 0, 1}};
            for (int i = 0; i < 4; ++i) { chunk.WriteInt(16); for (int j = 0; j < 4; ++j) chunk.WriteFloat(colors[i][j]); }
            chunk.WriteFloat(0); chunk.WriteInt(0); chunk.WriteDword(low);
            const CKDWORD modes[] = {4, 2, 2, 2, 1, 2, 3, 1, 0, 4}; for (int i = 0; i < 10; ++i) chunk.WriteDword(modes[i]);
        } else {
            chunk.WriteDword(0xffb2b2b2); chunk.WriteDword(0xff4c4c4c); chunk.WriteDword(0xff7f7f7f); chunk.WriteDword(0xff000000);
            chunk.WriteFloat(0); chunk.WriteInt(0); chunk.WriteDword(0); chunk.WriteDword(0x13212224); chunk.WriteDword(0x00080400 | low);
        }
        chunk.CloseChunk();
    }
};
}

TEST_F(MaterialBatchTest, FlushBeforeFirstAdditionKeepsBatchAbsentAndPreservesMaterialState) {
    RCKMaterial material(Context); EXPECT_EQ(nullptr, material.GetSprite3DBatch()); material.EnableAlphaTest(TRUE); material.SetAlphaRef(79);
    material.SetZFunc(VXCMP_GREATER); material.FlushSprite3DBatch(); EXPECT_EQ(nullptr, material.GetSprite3DBatch());
    EXPECT_TRUE(material.AlphaTestEnabled()); EXPECT_EQ(79, material.GetAlphaRef()); EXPECT_EQ(VXCMP_GREATER, material.GetZFunc());
}
TEST_F(MaterialBatchTest, FirstAdditionStartsQueueAndSecondAdditionAppendsRealSpriteVertices) {
    RCKMaterial material(Context); RCKSprite3D first(Context), second(Context); first.SetPosition3f(3, 5, 7); second.SetPosition3f(13, 17, 19);
    ASSERT_TRUE(material.AddSprite3DBatch(&first)); CKSprite3DBatch *batch = material.GetSprite3DBatch(); ASSERT_NE(nullptr, batch); ASSERT_EQ(4, batch->m_Vertices.Size());
    EXPECT_FLOAT_EQ(2, batch->m_Vertices[0].V.x); EXPECT_FLOAT_EQ(4, batch->m_Vertices[0].V.y); EXPECT_FLOAT_EQ(7, batch->m_Vertices[0].V.z);
    EXPECT_FLOAT_EQ(4, batch->m_Vertices[2].V.x); EXPECT_FLOAT_EQ(6, batch->m_Vertices[2].V.y);
    EXPECT_FLOAT_EQ(0, batch->m_Vertices[0].tu); EXPECT_FLOAT_EQ(1, batch->m_Vertices[0].tv);
    EXPECT_FALSE(material.AddSprite3DBatch(&second)); EXPECT_EQ(batch, material.GetSprite3DBatch()); ASSERT_EQ(8, batch->m_Vertices.Size());
    EXPECT_FLOAT_EQ(12, batch->m_Vertices[4].V.x); EXPECT_FLOAT_EQ(16, batch->m_Vertices[4].V.y); EXPECT_FLOAT_EQ(19, batch->m_Vertices[4].V.z);
    EXPECT_FLOAT_EQ(14, batch->m_Vertices[6].V.x); EXPECT_FLOAT_EQ(18, batch->m_Vertices[6].V.y); EXPECT_FLOAT_EQ(2, batch->m_Vertices[0].V.x);
}
TEST_F(MaterialBatchTest, QueueFlushClearsDrawStorageCountersAndFlagsThenReusesSameOwnedBatch) {
    RCKMaterial material(Context); RCKSprite3D sprite(Context); ASSERT_TRUE(material.AddSprite3DBatch(&sprite));
    CKSprite3DBatch *batch = material.GetSprite3DBatch(); ASSERT_NE(nullptr, batch); batch->m_Indices.PushBack(3); batch->m_Indices.PushBack(1);
    batch->m_IndexCount = 2; batch->m_VertexCount = 4; batch->m_Flags = 7; material.FlushSprite3DBatch();
    EXPECT_EQ(batch, material.GetSprite3DBatch()); EXPECT_EQ(0, batch->m_Indices.Size()); EXPECT_EQ(0, batch->m_Vertices.Size());
    EXPECT_EQ(0u, batch->m_IndexCount); EXPECT_EQ(0u, batch->m_VertexCount); EXPECT_EQ(0u, batch->m_Flags);
    ASSERT_TRUE(material.AddSprite3DBatch(&sprite)); EXPECT_EQ(batch, material.GetSprite3DBatch()); EXPECT_EQ(4, batch->m_Vertices.Size());
    EXPECT_FALSE(material.AddSprite3DBatch(&sprite)); EXPECT_EQ(8, batch->m_Vertices.Size());
}
TEST_F(MaterialBatchTest, BatchSchedulingAndFlushPreserveAllPublicRenderFlags) {
    RCKMaterial material(Context); RCKSprite3D sprite(Context); material.SetTwoSided(TRUE); material.EnableZWrite(FALSE); material.EnablePerspectiveCorrection(FALSE);
    material.EnableAlphaBlend(TRUE); material.EnableAlphaTest(TRUE); material.SetZFunc(VXCMP_GREATER); material.SetAlphaFunc(VXCMP_NOTEQUAL); material.SetAlphaRef(127);
    ASSERT_TRUE(material.AddSprite3DBatch(&sprite)); material.FlushSprite3DBatch(); EXPECT_TRUE(material.IsTwoSided()); EXPECT_FALSE(material.ZWriteEnabled());
    EXPECT_FALSE(material.PerspectiveCorrectionEnabled()); EXPECT_TRUE(material.AlphaBlendEnabled()); EXPECT_TRUE(material.AlphaTestEnabled());
    EXPECT_EQ(VXCMP_GREATER, material.GetZFunc()); EXPECT_EQ(VXCMP_NOTEQUAL, material.GetAlphaFunc()); EXPECT_EQ(127, material.GetAlphaRef());
}
TEST_F(MaterialBatchTest, CopyFromQueuedSourceDoesNotImportItsQueueStateOrShareItsBatch) {
    RCKMaterial source(Context), destination(Context); RCKSprite3D first(Context), second(Context); ASSERT_TRUE(source.AddSprite3DBatch(&first));
    CKSprite3DBatch *sourceBatch = source.GetSprite3DBatch(); source.SetDiffuse(VxColor(.2f, .3f, .4f, .5f)); CKDependencies options; Options(options);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(nullptr, destination.GetSprite3DBatch()); EXPECT_FLOAT_EQ(.2f, destination.GetDiffuse().r);
    EXPECT_TRUE(destination.AddSprite3DBatch(&second)); ASSERT_NE(nullptr, destination.GetSprite3DBatch()); EXPECT_NE(sourceBatch, destination.GetSprite3DBatch());
    EXPECT_EQ(4, sourceBatch->m_Vertices.Size()); EXPECT_EQ(4, destination.GetSprite3DBatch()->m_Vertices.Size());
    EXPECT_FALSE(source.AddSprite3DBatch(&first)); EXPECT_EQ(8, sourceBatch->m_Vertices.Size()); dependencies.StopDependencies();
}
TEST_F(MaterialBatchTest, CopyOntoQueuedDestinationPreservesExistingQueueAndNextAdditionIsContinuation) {
    RCKMaterial source(Context), destination(Context); RCKSprite3D sprite(Context); ASSERT_TRUE(destination.AddSprite3DBatch(&sprite));
    CKSprite3DBatch *batch = destination.GetSprite3DBatch(); source.SetAlphaRef(93); CKDependencies options; Options(options);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(batch, destination.GetSprite3DBatch()); EXPECT_EQ(4, batch->m_Vertices.Size()); EXPECT_EQ(93, destination.GetAlphaRef());
    EXPECT_FALSE(destination.AddSprite3DBatch(&sprite)); EXPECT_EQ(8, batch->m_Vertices.Size()); EXPECT_EQ(nullptr, source.GetSprite3DBatch()); dependencies.StopDependencies();
}
TEST_F(MaterialBatchTest, CopyAfterFlushedDestinationRetainsAllocatedStorageAndStartsNewQueue) {
    RCKMaterial source(Context), destination(Context); RCKSprite3D sprite(Context); ASSERT_TRUE(source.AddSprite3DBatch(&sprite));
    ASSERT_TRUE(destination.AddSprite3DBatch(&sprite)); CKSprite3DBatch *batch = destination.GetSprite3DBatch(); destination.FlushSprite3DBatch();
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_EQ(batch, destination.GetSprite3DBatch()); EXPECT_EQ(0, batch->m_Vertices.Size());
    EXPECT_TRUE(destination.AddSprite3DBatch(&sprite)); EXPECT_EQ(4, batch->m_Vertices.Size()); EXPECT_EQ(4, source.GetSprite3DBatch()->m_Vertices.Size()); dependencies.StopDependencies();
}
TEST_F(MaterialBatchTest, SelfCopyPreservesQueuedBatchPointerDataAndContinuationBehavior) {
    RCKMaterial material(Context); RCKSprite3D sprite(Context); ASSERT_TRUE(material.AddSprite3DBatch(&sprite)); CKSprite3DBatch *batch = material.GetSprite3DBatch();
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, material.Copy(material, dependencies)); EXPECT_EQ(batch, material.GetSprite3DBatch()); EXPECT_EQ(4, batch->m_Vertices.Size());
    EXPECT_FALSE(material.AddSprite3DBatch(&sprite)); EXPECT_EQ(8, batch->m_Vertices.Size()); dependencies.StopDependencies();
}
TEST_F(MaterialBatchTest, LegacyAndPackedLoadDoNotImportWireQueueBitIntoFreshMaterial) {
    for (int version : {4, 5}) {
        RCKMaterial material(Context); RCKSprite3D sprite(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); LiteralMaterialFlags(chunk, version, 0x26);
        ASSERT_EQ(nullptr, material.GetSprite3DBatch()); ASSERT_EQ(CK_OK, material.Load(&chunk, nullptr)); EXPECT_EQ(nullptr, material.GetSprite3DBatch());
        EXPECT_TRUE(material.AddSprite3DBatch(&sprite)); ASSERT_NE(nullptr, material.GetSprite3DBatch()); EXPECT_EQ(4, material.GetSprite3DBatch()->m_Vertices.Size());
        EXPECT_FALSE(material.AddSprite3DBatch(&sprite)); EXPECT_EQ(8, material.GetSprite3DBatch()->m_Vertices.Size());
    }
}
TEST_F(MaterialBatchTest, LegacyAndPackedLoadPreserveExistingQueuePointerVerticesAndContinuationBit) {
    for (int version : {4, 5}) {
        RCKMaterial material(Context); RCKSprite3D sprite(Context); ASSERT_TRUE(material.AddSprite3DBatch(&sprite)); CKSprite3DBatch *batch = material.GetSprite3DBatch();
        ASSERT_NE(nullptr, batch); ASSERT_EQ(4, batch->m_Vertices.Size()); const float x = batch->m_Vertices[0].V.x;
        CKStateChunk chunk(CKCID_MATERIAL, nullptr); LiteralMaterialFlags(chunk, version, 6); ASSERT_EQ(CK_OK, material.Load(&chunk, nullptr));
        EXPECT_EQ(batch, material.GetSprite3DBatch()); EXPECT_EQ(4, batch->m_Vertices.Size()); EXPECT_FLOAT_EQ(x, batch->m_Vertices[0].V.x);
        EXPECT_FALSE(material.AddSprite3DBatch(&sprite)); EXPECT_EQ(8, batch->m_Vertices.Size());
    }
}
