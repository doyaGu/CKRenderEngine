#include "TextureTestObserver.h"
#include "RCKMesh.h"
#include "RCKSprite.h"
#include "RCKSpriteText.h"
#include "RCKVertexBuffer.h"

namespace {
// Owns test resource bytes and acknowledges the public resource calls. No
// production rasterizer or GPU backend runs in these manager contract tests.
class ManagerResourceObserver : public TextureTestObserver {
public:
    CKVertexBufferDesc Vertices[16];
    CKIndexBufferDesc Indices[16];
    CKBYTE VertexBytes[16][256] = {};
    CKWORD IndexWords[16][32] = {};
    CKBOOL VertexLive[16] = {}, IndexLive[16] = {};
    CKDWORD NextVertex = 1, NextIndex = 1;
    CKBOOL RefuseVertexDelete = FALSE, RefuseIndexDelete = FALSE;
    int VertexDeletes = 0, IndexDeletes = 0;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data,
                              CKDWORD *handle) override {
        EXPECT_LT(NextVertex, 16u);
        if (NextVertex >= 16) return FALSE;
        *handle = NextVertex++; Vertices[*handle] = *desc; VertexLive[*handle] = TRUE;
        EXPECT_LE(desc->m_CurrentVCount * desc->m_VertexSize, 256u);
        if (data && desc->m_CurrentVCount * desc->m_VertexSize <= 256)
            memcpy(VertexBytes[*handle], data, desc->m_CurrentVCount * desc->m_VertexSize);
        return TRUE;
    }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data,
                             CKDWORD *handle) override {
        EXPECT_LT(NextIndex, 16u);
        if (NextIndex >= 16) return FALSE;
        *handle = NextIndex++; Indices[*handle] = *desc; IndexLive[*handle] = TRUE;
        EXPECT_LE(desc->m_CurrentICount, 32u);
        if (data && desc->m_CurrentICount <= 32)
            memcpy(IndexWords[*handle], data, desc->m_CurrentICount * sizeof(CKWORD));
        return TRUE;
    }
    CKBOOL GetVertexBufferDesc(CKDWORD handle, CKVertexBufferDesc *desc) const override {
        if (!handle || handle >= 16 || !VertexLive[handle]) return FALSE;
        *desc = Vertices[handle]; return TRUE;
    }
    CKBOOL GetIndexBufferDesc(CKDWORD handle, CKIndexBufferDesc *desc) const override {
        if (!handle || handle >= 16 || !IndexLive[handle]) return FALSE;
        *desc = Indices[handle]; return TRUE;
    }
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count,
                           CKRST_LOCKFLAGS) override {
        if (!handle || handle >= 16 || !VertexLive[handle] ||
            (start + count) * Vertices[handle].m_VertexSize > 256) return nullptr;
        return VertexBytes[handle] + start * Vertices[handle].m_VertexSize;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override {
        return handle && handle < 16 && VertexLive[handle];
    }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        if (type == CKRST_OBJ_VERTEXBUFFER) {
            ++VertexDeletes;
            if (RefuseVertexDelete || !handle || handle >= 16 || !VertexLive[handle]) return FALSE;
            VertexLive[handle] = FALSE; return TRUE;
        }
        if (type == CKRST_OBJ_INDEXBUFFER) {
            ++IndexDeletes;
            if (RefuseIndexDelete || !handle || handle >= 16 || !IndexLive[handle]) return FALSE;
            IndexLive[handle] = FALSE; return TRUE;
        }
        return TextureTestObserver::DeleteObject(handle, type);
    }
};

class RenderManagerResourcesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *View[2] = {};
    ManagerResourceObserver Boundary[2];
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite, CKCID_2DENTITY);
        CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        Manager->m_TextureVideoFormat.Value = _32_ARGB8888;
        Manager->m_SpriteVideoFormat.Value = _32_ARGB8888;
        for (int i = 0; i < 2; ++i) {
            View[i] = new RCKRenderContext(Context);
            View[i]->m_RasterizerContext = &Boundary[i];
            View[i]->m_RasterizerDriver = &Driver; View[i]->m_DriverIndex = 0;
            ASSERT_TRUE(Manager->RegisterRasterizerContext(&Boundary[i], 0));
        }
    }
    void TearDown() override {
        for (int i = 0; i < 2; ++i) {
            Boundary[i].DeleteResult = TRUE;
            Boundary[i].RefuseVertexDelete = Boundary[i].RefuseIndexDelete = FALSE;
            if (View[i]) { View[i]->m_RasterizerContext = nullptr; View[i]->m_RasterizerDriver = nullptr; delete View[i]; }
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static RCKMeshBufferState MeshState(RCKMesh &mesh) {
        RCKMeshBufferState result = {}; mesh.GetRasterizerBufferState(result); return result;
    }
    static RCKVertexBufferObjectState BufferState(RCKVertexBuffer &buffer) {
        RCKVertexBufferObjectState result = {}; buffer.GetRasterizerObjectState(result); return result;
    }
    static void Fill(CKVertexBuffer &buffer, CKRenderContext *view, float first) {
        ASSERT_EQ(CK_VB_LOST, buffer.Check(view, 3, CKRST_DP_TRANSFORM, FALSE));
        VxDrawPrimitiveData *data = buffer.Lock(view, 0, 3, CK_LOCK_DEFAULT);
        ASSERT_NE(nullptr, data);
        for (int i = 0; i < 3; ++i) {
            const VxVector point(first + i, 7.0f + i, 11.0f + i);
            memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride, &point, sizeof(point));
        }
        buffer.Unlock(view);
    }
    void MeshResources(RCKMesh &mesh, int context, CKBOOL track) {
        ManagerResourceObserver &boundary = Boundary[context];
        const VxVector positions[] = {VxVector(2, 3, 5), VxVector(7, 11, 13), VxVector(17, 19, 23)};
        const CKWORD indices[] = {2, 0, 1};
        CKVertexBufferDesc vertex;
        vertex.m_VertexFormat = CKRST_DP_TRANSFORM; vertex.m_VertexSize = sizeof(VxVector);
        vertex.m_MaxVertexCount = vertex.m_CurrentVCount = 3;
        CKIndexBufferDesc index; index.m_MaxIndexCount = index.m_CurrentICount = 3;
        RCKMeshBufferState state = {}; state.Context = &boundary;
        ASSERT_TRUE(boundary.CreateVertexBuffer(&vertex, positions, &state.VertexBuffer));
        ASSERT_TRUE(boundary.CreateIndexBuffer(&index, indices, &state.IndexBuffer));
        state.VertexBufferReady = 1; state.IndexBufferReady = TRUE;
        state.VertexBufferDpFlags = CKRST_DP_TRANSFORM; state.VertexBufferVertexFormat = CKRST_DP_TRANSFORM;
        state.VertexBufferStride = sizeof(VxVector); state.VertexBufferVertexCount = 3;
        state.IndexBufferIndexCount = 3; state.VertexBufferWrapAware = TRUE;
        mesh.SetRasterizerBufferState(state);
        if (track) ASSERT_TRUE(Manager->StoreMeshBuffers(&mesh));
    }
};
}

TEST_F(RenderManagerResourcesTest, FlushReleasesTextureSpriteAndTextCopiesAndRetriesOnlyRejectedOwners) {
    RCKTexture *textureObject = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    RCKSprite *spriteObject = static_cast<RCKSprite *>(Context->CreateObject(CKCID_SPRITE));
    RCKSpriteText *textObject = static_cast<RCKSpriteText *>(Context->CreateObject(CKCID_SPRITETEXT));
    ASSERT_NE(nullptr, textureObject); ASSERT_NE(nullptr, spriteObject); ASSERT_NE(nullptr, textObject);
    RCKTexture &texture = *textureObject; RCKSprite &sprite = *spriteObject; RCKSpriteText &text = *textObject;
    ASSERT_TRUE(texture.CreateImage(1, 1, 32)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff123456u));
    ASSERT_TRUE(sprite.Create(1, 1, 32)); ASSERT_TRUE(sprite.SetPixel(0, 0, 0xff345678u));
    ASSERT_TRUE(text.Create(1, 1, 32)); ASSERT_TRUE(text.SetPixel(0, 0, 0xff56789au));
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(texture.SystemToVideoMemory(View[i], FALSE));
        ASSERT_TRUE(sprite.SystemToVideoMemory(View[i], FALSE));
        ASSERT_TRUE(text.SystemToVideoMemory(View[i], FALSE));
        ASSERT_EQ(3, Boundary[i].Creates);
    }
    Boundary[0].DeleteResult = FALSE; Manager->FlushTextures();
    EXPECT_TRUE(texture.IsInVideoMemory()); EXPECT_TRUE(sprite.IsInVideoMemory()); EXPECT_TRUE(text.IsInVideoMemory());
    for (int handle = 1; handle <= 3; ++handle) {
        EXPECT_TRUE(Boundary[0].Live[handle]); EXPECT_FALSE(Boundary[1].Live[handle]);
    }
    EXPECT_EQ(3, Boundary[0].Deletes); EXPECT_EQ(3, Boundary[1].Deletes);
    Boundary[0].DeleteResult = TRUE; Manager->FlushTextures();
    EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_FALSE(sprite.IsInVideoMemory()); EXPECT_FALSE(text.IsInVideoMemory());
    EXPECT_EQ(6, Boundary[0].Deletes); EXPECT_EQ(3, Boundary[1].Deletes);
    EXPECT_EQ(0xff123456u, texture.GetPixel(0, 0)); EXPECT_EQ(0xff345678u, sprite.GetPixel(0, 0));
    EXPECT_EQ(0xff56789au, text.GetPixel(0, 0));
    Manager->FlushTextures(); EXPECT_EQ(6, Boundary[0].Deletes); EXPECT_EQ(3, Boundary[1].Deletes);
}

TEST_F(RenderManagerResourcesTest, DeleteAllVertexBuffersDestroysOwnedCopiesAndLeavesForeignBufferUsable) {
    CKVertexBuffer *first = Manager->CreateVertexBuffer(), *second = Manager->CreateVertexBuffer();
    ASSERT_NE(nullptr, first); ASSERT_NE(nullptr, second);
    RCKVertexBuffer foreign(Context); Fill(*first, View[0], 13); Fill(*second, View[1], 29);
    Fill(foreign, View[0], 41);
    Manager->DestroyVertexBuffer(&foreign);
    EXPECT_EQ(2, Manager->m_VertexBuffers.Size());
    const RCKVertexBufferObjectState foreignState = BufferState(foreign);
    EXPECT_TRUE(foreignState.Valid); EXPECT_TRUE(Boundary[0].VertexLive[foreignState.ObjectIndex]);
    Manager->DeleteAllVertexBuffers(); EXPECT_EQ(0, Manager->m_VertexBuffers.Size());
    EXPECT_EQ(1, Boundary[0].VertexDeletes); EXPECT_EQ(1, Boundary[1].VertexDeletes);
    EXPECT_TRUE(Boundary[0].VertexLive[foreignState.ObjectIndex]);
    VxVector observed; memcpy(&observed, Boundary[0].VertexBytes[foreignState.ObjectIndex], sizeof(observed));
    EXPECT_FLOAT_EQ(41, observed.x); EXPECT_FLOAT_EQ(7, observed.y); EXPECT_FLOAT_EQ(11, observed.z);
    Manager->DeleteAllVertexBuffers(); EXPECT_EQ(1, Boundary[0].VertexDeletes); EXPECT_EQ(1, Boundary[1].VertexDeletes);
}

TEST_F(RenderManagerResourcesTest, VertexBufferPartialDeletionKeepsTheRejectedCopyAndCanRetryWithoutDuplicateDeletes) {
    RCKVertexBuffer buffer(Context); Fill(buffer, View[0], 13);
    ASSERT_EQ(CK_VB_OK, buffer.Check(View[1], 3, CKRST_DP_TRANSFORM, FALSE));
    const RCKVertexBufferObjectState second = BufferState(buffer);
    ASSERT_TRUE(Manager->SelectVertexBufferObject(&buffer, &Boundary[0]));
    Boundary[0].RefuseVertexDelete = TRUE;
    EXPECT_FALSE(Manager->DeleteVertexBufferObjects(&buffer));
    RCKVertexBufferObjectState retained = BufferState(buffer);
    EXPECT_EQ(&Boundary[0], retained.Context); EXPECT_TRUE(retained.Valid);
    EXPECT_TRUE(Boundary[0].VertexLive[retained.ObjectIndex]); EXPECT_FALSE(Boundary[1].VertexLive[second.ObjectIndex]);
    EXPECT_EQ(1, Boundary[0].VertexDeletes); EXPECT_EQ(1, Boundary[1].VertexDeletes);
    Boundary[0].RefuseVertexDelete = FALSE;
    EXPECT_TRUE(Manager->DeleteVertexBufferObjects(&buffer));
    EXPECT_EQ(nullptr, BufferState(buffer).Context); EXPECT_EQ(0u, BufferState(buffer).ObjectIndex);
    EXPECT_EQ(2, Boundary[0].VertexDeletes); EXPECT_EQ(1, Boundary[1].VertexDeletes);
}

TEST_F(RenderManagerResourcesTest, ForgetVertexBufferContextRestoresAnotherCopyWithoutDeletingEitherBackend) {
    RCKVertexBuffer buffer(Context); Fill(buffer, View[0], 13);
    const RCKVertexBufferObjectState first = BufferState(buffer);
    ASSERT_EQ(CK_VB_OK, buffer.Check(View[1], 3, CKRST_DP_TRANSFORM, FALSE));
    const RCKVertexBufferObjectState second = BufferState(buffer);
    Manager->ForgetRasterizerContext(&Boundary[1]);
    EXPECT_EQ(first.Context, BufferState(buffer).Context); EXPECT_EQ(first.ObjectIndex, BufferState(buffer).ObjectIndex);
    EXPECT_EQ(first.ContentVersion, BufferState(buffer).ContentVersion); EXPECT_TRUE(BufferState(buffer).Valid);
    EXPECT_TRUE(Boundary[1].VertexLive[second.ObjectIndex]); EXPECT_EQ(0, Boundary[1].VertexDeletes);
    Manager->ForgetRasterizerContext(&Boundary[0]);
    EXPECT_EQ(nullptr, BufferState(buffer).Context); EXPECT_FALSE(BufferState(buffer).Valid);
    EXPECT_EQ(0, Boundary[0].VertexDeletes); EXPECT_TRUE(Boundary[0].VertexLive[first.ObjectIndex]);
    EXPECT_FALSE(Manager->SelectVertexBufferObject(&buffer, &Boundary[0]));
}

TEST_F(RenderManagerResourcesTest, MeshDirtyNotificationsReachEveryCopyAndPartialDeletionKeepsOnlyRejectedResources) {
    RCKMesh mesh(Context); MeshResources(mesh, 0, TRUE); const RCKMeshBufferState first = MeshState(mesh);
    MeshResources(mesh, 1, TRUE); const RCKMeshBufferState second = MeshState(mesh);
    Manager->MeshVerticesChanged(&mesh); Manager->MeshIndicesChanged(&mesh);
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(Manager->SelectMeshBuffers(&mesh, &Boundary[i]));
        const RCKMeshBufferState current = MeshState(mesh);
        EXPECT_EQ(0u, current.VertexBufferReady); EXPECT_FALSE(current.IndexBufferReady);
        EXPECT_NE(0u, current.VertexBuffer); EXPECT_NE(0u, current.IndexBuffer);
    }
    Boundary[0].RefuseVertexDelete = TRUE; EXPECT_FALSE(Manager->DeleteMeshBuffers(&mesh));
    const RCKMeshBufferState retained = MeshState(mesh);
    EXPECT_EQ(&Boundary[0], retained.Context); EXPECT_EQ(first.VertexBuffer, retained.VertexBuffer);
    EXPECT_EQ(0u, retained.IndexBuffer); EXPECT_EQ(0u, retained.IndexBufferIndexCount);
    EXPECT_EQ(3u, retained.VertexBufferVertexCount); EXPECT_TRUE(retained.VertexBufferWrapAware);
    EXPECT_TRUE(Boundary[0].VertexLive[first.VertexBuffer]); EXPECT_FALSE(Boundary[0].IndexLive[first.IndexBuffer]);
    EXPECT_FALSE(Boundary[1].VertexLive[second.VertexBuffer]); EXPECT_FALSE(Boundary[1].IndexLive[second.IndexBuffer]);
    Boundary[0].RefuseVertexDelete = FALSE; EXPECT_TRUE(Manager->DeleteMeshBuffers(&mesh));
    const RCKMeshBufferState empty = MeshState(mesh);
    EXPECT_EQ(nullptr, empty.Context); EXPECT_EQ(0u, empty.VertexBuffer); EXPECT_EQ(0u, empty.VertexBufferVertexCount);
    EXPECT_EQ(0u, empty.VertexBufferDpFlags); EXPECT_EQ(0u, empty.VertexBufferVertexFormat);
    EXPECT_EQ(0u, empty.VertexBufferStride); EXPECT_FALSE(empty.VertexBufferWrapAware);
    EXPECT_EQ(2, Boundary[0].VertexDeletes); EXPECT_EQ(1, Boundary[0].IndexDeletes);
    EXPECT_EQ(1, Boundary[1].VertexDeletes); EXPECT_EQ(1, Boundary[1].IndexDeletes);
}

TEST_F(RenderManagerResourcesTest, ForcedMeshCleanupReportsRejectedDeletesForTrackedAndSelectedObjects) {
    for (int tracked = 0; tracked < 2; ++tracked) {
        SCOPED_TRACE(tracked); RCKMesh mesh(Context); MeshResources(mesh, 0, tracked);
        const RCKMeshBufferState owned = MeshState(mesh);
        Boundary[0].RefuseVertexDelete = Boundary[0].RefuseIndexDelete = TRUE;
        const int vertexDeletes = Boundary[0].VertexDeletes, indexDeletes = Boundary[0].IndexDeletes;
        EXPECT_FALSE(Manager->DeleteMeshBuffers(&mesh, FALSE));
        EXPECT_EQ(vertexDeletes + 1, Boundary[0].VertexDeletes); EXPECT_EQ(indexDeletes + 1, Boundary[0].IndexDeletes);
        EXPECT_TRUE(Boundary[0].VertexLive[owned.VertexBuffer]); EXPECT_TRUE(Boundary[0].IndexLive[owned.IndexBuffer]);
        const RCKMeshBufferState empty = MeshState(mesh);
        EXPECT_EQ(nullptr, empty.Context); EXPECT_EQ(0u, empty.VertexBuffer); EXPECT_EQ(0u, empty.IndexBuffer);
        EXPECT_FALSE(Manager->SelectMeshBuffers(&mesh, &Boundary[0]));
        EXPECT_FALSE(Manager->DeleteMeshBuffers(&mesh, FALSE));
        EXPECT_EQ(vertexDeletes + 1, Boundary[0].VertexDeletes); EXPECT_EQ(indexDeletes + 1, Boundary[0].IndexDeletes);
        Boundary[0].RefuseVertexDelete = Boundary[0].RefuseIndexDelete = FALSE;
        ASSERT_TRUE(Boundary[0].DeleteObject(owned.VertexBuffer, CKRST_OBJ_VERTEXBUFFER));
        ASSERT_TRUE(Boundary[0].DeleteObject(owned.IndexBuffer, CKRST_OBJ_INDEXBUFFER));
    }
}

TEST_F(RenderManagerResourcesTest, SuccessfulVertexDeletionClearsLayoutWhileRejectedIndexKeepsItsOwnerAndCount) {
    RCKMesh mesh(Context); MeshResources(mesh, 0, TRUE);
    const RCKMeshBufferState owned = MeshState(mesh); Boundary[0].RefuseIndexDelete = TRUE;
    EXPECT_FALSE(Manager->DeleteMeshBuffers(&mesh));
    const RCKMeshBufferState retained = MeshState(mesh);
    EXPECT_EQ(&Boundary[0], retained.Context); EXPECT_EQ(owned.IndexBuffer, retained.IndexBuffer);
    EXPECT_EQ(3u, retained.IndexBufferIndexCount); EXPECT_FALSE(retained.IndexBufferReady);
    EXPECT_EQ(0u, retained.VertexBuffer); EXPECT_EQ(0u, retained.VertexBufferReady);
    EXPECT_EQ(0u, retained.VertexBufferDpFlags); EXPECT_EQ(0u, retained.VertexBufferVertexFormat);
    EXPECT_EQ(0u, retained.VertexBufferStride); EXPECT_EQ(0u, retained.VertexBufferVertexCount);
    EXPECT_FALSE(retained.VertexBufferWrapAware); EXPECT_FALSE(Boundary[0].VertexLive[owned.VertexBuffer]);
    EXPECT_TRUE(Boundary[0].IndexLive[owned.IndexBuffer]);
    Boundary[0].RefuseIndexDelete = FALSE; EXPECT_TRUE(Manager->DeleteMeshBuffers(&mesh));
    EXPECT_EQ(1, Boundary[0].VertexDeletes); EXPECT_EQ(2, Boundary[0].IndexDeletes);
}

TEST_F(RenderManagerResourcesTest, ForgetMeshContextSelectsRemainingMetadataAndDoesNotDeleteLostContextResources) {
    RCKMesh mesh(Context); MeshResources(mesh, 0, TRUE); const RCKMeshBufferState first = MeshState(mesh);
    MeshResources(mesh, 1, TRUE); const RCKMeshBufferState second = MeshState(mesh);
    Manager->ForgetRasterizerContext(&Boundary[1]);
    EXPECT_EQ(&Boundary[0], MeshState(mesh).Context); EXPECT_EQ(first.VertexBuffer, MeshState(mesh).VertexBuffer);
    EXPECT_EQ(first.IndexBuffer, MeshState(mesh).IndexBuffer); EXPECT_EQ(1u, MeshState(mesh).VertexBufferReady);
    EXPECT_TRUE(MeshState(mesh).IndexBufferReady); EXPECT_TRUE(MeshState(mesh).VertexBufferWrapAware);
    EXPECT_TRUE(Boundary[1].VertexLive[second.VertexBuffer]); EXPECT_TRUE(Boundary[1].IndexLive[second.IndexBuffer]);
    EXPECT_EQ(0, Boundary[1].VertexDeletes); EXPECT_EQ(0, Boundary[1].IndexDeletes);
    Manager->ForgetRasterizerContext(&Boundary[0]);
    EXPECT_EQ(nullptr, MeshState(mesh).Context); EXPECT_EQ(0u, MeshState(mesh).VertexBuffer);
    EXPECT_EQ(0, Boundary[0].VertexDeletes); EXPECT_EQ(0, Boundary[0].IndexDeletes);
}

TEST_F(RenderManagerResourcesTest, DeletingNonselectedTextureOrSpriteCopyPreservesTheOtherOwnerAndItsPixels) {
    RCKTexture texture(Context); RCKSprite sprite(Context);
    ASSERT_TRUE(texture.CreateImage(1, 1, 32)); ASSERT_TRUE(texture.SetPixel(0, 0, 0xff123456u));
    ASSERT_TRUE(sprite.Create(1, 1, 32)); ASSERT_TRUE(sprite.SetPixel(0, 0, 0xff345678u));
    for (int i = 0; i < 2; ++i) {
        ASSERT_TRUE(texture.SystemToVideoMemory(View[i], FALSE)); ASSERT_TRUE(sprite.SystemToVideoMemory(View[i], FALSE));
    }
    Boundary[0].DeleteResult = FALSE;
    EXPECT_FALSE(Manager->DeleteTextureObject(&texture, &Boundary[0]));
    EXPECT_FALSE(Manager->DeleteSpriteObject(&sprite, &Boundary[0]));
    EXPECT_TRUE(texture.IsInVideoMemory()); EXPECT_TRUE(sprite.IsInVideoMemory());
    Boundary[0].DeleteResult = TRUE;
    EXPECT_TRUE(Manager->DeleteTextureObject(&texture, &Boundary[0]));
    EXPECT_TRUE(Manager->DeleteSpriteObject(&sprite, &Boundary[0]));
    EXPECT_TRUE(texture.IsInVideoMemory()); EXPECT_TRUE(sprite.IsInVideoMemory());
    EXPECT_TRUE(Boundary[1].Live[1]); EXPECT_TRUE(Boundary[1].Live[2]);
    EXPECT_EQ(0, Boundary[1].Deletes);
    Manager->ForgetRasterizerContext(&Boundary[1]);
    EXPECT_FALSE(texture.IsInVideoMemory()); EXPECT_FALSE(sprite.IsInVideoMemory());
    EXPECT_EQ(UNKNOWN_PF, texture.GetVideoPixelFormat()); EXPECT_EQ(UNKNOWN_PF, sprite.GetVideoPixelFormat());
    EXPECT_EQ(0xff123456u, texture.GetPixel(0, 0)); EXPECT_EQ(0xff345678u, sprite.GetPixel(0, 0));
}
