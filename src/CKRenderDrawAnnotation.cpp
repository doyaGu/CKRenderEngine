#include "CKRenderDrawAnnotation.h"

#include "RCKRenderContext.h"
#include "CKObject.h"
#include "RCKRenderObject.h"

#include <cstring>

static CKDWORD CKDrawAnnotationObjectId(CKObject *Object)
{
    return Object ? (CKDWORD)Object->GetID() : 0u;
}

static CKSTRING CKDrawAnnotationObjectName(CKObject *Object)
{
    return Object && Object->GetName() ? Object->GetName() : (CKSTRING)"";
}

static void CKDrawAnnotationAssignToken(CKDrawAnnotationState *State,
                                        CKDrawAnnotation *Annotation)
{
    if (!State || !Annotation)
        return;
    if (Annotation->Token == 0) {
        ++State->NextToken;
        if (State->NextToken == 0)
            ++State->NextToken;
        Annotation->Token = State->NextToken;
    }
}

void CKDrawAnnotationStateInit(CKDrawAnnotationState *State)
{
    if (!State)
        return;
    memset(State, 0, sizeof(CKDrawAnnotationState));
}

void CKDrawAnnotationSetObject(CKDrawAnnotationObjectRef *Ref,
                               CKObject *Object)
{
    if (!Ref)
        return;
    Ref->Id = CKDrawAnnotationObjectId(Object);
    CKDrawAnnotationCopyText(Ref->Name, sizeof(Ref->Name),
                             CKDrawAnnotationObjectName(Object));
}

void CKDrawAnnotationStateSetPending(CKDrawAnnotationState *State,
                                     const CKDrawAnnotation *Annotation)
{
    if (!State || !Annotation)
        return;
    if (State->HasPending)
        ++State->PendingOverwriteCount;
    State->Pending = *Annotation;
    CKDrawAnnotationAssignToken(State, &State->Pending);
    State->HasPending = TRUE;
    ++State->PendingSetCount;
}

CKBOOL CKDrawAnnotationStateConsume(CKDrawAnnotationState *State,
                                    CKDrawAnnotation *Annotation)
{
    if (!State || !Annotation || !State->HasPending)
        return FALSE;
    *Annotation = State->Pending;
    State->HasPending = FALSE;
    CKDrawAnnotationInit(&State->Pending, CKDRAW_SOURCE_NONE);
    ++State->ExplicitCount;
    return TRUE;
}

CKBOOL CKDrawAnnotationStateHasPending(CKDrawAnnotationState *State)
{
    if (!State)
        return FALSE;
    return State->HasPending;
}

void CKDrawAnnotationStateClearPending(CKDrawAnnotationState *State)
{
    if (!State)
        return;
    State->HasPending = FALSE;
    CKDrawAnnotationInit(&State->Pending, CKDRAW_SOURCE_NONE);
}

void CKDrawAnnotationStateGetCounters(CKDrawAnnotationState *State,
                                      CKDrawAnnotationCounters *Counters)
{
    if (!State || !Counters)
        return;
    Counters->PendingSetCount = State->PendingSetCount;
    Counters->ExplicitCount = State->ExplicitCount;
    Counters->CallbackFallbackCount = State->CallbackFallbackCount;
    Counters->RawFallbackCount = State->RawFallbackCount;
    Counters->PendingOverwriteCount = State->PendingOverwriteCount;
}

void CKDrawAnnotationStateSetCallbackObject(CKDrawAnnotationState *State,
                                            const CKDrawAnnotationObjectRef *Object)
{
    if (!State)
        return;
    if (Object) {
        State->CallbackObject = *Object;
        State->HasCallbackObject = TRUE;
    } else {
        memset(&State->CallbackObject, 0, sizeof(State->CallbackObject));
        State->HasCallbackObject = FALSE;
    }
}

CKBOOL CKDrawAnnotationStateGetCallbackObject(CKDrawAnnotationState *State,
                                              CKDrawAnnotationObjectRef *Object)
{
    if (!State || !Object || !State->HasCallbackObject)
        return FALSE;
    *Object = State->CallbackObject;
    return TRUE;
}

void CKDrawAnnotationStateBuildFallback(CKDrawAnnotationState *State,
                                        CKDrawAnnotation *Annotation,
                                        VXPRIMITIVETYPE PrimitiveType,
                                        CKDWORD IndexCount,
                                        CKDWORD VertexCount)
{
    if (!Annotation)
        return;
    if (State && State->HasCallbackObject) {
        CKDrawAnnotationInit(Annotation, CKDRAW_SOURCE_CALLBACK);
        Annotation->Object = State->CallbackObject;
        ++State->CallbackFallbackCount;
    } else {
        CKDrawAnnotationInit(Annotation, CKDRAW_SOURCE_RAW_PRIMITIVE);
        if (State)
            ++State->RawFallbackCount;
    }
    Annotation->PrimitiveType = PrimitiveType;
    Annotation->IndexCount = IndexCount;
    Annotation->VertexCount = VertexCount;
    CKDrawAnnotationAssignToken(State, Annotation);
}

CKScopedDrawAnnotation::CKScopedDrawAnnotation(RCKRenderContext *Context,
                                               CKRenderObject *Object)
    : m_Context(Context), m_HadPrevious(FALSE)
{
    CKDrawAnnotationObjectRef ref;
    if (!m_Context || !m_Context->m_DrawAnnotationState) {
        m_Context = NULL;
        return;
    }
    m_HadPrevious = m_Context->GetDrawCallbackObject(&m_Previous);
    CKDrawAnnotationSetObject(&ref, (CKObject *)Object);
    m_Context->SetDrawCallbackObject(&ref);
}

CKScopedDrawAnnotation::~CKScopedDrawAnnotation()
{
    if (!m_Context)
        return;
    if (m_HadPrevious)
        m_Context->SetDrawCallbackObject(&m_Previous);
    else
        m_Context->SetDrawCallbackObject(NULL);
}
