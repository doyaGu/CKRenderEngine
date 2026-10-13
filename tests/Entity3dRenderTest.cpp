#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCK2dEntity.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "RCKSprite3D.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// This boundary spy stores only externally visible state. All culling, matrix,
// callbacks, PM creation and skin vertex calculations run in the real engine.
// The mesh observer stops at the draw boundary; this is not a GPU backend test.
class EntityRenderStateSpy : public CKRasterizerContext {
public:
    CKDWORD States[1024] = {};
    VxMatrix World = VxMatrix::Identity();
    CKViewportData Viewport = {};
    int WorldWrites = 0, StateWrites = 0, PresentCount = 0;
    CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES] = {};
    CKDWORD StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE] = {};
    VxMatrix Matrices[32];
    int DrawCount = 0, BatchVertexCount = 0, BatchIndexCount = 0;
    CKWORD BatchIndices[6] = {};
    VxVector4 BatchPositions[4];
    VxVector BatchWorldPosition;
    EntityRenderStateSpy() {
        for (int i = 0; i < 32; ++i) Matrices[i].SetIdentity();
    }
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override { ADD_FAILURE() << "Unexpected rasterizer call: Create"; return FALSE; }
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: Resize"; return FALSE; }
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override { ADD_FAILURE() << "Unexpected rasterizer call: SetOptions"; return FALSE; }
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override { return FALSE; }
    CKERROR GetDeviceStatus() const override { return CK_OK; }
    CKBOOL BeginShutdown() override { return TRUE; }
    CKBOOL IsIdle() const override { return TRUE; }
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects) override { ADD_FAILURE() << "Unexpected rasterizer call: Clear"; return FALSE; }
    CKBOOL BeginScene() override { ADD_FAILURE() << "Unexpected rasterizer call: BeginScene"; return FALSE; }
    CKBOOL EndScene() override { ADD_FAILURE() << "Unexpected rasterizer call: EndScene"; return FALSE; }
    CKBOOL BeginOverlayPhase() override { ADD_FAILURE() << "Unexpected rasterizer call: BeginOverlayPhase"; return FALSE; }
    CKBOOL BackToFront(CKBOOL VSync) override { ++PresentCount; return TRUE; }
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override { States[(unsigned int)State] = Value; ++StateWrites; return TRUE; }
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override { *Value = States[(unsigned int)State]; return TRUE; }
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override { StageStates[Stage][Tss] = Value; return TRUE; }
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override { *Value = StageStates[Stage][Tss]; return TRUE; }
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { return TRUE; } // Boundary acknowledgement; no pipeline implementation.
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override { Textures[Stage] = Texture; return TRUE; }
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override { *Texture = Textures[Stage]; return TRUE; }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override { Matrices[(unsigned int)Type] = Mat; if (Type == VXMATRIX_WORLD) { World = Mat; ++WorldWrites; } return TRUE; }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override { Mat = Matrices[(unsigned int)Type]; return TRUE; }
    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override { ADD_FAILURE() << "Unexpected rasterizer call: SetLight"; return FALSE; }
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override { ADD_FAILURE() << "Unexpected rasterizer call: EnableLight"; return FALSE; }
    CKBOOL SetMaterial(const CKMaterialData *Data) override { ADD_FAILURE() << "Unexpected rasterizer call: SetMaterial"; return FALSE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { return TRUE; } // No shading or material behavior is tested.
    CKBOOL SetViewport(const CKViewportData *Data) override { Viewport = *Data; return TRUE; }
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: SetUserClipPlane"; return FALSE; }
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override { ADD_FAILURE() << "Unexpected rasterizer call: GetUserClipPlane"; return FALSE; }
    void InitDefaultRenderStatesValue() override { ADD_FAILURE() << "Unexpected rasterizer call: InitDefaultRenderStatesValue"; }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data) override {
        ++DrawCount; EXPECT_EQ(VX_TRIANGLELIST, Type); EXPECT_EQ(6, IndexCount);
        EXPECT_NE(nullptr, Indices); EXPECT_NE(nullptr, Data);
        if (!Indices || !Data) return FALSE;
        BatchIndexCount = IndexCount; BatchVertexCount = Data->VertexCount;
        EXPECT_EQ(4, Data->VertexCount); EXPECT_NE(nullptr, Data->PositionPtr);
        for (int i = 0; i < 6 && i < IndexCount; ++i) BatchIndices[i] = Indices[i];
        if (Data->PositionPtr) {
            const unsigned char *points = static_cast<const unsigned char *>(Data->PositionPtr);
            for (int i = 0; i < 4 && i < Data->VertexCount; ++i)
                BatchPositions[i] = *reinterpret_cast<const VxVector4 *>(points + i * Data->PositionStride);
        }
        BatchWorldPosition = World[3]; return TRUE;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVB"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected rasterizer call: DrawPrimitiveVBIB"; return FALSE; }
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateTexture"; return FALSE; }
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel, CKRST_CUBEFACE Face, const CKRECT *Region) override { ADD_FAILURE() << "Unexpected rasterizer call: LoadTexture"; return FALSE; }
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetTextureDesc"; return FALSE; }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateVertexBuffer"; return FALSE; }
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetVertexBufferDesc"; return FALSE; }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected rasterizer call: CreateIndexBuffer"; return FALSE; }
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected rasterizer call: GetIndexBufferDesc"; return FALSE; }
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockVertexBuffer"; return nullptr; }
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockVertexBuffer"; return FALSE; }
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected rasterizer call: LockIndexBuffer"; return nullptr; }
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override { ADD_FAILURE() << "Unexpected rasterizer call: UnlockIndexBuffer"; return FALSE; }
    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override { ADD_FAILURE() << "Unexpected rasterizer call: DeleteObject"; return FALSE; }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected rasterizer call: FlushObjects"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected rasterizer call: SetResourceName"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: SetTargetTexture"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToTexture"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyToMemoryBuffer"; return FALSE; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected rasterizer call: CopyFromMemoryBuffer"; return FALSE; }
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override { ADD_FAILURE() << "Unexpected rasterizer call: RequestReadback"; return FALSE; }
};

struct RenderTrace {
    int Count = 0;
    int Order[32] = {};
    RCK3dEntity *Current[32] = {};
    VxVector Vertices[32];
    VxVector WorldPositions[32];
    CKDWORD Winding[32] = {};
    CKRenderContext *Devices[32] = {};
    CKRenderObject *Objects[32] = {};
    RCKMesh *Mesh = nullptr;
    EntityRenderStateSpy *State = nullptr;
    int BatchDraws[32] = {};
    void Record(int value, RCKRenderContext *device, CKRenderObject *entity) {
        ASSERT_LT(Count, 32);
        const int i = Count++;
        Order[i] = value; Current[i] = device->m_Current3dEntity;
        Devices[i] = device; Objects[i] = entity;
        WorldPositions[i] = device->GetWorldTransformationMatrix()[3];
        Winding[i] = device->GetState(VXRENDERSTATE_INVERSEWINDING);
        if (State) BatchDraws[i] = State->DrawCount;
        if (Mesh && Mesh->GetVertexCount()) Mesh->GetVertexPosition(0, &Vertices[i]);
    }
};

class ObservedEntityMesh : public RCKMesh {
public:
    RenderTrace *Trace;
    int Value;
    int Draws = 0;
    ObservedEntityMesh(CKContext *context, RenderTrace *trace, int value)
        : RCKMesh(context), Trace(trace), Value(value) {}
    CKERROR Render(CKRenderContext *device, CK3dEntity *entity) override {
        ++Draws;
        Trace->Record(Value, static_cast<RCKRenderContext *>(device),
                      reinterpret_cast<RCK3dEntity *>(entity));
        return CKERR_INVALIDOPERATION; // Entity routing does not propagate a mesh return value.
    }
};

struct CallbackEntry {
    RenderTrace *Trace;
    int Value;
    RCK3dEntity *Bone = nullptr;
    float BoneX = 0;
    CKMesh *Replacement = nullptr;
    CKBOOL HideMesh = FALSE;
    CKBOOL SetWinding = FALSE;
    CKDWORD Winding = 0;
};
CKBOOL ObserveEntityCallback(CKRenderContext *device, CKRenderObject *object, void *argument) {
    CallbackEntry *entry = static_cast<CallbackEntry *>(argument);
    RCKRenderContext *runtimeDevice = static_cast<RCKRenderContext *>(device);
    entry->Trace->Record(entry->Value, runtimeDevice, object);
    RCK3dEntity *entity = static_cast<RCK3dEntity *>(object);
    if (entry->Bone) entry->Bone->SetPosition3f(entry->BoneX, 0, 0);
    if (entry->Replacement) entity->SetCurrentMesh(entry->Replacement);
    if (entry->HideMesh) entity->GetCurrentMesh()->SetFlags(entity->GetCurrentMesh()->GetFlags() & ~VXMESH_VISIBLE);
    if (entry->SetWinding) runtimeDevice->SetState(VXRENDERSTATE_INVERSEWINDING, entry->Winding);
    return FALSE; // FALSE is deliberately ignored by this consumer.
}

class Entity3dRenderTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCK3dEntity *Object = nullptr;
    EntityRenderStateSpy State;
    RenderTrace Trace;
    ObservedEntityMesh *Meshes[4] = {};
    int MeshCount = 0;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKSprite3D, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        // The constructor seeds 500 extents slots; the public detach operation
        // clears prior associations and starts this isolated draw with no extents.
        Device->DetachAll();
        Device->m_RasterizerContext = &State;
        VxRect viewport(0, 0, 200, 200); Device->SetViewRect(viewport);
        Device->SetViewTransformationMatrix(VxMatrix::Identity());
        VxMatrix projection; projection.Perspective(1.5707963267948966f, 1, 1, 20);
        Device->SetProjectionTransformationMatrix(projection);
        Device->SetWorldTransformationMatrix(VxMatrix::Identity());
        Device->m_Flags = 0; Device->m_Current3dEntity = nullptr; Trace.State = &State;
        Object = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        ASSERT_NE(nullptr, Object); Object->SetPosition3f(0, 0, 5);
        State.WorldWrites = 0; State.StateWrites = 0;
    }
    void TearDown() override {
        if (Object) {
            Object->SetCurrentMesh(nullptr);
            for (int i = 0; i < MeshCount; ++i) Object->RemoveMesh(Meshes[i]);
        }
        for (int i = 0; i < MeshCount; ++i) delete Meshes[i];
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static CK3dEntity *API(RCK3dEntity *entity) { return reinterpret_cast<CK3dEntity *>(entity); }
    ObservedEntityMesh *Mesh(int value = 20) {
        ObservedEntityMesh *mesh = new ObservedEntityMesh(Context, &Trace, value);
        Meshes[MeshCount++] = mesh;
        mesh->SetVertexCount(3); mesh->SetFaceCount(1);
        VxVector points[3] = {VxVector(-.5f, -.5f, 0), VxVector(.5f, -.5f, 0), VxVector(0, .5f, 0)};
        for (int i = 0; i < 3; ++i) mesh->SetVertexPosition(i, &points[i]);
        mesh->SetFaceVertexIndex(0, 0, 1, 2); mesh->BuildFaceNormals(); mesh->VertexMove();
        return mesh;
    }
    ObservedEntityMesh *AttachMesh() {
        ObservedEntityMesh *mesh = Mesh(); Object->SetCurrentMesh(mesh); Trace.Mesh = mesh; return mesh;
    }
    void Cached() { Object->m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE; }
    void ExpectWorld(int index, float x, float y, float z) {
        EXPECT_NEAR(x, Trace.WorldPositions[index].x, 1e-5f);
        EXPECT_NEAR(y, Trace.WorldPositions[index].y, 1e-5f);
        EXPECT_NEAR(z, Trace.WorldPositions[index].z, 1e-5f);
    }
    void ExpectCallbacks(const int *order, int count) {
        ASSERT_EQ(count, Trace.Count);
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(order[i], Trace.Order[i]); EXPECT_EQ(Device, Trace.Devices[i]);
            EXPECT_EQ(Object, Trace.Objects[i]);
            EXPECT_EQ(order[i] == 20 || order[i] == 40 ? Object : nullptr, Trace.Current[i]);
        }
        EXPECT_EQ(nullptr, Device->m_Current3dEntity);
    }
    RCK3dEntity *Skin(ObservedEntityMesh *mesh) {
        RCK3dEntity *bone = static_cast<RCK3dEntity *>(Context->CreateObject(CKCID_3DENTITY));
        CKSkin *skin = Object->CreateSkin();
        skin->SetObjectInitMatrix(VxMatrix::Identity()); skin->SetBoneCount(1);
        skin->GetBoneData(0)->SetBone(API(bone));
        skin->GetBoneData(0)->SetBoneInitialInverseMatrix(VxMatrix::Identity());
        skin->SetVertexCount(mesh->GetVertexCount());
        for (int i = 0; i < mesh->GetVertexCount(); ++i) {
            VxVector point; mesh->GetVertexPosition(i, &point);
            CKSkinVertexData *vertex = skin->GetVertexData(i);
            vertex->SetInitialPos(point); vertex->SetBoneCount(1); vertex->SetBone(0, 0); vertex->SetWeight(0, 1);
        }
        Object->SetPosition3f(0, 0, 0); Cached(); return bone;
    }
};

TEST_F(Entity3dRenderTest, EmptyObjectReturnsFalseWithoutChangingContextOrAppendingExtents) {
    EXPECT_FALSE(Object->Render(Device, 1)); EXPECT_EQ(0, State.WorldWrites);
    EXPECT_EQ(0, Device->m_ObjectExtents.Size()); EXPECT_EQ(0, Trace.Count);
}

TEST_F(Entity3dRenderTest, NewlyVisibleRealMeshUsesEntityWorldAndReturnsTrueDespiteMeshError) {
    ObservedEntityMesh *mesh = AttachMesh(); Object->SetPosition3f(1, 2, 5);
    VxMatrix stale = VxMatrix::Identity(); stale[3].Set(100, 0, 5); Device->SetWorldTransformationMatrix(stale);
    ASSERT_TRUE(Object->Render(Device, 1)); EXPECT_EQ(1, mesh->Draws);
    const int order[] = {20}; ExpectCallbacks(order, 1); ExpectWorld(0, 1, 2, 5);
    EXPECT_NEAR(1, State.World[3].x, 1e-5f); ASSERT_EQ(1, Device->m_ObjectExtents.Size());
    EXPECT_EQ(API(Object), Device->m_ObjectExtents[0].m_Entity);
    const VxRect &extent = Device->m_ObjectExtents[0].m_Rect;
    EXPECT_LT(extent.left, extent.right); EXPECT_LT(extent.top, extent.bottom);
}

TEST_F(Entity3dRenderTest, CachedVisibleMeshSynchronizesWorldWithoutAnotherVisibilityTest) {
    ObservedEntityMesh *mesh = AttachMesh(); Object->SetPosition3f(100, 0, 5); Cached();
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1, mesh->Draws); ExpectWorld(0, 100, 0, 5);
    EXPECT_EQ(0, Device->m_ObjectExtents.Size()); EXPECT_GT(State.WorldWrites, 0);
}

TEST_F(Entity3dRenderTest, ClearViewportCachedMeshPreservesCallerWorldTransformation) {
    ObservedEntityMesh *mesh = AttachMesh(); Cached();
    VxMatrix caller = VxMatrix::Identity(); caller[3].Set(2, 3, 7); Device->SetWorldTransformationMatrix(caller); State.WorldWrites = 0;
    ASSERT_TRUE(Object->Render(Device, CK_RENDER_CLEARVIEWPORT)); EXPECT_EQ(1, mesh->Draws);
    ExpectWorld(0, 2, 3, 7); EXPECT_EQ(0, State.WorldWrites); EXPECT_EQ(0, Device->m_ObjectExtents.Size());
}

TEST_F(Entity3dRenderTest, ClearViewportNewVisibilityUsesCallerWorldRatherThanOutsideEntityWorld) {
    ObservedEntityMesh *mesh = AttachMesh(); Object->SetPosition3f(100, 0, 5);
    VxMatrix caller = VxMatrix::Identity(); caller[3].Set(0, 0, 5); Device->SetWorldTransformationMatrix(caller); State.WorldWrites = 0;
    ASSERT_TRUE(Object->Render(Device, CK_RENDER_CLEARVIEWPORT | 1)); EXPECT_EQ(1, mesh->Draws);
    ASSERT_EQ(1, Trace.Count); ExpectWorld(0, 0, 0, 5); EXPECT_EQ(0, State.WorldWrites);
    ASSERT_EQ(1, Device->m_ObjectExtents.Size());
    // SIMD projection uses an approximate reciprocal; z = 5 produces about
    // 0.00244 pixel error at this viewport. Keep a 0.005 pixel numeric tolerance.
    EXPECT_NEAR(90, Device->m_ObjectExtents[0].m_Rect.left, .005f);
    EXPECT_NEAR(110, Device->m_ObjectExtents[0].m_Rect.right, .005f);
}

TEST_F(Entity3dRenderTest, CulledMeshSkipsEveryCallbackDrawAndExtentInsertionButReturnsTrue) {
    ObservedEntityMesh *mesh = AttachMesh(); Object->SetPosition3f(100, 0, 5);
    CallbackEntry pre = {&Trace, 10}, main = {&Trace, 50}, post = {&Trace, 30};
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->SetRenderCallBack(ObserveEntityCallback, &main));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 1)); EXPECT_EQ(0, mesh->Draws); EXPECT_EQ(0, Trace.Count);
    EXPECT_EQ(0, Device->m_ObjectExtents.Size()); EXPECT_EQ(nullptr, Device->m_Current3dEntity);
}

TEST_F(Entity3dRenderTest, RealMeshDrawOccursBetweenOrderedCallbacksWithCurrentEntityScopedToDraw) {
    ObservedEntityMesh *mesh = AttachMesh(); Cached();
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30};
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1, mesh->Draws);
    const int order[] = {10, 20, 30}; ExpectCallbacks(order, 3);
}

TEST_F(Entity3dRenderTest, MainCallbackReplacesInvisibleMeshAndDoesNotSetCurrentEntity) {
    ObservedEntityMesh *mesh = AttachMesh(); mesh->SetFlags(mesh->GetFlags() & ~VXMESH_VISIBLE); Cached();
    CallbackEntry pre = {&Trace, 10}, main = {&Trace, 50}, post = {&Trace, 30};
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->SetRenderCallBack(ObserveEntityCallback, &main));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(0, mesh->Draws);
    const int order[] = {10, 50, 30}; ExpectCallbacks(order, 3);
}

TEST_F(Entity3dRenderTest, PreCallbackMeshReplacementIsUsedByTheDefaultDraw) {
    ObservedEntityMesh *first = AttachMesh(), *second = Mesh(40); Cached();
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30}; pre.Replacement = second;
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(0, first->Draws); EXPECT_EQ(1, second->Draws);
    const int order[] = {10, 40, 30}; ExpectCallbacks(order, 3);
}

TEST_F(Entity3dRenderTest, PreCallbackCanHideMeshWhilePostCallbacksStillExecute) {
    ObservedEntityMesh *mesh = AttachMesh(); Cached();
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30}; pre.HideMesh = TRUE;
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(0, mesh->Draws);
    const int order[] = {10, 30}; ExpectCallbacks(order, 2);
}

TEST_F(Entity3dRenderTest, NonProgressiveSkinUpdatesActualVerticesBeforePreCallbacks) {
    ObservedEntityMesh *mesh = AttachMesh(); ASSERT_FALSE(mesh->IsPM()); RCK3dEntity *bone = Skin(mesh); bone->SetPosition3f(3, 0, 0);
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30}; pre.Bone = bone; pre.BoneX = 7;
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); const int order[] = {10, 20, 30}; ExpectCallbacks(order, 3);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(2.5f, Trace.Vertices[i].x, 1e-5f);
    EXPECT_NEAR(7, bone->GetWorldMatrix()[3].x, 1e-5f); EXPECT_GE(Device->m_Stats.SkinTime, 0);
}

TEST_F(Entity3dRenderTest, ProgressiveSkinUpdatesActualVerticesAfterPreCallbacksBeforeDrawAndPost) {
    ObservedEntityMesh *mesh = AttachMesh(); ASSERT_EQ(CK_OK, mesh->CreatePM()); ASSERT_TRUE(mesh->IsPM());
    VxVector initial; mesh->GetVertexPosition(0, &initial); RCK3dEntity *bone = Skin(mesh); bone->SetPosition3f(3, 0, 0);
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30}; pre.Bone = bone; pre.BoneX = 7;
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); const int order[] = {10, 20, 30}; ExpectCallbacks(order, 3);
    EXPECT_NEAR(initial.x, Trace.Vertices[0].x, 1e-5f);
    EXPECT_NEAR(initial.x + 7, Trace.Vertices[1].x, 1e-5f); EXPECT_NEAR(initial.x + 7, Trace.Vertices[2].x, 1e-5f);
}

TEST_F(Entity3dRenderTest, ProgressiveSkinUpdateRunsWithAllocatedEmptyObjectCallbackContainer) {
    ObservedEntityMesh *mesh = AttachMesh(); ASSERT_EQ(CK_OK, mesh->CreatePM()); ASSERT_TRUE(mesh->IsPM());
    VxVector initial; mesh->GetVertexPosition(0, &initial); RCK3dEntity *bone = Skin(mesh); bone->SetPosition3f(4, 0, 0);
    EXPECT_FALSE(Object->AddPreRenderCallBack(nullptr, nullptr, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); const int order[] = {20}; ExpectCallbacks(order, 1);
    EXPECT_NEAR(initial.x + 4, Trace.Vertices[0].x, 1e-5f);
}

TEST_F(Entity3dRenderTest, MirroredDrawTogglesWindingAndRestoresBothBooleanEntryStates) {
    AttachMesh(); Cached(); Object->m_MoveableFlags |= VX_MOVEABLE_INDIRECTMATRIX;
    CallbackEntry pre = {&Trace, 10}, post = {&Trace, 30};
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    for (CKDWORD original = 0; original < 2; ++original) {
        Trace.Count = 0; State.States[VXRENDERSTATE_INVERSEWINDING] = original;
        ASSERT_TRUE(Object->Render(Device, 0)); const int order[] = {10, 20, 30}; ExpectCallbacks(order, 3);
        for (int i = 0; i < 3; ++i) EXPECT_EQ(original ? 0u : 1u, Trace.Winding[i]);
        EXPECT_EQ(original, State.States[VXRENDERSTATE_INVERSEWINDING]);
    }
}

TEST_F(Entity3dRenderTest, MirroredDrawRestoresEntryStateEvenWhenPostCallbackChangesWinding) {
    AttachMesh(); Cached(); Object->m_MoveableFlags |= VX_MOVEABLE_INDIRECTMATRIX;
    CallbackEntry post = {&Trace, 30}; post.SetWinding = TRUE; post.Winding = 0;
    ASSERT_TRUE(Object->AddPostRenderCallBack(ObserveEntityCallback, &post, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(0u, State.States[VXRENDERSTATE_INVERSEWINDING]);
    ASSERT_EQ(2, Trace.Count); EXPECT_EQ(1u, Trace.Winding[0]); EXPECT_EQ(1u, Trace.Winding[1]);
}

TEST_F(Entity3dRenderTest, ExtentFlagsUseAnyLowByteBitAndPreserveAssociatedObjectIdentity) {
    AttachMesh(); Cached(); Object->m_RenderExtents = VxRect(10, 20, 30, 40);
    const CKDWORD flags[5] = {0, 1, 0x80, 0x100, 0x10000};
    const int expected[5] = {0, 1, 2, 2, 2};
    for (int i = 0; i < 5; ++i) { ASSERT_TRUE(Object->Render(Device, flags[i])); EXPECT_EQ(expected[i], Device->m_ObjectExtents.Size()); }
    for (int i = 0; i < Device->m_ObjectExtents.Size(); ++i) {
        const CKObjectExtents &extent = Device->m_ObjectExtents[i];
        EXPECT_EQ(API(Object), extent.m_Entity); EXPECT_FLOAT_EQ(10, extent.m_Rect.left); EXPECT_FLOAT_EQ(40, extent.m_Rect.bottom);
    }
}

TEST_F(Entity3dRenderTest, DrawnDiagnosticIdentifiesTransparentObjectAndDecrementsPresentationInterval) {
    AttachMesh(); Cached(); Object->SetName("RenderProbe"); Object->SetRenderAsTransparent(TRUE);
    Device->m_Flags = 1; Device->m_FpsInterval = 2;
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1u, Device->m_FpsInterval); EXPECT_EQ(0, State.PresentCount);
    EXPECT_NE(nullptr, strstr(Device->m_CurrentObjectDesc.CStr(), "RenderProbe"));
    EXPECT_NE(nullptr, strstr(Device->m_CurrentObjectDesc.CStr(), "as transparent Object"));
    EXPECT_NE(nullptr, strstr(Device->m_CurrentObjectDesc.CStr(), "Drawn"));
}

TEST_F(Entity3dRenderTest, DrawnDiagnosticZeroIntervalPresentsUsingSignedUnderflowContract) {
    AttachMesh(); Cached(); Device->m_Flags = 1; Device->m_FpsInterval = 0;
    Device->m_RenderFlags = CK_RENDER_DOBACKTOFRONT;
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1, State.PresentCount);
    EXPECT_EQ(0xFFFFFFFFu, Device->m_FpsInterval);
}

TEST_F(Entity3dRenderTest, CulledDiagnosticZeroIntervalPresentsAndDescribesNotDrawn) {
    AttachMesh(); Object->SetPosition3f(100, 0, 5); Object->SetName("CullProbe");
    Device->m_Flags = 1; Device->m_FpsInterval = 0; Device->m_RenderFlags = CK_RENDER_DOBACKTOFRONT;
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(0, Trace.Count); EXPECT_EQ(1, State.PresentCount);
    EXPECT_NE(nullptr, strstr(Device->m_CurrentObjectDesc.CStr(), "CullProbe"));
    EXPECT_NE(nullptr, strstr(Device->m_CurrentObjectDesc.CStr(), "Not drawn"));
}
TEST_F(Entity3dRenderTest, SortedRenderingFlushesRealNonEmptySpriteBatchBeforeEntityCallbacksAndDraw) {
    ObservedEntityMesh *mesh = AttachMesh(); Cached();
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    ASSERT_NE(nullptr, material); ASSERT_NE(nullptr, sprite);
    sprite->SetMaterial(material); Vx2DVector size(2, 2); sprite->SetSize(size); sprite->SetPosition3f(0, 0, 5);
    Device->m_SortTransparentObjects = 1;
    Device->AddSprite3DBatch(sprite);
    ASSERT_EQ(1, Device->m_Sprite3DBatches.Size());
    ASSERT_NE(nullptr, material->GetSprite3DBatch()); ASSERT_EQ(4, material->GetSprite3DBatch()->m_Vertices.Size());
    CallbackEntry pre = {&Trace, 10};
    ASSERT_TRUE(Object->AddPreRenderCallBack(ObserveEntityCallback, &pre, FALSE));
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1, mesh->Draws); EXPECT_EQ(1, State.DrawCount);
    const int order[] = {10, 20}; ExpectCallbacks(order, 2);
    EXPECT_EQ(1, Trace.BatchDraws[0]); EXPECT_EQ(1, Trace.BatchDraws[1]);
    EXPECT_EQ(0, Device->m_Sprite3DBatches.Size()); EXPECT_EQ(0, material->GetSprite3DBatch()->m_Vertices.Size());
    EXPECT_EQ(4, State.BatchVertexCount); EXPECT_EQ(6, State.BatchIndexCount);
    const CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    const float x[4] = {-1, -1, 1, 1}, y[4] = {-1, 1, 1, -1};
    for (int i = 0; i < 6; ++i) EXPECT_EQ(indices[i], State.BatchIndices[i]);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(x[i], State.BatchPositions[i].x, 1e-5f); EXPECT_NEAR(y[i], State.BatchPositions[i].y, 1e-5f);
        EXPECT_NEAR(5, State.BatchPositions[i].z, 1e-5f); EXPECT_NEAR(1, State.BatchPositions[i].w, 1e-5f);
    }
    EXPECT_FLOAT_EQ(0, State.BatchWorldPosition.z); ExpectWorld(1, 0, 0, 5);
}

TEST_F(Entity3dRenderTest, UnsortedRenderingPreservesRealNonEmptySpriteBatchForLaterConsumer) {
    ObservedEntityMesh *mesh = AttachMesh(); Cached();
    RCKMaterial *material = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL));
    RCKSprite3D *sprite = static_cast<RCKSprite3D *>(Context->CreateObject(CKCID_SPRITE3D));
    ASSERT_NE(nullptr, material); ASSERT_NE(nullptr, sprite);
    sprite->SetMaterial(material); Vx2DVector size(2, 2); sprite->SetSize(size); sprite->SetPosition3f(0, 0, 5);
    Device->m_SortTransparentObjects = 0; Device->AddSprite3DBatch(sprite);
    ASSERT_EQ(1, Device->m_Sprite3DBatches.Size()); ASSERT_EQ(4, material->GetSprite3DBatch()->m_Vertices.Size());
    ASSERT_TRUE(Object->Render(Device, 0)); EXPECT_EQ(1, mesh->Draws); EXPECT_EQ(0, State.DrawCount);
    const int order[] = {20}; ExpectCallbacks(order, 1); EXPECT_EQ(0, Trace.BatchDraws[0]);
    EXPECT_EQ(1, Device->m_Sprite3DBatches.Size()); EXPECT_EQ(material, Device->m_Sprite3DBatches[0]);
    ASSERT_EQ(4, material->GetSprite3DBatch()->m_Vertices.Size());
    EXPECT_NEAR(-1, material->GetSprite3DBatch()->m_Vertices[0].V.x, 1e-5f);
    EXPECT_NEAR(-1, material->GetSprite3DBatch()->m_Vertices[0].V.y, 1e-5f);
    EXPECT_NEAR(5, material->GetSprite3DBatch()->m_Vertices[0].V.z, 1e-5f);
}
} // namespace
