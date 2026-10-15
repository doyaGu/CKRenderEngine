#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
class EditingMesh : public RCKMesh {
public:
    int Notifications = 0;
    explicit EditingMesh(CKContext *context) : RCKMesh(context) {}
    void ColorChanged() override {
        ++Notifications;
        RCKMesh::ColorChanged();
    }
    void Ready() { m_Flags &= ~VXMESH_COLOR_CHANGED; m_Valid = TRUE; Notifications = 0; }
    CKBOOL Valid() const { return m_Valid; }
};
class MeshEditingStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};
}

TEST_F(MeshEditingStateTest, DiffuseWritePreservesSpecularAndNotifiesVirtualColorConsumer) {
    EditingMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2));
    mesh.SetVertexSpecularColor(1, 0x12345678); mesh.Ready();
    const CKDWORD before = mesh.GetFlags();
    mesh.SetVertexColor(1, 0x80402010);
    EXPECT_EQ(0x80402010u, mesh.GetVertexColor(1));
    EXPECT_EQ(0x12345678u, mesh.GetVertexSpecularColor(1));
    EXPECT_EQ(0xffffffffu, mesh.GetVertexColor(0));
    EXPECT_EQ(1, mesh.Notifications); EXPECT_FALSE(mesh.Valid());
    EXPECT_EQ(before | VXMESH_COLOR_CHANGED, mesh.GetFlags());
}
TEST_F(MeshEditingStateTest, SpecularWritePreservesDiffuseAndNotifiesVirtualColorConsumer) {
    EditingMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2));
    mesh.SetVertexColor(0, 0x80402010); mesh.Ready();
    const CKDWORD before = mesh.GetFlags();
    mesh.SetVertexSpecularColor(0, 0x12345678);
    EXPECT_EQ(0x80402010u, mesh.GetVertexColor(0));
    EXPECT_EQ(0x12345678u, mesh.GetVertexSpecularColor(0));
    EXPECT_EQ(0u, mesh.GetVertexSpecularColor(1));
    EXPECT_EQ(1, mesh.Notifications); EXPECT_FALSE(mesh.Valid());
    EXPECT_EQ(before | VXMESH_COLOR_CHANGED, mesh.GetFlags());
}
TEST_F(MeshEditingStateTest, EqualColorWritesStillNotifyConsumer) {
    EditingMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(1)); mesh.Ready();
    mesh.SetVertexColor(0, 0xffffffff); mesh.SetVertexSpecularColor(0, 0);
    EXPECT_EQ(2, mesh.Notifications); EXPECT_FALSE(mesh.Valid());
}
TEST_F(MeshEditingStateTest, OutsideColorDomainPreservesDataAndConsumerValidity) {
    EditingMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(1)); mesh.Ready();
    const CKDWORD before = mesh.GetFlags();
    mesh.SetVertexColor(-1, 0); mesh.SetVertexColor(1, 0);
    mesh.SetVertexSpecularColor(-1, 0xffffffff); mesh.SetVertexSpecularColor(1, 0xffffffff);
    EXPECT_EQ(0, mesh.Notifications); EXPECT_TRUE(mesh.Valid()); EXPECT_EQ(before, mesh.GetFlags());
    EXPECT_EQ(0xffffffffu, mesh.GetVertexColor(0)); EXPECT_EQ(0u, mesh.GetVertexSpecularColor(0));
}
TEST_F(MeshEditingStateTest, DirectColorNotificationInvalidatesWithoutChangingPayloadOrTopology) {
    EditingMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(2)); ASSERT_TRUE(mesh.SetLineCount(1));
    mesh.SetLine(0, 0, 1); mesh.SetVertexColor(1, 0x10203040); mesh.Ready();
    mesh.ColorChanged();
    EXPECT_EQ(1, mesh.Notifications); EXPECT_FALSE(mesh.Valid());
    EXPECT_EQ(0x10203040u, mesh.GetVertexColor(1));
    int a = -1, b = -1; mesh.GetLine(0, &a, &b); EXPECT_EQ(0, a); EXPECT_EQ(1, b);
}
TEST_F(MeshEditingStateTest, RawLineWordsMutateTheActualAuthoredTopology) {
    RCKMesh mesh(Context); EXPECT_EQ(nullptr, mesh.GetLineIndices());
    ASSERT_TRUE(mesh.SetVertexCount(4)); ASSERT_TRUE(mesh.SetLineCount(2));
    mesh.SetLine(0, 0, 1); mesh.SetLine(1, 2, 3);
    CKWORD *words = mesh.GetLineIndices(); ASSERT_NE(nullptr, words);
    EXPECT_EQ(0, words[0]); EXPECT_EQ(1, words[1]); EXPECT_EQ(2, words[2]); EXPECT_EQ(3, words[3]);
    words[2] = 3; words[3] = 0;
    int a = -1, b = -1; mesh.GetLine(1, &a, &b); EXPECT_EQ(3, a); EXPECT_EQ(0, b);
}
TEST_F(MeshEditingStateTest, EmptyLogicalLineListExposesRetainedRawStorage) {
    RCKMesh mesh(Context); ASSERT_TRUE(mesh.SetLineCount(2));
    CKWORD *words = mesh.GetLineIndices(); ASSERT_NE(nullptr, words);
    ASSERT_TRUE(mesh.SetLineCount(0)); EXPECT_EQ(0, mesh.GetLineCount());
    EXPECT_EQ(words, mesh.GetLineIndices());
}
TEST_F(MeshEditingStateTest, MaterialConvenienceRemovalKeepsLaterDuplicateAndItsUVAndFaceBit) {
    RCKMesh mesh(Context); RCKMaterial a(Context), b(Context), c(Context);
    ASSERT_TRUE(mesh.SetVertexCount(1)); ASSERT_TRUE(mesh.SetFaceCount(1));
    ASSERT_EQ(0, mesh.AddChannel(&a, FALSE)); ASSERT_EQ(1, mesh.AddChannel(&b, FALSE));
    ASSERT_EQ(2, mesh.AddChannel(&c, FALSE)); mesh.SetChannelMaterial(2, &a);
    mesh.SetVertexTextureCoordinates(0, .75f, .875f, 2); mesh.SetFaceChannelMask(0, 5);
    mesh.RemoveChannel(&a);
    EXPECT_EQ(2, mesh.GetChannelCount()); EXPECT_EQ(&b, mesh.GetChannelMaterial(0));
    EXPECT_EQ(&a, mesh.GetChannelMaterial(1)); EXPECT_EQ(2, mesh.GetFaceChannelMask(0));
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(0, &u, &v, 1);
    EXPECT_FLOAT_EQ(.75f, u); EXPECT_FLOAT_EQ(.875f, v);
}
TEST_F(MeshEditingStateTest, MissingOrNullMaterialConvenienceRemovalDoesNotChangeChannels) {
    RCKMesh mesh(Context); RCKMaterial a(Context), missing(Context);
    ASSERT_TRUE(mesh.SetVertexCount(1)); ASSERT_TRUE(mesh.SetFaceCount(1));
    mesh.AddChannel(&a, FALSE); mesh.SetFaceChannelMask(0, 1);
    mesh.RemoveChannel(&missing); mesh.RemoveChannel(static_cast<CKMaterial *>(nullptr));
    EXPECT_EQ(1, mesh.GetChannelCount()); EXPECT_EQ(&a, mesh.GetChannelMaterial(0));
    EXPECT_EQ(1, mesh.GetFaceChannelMask(0));
}
