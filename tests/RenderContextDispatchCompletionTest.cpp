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
static CKBOOL DrawWithScopedOwner(RCKRenderContext *device, RCKRenderObject *owner,
                                  VxDrawPrimitiveData *data) {
    CKScopedDrawAnnotation scope(device, owner);
    return device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, data);
}

TEST_F(RenderContextDispatchCompletionTest, ScopedOwnerRestoresThePreviousSnapshotAfterARejectedDrawReturns) {
    EnableAnnotations();
    CKDrawAnnotationObjectRef previous = {};
    previous.Id = 701;
    CKDrawAnnotationCopyText(previous.Name, sizeof(previous.Name), "previous owner");
    Device->SetDrawCallbackObject(&previous);
    RCK3dEntity owner(Context, (CKSTRING)"temporary owner");
    Boundary.DrawAccepted = FALSE;
    EXPECT_FALSE(DrawWithScopedOwner(Device, &owner, &Data));
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    CKDrawAnnotationObjectRef restored = {};
    ASSERT_TRUE(Device->GetDrawCallbackObject(&restored));
    EXPECT_EQ(701u, restored.Id);
    EXPECT_STREQ("previous_owner", restored.Name);
    previous.Id = 999;
    previous.Name[0] = 'X';
    Boundary.DrawAccepted = TRUE;
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(2, Boundary.MarkerCount);
    EXPECT_EQ(owner.GetID(), Parsed(0).Object.Id);
    EXPECT_STREQ("temporary_owner", Parsed(0).Object.Name);
    EXPECT_EQ(701u, Parsed(1).Object.Id);
    EXPECT_STREQ("previous_owner", Parsed(1).Object.Name);
    EXPECT_EQ(2, Boundary.DrawCount);
    EXPECT_FLOAT_EQ(9, Boundary.LastVertex.z);
}

TEST_F(RenderContextDispatchCompletionTest, InactiveScopeDoesNotEnableDiagnosticsOrRestoreIntoALaterEnabledState) {
    ASSERT_EQ(nullptr, Device->m_DrawAnnotationState);
    RCK3dEntity owner(Context, (CKSTRING)"inactive owner");
    {
        CKScopedDrawAnnotation nullContext(nullptr, &owner);
        CKScopedDrawAnnotation inactive(Device, &owner);
        EXPECT_EQ(nullptr, Device->m_DrawAnnotationState);
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
        EXPECT_EQ(0, Boundary.MarkerCount);
        EnableAnnotations();
        CKDrawAnnotationObjectRef later = {};
        later.Id = 702;
        CKDrawAnnotationCopyText(later.Name, sizeof(later.Name), "later owner");
        Device->SetDrawCallbackObject(&later);
    }
    CKDrawAnnotationObjectRef current = {};
    ASSERT_TRUE(Device->GetDrawCallbackObject(&current));
    EXPECT_EQ(702u, current.Id);
    EXPECT_STREQ("later_owner", current.Name);
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(1, Boundary.MarkerCount);
    EXPECT_EQ(702u, Parsed(0).Object.Id);
    EXPECT_EQ(2, Boundary.DrawCount);
    EXPECT_FLOAT_EQ(9, Boundary.LastVertex.z);
}

TEST_F(RenderContextDispatchCompletionTest, OverlappingScopesOnDistinctContextsRestoreOnlyTheirOwnOwners) {
    EnableAnnotations();
    DispatchBoundary otherBoundary;
    RCKRenderContext other(Context);
    other.ApplyDrawAnnotationDebugFlags(CKRST_DEBUG_DRAWMAP_MARKERS);
    RCK3dEntity first(Context, (CKSTRING)"first scoped owner");
    RCK3dEntity second(Context, (CKSTRING)"second scoped owner");
    CKDrawAnnotationObjectRef previous = {};
    previous.Id = 703;
    Device->SetDrawCallbackObject(&previous);
    other.m_RasterizerContext = &otherBoundary;
    {
        CKScopedDrawAnnotation outer(Device, &first);
        {
            CKScopedDrawAnnotation inner(&other, &second);
            EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
            EXPECT_TRUE(other.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
        }
        EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
        EXPECT_TRUE(other.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    }
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    other.m_RasterizerContext = nullptr;
    ASSERT_EQ(3, Boundary.MarkerCount);
    EXPECT_EQ(first.GetID(), Parsed(0).Object.Id);
    EXPECT_EQ(first.GetID(), Parsed(1).Object.Id);
    EXPECT_EQ(703u, Parsed(2).Object.Id);
    ASSERT_EQ(2, otherBoundary.MarkerCount);
    CKDrawAnnotationParsed otherFirst = {}, otherLast = {};
    ASSERT_TRUE(CKDrawAnnotationParseLabel(otherBoundary.Markers[0], &otherFirst));
    ASSERT_TRUE(CKDrawAnnotationParseLabel(otherBoundary.Markers[1], &otherLast));
    EXPECT_EQ(second.GetID(), otherFirst.Object.Id);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, otherLast.Source);
    EXPECT_EQ(2, otherBoundary.DrawCount);
}

TEST_F(RenderContextDispatchCompletionTest, NullObjectScopeKeepsCallbackAttributionAndRestoresItsOuterOwner) {
    EnableAnnotations();
    RCK3dEntity owner(Context, (CKSTRING)"named outer owner");
    {
        CKScopedDrawAnnotation outer(Device, &owner);
        {
            CKScopedDrawAnnotation unnamed(Device, nullptr);
            CKDrawAnnotationObjectRef current = {};
            ASSERT_TRUE(Device->GetDrawCallbackObject(&current));
            EXPECT_EQ(0u, current.Id);
            EXPECT_STREQ("", current.Name);
            EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
        }
        EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    }
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(3, Boundary.MarkerCount);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, Parsed(0).Source);
    EXPECT_EQ(0u, Parsed(0).Object.Id);
    EXPECT_EQ(owner.GetID(), Parsed(1).Object.Id);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, Parsed(2).Source);
}

struct ScopedEntityDraws {
    VxDrawPrimitiveData *Data;
    int Count = 0;
    int Phases[3] = {};
    CKRenderObject *Objects[3] = {};
    CKRenderContext *Contexts[3] = {};
};
struct ScopedEntityPhase {
    ScopedEntityDraws *Draws;
    int Phase;
};
static CKBOOL DrawFromActualEntityScope(CKRenderContext *context, CKRenderObject *object, void *argument) {
    ScopedEntityPhase *phase = static_cast<ScopedEntityPhase *>(argument);
    ScopedEntityDraws *draws = phase->Draws;
    EXPECT_LT(draws->Count, 3);
    if (draws->Count >= 3) return FALSE;
    const int index = draws->Count++;
    draws->Phases[index] = phase->Phase;
    draws->Objects[index] = object;
    draws->Contexts[index] = context;
    return static_cast<RCKRenderContext *>(context)->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, draws->Data);
}

TEST_F(RenderContextDispatchCompletionTest, ActualEntityPreMainAndPostCallbacksKeepOrderArgumentsAndScopedOwner) {
    EnableAnnotations();
    RCK3dEntity owner(Context, (CKSTRING)"actual entity owner");
    owner.m_MoveableFlags |= VX_MOVEABLE_EXTENTSUPTODATE;
    ScopedEntityDraws draws = {&Data};
    ScopedEntityPhase phases[] = {{&draws, 1}, {&draws, 2}, {&draws, 3}};
    ASSERT_TRUE(owner.AddPreRenderCallBack(DrawFromActualEntityScope, &phases[0], FALSE));
    ASSERT_TRUE(owner.SetRenderCallBack(DrawFromActualEntityScope, &phases[1]));
    ASSERT_TRUE(owner.AddPostRenderCallBack(DrawFromActualEntityScope, &phases[2], FALSE));
    EXPECT_TRUE(owner.Render(Device, CK_RENDER_CLEARVIEWPORT));
    ASSERT_EQ(3, draws.Count);
    ASSERT_EQ(3, Boundary.DrawCount);
    ASSERT_EQ(3, Boundary.MarkerCount);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(i + 1, draws.Phases[i]);
        EXPECT_EQ(&owner, draws.Objects[i]);
        EXPECT_EQ(Device, draws.Contexts[i]);
        EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, Parsed(i).Source);
        EXPECT_EQ(owner.GetID(), Parsed(i).Object.Id);
        EXPECT_STREQ("actual_entity_owner", Parsed(i).Object.Name);
    }
    CKDrawAnnotationObjectRef after = {};
    EXPECT_FALSE(Device->GetDrawCallbackObject(&after));
    EXPECT_EQ(nullptr, Device->m_Current3dEntity);
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &Data));
    ASSERT_EQ(4, Boundary.MarkerCount);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, Parsed(3).Source);
    EXPECT_FLOAT_EQ(9, Boundary.LastVertex.z);
}
static void ExpectAnnotationCounters(CKDrawAnnotationState &state, CKDWORD sets, CKDWORD consumed,
                                     CKDWORD callbacks, CKDWORD raw, CKDWORD overwritten) {
    CKDrawAnnotationCounters counters = {};
    CKDrawAnnotationStateGetCounters(&state, &counters);
    EXPECT_EQ(sets, counters.PendingSetCount);
    EXPECT_EQ(consumed, counters.ExplicitCount);
    EXPECT_EQ(callbacks, counters.CallbackFallbackCount);
    EXPECT_EQ(raw, counters.RawFallbackCount);
    EXPECT_EQ(overwritten, counters.PendingOverwriteCount);
}

TEST(DrawAnnotationStateTest, ReinitializingPopulatedStateClearsPayloadOwnerTokensAndAllCounters) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.Mesh.Id = 805;
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationObjectRef owner = {};
    owner.Id = 806;
    CKDrawAnnotationCopyText(owner.Name, sizeof(owner.Name), "reset_owner");
    CKDrawAnnotationStateSetCallbackObject(&state, &owner);
    CKDrawAnnotation output;
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_TRIANGLELIST, 3, 9);
    CKDrawAnnotationStateSetCallbackObject(&state, nullptr);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_LINELIST, 4, 7);
    ExpectAnnotationCounters(state, 2, 1, 1, 1, 1);
    CKDrawAnnotationStateSetCallbackObject(&state, &owner);
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationStateInit(&state);
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(&state));
    EXPECT_FALSE(CKDrawAnnotationStateGetCallbackObject(&state, &owner));
    EXPECT_EQ(0u, state.NextToken);
    EXPECT_EQ(CKDRAW_SOURCE_NONE, state.Pending.Source);
    EXPECT_EQ(0u, state.Pending.Mesh.Id);
    EXPECT_EQ(0u, state.CallbackObject.Id);
    EXPECT_STREQ("", state.CallbackObject.Name);
    ExpectAnnotationCounters(state, 0, 0, 0, 0, 0);
    CKDrawAnnotationStateSetPending(&state, &pending);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(1u, output.Token);
    EXPECT_EQ(805u, output.Mesh.Id);
    ExpectAnnotationCounters(state, 1, 1, 0, 0, 0);
}

TEST(DrawAnnotationStateTest, PendingReplacementCopiesTheWholePayloadAndClearDoesNotCountAsConsumption) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_SPRITE);
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.PrimitiveType = VX_TRIANGLESTRIP;
    pending.IndexCount = 12; pending.VertexCount = 8;
    pending.Mesh.Id = 11; pending.Entity.Id = 13; pending.Material.Id = 17;
    pending.GroupIndex = 19; pending.PrimitiveIndex = 23;
    CKDrawAnnotationCopyText(pending.Mesh.Name, sizeof(pending.Mesh.Name), "copied_mesh");
    CKDrawAnnotationCopyText(pending.Path, sizeof(pending.Path), "test_path");
    CKDrawAnnotationStateSetPending(&state, &pending);
    pending.Mesh.Id = 999;
    pending.Mesh.Name[0] = 'X'; pending.Path[0] = 'X';
    CKDrawAnnotation output;
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(CKDRAW_SOURCE_MESH, output.Source);
    EXPECT_EQ(VX_TRIANGLESTRIP, output.PrimitiveType);
    EXPECT_EQ(12u, output.IndexCount); EXPECT_EQ(8u, output.VertexCount);
    EXPECT_EQ(11u, output.Mesh.Id); EXPECT_EQ(13u, output.Entity.Id); EXPECT_EQ(17u, output.Material.Id);
    EXPECT_EQ(19, output.GroupIndex); EXPECT_EQ(23, output.PrimitiveIndex);
    EXPECT_STREQ("copied_mesh", output.Mesh.Name); EXPECT_STREQ("test_path", output.Path);
    EXPECT_EQ(2u, output.Token);
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(&state));
    EXPECT_EQ(CKDRAW_SOURCE_NONE, state.Pending.Source);
    EXPECT_EQ(0u, state.Pending.Token);
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationStateClearPending(&state);
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(&state));
    EXPECT_FALSE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(2u, output.Token) << "Failed consume must not change the caller's output";
    EXPECT_EQ(3u, state.NextToken);
    ExpectAnnotationCounters(state, 3, 1, 0, 0, 1);
}

TEST(DrawAnnotationStateTest, AutomaticTokenWrapSkipsZeroAndSuppliedTokenDoesNotAdvanceTheGenerator) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    state.NextToken = 0xfffffffeu;
    CKDrawAnnotation pending, output;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    CKDrawAnnotationStateSetPending(&state, &pending);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(0xffffffffu, output.Token);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_TRIANGLELIST, 3, 3);
    EXPECT_EQ(1u, output.Token);
    pending.Token = 400;
    CKDrawAnnotationStateSetPending(&state, &pending);
    EXPECT_EQ(1u, state.NextToken);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(400u, output.Token);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_POINTLIST, 2, 2);
    EXPECT_EQ(2u, output.Token);
    ExpectAnnotationCounters(state, 2, 2, 0, 2, 0);
}

TEST(DrawAnnotationStateTest, FallbackCopiesCallbackIdentityWithoutConsumingOrReplacingTheQueuedPayload) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotationObjectRef owner = {};
    owner.Id = 31;
    CKDrawAnnotationCopyText(owner.Name, sizeof(owner.Name), "copied_owner");
    CKDrawAnnotationStateSetCallbackObject(&state, &owner);
    owner.Id = 99; owner.Name[0] = 'X';
    CKDrawAnnotationObjectRef copy = {};
    ASSERT_TRUE(CKDrawAnnotationStateGetCallbackObject(&state, &copy));
    EXPECT_EQ(31u, copy.Id); EXPECT_STREQ("copied_owner", copy.Name);
    copy.Id = 101; copy.Name[0] = 'Y';
    CKDrawAnnotation pending, output;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.Token = 100; pending.Mesh.Id = 41;
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_TRIANGLEFAN, 4, 6);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, output.Source);
    EXPECT_EQ(31u, output.Object.Id); EXPECT_STREQ("copied_owner", output.Object.Name);
    EXPECT_EQ(VX_TRIANGLEFAN, output.PrimitiveType);
    EXPECT_EQ(4u, output.IndexCount); EXPECT_EQ(6u, output.VertexCount);
    EXPECT_TRUE(CKDrawAnnotationStateHasPending(&state));
    CKDrawAnnotationStateSetCallbackObject(&state, nullptr);
    EXPECT_FALSE(CKDrawAnnotationStateGetCallbackObject(&state, &copy));
    EXPECT_EQ(101u, copy.Id);
    EXPECT_EQ(0u, state.CallbackObject.Id); EXPECT_STREQ("", state.CallbackObject.Name);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_LINELIST, 8, 9);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, output.Source);
    EXPECT_EQ(0u, output.Object.Id);
    EXPECT_EQ(8u, output.IndexCount); EXPECT_EQ(9u, output.VertexCount);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(CKDRAW_SOURCE_MESH, output.Source);
    EXPECT_EQ(100u, output.Token); EXPECT_EQ(41u, output.Mesh.Id);
    ExpectAnnotationCounters(state, 1, 1, 1, 1, 0);
}

TEST_F(RenderContextDispatchCompletionTest, ObjectReferenceCaptureCopiesLongNamesAndHandlesUnnamedAndNullObjects) {
    char name[160];
    for (int i = 0; i < 159; ++i) name[i] = 'a' + i % 26;
    name[17] = ' '; name[159] = '\0';
    RCK3dEntity owner(Context, name), unnamed(Context);
    CKDrawAnnotationObjectRef ref = {};
    CKDrawAnnotationSetObject(&ref, &owner);
    EXPECT_EQ(owner.GetID(), ref.Id);
    EXPECT_EQ(95u, std::strlen(ref.Name));
    EXPECT_EQ('_', ref.Name[17]);
    EXPECT_EQ('\0', ref.Name[95]);
    for (int i = 0; i < 95; ++i) EXPECT_EQ(i == 17 ? '_' : name[i], ref.Name[i]);
    owner.SetName((CKSTRING)"changed_owner");
    EXPECT_EQ('a', ref.Name[0]);
    EXPECT_EQ(95u, std::strlen(ref.Name));
    CKDrawAnnotationSetObject(&ref, &unnamed);
    EXPECT_EQ(unnamed.GetID(), ref.Id); EXPECT_STREQ("", ref.Name);
    CKDrawAnnotationSetObject(&ref, nullptr);
    EXPECT_EQ(0u, ref.Id); EXPECT_STREQ("", ref.Name);
}

TEST(DrawAnnotationStateTest, CounterSnapshotsAndImplicitStateCopiesDoNotShareMutableStorage) {
    CKDrawAnnotationState state, other;
    CKDrawAnnotationStateInit(&state); CKDrawAnnotationStateInit(&other);
    state.PendingSetCount = 11; state.ExplicitCount = 13;
    state.CallbackFallbackCount = 17; state.RawFallbackCount = 19; state.PendingOverwriteCount = 23;
    state.NextToken = 29; state.Pending.Mesh.Id = 31;
    CKDrawAnnotationCounters counters = {};
    CKDrawAnnotationStateGetCounters(&state, &counters);
    EXPECT_EQ(11u, counters.PendingSetCount); EXPECT_EQ(13u, counters.ExplicitCount);
    EXPECT_EQ(17u, counters.CallbackFallbackCount); EXPECT_EQ(19u, counters.RawFallbackCount);
    EXPECT_EQ(23u, counters.PendingOverwriteCount);
    CKDrawAnnotationCounters copied = counters;
    counters.PendingSetCount = 999; counters.RawFallbackCount = 998;
    EXPECT_EQ(11u, copied.PendingSetCount); EXPECT_EQ(19u, copied.RawFallbackCount);
    ExpectAnnotationCounters(state, 11, 13, 17, 19, 23);
    CKDrawAnnotationState snapshot = state;
    snapshot.Pending.Mesh.Id = 99; snapshot.NextToken = 98; snapshot.ExplicitCount = 97;
    EXPECT_EQ(31u, state.Pending.Mesh.Id); EXPECT_EQ(29u, state.NextToken);
    EXPECT_EQ(13u, state.ExplicitCount);
    CKDrawAnnotation pending;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_SPRITE);
    CKDrawAnnotationStateSetPending(&other, &pending);
    EXPECT_EQ(1u, other.NextToken); EXPECT_EQ(29u, state.NextToken);
    ExpectAnnotationCounters(other, 1, 0, 0, 0, 0);
}

TEST(DrawAnnotationStateTest, CounterOverflowUsesIndependentUnsignedDwordArithmetic) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotation pending, output;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    CKDrawAnnotationStateSetPending(&state, &pending);
    state.PendingSetCount = state.ExplicitCount = state.CallbackFallbackCount = 0xffffffffu;
    state.RawFallbackCount = state.PendingOverwriteCount = 0xffffffffu;
    CKDrawAnnotationStateSetPending(&state, &pending);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    CKDrawAnnotationObjectRef owner = {};
    CKDrawAnnotationStateSetCallbackObject(&state, &owner);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_POINTLIST, 1, 1);
    EXPECT_EQ(CKDRAW_SOURCE_CALLBACK, output.Source);
    EXPECT_EQ(0u, output.Object.Id) << "Present anonymous callback is distinct from no callback";
    CKDrawAnnotationStateSetCallbackObject(&state, nullptr);
    CKDrawAnnotationStateBuildFallback(&state, &output, VX_POINTLIST, 1, 1);
    ExpectAnnotationCounters(state, 0, 0, 0, 0, 0);
}

TEST(DrawAnnotationStateTest, MissingInputsAndOutputsLeaveQueuedStateAndCallerSnapshotsIntact) {
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotation pending, output;
    CKDrawAnnotationInit(&pending, CKDRAW_SOURCE_MESH);
    pending.Mesh.Id = 51;
    CKDrawAnnotationStateSetPending(&state, &pending);
    CKDrawAnnotationStateSetPending(&state, nullptr);
    CKDrawAnnotationStateSetPending(nullptr, &pending);
    EXPECT_FALSE(CKDrawAnnotationStateConsume(&state, nullptr));
    EXPECT_TRUE(CKDrawAnnotationStateHasPending(&state));
    CKDrawAnnotationStateBuildFallback(&state, nullptr, VX_POINTLIST, 1, 1);
    EXPECT_EQ(1u, state.NextToken);
    ExpectAnnotationCounters(state, 1, 0, 0, 0, 0);
    CKDrawAnnotationCounters counters = {};
    counters.PendingSetCount = 61;
    CKDrawAnnotationStateGetCounters(nullptr, &counters);
    CKDrawAnnotationStateGetCounters(&state, nullptr);
    EXPECT_EQ(61u, counters.PendingSetCount);
    CKDrawAnnotationObjectRef owner = {}; owner.Id = 67;
    CKDrawAnnotationStateSetCallbackObject(&state, &owner);
    EXPECT_FALSE(CKDrawAnnotationStateGetCallbackObject(&state, nullptr));
    EXPECT_FALSE(CKDrawAnnotationStateGetCallbackObject(nullptr, &owner));
    EXPECT_EQ(67u, owner.Id);
    CKDrawAnnotationSetObject(nullptr, nullptr);
    CKDrawAnnotationStateInit(nullptr);
    CKDrawAnnotationStateClearPending(nullptr);
    CKDrawAnnotationStateSetCallbackObject(nullptr, &owner);
    EXPECT_FALSE(CKDrawAnnotationStateHasPending(nullptr));
    CKDrawAnnotationInit(&output, CKDRAW_SOURCE_SPRITE); output.Token = 71;
    EXPECT_FALSE(CKDrawAnnotationStateConsume(nullptr, &output));
    EXPECT_EQ(71u, output.Token); EXPECT_EQ(CKDRAW_SOURCE_SPRITE, output.Source);
    CKDrawAnnotationStateBuildFallback(nullptr, &output, VX_LINESTRIP, 7, 8);
    EXPECT_EQ(CKDRAW_SOURCE_RAW_PRIMITIVE, output.Source); EXPECT_EQ(0u, output.Token);
    EXPECT_EQ(7u, output.IndexCount); EXPECT_EQ(8u, output.VertexCount);
    ASSERT_TRUE(CKDrawAnnotationStateConsume(&state, &output));
    EXPECT_EQ(51u, output.Mesh.Id); EXPECT_EQ(1u, output.Token);
    ExpectAnnotationCounters(state, 1, 1, 0, 0, 0);
}
} // namespace
