#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKBodyPart.h"
#include "RCKRenderObject.h"
#define private public
#include "RCKKinematicChain.h"
#undef private

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class ChainDependencyMap : public CKDependenciesContext {
public:
    explicit ChainDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class KinematicChainLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKKinematicChain *Chain = nullptr;
    RCKBodyPart *Start = nullptr, *Middle = nullptr, *End = nullptr, *Other = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK3dObject, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
        CKCLASSREGISTERCID(RCKKinematicChain, CKCID_OBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Chain = static_cast<RCKKinematicChain *>(Context->CreateObject(CKCID_KINEMATICCHAIN));
        Start = Part(); Middle = Part(); End = Part(); Other = Part();
        ASSERT_NE(nullptr, Chain); ASSERT_NE(nullptr, Start); ASSERT_NE(nullptr, Middle);
        ASSERT_NE(nullptr, End); ASSERT_NE(nullptr, Other);
        ASSERT_TRUE(Middle->SetParent(Entity(Start), FALSE)); ASSERT_TRUE(End->SetParent(Entity(Middle), FALSE));
        Start->SetPosition3f(10, 20, 30); Middle->SetPosition3f(3, 0, 0, Entity(Start));
        End->SetPosition3f(0, 4, 0, Entity(Middle));
    }
    void TearDown() override {
        if (End) End->SetParent(nullptr, TRUE);
        if (Middle) Middle->SetParent(nullptr, TRUE);
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    RCKBodyPart *Part() { return static_cast<RCKBodyPart *>(Context->CreateObject(CKCID_BODYPART)); }
    static CKBodyPart *Body(RCKBodyPart *part) { return reinterpret_cast<CKBodyPart *>(part); }
    static CK3dEntity *Entity(RCK3dEntity *part) { return reinterpret_cast<CK3dEntity *>(part); }
    void Linked() { Chain->SetStartEffector(Body(Start)); Chain->SetEndEffector(Body(End)); }
    static void Options(CKDependencies &options) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0;
        options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    }
    void Literal(CKStateChunk &chunk, int version, CK_ID first, CK_ID last, CK_ID placeholder = 0) {
        chunk.StartWrite(); chunk.SetDataVersion(version); chunk.WriteIdentifier(0xFFu);
        chunk.WriteInt(placeholder); chunk.WriteInt(first); chunk.WriteInt(last); chunk.CloseChunk();
    }
};
}

TEST_F(KinematicChainLifecycleTest, FactoryRegistrationAndDefaultStateUseRealContext) {
    EXPECT_EQ(CKCID_KINEMATICCHAIN, Chain->GetClassID());
    EXPECT_STREQ("Kinematic Chain", RCKKinematicChain::GetClassName());
    EXPECT_TRUE(CKIsChildClassOf(Chain, CKCID_OBJECT));
    EXPECT_EQ(nullptr, Chain->GetStartEffector()); EXPECT_EQ(nullptr, Chain->GetEndEffector());
    EXPECT_EQ(0u, Chain->m_ChainBodyCount); EXPECT_EQ(nullptr, Chain->m_ChainData);
    for (int mode : {0, 1, 2, 3}) {
        EXPECT_EQ(0, RCKKinematicChain::GetDependenciesCount(mode));
        EXPECT_EQ(nullptr, RCKKinematicChain::GetDependencies(0, mode));
    }
    CKClassDesc *desc = CKGetClassDesc(CKCID_KINEMATICCHAIN); ASSERT_NE(nullptr, desc);
    desc->ToBeNotify.Clear(); desc->Parameter = CKGUID(); desc->DefaultOptions = 0; RCKKinematicChain::Register();
    EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_BODYPART)); EXPECT_TRUE(desc->ToBeNotify.IsSet(CKCID_CHARACTER));
    EXPECT_EQ(CKPGUID_KINEMATICCHAIN, desc->Parameter); EXPECT_EQ(CK_GENERALOPTIONS_NODUPLICATENAMECHECK, desc->DefaultOptions);
}
TEST_F(KinematicChainLifecycleTest, MissingEndpointsDoNotInventAOneBodyChain) {
    EXPECT_EQ(0, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(0, Chain->GetChainLength());
    EXPECT_EQ(nullptr, Chain->GetEffector(0));
    Chain->SetEndEffector(Body(End));
    EXPECT_EQ(0, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(0, Chain->GetChainLength());
    Chain->SetEndEffector(nullptr); Chain->SetStartEffector(Body(Start));
    EXPECT_EQ(0, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(0, Chain->GetChainLength());
}
TEST_F(KinematicChainLifecycleTest, ConnectedQueriesPreserveEndpointOrderAndWorldSegmentLength) {
    Linked(); EXPECT_EQ(3, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(7, Chain->GetChainLength());
    EXPECT_EQ(Body(Start), Chain->GetEffector(0)); EXPECT_EQ(Body(Middle), Chain->GetEffector(1));
    EXPECT_EQ(Body(End), Chain->GetEffector(2)); EXPECT_EQ(nullptr, Chain->GetEffector(-1));
    EXPECT_EQ(nullptr, Chain->GetEffector(3));
    EXPECT_EQ(2, Chain->GetChainBodyCount(Body(Middle))); EXPECT_FLOAT_EQ(3, Chain->GetChainLength(Body(Middle)));
    EXPECT_EQ(Body(End), Chain->GetEndEffector());
    EXPECT_EQ(1, Chain->GetChainBodyCount(Body(Start))); EXPECT_FLOAT_EQ(0, Chain->GetChainLength(Body(Start)));
}
TEST_F(KinematicChainLifecycleTest, UnrelatedEndpointsDoNotExposeAnUnrelatedAncestorChain) {
    Linked(); Chain->SetStartEffector(Body(Other));
    EXPECT_EQ(0, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(0, Chain->GetChainLength());
    EXPECT_EQ(nullptr, Chain->GetEffector(0)); EXPECT_EQ(nullptr, Chain->GetEffector(1));
    Chain->SetStartEffector(Body(Start));
    EXPECT_EQ(0, Chain->GetChainBodyCount(Body(Other))); EXPECT_FLOAT_EQ(0, Chain->GetChainLength(Body(Other)));
    EXPECT_EQ(Body(End), Chain->GetEndEffector());
}
TEST_F(KinematicChainLifecycleTest, NonBodyPartIntermediateEntityIsNotAChainJoint) {
    Linked(); RCK3dEntity bridge(Context);
    ASSERT_TRUE(bridge.SetParent(Entity(Start), FALSE)); ASSERT_TRUE(End->SetParent(Entity(&bridge), FALSE));
    EXPECT_EQ(0, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(0, Chain->GetChainLength());
    EXPECT_EQ(nullptr, Chain->GetEffector(0)); End->SetParent(Entity(Middle), FALSE); bridge.SetParent(nullptr, TRUE);
}
TEST_F(KinematicChainLifecycleTest, EndpointSettersBorrowWithoutReparentingOrChangingTransforms) {
    const VxMatrix before = End->GetWorldMatrix(); Linked();
    EXPECT_EQ(Body(Start), Chain->GetStartEffector()); EXPECT_EQ(Body(End), Chain->GetEndEffector());
    EXPECT_EQ(Entity(Middle), End->GetParent());
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) EXPECT_FLOAT_EQ(before[r][c], End->GetWorldMatrix()[r][c]);
    EXPECT_EQ(CK_OK, Chain->SetStartEffector(nullptr)); EXPECT_EQ(CK_OK, Chain->SetEndEffector(nullptr));
    EXPECT_EQ(Entity(Middle), End->GetParent());
}
TEST_F(KinematicChainLifecycleTest, LiteralEndpointWireIsVersionIndependentAndDiscardsPlaceholder) {
    for (int version : {0, 4, 5, 6, 8}) {
        CKStateChunk chunk(CKCID_KINEMATICCHAIN, nullptr); Literal(chunk, version, Start->GetID(), End->GetID(), Other->GetID());
        ASSERT_EQ(CK_OK, Chain->Load(&chunk, nullptr));
        EXPECT_EQ(Body(Start), Chain->GetStartEffector()); EXPECT_EQ(Body(End), Chain->GetEndEffector());
        EXPECT_EQ(3, Chain->GetChainBodyCount()); EXPECT_FLOAT_EQ(7, Chain->GetChainLength());
    }
}
TEST_F(KinematicChainLifecycleTest, MissingOwnIdentifierPreservesEndpointsAndNullLoadRejects) {
    Linked(); EXPECT_EQ(CKERR_INVALIDPARAMETER, Chain->Load(nullptr, nullptr));
    CKStateChunk chunk(CKCID_KINEMATICCHAIN, nullptr); chunk.StartWrite(); chunk.WriteIdentifier(0x1357u); chunk.WriteInt(99); chunk.CloseChunk();
    ASSERT_EQ(CK_OK, Chain->Load(&chunk, nullptr)); EXPECT_EQ(Body(Start), Chain->GetStartEffector()); EXPECT_EQ(Body(End), Chain->GetEndEffector());
}
TEST_F(KinematicChainLifecycleTest, LiteralZeroReferencesClearOnlyEndpoints) {
    Linked(); CKStateChunk chunk(CKCID_KINEMATICCHAIN, nullptr); Literal(chunk, 6, 0, 0);
    ASSERT_EQ(CK_OK, Chain->Load(&chunk, nullptr)); EXPECT_EQ(nullptr, Chain->GetStartEffector()); EXPECT_EQ(nullptr, Chain->GetEndEffector());
    EXPECT_EQ(Entity(Middle), End->GetParent()); EXPECT_EQ(End, Context->GetObject(End->GetID()));
}
TEST_F(KinematicChainLifecycleTest, SaveIncludesExactThreeObjectWireForAnyLowByteFlag) {
    Linked(); for (CKDWORD flags : {1u, 0x80u, 0xFFu, 0x101u}) {
        CKStateChunk *chunk = Chain->Save(nullptr, flags); ASSERT_NE(nullptr, chunk); chunk->StartRead();
        ASSERT_TRUE(chunk->SeekIdentifier(0xFFu)); EXPECT_EQ(0u, chunk->ReadObjectID());
        EXPECT_EQ(Start->GetID(), chunk->ReadObjectID()); EXPECT_EQ(End->GetID(), chunk->ReadObjectID());
        EXPECT_EQ(CKCID_KINEMATICCHAIN, chunk->GetChunkClassID()); DeleteCKStateChunk(chunk);
    }
}
TEST_F(KinematicChainLifecycleTest, SaveOmitsOwnIdentifierForZeroLowByteAndFileForcesIt) {
    Linked(); for (CKDWORD flags : {0u, 0x100u, 0x10000u}) {
        CKStateChunk *chunk = Chain->Save(nullptr, flags); ASSERT_NE(nullptr, chunk); chunk->StartRead();
        EXPECT_FALSE(chunk->SeekIdentifier(0xFFu)); DeleteCKStateChunk(chunk);
    }
    CKFile file(Context); CKStateChunk *chunk = Chain->Save(&file, 0); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    EXPECT_TRUE(chunk->SeekIdentifier(0xFFu)); DeleteCKStateChunk(chunk);
}
TEST_F(KinematicChainLifecycleTest, CopyBorrowsEndpointsWithoutCopyingWorkspaceAndRemapIsSeparate) {
    Linked(); RCKKinematicChain copy(Context); CKDependencies options; Options(options); ChainDependencyMap map(Context); map.StartDependencies(&options);
    map.Map(Start->GetID(), Other->GetID()); ASSERT_EQ(CK_OK, copy.Copy(*Chain, map));
    EXPECT_EQ(Body(Start), copy.GetStartEffector()); EXPECT_EQ(Body(End), copy.GetEndEffector());
    EXPECT_EQ(nullptr, copy.m_ChainData); EXPECT_EQ(0u, copy.m_ChainBodyCount);
    ASSERT_EQ(CK_OK, copy.RemapDependencies(map)); EXPECT_EQ(Body(Other), copy.GetStartEffector()); EXPECT_EQ(Body(End), copy.GetEndEffector());
    EXPECT_EQ(Body(Start), Chain->GetStartEffector()); map.StopDependencies();
}
TEST_F(KinematicChainLifecycleTest, RemapExplicitNullAndUnmappedIdentityUseContextMap) {
    Linked(); CKDependencies options; Options(options); ChainDependencyMap map(Context); map.StartDependencies(&options);
    map.Map(Start->GetID(), 0); ASSERT_EQ(CK_OK, Chain->RemapDependencies(map));
    EXPECT_EQ(nullptr, Chain->GetStartEffector()); EXPECT_EQ(Body(End), Chain->GetEndEffector()); map.StopDependencies();
}
TEST_F(KinematicChainLifecycleTest, ActualContextCopyKeepsBorrowedEndpointsAndOwnLifetime) {
    Linked(); CKDependencies options; Options(options); CKObject *copyObject = Context->CopyObject(Chain, &options); ASSERT_NE(nullptr, copyObject);
    RCKKinematicChain *copy = static_cast<RCKKinematicChain *>(copyObject);
    EXPECT_NE(Chain, copy); EXPECT_EQ(Body(Start), copy->GetStartEffector()); EXPECT_EQ(Body(End), copy->GetEndEffector());
    EXPECT_EQ(3, copy->GetChainBodyCount()); EXPECT_FLOAT_EQ(7, copy->GetChainLength());
}
TEST_F(KinematicChainLifecycleTest, DirectFactoryAndVirtualDestructorPreserveBorrowedBodyLifetime) {
    CKKinematicChain *api = RCKKinematicChain::CreateInstance(Context); ASSERT_NE(nullptr, api);
    EXPECT_EQ(CKCID_KINEMATICCHAIN, api->GetClassID()); EXPECT_EQ(nullptr, api->GetName());
    api->SetStartEffector(Body(Start)); api->SetEndEffector(Body(End)); const CK_ID id = api->GetID();
    delete api; EXPECT_EQ(nullptr, Context->GetObject(id)); EXPECT_EQ(Start, Context->GetObject(Start->GetID()));
    EXPECT_EQ(End, Context->GetObject(End->GetID()));
}
TEST_F(KinematicChainLifecycleTest, ObjectUseAndPreDeletionFollowEndpointClassAndDeleteFlags) {
    Linked(); EXPECT_TRUE(Chain->IsObjectUsed(Start, CKCID_BODYPART)); EXPECT_TRUE(Chain->IsObjectUsed(End, CKCID_BODYPART));
    EXPECT_FALSE(Chain->IsObjectUsed(Middle, CKCID_BODYPART)); EXPECT_FALSE(Chain->IsObjectUsed(Start, CKCID_3DENTITY));
    End->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); Chain->CheckPreDeletion();
    EXPECT_EQ(nullptr, Chain->GetEndEffector()); EXPECT_EQ(Body(Start), Chain->GetStartEffector());
    Start->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); Chain->CheckPreDeletion(); EXPECT_EQ(nullptr, Chain->GetStartEffector());
    End->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED); Start->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}
TEST_F(KinematicChainLifecycleTest, WorkspaceMemoryUsesPortableStrideAndDestructorDoesNotOwnBodies) {
    Linked(); const int base = Chain->GetMemoryOccupation();
    Chain->m_ChainData = new CKIKChainBodyData[3]; Chain->m_ChainBodyCount = 3;
    EXPECT_EQ(base + 3 * static_cast<int>(sizeof(CKIKChainBodyData)), Chain->GetMemoryOccupation());
    CK_ID startID = Start->GetID(), endID = End->GetID(); Context->DestroyObject(Chain); Chain = nullptr;
    EXPECT_EQ(Start, Context->GetObject(startID)); EXPECT_EQ(End, Context->GetObject(endID));
}
TEST_F(KinematicChainLifecycleTest, PortableLayoutMeasuresBothChainAndJointWorkspace) {
    RecordProperty("chain_size", static_cast<int>(sizeof(RCKKinematicChain))); RecordProperty("chain_alignment", static_cast<int>(alignof(RCKKinematicChain)));
    const void *fields[] = {&Chain->m_StartEffector, &Chain->m_EndEffector, &Chain->m_ChainBodyCount, &Chain->m_ChainData};
    const size_t expected[] = {sizeof(void *) == 8 ? 40u : 20u, sizeof(void *) == 8 ? 48u : 24u,
        sizeof(void *) == 8 ? 56u : 28u, sizeof(void *) == 8 ? 64u : 32u};
    const char *keys[] = {"chain_start_offset", "chain_end_offset", "chain_count_offset", "chain_data_offset"};
    for (int i = 0; i < 4; ++i) {
        const int offset = static_cast<int>(static_cast<const char *>(fields[i]) - reinterpret_cast<const char *>(Chain));
        EXPECT_EQ(expected[i], offset); RecordProperty(keys[i], offset);
    }
    CKIKChainBodyData data; const void *members[] = {&data.m_RotationJoint, &data.m_BodyPart, &data.m_LocalTransform, &data.m_IsLocked, &data.m_WasAtLimit};
    const size_t offsets[] = {0u, 40u, sizeof(void *) == 8 ? 48u : 44u, sizeof(void *) == 8 ? 112u : 108u, sizeof(void *) == 8 ? 116u : 112u};
    const char *workspaceKeys[] = {"workspace_joint_offset", "workspace_body_offset", "workspace_matrix_offset", "workspace_locked_offset", "workspace_limit_offset"};
    for (int i = 0; i < 5; ++i) {
        const int offset = static_cast<int>(static_cast<const char *>(members[i]) - reinterpret_cast<const char *>(&data));
        EXPECT_EQ(offsets[i], offset); RecordProperty(workspaceKeys[i], offset);
    }
    EXPECT_EQ(sizeof(void *) == 8 ? 72u : 36u, sizeof(RCKKinematicChain));
    EXPECT_EQ(sizeof(void *) == 8 ? 120u : 116u, sizeof(CKIKChainBodyData));
    RecordProperty("workspace_size", static_cast<int>(sizeof(CKIKChainBodyData))); RecordProperty("workspace_alignment", static_cast<int>(alignof(CKIKChainBodyData)));
}
