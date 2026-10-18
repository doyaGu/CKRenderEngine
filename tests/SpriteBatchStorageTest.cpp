#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <new>
#include "TextureTestObserver.h"
#include "RCKMaterial.h"
#include "RCKSprite3D.h"
#include "CKSceneGraph.h"

namespace {
class SpriteBatchBoundary : public TextureTestObserver {
public:
    CKSprite3DBatch *Batch = nullptr;
    int Draws = 0, VertexCount = 0, IndexCount = 0;
    CKDWORD Flags = 0, LiveVertexCount = 0, LiveIndexCount = 0;
    CKVertex Vertices[8] = {};
    CKWORD Indices[12] = {};
    int Strides[4] = {};

    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        ++Draws;
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_NE(nullptr, Batch);
        EXPECT_LE(data->VertexCount, 8u);
        EXPECT_LE(count, 12);
        VertexCount = data->VertexCount; IndexCount = count; Flags = data->Flags;
        LiveVertexCount = Batch->m_VertexCount; LiveIndexCount = Batch->m_IndexCount;
        Strides[0] = data->PositionStride; Strides[1] = data->ColorStride;
        Strides[2] = data->SpecularColorStride; Strides[3] = data->TexCoordStride;
        for (int i = 0; i < VertexCount && i < 8; ++i) {
            CKVertex &vertex = Vertices[i];
            memcpy(&vertex.V, static_cast<const char *>(data->PositionPtr) + i * data->PositionStride, 16);
            memcpy(&vertex.Diffuse, static_cast<const char *>(data->ColorPtr) + i * data->ColorStride, 4);
            memcpy(&vertex.Specular, static_cast<const char *>(data->SpecularColorPtr) + i * data->SpecularColorStride, 4);
            memcpy(&vertex.tu, static_cast<const char *>(data->TexCoordPtr) + i * data->TexCoordStride, 8);
        }
        for (int i = 0; i < count && i < 12; ++i) Indices[i] = indices[i];
        return TRUE;
    }
};

class SpriteBatchStorageTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    SpriteBatchBoundary State;
    CKRasterizerDriver Driver;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        ASSERT_GT(Manager->m_DriverCount, 0);
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 8;
        Manager->m_Drivers[0].Caps3D.MaxNumberBlendStage = 8;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_ViewportData.ViewWidth = 320; Device->m_ViewportData.ViewHeight = 200;
        Device->m_Active = TRUE; State.Viewport = Device->m_ViewportData;
        Device->BeginFrameErrorTracking();
    }

    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }

    RCKSprite3D *Sprite(RCKMaterial &material, float x, float y, float z) {
        RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
        if (sprite) {
            sprite->SetMaterial(&material); sprite->SetMode(VXSPRITE3D_ORIENTABLE);
            sprite->SetPosition3f(x, y, z); sprite->GetWorldMatrix();
            sprite->m_SceneGraphNode->SetAsInsideFrustum();
        }
        return sprite;
    }

    static CKVertex Payload() {
        const CKDWORD words[8] = {0x3f800000u, 0xc0000000u, 0x3f000000u, 0x3f800000u,
                                 0x80402010u, 0xff012345u, 0x3e800000u, 0x3f400000u};
        CKVertex result;
        memcpy(&result, words, sizeof(words));
        return result;
    }

    static void SameVertex(const CKVertex &expected, const CKVertex &actual) {
        EXPECT_EQ(expected.V.x, actual.V.x); EXPECT_EQ(expected.V.y, actual.V.y);
        EXPECT_EQ(expected.V.z, actual.V.z); EXPECT_EQ(expected.V.w, actual.V.w);
        EXPECT_EQ(expected.Diffuse, actual.Diffuse); EXPECT_EQ(expected.Specular, actual.Specular);
        EXPECT_EQ(expected.tu, actual.tu); EXPECT_EQ(expected.tv, actual.tv);
    }

    static void Empty(const CKSprite3DBatch &batch) {
        EXPECT_EQ(0, batch.m_Indices.Size()); EXPECT_EQ(0, batch.m_Vertices.Size());
        EXPECT_EQ(0u, batch.m_VertexCount); EXPECT_EQ(0u, batch.m_IndexCount);
        EXPECT_EQ(0u, batch.m_Flags);
    }
};
}

TEST_F(SpriteBatchStorageTest, ConstructionInitializesBothArraysCountersAndClippingOnNonzeroStorage) {
    alignas(CKSprite3DBatch) unsigned char memory[sizeof(CKSprite3DBatch)];
    memset(memory, 0xa5, sizeof(memory));
    CKSprite3DBatch *batch = new (memory) CKSprite3DBatch;
    Empty(*batch);
    EXPECT_EQ(nullptr, batch->m_Indices.Begin()); EXPECT_EQ(nullptr, batch->m_Indices.End());
    EXPECT_EQ(nullptr, batch->m_Vertices.Begin()); EXPECT_EQ(nullptr, batch->m_Vertices.End());
    batch->~CKSprite3DBatch();
}

TEST_F(SpriteBatchStorageTest, PortableLayoutKeepsFixedVertexWordsAndNativeWidthArrayPointers) {
    EXPECT_EQ(32u, sizeof(CKVertex)); EXPECT_EQ(4u, alignof(CKVertex));
    EXPECT_EQ(0u, offsetof(CKVertex, V)); EXPECT_EQ(16u, offsetof(CKVertex, Diffuse));
    EXPECT_EQ(20u, offsetof(CKVertex, Specular)); EXPECT_EQ(24u, offsetof(CKVertex, tu));
    EXPECT_EQ(28u, offsetof(CKVertex, tv));
    const bool wide = sizeof(void *) == 8;
    EXPECT_EQ(wide ? 64u : 36u, sizeof(CKSprite3DBatch));
    EXPECT_EQ(wide ? 8u : 4u, alignof(CKSprite3DBatch));
    EXPECT_EQ(0u, offsetof(CKSprite3DBatch, m_Indices));
    EXPECT_EQ(wide ? 24u : 12u, offsetof(CKSprite3DBatch, m_Vertices));
    EXPECT_EQ(wide ? 48u : 24u, offsetof(CKSprite3DBatch, m_VertexCount));
    EXPECT_EQ(wide ? 52u : 28u, offsetof(CKSprite3DBatch, m_IndexCount));
    EXPECT_EQ(wide ? 56u : 32u, offsetof(CKSprite3DBatch, m_Flags));
}

TEST_F(SpriteBatchStorageTest, IndependentPackedWordsDecodePositionColorsAndUVWithoutOverlap) {
    CKVertex vertex = Payload();
    EXPECT_EQ(1, vertex.V.x); EXPECT_EQ(-2, vertex.V.y); EXPECT_EQ(.5f, vertex.V.z);
    EXPECT_EQ(1, vertex.V.w); EXPECT_EQ(0x80402010u, vertex.Diffuse);
    EXPECT_EQ(0xff012345u, vertex.Specular); EXPECT_EQ(.25f, vertex.tu); EXPECT_EQ(.75f, vertex.tv);
    CKVertex copied = vertex;
    vertex.Diffuse = 0x12345678; vertex.tu = -1;
    SameVertex(Payload(), copied);
    EXPECT_EQ(0xff012345u, vertex.Specular); EXPECT_EQ(.75f, vertex.tv); EXPECT_EQ(1, vertex.V.w);
}

TEST_F(SpriteBatchStorageTest, ArrayGrowthPreservesPayloadAndDoesNotInventDrawMetadata) {
    CKSprite3DBatch batch;
    batch.m_Vertices.PushBack(Payload());
    batch.m_Indices.PushBack(65535); batch.m_Indices.PushBack(1); batch.m_Indices.PushBack(0);
    batch.m_VertexCount = 4; batch.m_IndexCount = 6; batch.m_Flags = 0x41;
    batch.m_Vertices.Resize(400); batch.m_Indices.Resize(600);
    SameVertex(Payload(), batch.m_Vertices[0]);
    EXPECT_EQ(65535, batch.m_Indices[0]); EXPECT_EQ(1, batch.m_Indices[1]); EXPECT_EQ(0, batch.m_Indices[2]);
    EXPECT_EQ(4u, batch.m_VertexCount); EXPECT_EQ(6u, batch.m_IndexCount); EXPECT_EQ(0x41u, batch.m_Flags);
}

TEST_F(SpriteBatchStorageTest, ImplicitCopiesOwnTheirArrayStorageAfterTheSourceIsDestroyed) {
    CKSprite3DBatch assigned;
    {
        CKSprite3DBatch source;
        source.m_Vertices.PushBack(Payload()); source.m_Indices.PushBack(3);
        source.m_VertexCount = 4; source.m_IndexCount = 6; source.m_Flags = 1;
        CKSprite3DBatch copied(source); assigned = copied;
        EXPECT_NE(source.m_Indices.Begin(), copied.m_Indices.Begin());
        EXPECT_NE(source.m_Vertices.Begin(), copied.m_Vertices.Begin());
        EXPECT_NE(copied.m_Indices.Begin(), assigned.m_Indices.Begin());
        EXPECT_NE(copied.m_Vertices.Begin(), assigned.m_Vertices.Begin());
        source.m_Vertices[0].Diffuse = 0; source.m_Indices[0] = 99;
        SameVertex(Payload(), copied.m_Vertices[0]); EXPECT_EQ(3, copied.m_Indices[0]);
    }
    ASSERT_EQ(1, assigned.m_Vertices.Size()); ASSERT_EQ(1, assigned.m_Indices.Size());
    SameVertex(Payload(), assigned.m_Vertices[0]); EXPECT_EQ(3, assigned.m_Indices[0]);
    EXPECT_EQ(4u, assigned.m_VertexCount); EXPECT_EQ(6u, assigned.m_IndexCount); EXPECT_EQ(1u, assigned.m_Flags);
}

TEST_F(SpriteBatchStorageTest, RealProducerAppendsWithoutChangingPriorPayloadOrDeferredDrawMetadata) {
    RCKMaterial material(Context);
    RCKSprite3D *sprite = Sprite(material, 10, 20, 30); ASSERT_NE(nullptr, sprite);
    CKSprite3DBatch batch;
    for (int i = 0; i < 4; ++i) batch.m_Vertices.PushBack(Payload());
    const CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) batch.m_Indices.PushBack(indices[i]);
    batch.m_VertexCount = 4; batch.m_IndexCount = 6; batch.m_Flags = 0x40;
    sprite->FillBatch(&batch);
    ASSERT_EQ(8, batch.m_Vertices.Size()); ASSERT_EQ(6, batch.m_Indices.Size());
    for (int i = 0; i < 4; ++i) SameVertex(Payload(), batch.m_Vertices[i]);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(indices[i], batch.m_Indices[i]);
    EXPECT_EQ(9, batch.m_Vertices[4].V.x); EXPECT_EQ(19, batch.m_Vertices[4].V.y);
    EXPECT_EQ(30, batch.m_Vertices[4].V.z); EXPECT_EQ(1, batch.m_Vertices[4].V.w);
    EXPECT_EQ(4u, batch.m_VertexCount); EXPECT_EQ(6u, batch.m_IndexCount); EXPECT_EQ(0x40u, batch.m_Flags);
}

TEST_F(SpriteBatchStorageTest, RealSubmissionUsesIndependentQuadIndicesColorsUVAndEveryVertexWord) {
    RCKMaterial material(Context); material.SetDiffuse(VxColor(0.0f, 1.0f, 0.0f, 1.0f)); material.SetSpecular(VxColor(0.0f, 0.0f, 1.0f, 0.0f));
    RCKSprite3D *first = Sprite(material, 0, 0, 5), *second = Sprite(material, 10, 2, 6);
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    first->SetUVMapping(VxRect(.25f, .125f, .75f, .875f));
    second->m_SceneGraphNode->SetAsPotentiallyVisible();
    second->m_SceneGraphNode->SetAsOutsideFrustum();
    Device->AddSprite3DBatch(first); Device->AddSprite3DBatch(second);
    State.Batch = material.GetSprite3DBatch(); ASSERT_NE(nullptr, State.Batch);
    EXPECT_EQ(0u, State.Batch->m_VertexCount); EXPECT_EQ(0u, State.Batch->m_IndexCount);
    Device->CallSprite3DBatches();
    ASSERT_EQ(1, State.Draws); EXPECT_EQ(8, State.VertexCount); EXPECT_EQ(12, State.IndexCount);
    EXPECT_EQ(8u, State.LiveVertexCount); EXPECT_EQ(12u, State.LiveIndexCount);
    const CKWORD indices[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    for (int i = 0; i < 12; ++i) EXPECT_EQ(indices[i], State.Indices[i]);
    const float x[8] = {-1, -1, 1, 1, 9, 9, 11, 11};
    const float y[8] = {-1, 1, 1, -1, 1, 3, 3, 1};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(x[i], State.Vertices[i].V.x); EXPECT_EQ(y[i], State.Vertices[i].V.y);
        EXPECT_EQ(i < 4 ? 5 : 6, State.Vertices[i].V.z); EXPECT_EQ(1, State.Vertices[i].V.w);
        EXPECT_EQ(0xff00ff00u, State.Vertices[i].Diffuse); EXPECT_EQ(0xff0000ffu, State.Vertices[i].Specular);
    }
    const float u[4] = {.25f, .25f, .75f, .75f}, v[4] = {.875f, .125f, .125f, .875f};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(u[i], State.Vertices[i].tu); EXPECT_EQ(v[i], State.Vertices[i].tv);
        EXPECT_EQ(32, State.Strides[i]);
    }
    EXPECT_NE(0u, State.Flags & CKRST_DP_DOCLIP);
    Empty(*State.Batch); EXPECT_EQ(0, Device->m_Sprite3DBatches.Size());
}

TEST_F(SpriteBatchStorageTest, ConsumedBatchReusesTheOwnerWithFreshGeometryColorsIndicesAndClipState) {
    RCKMaterial material(Context);
    RCKSprite3D *sprite = Sprite(material, 0, 0, 5); ASSERT_NE(nullptr, sprite);
    sprite->m_SceneGraphNode->SetAsPotentiallyVisible();
    sprite->m_SceneGraphNode->SetAsOutsideFrustum();
    Device->AddSprite3DBatch(sprite); Device->AddSprite3DBatch(sprite);
    State.Batch = material.GetSprite3DBatch(); ASSERT_NE(nullptr, State.Batch);
    Device->CallSprite3DBatches(); ASSERT_EQ(1, State.Draws); EXPECT_EQ(12, State.IndexCount);
    EXPECT_NE(0u, State.Flags & CKRST_DP_DOCLIP); Empty(*State.Batch);
    material.SetDiffuse(VxColor(1.0f, 0.0f, 0.0f, 1.0f)); material.SetSpecular(VxColor(0.0f, 1.0f, 0.0f, 0.0f));
    sprite->SetPosition3f(42, 4, 7); sprite->GetWorldMatrix();
    sprite->m_SceneGraphNode->SetAsInsideFrustum(); Device->AddSprite3DBatch(sprite);
    EXPECT_EQ(State.Batch, material.GetSprite3DBatch()); EXPECT_EQ(0u, State.Batch->m_Flags);
    Device->CallSprite3DBatches(); ASSERT_EQ(2, State.Draws);
    EXPECT_EQ(4, State.VertexCount); EXPECT_EQ(6, State.IndexCount);
    EXPECT_EQ(4u, State.LiveVertexCount); EXPECT_EQ(6u, State.LiveIndexCount);
    const CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(indices[i], State.Indices[i]);
    EXPECT_EQ(41, State.Vertices[0].V.x); EXPECT_EQ(3, State.Vertices[0].V.y); EXPECT_EQ(7, State.Vertices[0].V.z);
    EXPECT_EQ(0xffff0000u, State.Vertices[0].Diffuse); EXPECT_EQ(0xff00ff00u, State.Vertices[0].Specular);
    EXPECT_EQ(0u, State.Flags & CKRST_DP_DOCLIP); Empty(*State.Batch);
}
