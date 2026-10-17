#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "MaterialRenderObserver.h"
#include "../src/CKRenderDrawAnnotation.h"
#include <cstring>

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Explicit instantiation may name a private member. Keep the original member
// pointer and access decoration instead of redefining the class declaration.
template <class Tag, typename Tag::Type Member>
struct DispatchMemberAccess {
    friend typename Tag::Type DispatchMember(Tag) { return Member; }
};
struct PreDispatchTag {
    using Type = void (RCKRenderContext::*)();
    friend Type DispatchMember(PreDispatchTag);
};
struct PostDispatchTag {
    using Type = void (RCKRenderContext::*)(CKBOOL);
    friend Type DispatchMember(PostDispatchTag);
};
struct SpriteDispatchTag {
    using Type = void (RCKRenderContext::*)();
    friend Type DispatchMember(SpriteDispatchTag);
};
template struct DispatchMemberAccess<PreDispatchTag, &RCKRenderContext::ExecutePreRenderCallbacks>;
template struct DispatchMemberAccess<PostDispatchTag, &RCKRenderContext::ExecutePostRenderCallbacks>;
template struct DispatchMemberAccess<SpriteDispatchTag, &RCKRenderContext::ExecutePostSpriteCallbacks>;

void DispatchPre(RCKRenderContext *context) {
    (context->*DispatchMember(PreDispatchTag()))();
}
void DispatchPost(RCKRenderContext *context, CKBOOL beforeTransparent) {
    (context->*DispatchMember(PostDispatchTag()))(beforeTransparent);
}
void DispatchSprite(RCKRenderContext *context) {
    (context->*DispatchMember(SpriteDispatchTag()))();
}

class DispatchBoundary : public MaterialRenderObserver {
public:
    CKBOOL DrawAccepted = TRUE;
    int MarkerCount = 0, DrawCount = 0;
    char Markers[16][CKDRAW_ANNOTATION_LABEL_SIZE] = {};
    VxVector LastVertex;
    void SetDebugMarker(CKSTRING label) override {
        ASSERT_LT(MarkerCount, 16);
        std::strncpy(Markers[MarkerCount], label, CKDRAW_ANNOTATION_LABEL_SIZE - 1);
        Markers[MarkerCount][CKDRAW_ANNOTATION_LABEL_SIZE - 1] = '\0';
        ++MarkerCount;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE, CKWORD *, int, VxDrawPrimitiveData *data) override {
        ++DrawCount;
        LastVertex = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr)
            + (data->VertexCount - 1) * data->PositionStride);
        return DrawAccepted;
    }
};

struct DispatchTrace {
    int Count = 0;
    int Values[16] = {};
    CKRenderContext *Contexts[16] = {};
};
struct DispatchCall {
    DispatchTrace *Trace;
    int Value;
};
void RecordDispatch(CKRenderContext *context, void *argument) {
    DispatchCall *call = static_cast<DispatchCall *>(argument);
    ASSERT_LT(call->Trace->Count, 16);
    const int index = call->Trace->Count++;
    call->Trace->Values[index] = call->Value;
    call->Trace->Contexts[index] = context;
}

struct CallbackDraw {
    VxDrawPrimitiveData *Data;
    CKBOOL Accepted = FALSE;
    CKBOOL HasOwner = FALSE;
    CKDrawAnnotationObjectRef Owner = {};
};
void DrawFromDispatch(CKRenderContext *context, void *argument) {
    CallbackDraw *call = static_cast<CallbackDraw *>(argument);
    RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
    call->HasOwner = device->GetDrawCallbackObject(&call->Owner);
    call->Accepted = device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, call->Data);
}

class RenderContextDispatchCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    DispatchBoundary Boundary;
    VxVector Vertices[3] = {VxVector(1, 2, 3), VxVector(4, 5, 6), VxVector(7, 8, 9)};
    VxDrawPrimitiveData Data = {};

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->BeginFrameErrorTracking();
        Data.Flags = CKRST_DP_TRANSFORM;
        Data.VertexCount = 3;
        Data.PositionPtr = Vertices;
        Data.PositionStride = sizeof(VxVector);
    }
    void TearDown() override {
        Device->m_RasterizerContext = nullptr;
        delete Device;
        CKCloseContext(Context);
        CKShutdown();
    }
    CKDrawAnnotationParsed Parsed(int marker) {
        CKDrawAnnotationParsed parsed = {};
        EXPECT_GE(marker, 0);
        EXPECT_LT(marker, Boundary.MarkerCount);
        SCOPED_TRACE(Boundary.Markers[marker]);
        EXPECT_TRUE(CKDrawAnnotationParseLabel(Boundary.Markers[marker], &parsed));
        return parsed;
    }
    void EnableAnnotations() {
        Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_MARKERS);
        ASSERT_NE(nullptr, Device->m_DrawAnnotationState);
    }
};

TEST_F(RenderContextDispatchCompletionTest, RegisteredCallbacksDispatchAllFourPhasesAndHonorManagerTemporaryCleanup) {
    DispatchTrace trace;
    DispatchCall calls[8] = {};
    for (int i = 0; i < 8; ++i) calls[i] = {&trace, i + 1};
    Device->AddPreRenderCallBack(RecordDispatch, &calls[0], FALSE);
    Device->AddPreRenderCallBack(RecordDispatch, &calls[1], TRUE);
    Device->AddPostRenderCallBack(RecordDispatch, &calls[2], FALSE, TRUE);
    Device->AddPostRenderCallBack(RecordDispatch, &calls[3], TRUE, TRUE);
    Device->AddPostRenderCallBack(RecordDispatch, &calls[4], FALSE, FALSE);
    Device->AddPostRenderCallBack(RecordDispatch, &calls[5], TRUE, FALSE);
    Device->AddPostSpriteRenderCallBack(RecordDispatch, &calls[6], FALSE);
    Device->AddPostSpriteRenderCallBack(RecordDispatch, &calls[7], TRUE);
    DispatchPre(Device);
    DispatchPost(Device, TRUE);
    DispatchPost(Device, FALSE);
    DispatchSprite(Device);
    ASSERT_EQ(8, trace.Count);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(i + 1, trace.Values[i]);
        EXPECT_EQ(Device, trace.Contexts[i]);
    }
    Manager->RemoveAllTemporaryCallbacks();
    DispatchPre(Device);
    DispatchPost(Device, TRUE);
    DispatchPost(Device, FALSE);
    DispatchSprite(Device);
    ASSERT_EQ(12, trace.Count);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(2 * i + 1, trace.Values[8 + i]);
        EXPECT_EQ(Device, trace.Contexts[8 + i]);
    }
}

TEST_F(RenderContextDispatchCompletionTest, ActualCallbackDrawsObserveCopiedOwnersAndNestedScopeRestoration) {
    EnableAnnotations();
    RCK3dEntity first(Context, (CKSTRING)"first owner");
    RCK3dEntity second(Context, (CKSTRING)"second owner");
    CallbackDraw call = {&Data};
    Device->AddPreRenderCallBack(DrawFromDispatch, &call);
    {
        CKScopedDrawAnnotation outer(Device, &first);
        CKDrawAnnotationObjectRef copy = {};
        ASSERT_TRUE(Device->GetDrawCallbackObject(&copy));
        EXPECT_EQ(first.GetID(), copy.Id);
        copy.Id = 9999;
        EXPECT_FALSE(Device->GetDrawCallbackObject(nullptr));
        DispatchPre(Device);
        EXPECT_TRUE(call.Accepted); EXPECT_TRUE(call.HasOwner);
        EXPECT_EQ(first.GetID(), call.Owner.Id);
        {
            CKScopedDrawAnnotation inner(Device, &second);
            DispatchPre(Device);
            EXPECT_EQ(second.GetID(), call.Owner.Id);
        }
        DispatchPre(Device);
        EXPECT_EQ(first.GetID(), call.Owner.Id);
    }
    DispatchPre(Device);
    EXPECT_FALSE(call.HasOwner);
    ASSERT_EQ(4, Boundary.MarkerCount); EXPECT_EQ(4, Boundary.DrawCount);
    EXPECT_EQ(first.GetID(), Parsed(0).Object.Id);
    EXPECT_EQ(second.GetID(), Parsed(1).Object.Id);
    EXPECT_EQ(first.GetID(), Parsed(2).Object.Id);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, Parsed(2).Source);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, Parsed(3).Source);
    EXPECT_FLOAT_EQ(9, Boundary.LastVertex.z);
}

TEST_F(RenderContextDispatchCompletionTest, ExplicitPayloadIsCopiedReplacedAndConsumedOnceBeforeOwnerAndRawFallbacks) {
    EnableAnnotations();
    CKDrawAnnotationObjectRef owner = {}; owner.Id = 42;
    CKDrawAnnotationCopyText(owner.Name, sizeof(owner.Name), "callback source");
    Device->SetDrawCallbackObject(&owner);
    owner.Id = 9999;
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_SPRITE);
    Device->SetDrawAnnotation(&pending);
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.PrimitiveType = VX_TRIANGLELIST; pending.IndexCount = 3; pending.VertexCount = 3;
    pending.GroupIndex = 5; pending.PrimitiveIndex = 6;
    pending.Entity.Id = 7; pending.Mesh.Id = 8; pending.Material.Id = 9;
    CKDrawAnnotationCopyText(pending.Path, sizeof(pending.Path), "context-dispatch");
    Device->SetDrawAnnotation(&pending);
    pending.Mesh.Id = 9999;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    Device->SetDrawCallbackObject(nullptr);
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(3, Boundary.MarkerCount);
    CKDrawAnnotationParsed first = Parsed(0), second = Parsed(1), third = Parsed(2);
    EXPECT_EQ(CKDRAW_SOURCE_MESH, first.Source);
    EXPECT_EQ(5, first.GroupIndex); EXPECT_EQ(6, first.PrimitiveIndex);
    EXPECT_EQ(7u, first.Entity.Id); EXPECT_EQ(8u, first.Mesh.Id); EXPECT_EQ(9u, first.Material.Id);
    EXPECT_STREQ("context-dispatch", first.Path);
    EXPECT_EQ(3u, first.IndexCount); EXPECT_EQ(3u, first.VertexCount);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, second.Source); EXPECT_EQ(42u, second.Object.Id);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, third.Source);
    EXPECT_NE(0u, first.Token); EXPECT_NE(first.Token, second.Token); EXPECT_NE(second.Token, third.Token);
    CKDrawAnnotationCounters counts = {};
    CKDrawAnnotationStateGetCounters(Device->m_DrawAnnotationState, &counts);
    EXPECT_EQ(2u, counts.PendingSetCount); EXPECT_EQ(1u, counts.PendingOverwriteCount);
    EXPECT_EQ(1u, counts.ExplicitCount); EXPECT_EQ(1u, counts.CallbackFallbackCount);
    EXPECT_EQ(1u, counts.RawFallbackCount);
}

TEST_F(RenderContextDispatchCompletionTest, MissingOutputAndMissingDeviceDoNotDiscardAQueuedExplicitAnnotation) {
    EnableAnnotations();
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_2D_ENTITY);
    pending.Object.Id = 87; pending.PrimitiveType = VX_TRIANGLELIST;
    pending.IndexCount = 3; pending.VertexCount = 3;
    Device->SetDrawAnnotation(&pending);
    EXPECT_FALSE(Device->ConsumeDrawAnnotation(nullptr, VX_POINTLIST, 1, 1));
    Device->m_RasterizerContext = nullptr;
    Device->ApplyDrawAnnotation(VX_POINTLIST, 1, 1);
    EXPECT_TRUE(CKDrawAnnotationStateHasPending(Device->m_DrawAnnotationState));
    EXPECT_EQ(0, Boundary.MarkerCount);
    Device->m_RasterizerContext = &Boundary;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(1, Boundary.MarkerCount);
    EXPECT_EQ(CKDRAW_SOURCE_2D_ENTITY, Parsed(0).Source);
    EXPECT_EQ(87u, Parsed(0).Object.Id);
    CKDrawAnnotation fallback;
    EXPECT_FALSE(Device->ConsumeDrawAnnotation(&fallback, VX_LINELIST, 4, 6));
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, fallback.Source);
    EXPECT_EQ(VX_LINELIST, fallback.PrimitiveType);
    EXPECT_EQ(4u, fallback.IndexCount); EXPECT_EQ(6u, fallback.VertexCount);
}

TEST_F(RenderContextDispatchCompletionTest, RejectedSubmissionOwnsItsMarkerAndDoesNotLeakExplicitMetadataToTheNextDraw) {
    EnableAnnotations();
    CKDrawAnnotationObjectRef owner = {}; owner.Id = 123;
    Device->SetDrawCallbackObject(&owner);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.Mesh.Id = 456; pending.PrimitiveType = VX_TRIANGLELIST;
    pending.IndexCount = 3; pending.VertexCount = 3;
    Device->SetDrawAnnotation(&pending);
    Boundary.DrawAccepted = FALSE;
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    Boundary.DrawAccepted = TRUE;
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(2, Boundary.MarkerCount); EXPECT_EQ(2, Boundary.DrawCount);
    EXPECT_EQ(CKDRAW_SOURCE_MESH, Parsed(0).Source); EXPECT_EQ(456u, Parsed(0).Mesh.Id);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, Parsed(1).Source); EXPECT_EQ(123u, Parsed(1).Object.Id);
    EXPECT_EQ(1u, Device->m_DrawAnnotationState->ExplicitCount);
    EXPECT_EQ(1u, Device->m_DrawAnnotationState->CallbackFallbackCount);
}

TEST_F(RenderContextDispatchCompletionTest, EnablingAnotherAnnotationModePreservesStateAndDisablingStartsANewEpoch) {
    EnableAnnotations();
    CKDrawAnnotationState *state = Device->m_DrawAnnotationState;
    CKDrawAnnotationObjectRef owner = {}; owner.Id = 321;
    Device->SetDrawCallbackObject(&owner);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_SPRITE);
    Device->SetDrawAnnotation(&pending);
    Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_SUBMITS);
    EXPECT_EQ(state, Device->m_DrawAnnotationState);
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(CKDRAW_SOURCE_SPRITE, Parsed(0).Source);
    Device->SetDrawAnnotation(&pending);
    Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_NONE);
    EXPECT_EQ(nullptr, Device->m_DrawAnnotationState);
    EXPECT_FALSE(Device->GetDrawCallbackObject(&owner));
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_EQ(1, Boundary.MarkerCount);
    Device->ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_MARKERS);
    EXPECT_FALSE(Device->GetDrawCallbackObject(&owner));
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(2, Boundary.MarkerCount);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, Parsed(1).Source);
    EXPECT_EQ(1u, Parsed(1).Token);
    EXPECT_EQ(0u, Device->m_DrawAnnotationState->PendingSetCount);
}

TEST_F(RenderContextDispatchCompletionTest, PendingPayloadAndCallbackOwnerRemainLocalToTheRequestingContext) {
    EnableAnnotations();
    DispatchBoundary secondBoundary;
    RCKRenderContext second(Context);
    second.m_RasterizerContext = &secondBoundary;
    second.ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_MARKERS);
    CKDrawAnnotationObjectRef owner = {}; owner.Id = 15;
    Device->SetDrawCallbackObject(&owner);
    owner.Id = 25; second.SetDrawCallbackObject(&owner);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH); pending.Mesh.Id = 35;
    pending.PrimitiveType = VX_TRIANGLELIST; pending.IndexCount = 3; pending.VertexCount = 3;
    Device->SetDrawAnnotation(&pending);
    EXPECT_TRUE(second.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    EXPECT_TRUE(CKDrawAnnotationStateHasPending(Device->m_DrawAnnotationState));
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    second.m_RasterizerContext = nullptr;
    CKDrawAnnotationParsed other = {};
    ASSERT_EQ(1, secondBoundary.MarkerCount);
    EXPECT_TRUE(CKDrawAnnotationParseLabel(secondBoundary.Markers[0], &other));
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, other.Source); EXPECT_EQ(25u, other.Object.Id);
    ASSERT_EQ(1, Boundary.MarkerCount);
    EXPECT_EQ(CKDRAW_SOURCE_MESH, Parsed(0).Source); EXPECT_EQ(35u, Parsed(0).Mesh.Id);
}
} // namespace
