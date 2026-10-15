#include "TextureTestObserver.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKObjectAnimation.h"
#include "RCKKeyframeData.h"

namespace {
class GeometryCompletionObserver : public TextureTestObserver {
public:
    struct Draw {
        VXPRIMITIVETYPE Type = VX_TRIANGLELIST;
        int Count = 0, VertexCount = 0;
        float MaterialTag = 0.0f;
        CKWORD Indices[12] = {};
        VxVector Positions[12];
        Vx2DVector UV[12];
    };
    Draw Draws[24]; int Count = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data); EXPECT_LE(count, 12); EXPECT_LT(Count, 24);
        if (!indices || !data || count > 12 || Count >= 24) return FALSE;
        Draw &draw = Draws[Count++];
        draw.Type = type; draw.Count = count; draw.VertexCount = data->VertexCount;
        draw.MaterialTag = Material.Diffuse.r;
        EXPECT_NE(nullptr, data->PositionPtr);
        if (!data->PositionPtr) return FALSE;
        for (int i = 0; i < count; ++i) {
            draw.Indices[i] = indices[i]; EXPECT_LT(indices[i], data->VertexCount);
            if (indices[i] >= data->VertexCount) return FALSE;
            memcpy(&draw.Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride, sizeof(VxVector));
            if (data->TexCoordPtr)
                memcpy(&draw.UV[i], static_cast<CKBYTE *>(data->TexCoordPtr) + indices[i] * data->TexCoordStride, sizeof(Vx2DVector));
        }
        return TRUE;
    }
};
class CompletionMesh : public RCKMesh {
public:
    explicit CompletionMesh(CKContext *context) : RCKMesh(context) {}
    int DeclaredPMCount() const { return m_ProgressiveMesh ? m_ProgressiveMesh->m_Reserved : -1; }
    int ParentCount() const { return m_ProgressiveMesh ? m_ProgressiveMesh->m_Data.Size() : 0; }
    int Parent(int index) const { return static_cast<int>(m_ProgressiveMesh->m_Data[index]); }
};
class RegisteredGeometryConsumers {
public:
    explicit RegisteredGeometryConsumers(CKContext *context) : Context(context) {
        Entity = reinterpret_cast<RCK3dEntity *>(context->CreateObject(CKCID_3DENTITY));
        Animation = static_cast<RCKObjectAnimation *>(context->CreateObject(CKCID_OBJECTANIMATION));
    }
    ~RegisteredGeometryConsumers() {
        if (Animation) {
            Animation->Set3dEntity(nullptr);
            Context->DestroyObject(Animation->GetID());
        }
        if (Entity) {
            // DestroyObject follows entity dependency policy. Mesh and bones
            // are borrowed fixture objects, so detach before deleting the owner.
            Entity->DestroySkin();
            Entity->SetCurrentMesh(nullptr);
            while (Entity->GetMeshCount()) Entity->RemoveMesh(Entity->GetMesh(0));
            Context->DestroyObject(Entity->GetID());
        }
    }
    CKContext *Context;
    RCK3dEntity *Entity = nullptr;
    RCKObjectAnimation *Animation = nullptr;
};
class MeshGeometryCompletionTest : public TextureTestWorld {
protected:
    GeometryCompletionObserver Target;
    void SetUp() override {
        TextureTestWorld::SetUp();
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_ANIMATION); CKBuildClassHierarchyTable();
        Device->DetachAll(); Device->m_RasterizerContext = &Target;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
        Manager->m_VertexCache.Value = 0;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 1;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 1;
        Device->m_DisplayWireframe = FALSE;
    }
    static VxVector Point(int source) { return VxVector(static_cast<float>(source), static_cast<float>(source * source), 1.0f); }
    static void Vec(const VxVector &actual, const VxVector &expected) {
        EXPECT_NEAR(expected.x, actual.x, .0001f); EXPECT_NEAR(expected.y, actual.y, .0001f); EXPECT_NEAR(expected.z, actual.z, .0001f);
    }
    static void Payload(RCKMesh &mesh, int count) {
        ASSERT_TRUE(mesh.SetVertexCount(count)); mesh.SetVertexWeightsCount(count);
        for (int i = 0; i < count; ++i) {
            VxVector position = Point(i), normal(0.0f, 0.0f, 1.0f);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexWeight(i, .125f + i * .0625f);
            mesh.SetVertexColor(i, 0xff102000u + i); mesh.SetVertexSpecularColor(i, 0x11223000u + i);
            mesh.SetVertexTextureCoordinates(i, .125f + i * .0625f, .75f, -1);
        }
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    }
    static int Source(RCKMesh &mesh, int vertex) {
        VxVector position; mesh.GetVertexPosition(vertex, &position);
        const int source = static_cast<int>(position.x);
        Vec(position, Point(source)); return source;
    }
    static void ExpectedPayload(RCKMesh &mesh, int vertex, int source, int channel = -1) {
        VxVector position; mesh.GetVertexPosition(vertex, &position); Vec(position, Point(source));
        EXPECT_FLOAT_EQ(.125f + source * .0625f, mesh.GetVertexWeight(vertex));
        EXPECT_EQ(0xff102000u + source, mesh.GetVertexColor(vertex));
        EXPECT_EQ(0x11223000u + source, mesh.GetVertexSpecularColor(vertex));
        float u = 0.0f, v = 0.0f; mesh.GetVertexTextureCoordinates(vertex, &u, &v, channel);
        EXPECT_FLOAT_EQ((channel < 0 ? .125f : .5f) + source * .0625f, u); EXPECT_FLOAT_EQ(.75f, v);
    }
    static CKSkin *Skin(RCK3dEntity &entity, RCKMesh &mesh, RCK3dEntity &a, RCK3dEntity &b) {
        entity.SetCurrentMesh(&mesh); CKSkin *skin = entity.CreateSkin(); EXPECT_NE(nullptr, skin);
        if (!skin) return nullptr;
        VxMatrix first = VxMatrix::Identity(), second = VxMatrix::Identity();
        first[3].x = 4.0f; second[3].y = 8.0f; a.SetWorldMatrix(first); b.SetWorldMatrix(second);
        skin->SetBoneCount(2); skin->SetVertexCount(mesh.GetVertexCount()); skin->SetNormalCount(mesh.GetVertexCount());
        skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&a));
        skin->GetBoneData(1)->SetBone(reinterpret_cast<CK3dEntity *>(&b));
        for (int i = 0; i < mesh.GetVertexCount(); ++i) {
            CKSkinVertexData *vertex = skin->GetVertexData(i); VxVector point = Point(i);
            vertex->SetInitialPos(point); vertex->SetBoneCount(2);
            vertex->SetBone(0, 0); vertex->SetBone(1, 1);
            vertex->SetWeight(0, .1f * (i + 1)); vertex->SetWeight(1, 1.0f - .1f * (i + 1));
            skin->SetNormal(i, VxVector(0.0f, 0.0f, 1.0f));
        }
        return skin;
    }
    static RCKMorphController *Morph(RCKObjectAnimation &animation, RCK3dEntity &entity, int count) {
        animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
        RCKMorphController *morph = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
        EXPECT_NE(nullptr, morph); if (!morph) return nullptr;
        morph->SetMorphVertexCount(count);
        for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
            const int index = morph->AddKey(keyIndex * 10.0f, TRUE); EXPECT_EQ(keyIndex, index);
            if (index < 0) return nullptr;
            CKMorphKey *key = static_cast<CKMorphKey *>(morph->GetKey(index));
            for (int vertex = 0; vertex < count; ++vertex) {
                key->PosArray[vertex] = Point(vertex) + VxVector(keyIndex * 10.0f, keyIndex * 20.0f, keyIndex * 30.0f);
                key->NormArray[vertex].xa = static_cast<CKWORD>(11 + vertex);
                key->NormArray[vertex].ya = static_cast<CKWORD>(21 + vertex);
            }
        }
        return morph;
    }
    void Consumers(CKSkin &skin, RCKMorphController &morph, const int *sources, int count) {
        ASSERT_EQ(count, skin.GetVertexCount()); ASSERT_EQ(count, skin.GetNormalCount());
        ASSERT_EQ(count, morph.GetMorphVertexCount()); ASSERT_EQ(2, morph.GetKeyCount());
        VxVector points[12], normals[12]; VxCompressedVector compressed[12];
        ASSERT_LE(count, 12); skin.ConstructBoneTransfoMatrices(Context);
        ASSERT_TRUE(skin.CalcPointsEx(count, reinterpret_cast<CKBYTE *>(points), sizeof(VxVector), reinterpret_cast<CKBYTE *>(normals), sizeof(VxVector)));
        for (int i = 0; i < count; ++i) {
            const float weight = .1f * (sources[i] + 1);
            Vec(points[i], Point(sources[i]) + VxVector(4.0f * weight, 8.0f * (1.0f - weight), 0.0f));
            Vec(normals[i], VxVector(0.0f, 0.0f, 1.0f));
        }
        for (int key = 0; key < 2; ++key) {
            ASSERT_TRUE(morph.Evaluate(key * 10.0f, count, points, sizeof(VxVector), compressed));
            for (int i = 0; i < count; ++i) {
                Vec(points[i], Point(sources[i]) + VxVector(key * 10.0f, key * 20.0f, key * 30.0f));
                EXPECT_EQ(11 + sources[i], compressed[i].xa); EXPECT_EQ(21 + sources[i], compressed[i].ya);
            }
        }
    }
};

TEST_F(MeshGeometryCompletionTest, FreshCreatePMBuildsFaceCacheWithoutDiscardingAuthoredTriangleOrInventingUVs) {
    CompletionMesh mesh(Context); RCKMaterial material(Context);
    ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
    for (int i = 0; i < 3; ++i) { VxVector p = Point(i); mesh.SetVertexPosition(i, &p); }
    mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceMaterial(0, &material);
    mesh.SetFlags(mesh.GetFlags() & ~VXMESH_STRIPIFY);
    ASSERT_EQ(CK_OK, mesh.CreatePM()); ASSERT_EQ(3, mesh.GetVertexCount()); ASSERT_EQ(1, mesh.GetFaceCount());
    mesh.BuildRenderMesh(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, Target.Count);
    EXPECT_EQ(3, Target.Draws[0].Count); EXPECT_EQ(3, Target.Draws[0].VertexCount);
    for (int i = 0; i < 3; ++i) { EXPECT_FLOAT_EQ(0.0f, Target.Draws[0].UV[i].x); EXPECT_FLOAT_EQ(0.0f, Target.Draws[0].UV[i].y); }
}

// Old weighted RemapVertices aliases owned influence arrays and can crash at
// destruction. These two WeightedSkin cases are current-only, not safe baselines.
TEST_F(MeshGeometryCompletionTest, CreatePMPermutationPreservesWeightedSkinMorphWeightsUVAndLineConsumers) {
    CompletionMesh mesh(Context); RCKMaterial base(Context), layer(Context);
    Payload(mesh, 4); mesh.SetFaceCount(2); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3);
    mesh.SetFaceMaterial(0, &base); mesh.SetFaceMaterial(1, &base); mesh.SetLineCount(1); mesh.SetLine(0, 0, 3);
    ASSERT_EQ(0, mesh.AddChannel(&layer, FALSE)); mesh.ActivateChannel(0, FALSE);
    for (int i = 0; i < 4; ++i) mesh.SetVertexTextureCoordinates(i, .5f + i * .0625f, .75f, 0);
    RCK3dEntity a(Context), b(Context);
    RegisteredGeometryConsumers consumers(Context); ASSERT_NE(nullptr, consumers.Entity); ASSERT_NE(nullptr, consumers.Animation);
    CKSkin *skin = Skin(*consumers.Entity, mesh, a, b); ASSERT_NE(nullptr, skin);
    RCKMorphController *morph = Morph(*consumers.Animation, *consumers.Entity, 4); ASSERT_NE(nullptr, morph);
    mesh.BuildFaceNormals(); ASSERT_EQ(CK_OK, mesh.CreatePM()); ASSERT_EQ(4, mesh.GetVertexCount());
    int sources[4]; CKBOOL reordered = FALSE;
    for (int i = 0; i < 4; ++i) { sources[i] = Source(mesh, i); reordered |= sources[i] != i; ExpectedPayload(mesh, i, sources[i]); ExpectedPayload(mesh, i, sources[i], 0); }
    EXPECT_TRUE(reordered); Consumers(*skin, *morph, sources, 4);
    int first = 0, last = 0; mesh.GetLine(0, &first, &last); VxVector p;
    mesh.GetVertexPosition(first, &p); Vec(p, Point(0)); mesh.GetVertexPosition(last, &p); Vec(p, Point(3));
    mesh.BuildRenderMesh(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(2, Target.Count);
    EXPECT_EQ(VX_LINELIST, Target.Draws[1].Type); Vec(Target.Draws[1].Positions[0], Point(0)); Vec(Target.Draws[1].Positions[1], Point(3));
}

TEST_F(MeshGeometryCompletionTest, ConsolidatePreservesWeightedSkinAndBothMorphKeysAcrossUnusedVertexHoles) {
    CompletionMesh mesh(Context); Payload(mesh, 6); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 2, 4, 5); mesh.BuildFaceNormals();
    RCK3dEntity a(Context), b(Context);
    RegisteredGeometryConsumers consumers(Context); ASSERT_NE(nullptr, consumers.Entity); ASSERT_NE(nullptr, consumers.Animation);
    CKSkin *skin = Skin(*consumers.Entity, mesh, a, b); ASSERT_NE(nullptr, skin);
    RCKMorphController *morph = Morph(*consumers.Animation, *consumers.Entity, 6); ASSERT_NE(nullptr, morph);
    CKKey *firstKey = morph->GetKey(0), *lastKey = morph->GetKey(1);
    mesh.Consolidate(); ASSERT_EQ(3, mesh.GetVertexCount()); const int sources[] = {2, 4, 5};
    Consumers(*skin, *morph, sources, 3);
    EXPECT_EQ(firstKey, morph->GetKey(0)); EXPECT_EQ(lastKey, morph->GetKey(1));
    for (int i = 0; i < 3; ++i) ExpectedPayload(mesh, i, sources[i]);
    mesh.Consolidate(); Consumers(*skin, *morph, sources, 3);
}

TEST_F(MeshGeometryCompletionTest, DissociationDuplicatesSkinAndMorphPayloadAndKeepsIndependentLineEndpoint) {
    CompletionMesh mesh(Context); RCKMaterial layer(Context); Payload(mesh, 5);
    mesh.SetFaceCount(2); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3);
    mesh.SetLineCount(1); mesh.SetLine(0, 1, 4); ASSERT_EQ(0, mesh.AddChannel(&layer, FALSE));
    for (int i = 0; i < 5; ++i) mesh.SetVertexTextureCoordinates(i, .5f + i * .0625f, .75f, 0);
    RCK3dEntity a(Context), b(Context);
    RegisteredGeometryConsumers consumers(Context); ASSERT_NE(nullptr, consumers.Entity); ASSERT_NE(nullptr, consumers.Animation);
    CKSkin *skin = Skin(*consumers.Entity, mesh, a, b); ASSERT_NE(nullptr, skin);
    RCKMorphController *morph = Morph(*consumers.Animation, *consumers.Entity, 5); ASSERT_NE(nullptr, morph);
    mesh.DissociateAllFaces(); ASSERT_EQ(7, mesh.GetVertexCount()); ASSERT_EQ(1, mesh.GetLineCount());
    const int sources[] = {0, 1, 2, 0, 2, 3, 4}; Consumers(*skin, *morph, sources, 7);
    for (int i = 0; i < 7; ++i) ExpectedPayload(mesh, i, sources[i], 0);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(i, mesh.GetFacesIndices()[i]);
    int first = 0, last = 0; mesh.GetLine(0, &first, &last); EXPECT_EQ(1, first); EXPECT_EQ(6, last);
    skin->GetVertexData(3)->SetWeight(0, .9f); EXPECT_FLOAT_EQ(.1f, skin->GetVertexData(0)->GetWeight(0));
}

TEST_F(MeshGeometryCompletionTest, DissociationLeavesPureLineMeshAndWordMaximumEndpointIntact) {
    CompletionMesh mesh(Context); ASSERT_TRUE(mesh.SetVertexCount(65536)); ASSERT_TRUE(mesh.SetLineCount(1));
    VxVector a(1.0f, 2.0f, 3.0f), b(4.0f, 5.0f, 6.0f);
    mesh.SetVertexPosition(0, &a); mesh.SetVertexPosition(65535, &b); mesh.SetLine(0, 0, 65535);
    mesh.DissociateAllFaces(); ASSERT_EQ(65536, mesh.GetVertexCount()); ASSERT_EQ(1, mesh.GetLineCount());
    ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); ASSERT_EQ(1, Target.Count);
    EXPECT_EQ(VX_LINELIST, Target.Draws[0].Type); EXPECT_EQ(65535, Target.Draws[0].Indices[1]);
    Vec(Target.Draws[0].Positions[0], a); Vec(Target.Draws[0].Positions[1], b);
}

TEST_F(MeshGeometryCompletionTest, DissociationUsesLastWordSlotForLineOnlyEndpoint) {
    CompletionMesh mesh(Context); Payload(mesh, 4); ASSERT_TRUE(mesh.SetFaceCount(21845));
    for (int i = 0; i < 21845; ++i) mesh.SetFaceVertexIndex(i, 0, 1, 2);
    mesh.SetLineCount(1); mesh.SetLine(0, 2, 3); mesh.DissociateAllFaces();
    ASSERT_EQ(65536, mesh.GetVertexCount()); EXPECT_EQ(65534, mesh.GetFacesIndices()[65534]);
    int a = 0, b = 0; mesh.GetLine(0, &a, &b); EXPECT_EQ(2, a); EXPECT_EQ(65535, b);
    ExpectedPayload(mesh, 65535, 3);
}

TEST_F(MeshGeometryCompletionTest, DissociationOverWordBudgetLeavesGeometryAndPrivateUVsUnchanged) {
    CompletionMesh mesh(Context); RCKMaterial layer(Context); Payload(mesh, 5);
    ASSERT_EQ(0, mesh.AddChannel(&layer, FALSE)); mesh.SetVertexTextureCoordinates(4, .875f, .75f, 0);
    ASSERT_TRUE(mesh.SetFaceCount(21845));
    for (int i = 0; i < 21845; ++i) mesh.SetFaceVertexIndex(i, 0, 1, 2);
    mesh.SetLineCount(1); mesh.SetLine(0, 3, 4); mesh.DissociateAllFaces();
    EXPECT_EQ(5, mesh.GetVertexCount()); EXPECT_EQ(21845, mesh.GetFaceCount()); EXPECT_EQ(1, mesh.GetLineCount());
    EXPECT_EQ(0, mesh.GetFacesIndices()[65532]); EXPECT_EQ(2, mesh.GetFacesIndices()[65534]);
    int a = 0, b = 0; mesh.GetLine(0, &a, &b); EXPECT_EQ(3, a); EXPECT_EQ(4, b);
    float u = 0.0f, v = 0.0f; mesh.GetVertexTextureCoordinates(4, &u, &v, 0); EXPECT_FLOAT_EQ(.875f, u);
    ExpectedPayload(mesh, 4, 4);
}


TEST_F(MeshGeometryCompletionTest, CreateDestroyRecreateKeepsSingleProgressiveCallbackAndRebuildsCurrentLod) {
    CompletionMesh mesh(Context); RCKMaterial material(Context); Payload(mesh, 4);
    mesh.SetFaceCount(2); mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceVertexIndex(1, 0, 2, 3);
    mesh.SetFaceMaterial(0, &material); mesh.SetFaceMaterial(1, &material); mesh.BuildFaceNormals();
    for (int cycle = 0; cycle < 3; ++cycle) {
        ASSERT_EQ(CK_OK, mesh.CreatePM()); EXPECT_EQ(CKERR_INVALIDPARAMETER, mesh.CreatePM());
        ASSERT_EQ(4, mesh.ParentCount());
        for (int i = 1; i < 4; ++i) EXPECT_LT(mesh.Parent(i), i);
        const int before = Target.Count; ASSERT_EQ(CK_OK, mesh.Render(Device, nullptr));
        EXPECT_EQ(before + 1, Target.Count); EXPECT_EQ(6, Target.Draws[before].Count);
        mesh.SetVerticesRendered(1); mesh.BuildRenderMesh(); EXPECT_EQ(1, mesh.DeclaredPMCount());
        ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); EXPECT_EQ(before + 1, Target.Count);
        mesh.DestroyPM(); EXPECT_FALSE(mesh.IsPM());
    }
}


} // namespace
