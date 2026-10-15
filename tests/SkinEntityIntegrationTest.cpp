#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKRenderObject.h"
#include "RCKSkin.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SkinEntityIntegrationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCK3dEntity *Owner = nullptr, *Bone = nullptr, *Copy = nullptr;
    RCKMesh *Mesh = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Owner = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        Bone = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        Copy = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        Mesh = static_cast<RCKMesh *>(Context->CreateObject(CKCID_MESH));
        ASSERT_NE(nullptr, Owner); ASSERT_NE(nullptr, Bone); ASSERT_NE(nullptr, Copy); ASSERT_NE(nullptr, Mesh);
        ASSERT_TRUE(Mesh->SetVertexCount(2)); Owner->SetCurrentMesh(Mesh, TRUE);
        Bone->SetPosition3f(4, 0, 0);
    }
    void TearDown() override {
        if (Owner) { Owner->DestroySkin(); Owner->SetCurrentMesh(nullptr); Owner->RemoveMesh(Mesh); }
        if (Copy) { Copy->DestroySkin(); Copy->SetCurrentMesh(nullptr); Copy->RemoveMesh(Mesh); }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Vec(const VxVector &actual, float x, float y, float z) {
        EXPECT_NEAR(x, actual.x, 1e-5f); EXPECT_NEAR(y, actual.y, 1e-5f); EXPECT_NEAR(z, actual.z, 1e-5f);
    }
    void Wire(CKStateChunk &chunk, int version) {
        chunk.StartWrite(); chunk.SetDataVersion(version);
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYSKINDATA);
        if (version < 6) chunk.WriteInt(64);
        chunk.WriteMatrix(VxMatrix::Identity()); chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(Bone);
        chunk.WriteDword(0x112233);
        if (version < 6) chunk.WriteInt(64);
        chunk.WriteMatrix(VxMatrix::Identity()); chunk.WriteInt(2);
        for (int i = 0; i < 2; ++i) {
            chunk.WriteInt(1);
            if (version < 6) chunk.WriteInt(12);
            chunk.WriteVector(i ? VxVector(-1, 2, 0) : VxVector(2, 3, 4));
            if (version < 6) chunk.WriteInt(4);
            chunk.WriteInt(0);
            if (version < 6) chunk.WriteInt(4);
            chunk.WriteFloat(i ? .25f : 1.0f);
        }
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYSKINDATANORMALS);
        chunk.WriteVector(VxVector(0, 1, 0)); chunk.WriteVector(VxVector(1, 0, 0));
        chunk.CloseChunk();
    }
    void LoadedMesh() {
        CKSkin *skin = Owner->GetSkin(); ASSERT_NE(nullptr, skin);
        EXPECT_EQ(2, skin->GetVertexCount()); EXPECT_EQ(2, skin->GetNormalCount());
        ASSERT_TRUE(Owner->UpdateSkin());
        VxVector value; Mesh->GetVertexPosition(0, &value); Vec(value, 6, 3, 4);
        Mesh->GetVertexPosition(1, &value); Vec(value, 0, 2, 0);
        Mesh->GetVertexNormal(0, &value); Vec(value, 0, 1, 0);
        Mesh->GetVertexNormal(1, &value); Vec(value, .25f, 0, 0);
        const VxBbox &bounds = Mesh->GetLocalBox(); Vec(bounds.Min, 0, 2, 0); Vec(bounds.Max, 6, 3, 4);
        // A writable SDK weight changes the next real mesh update, without
        // manually clearing any cache or preparing a private test-only path.
        skin->GetVertexData(1)->SetWeight(0, .5f);
        ASSERT_TRUE(Owner->UpdateSkin()); Mesh->GetVertexPosition(1, &value); Vec(value, 1, 2, 0);
    }
};
}

TEST_F(SkinEntityIntegrationTest, CurrentLiteralSkinFeedsWeightedMeshNormalsAndBounds) {
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); Wire(chunk, 6);
    ASSERT_EQ(CK_OK, Owner->Load(&chunk, nullptr)); LoadedMesh();
}

TEST_F(SkinEntityIntegrationTest, LegacyLiteralSkinPrefixesPreserveTheActualMeshConsumer) {
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); Wire(chunk, 5);
    ASSERT_EQ(CK_OK, Owner->Load(&chunk, nullptr)); LoadedMesh();
}

TEST_F(SkinEntityIntegrationTest, EntitySaveUsesUnsizedNativeSkinPayloadsAndReloadsForEvaluation) {
    CKStateChunk literal(CKCID_3DENTITY, nullptr); Wire(literal, 6);
    ASSERT_EQ(CK_OK, Owner->Load(&literal, nullptr));
    CKStateChunk *saved = Owner->Save(nullptr, CK_STATESAVE_3DENTITYMATRIX); ASSERT_NE(nullptr, saved);
    saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_3DENTITYSKINDATA));
    VxMatrix matrix; saved->ReadMatrix(matrix);
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col)
        EXPECT_FLOAT_EQ(row == col ? 1.0f : 0.0f, matrix[row][col]);
    ASSERT_EQ(1, saved->StartReadSequence()); EXPECT_EQ(Bone->GetID(), saved->ReadObjectID());
    EXPECT_EQ(0x112233u, saved->ReadDword()); saved->ReadMatrix(matrix);
    ASSERT_EQ(2, saved->ReadInt());
    for (int i = 0; i < 2; ++i) {
        EXPECT_EQ(1, saved->ReadInt()); VxVector position; saved->ReadVector(&position);
        if (i) Vec(position, -1, 2, 0); else Vec(position, 2, 3, 4);
        EXPECT_EQ(0, saved->ReadInt()); EXPECT_FLOAT_EQ(i ? .25f : 1.0f, saved->ReadFloat());
    }
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_3DENTITYSKINDATANORMALS));
    VxVector normal; saved->ReadVector(&normal); Vec(normal, 0, 1, 0);
    saved->ReadVector(&normal); Vec(normal, 1, 0, 0);
    ASSERT_EQ(CK_OK, Owner->Load(saved, nullptr)); DeleteCKStateChunk(saved);
    LoadedMesh();
}

TEST_F(SkinEntityIntegrationTest, CopiedSkinSurvivesSourceDestructionAndUpdatesIndependentMeshData) {
    CKStateChunk literal(CKCID_3DENTITY, nullptr); Wire(literal, 6);
    ASSERT_EQ(CK_OK, Owner->Load(&literal, nullptr));
    CKDependencies options; options.Resize(80);
    for (int i = 0; i < 80; ++i) options[i] = 0;
    options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, Copy->Copy(*Owner, dependencies)); dependencies.StopDependencies();
    CKSkin *skin = Copy->GetSkin(); ASSERT_NE(nullptr, skin); EXPECT_NE(Owner->GetSkin(), skin);
    Owner->DestroySkin();
    VxVector positions[2], normals[2];
    ASSERT_TRUE(skin->CalcPointsEx(2, reinterpret_cast<CKBYTE *>(positions), sizeof(VxVector),
                                  reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector)));
    Vec(positions[0], 6, 3, 4); Vec(positions[1], 0, 2, 0); Vec(normals[1], .25f, 0, 0);
    skin->GetVertexData(1)->SetWeight(0, .5f);
    ASSERT_TRUE(skin->CalcPoints(2, reinterpret_cast<CKBYTE *>(positions), sizeof(VxVector)));
    Vec(positions[1], 1, 2, 0);
}
