#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKKeyframeData.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMesh.h"
#include "RCKObjectAnimation.h"
#include "RCKSkin.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class Entity3dSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_SCENEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Pose(CKStateChunk &chunk, CKDWORD flags, CKDWORD movable, const VxVector &position) {
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(flags); chunk.WriteDword(movable);
        chunk.WriteVector(VxVector(-2, 0, 0)); chunk.WriteVector(VxVector(0, 3, 0));
        chunk.WriteVector(VxVector(0, 0, 4)); chunk.WriteVector(position);
    }
    static CKERROR Load(RCK3dEntity &entity, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return entity.Load(&chunk, file);
    }
    static void SkinWire(CKStateChunk &chunk, RCK3dEntity &bone, int version) {
        chunk.SetDataVersion(version); chunk.WriteIdentifier(CK_STATESAVE_3DENTITYSKINDATA);
        VxMatrix initial = VxMatrix::Identity(); initial[3][0] = 6;
        if (version < 6) chunk.WriteInt(64);
        chunk.WriteMatrix(initial); chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(&bone);
        chunk.WriteDword(7); if (version < 6) chunk.WriteInt(64); chunk.WriteMatrix(VxMatrix::Identity());
        chunk.WriteInt(1); chunk.WriteInt(1); if (version < 6) chunk.WriteInt(12);
        chunk.WriteVector(VxVector(2, 3, 4));
        if (version < 6) chunk.WriteInt(4); chunk.WriteInt(0);
        if (version < 6) chunk.WriteInt(4); chunk.WriteFloat(.75f);
        chunk.WriteIdentifier(CK_STATESAVE_3DENTITYSKINDATANORMALS); chunk.WriteVector(VxVector(0, 1, 0));
    }
    static void ExpectSkin(RCK3dEntity &entity, RCK3dEntity &bone) {
        ASSERT_NE(nullptr, entity.m_Skin); EXPECT_EQ(1, entity.m_Skin->GetBoneCount());
        EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&bone), entity.m_Skin->GetBoneData(0)->GetBone());
        EXPECT_EQ(7u, static_cast<RCKSkinBoneData *>(entity.m_Skin->GetBoneData(0))->GetFlags());
        EXPECT_FLOAT_EQ(6, entity.m_Skin->GetObjectInitMatrix()[3][0]);
        ASSERT_EQ(1, entity.m_Skin->GetVertexCount()); CKSkinVertexData *vertex = entity.m_Skin->GetVertexData(0);
        ASSERT_NE(nullptr, vertex); EXPECT_EQ(1, vertex->GetBoneCount()); EXPECT_EQ(0, vertex->GetBone(0));
        EXPECT_FLOAT_EQ(.75f, vertex->GetWeight(0)); EXPECT_FLOAT_EQ(3, vertex->GetInitialPos().y);
        ASSERT_EQ(1, entity.m_Skin->GetNormalCount()); EXPECT_FLOAT_EQ(1, entity.m_Skin->GetNormal(0).y);
    }
};
class DerivedSerializationEntity : public RCK3dEntity {
public:
    explicit DerivedSerializationEntity(CKContext *context) : RCK3dEntity(context) {}
    CK_CLASSID GetClassID() override { return CKCID_3DOBJECT; }
};
class CurveSerializationEntity : public RCK3dEntity {
public:
    explicit CurveSerializationEntity(CKContext *context) : RCK3dEntity(context) {}
    CK_CLASSID GetClassID() override { return CKCID_CURVE; }
};
}

TEST_F(Entity3dSerializationTest, CurrentIndependentWireRestoresParentPoseAndIndirectHandedness) {
    RCK3dEntity parent(Context), entity(Context); entity.m_MoveableFlags &= ~VX_MOVEABLE_WORLDALIGNED;
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    Pose(chunk, CK_3DENTITY_PARENTVALID, VX_MOVEABLE_VISIBLE | VX_MOVEABLE_INVERSEWORLDMATVALID, VxVector(7, 8, 9));
    chunk.WriteObject(&parent); ASSERT_EQ(CK_OK, Load(entity, chunk));
    EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&parent), entity.GetParent());
    EXPECT_EQ(1, parent.GetChildrenCount()); EXPECT_TRUE(entity.GetMoveableFlags() & VX_MOVEABLE_INDIRECTMATRIX);
    VxVector zero(0, 0, 0), transformed; entity.Transform(&transformed, &zero);
    EXPECT_FLOAT_EQ(7, transformed.x); EXPECT_FLOAT_EQ(8, transformed.y); EXPECT_FLOAT_EQ(9, transformed.z);
    entity.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dSerializationTest, MemoryLoadPreservesWorldAlignedPoseButFileLoadAppliesWirePose) {
    RCK3dEntity entity(Context); VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 22; entity.SetWorldMatrix(matrix, TRUE);
    // SetWorldMatrix legitimately clears WORLDALIGNED. Restore the snapshot
    // load's preservation precondition explicitly through the public flag API.
    entity.SetMoveableFlags(entity.GetMoveableFlags() | VX_MOVEABLE_WORLDALIGNED);
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    Pose(chunk, 0, 0, VxVector(7, 8, 9)); ASSERT_EQ(CK_OK, Load(entity, chunk));
    EXPECT_FLOAT_EQ(22, entity.GetWorldMatrix()[3][0]);
    CKFile file(Context); ASSERT_EQ(CK_OK, entity.Load(&chunk, &file)); EXPECT_FLOAT_EQ(7, entity.GetWorldMatrix()[3][0]);
}

TEST_F(Entity3dSerializationTest, LegacyIndependentMatrixUsesByteLengthPrefixAndParentIdentifier) {
    RCK3dEntity parent(Context), entity(Context); entity.m_MoveableFlags &= ~VX_MOVEABLE_WORLDALIGNED;
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    chunk.WriteIdentifier(CK_STATESAVE_PARENT); chunk.WriteObject(&parent);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYFLAGS); chunk.WriteDword(0); chunk.WriteDword(0);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][1] = 15;
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYMATRIX); chunk.WriteInt(64); chunk.WriteMatrix(matrix);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); EXPECT_EQ(reinterpret_cast<CK3dEntity *>(&parent), entity.GetParent());
    EXPECT_FLOAT_EQ(15, entity.GetWorldMatrix()[3][1]); entity.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dSerializationTest, MissingParentIdentifierDetachesExistingParentAndNullChunkFails) {
    RCK3dEntity parent(Context), entity(Context); entity.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, entity.Load(nullptr, nullptr)); EXPECT_EQ(1, parent.GetChildrenCount());
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    ASSERT_EQ(CK_OK, Load(entity, chunk)); EXPECT_EQ(nullptr, entity.GetParent()); EXPECT_EQ(0, parent.GetChildrenCount());
}

TEST_F(Entity3dSerializationTest, CurrentIndependentSkinWireHasUnsizedBoneWeightAndNormalPayloads) {
    RCK3dEntity bone(Context), entity(Context); CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite();
    SkinWire(chunk, bone, 6); ASSERT_EQ(CK_OK, Load(entity, chunk)); ExpectSkin(entity, bone);
}

TEST_F(Entity3dSerializationTest, LegacySkinWireConsumesEachSizePrefixWithoutShiftingWeights) {
    RCK3dEntity bone(Context), entity(Context); CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite();
    SkinWire(chunk, bone, 5); ASSERT_EQ(CK_OK, Load(entity, chunk)); ExpectSkin(entity, bone);
}

TEST_F(Entity3dSerializationTest, SaveWritesIndependentExpectedPoseAndOptionalParentOrder) {
    RCK3dEntity parent(Context), entity(Context); entity.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 13; entity.SetWorldMatrix(matrix, TRUE);
    CKStateChunk *saved = entity.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX); ASSERT_NE(nullptr, saved);
    EXPECT_EQ(CKCID_3DENTITY, saved->GetChunkClassID()); EXPECT_EQ(-1, saved->GetCurrentPos()); saved->StartRead();
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); CKDWORD flags = saved->ReadDword();
    EXPECT_TRUE(flags & CK_3DENTITY_PARENTVALID); EXPECT_FALSE(flags & CK_3DENTITY_PLACEVALID);
    saved->ReadDword(); const float expected[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 13, 0, 0};
    for (int i = 0; i < 12; ++i) EXPECT_FLOAT_EQ(expected[i], saved->ReadFloat());
    EXPECT_EQ(parent.GetID(), saved->ReadObjectID()); DeleteCKStateChunk(saved); entity.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dSerializationTest, SaveMeshAndAnimationSequencesKeepReferenceOrder) {
    RCK3dEntity entity(Context); RCKMesh first(Context), second(Context); RCKObjectAnimation one(Context), two(Context);
    entity.AddMesh(&first); entity.AddMesh(&second); entity.SetCurrentMesh(&second, FALSE);
    entity.AddObjectAnimation(&one); entity.AddObjectAnimation(&two);
    CKStateChunk *saved = entity.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX); ASSERT_NE(nullptr, saved); saved->StartRead();
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_MESHS)); EXPECT_EQ(second.GetID(), saved->ReadObjectID());
    ASSERT_EQ(2, saved->StartReadSequence()); EXPECT_EQ(first.GetID(), saved->ReadObjectID()); EXPECT_EQ(second.GetID(), saved->ReadObjectID());
    ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_ANIMATION)); ASSERT_EQ(2, saved->StartReadSequence());
    EXPECT_EQ(one.GetID(), saved->ReadObjectID()); EXPECT_EQ(two.GetID(), saved->ReadObjectID()); DeleteCKStateChunk(saved);
}

TEST_F(Entity3dSerializationTest, SaveZeroFlagsDelegatesAndDerivedChunkRemainsWritable) {
    RCK3dEntity entity(Context); CKStateChunk *base = entity.Save(nullptr, 0); ASSERT_NE(nullptr, base); base->StartRead();
    EXPECT_FALSE(base->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA)); DeleteCKStateChunk(base);
    DerivedSerializationEntity derived(Context); CKStateChunk *saved = derived.Save(nullptr, CK_STATESAVE_3DENTITYMATRIX);
    ASSERT_NE(nullptr, saved); EXPECT_GE(saved->GetCurrentPos(), 0); saved->WriteIdentifier(0x76543210); saved->WriteInt(19);
    saved->CloseChunk(); saved->StartRead(); ASSERT_TRUE(saved->SeekIdentifier(0x76543210)); EXPECT_EQ(19, saved->ReadInt()); DeleteCKStateChunk(saved);
}

TEST_F(Entity3dSerializationTest, PreSaveQueuesActualReferencesAndHierarchyUsesAllChildSaveFlags) {
    RCK3dEntity entity(Context), child(Context); RCKMesh mesh(Context); RCKObjectAnimation animation(Context); CKFile file(Context);
    // Initialize the same per-class save table normally prepared by StartSave,
    // while exercising PreSave's real object registration without filesystem IO.
    file.m_IndexByClassId.Resize(80);
    entity.AddMesh(&mesh); entity.AddObjectAnimation(&animation); child.SetParent(reinterpret_cast<CK3dEntity *>(&entity), TRUE);
    entity.PreSave(&file, CK_STATESAVE_3DENTITYHIERARCHY);
    EXPECT_TRUE(file.IsObjectToBeSaved(mesh.GetID())); EXPECT_TRUE(file.IsObjectToBeSaved(animation.GetID()));
    EXPECT_TRUE(file.IsObjectToBeSaved(child.GetID()));
    bool found = false; for (int i = 0; i < file.m_FileObjects.Size(); ++i) if (file.m_FileObjects[i].Object == child.GetID()) {
        found = true; EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[i].SaveFlags);
    }
    EXPECT_TRUE(found); child.SetParent(nullptr, TRUE);
}

TEST_F(Entity3dSerializationTest, PreSaveCurveSkipsMeshesAndSkinMarksCurrentMeshProcedural) {
    CurveSerializationEntity curve(Context); RCKMesh mesh(Context); curve.AddMesh(&mesh); CKFile file(Context);
    curve.PreSave(&file, 0); EXPECT_FALSE(file.IsObjectToBeSaved(mesh.GetID()));
    RCK3dEntity entity(Context); entity.SetCurrentMesh(&mesh, TRUE); ASSERT_NE(nullptr, entity.CreateSkin());
    entity.PreSave(nullptr, 0); EXPECT_TRUE(mesh.GetFlags() & VXMESH_PROCEDURALPOS);
}

TEST_F(Entity3dSerializationTest, PostLoadCreatesMissingScaleKeysForActualRotationAnimation) {
    RCK3dEntity entity(Context); RCKObjectAnimation animation(Context);
    ASSERT_NE(nullptr, animation.CreateController(CKANIMATION_LINROT_CONTROL));
    // Associate the channel through the real Load path. AddObjectAnimation
    // already creates scale keys, whereas file loading defers this to PostLoad.
    entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    CKStateChunk chunk(CKCID_3DENTITY, nullptr); chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_ANIMATION);
    chunk.StartObjectIDSequence(1); chunk.WriteObjectSequence(&animation);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(2, 0, 0)); chunk.WriteVector(VxVector(0, 3, 0));
    chunk.WriteVector(VxVector(0, 0, 4)); chunk.WriteVector(VxVector(0, 0, 0));
    ASSERT_EQ(CK_OK, Load(entity, chunk)); ASSERT_EQ(1, entity.GetObjectAnimationCount());
    EXPECT_EQ(&animation, entity.GetObjectAnimation(0)); ASSERT_EQ(nullptr, animation.GetScaleController());
    entity.PostLoad();
    ASSERT_NE(nullptr, animation.GetScaleController()); EXPECT_EQ(1, animation.GetScaleController()->GetKeyCount());
    VxVector evaluated; ASSERT_TRUE(animation.EvaluateScale(0, evaluated));
    EXPECT_FLOAT_EQ(2, evaluated.x); EXPECT_FLOAT_EQ(3, evaluated.y); EXPECT_FLOAT_EQ(4, evaluated.z);
    entity.PostLoad(); EXPECT_EQ(1, animation.GetScaleController()->GetKeyCount());
}
