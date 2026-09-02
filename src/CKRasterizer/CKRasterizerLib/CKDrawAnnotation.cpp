#include "CKDrawAnnotation.h"

#include "RCKRenderContext.h"
#include "CKObject.h"
#include "RCKRenderObject.h"

#include <cstdio>
#include <cstring>

static CKDWORD CKDrawAnnotationObjectId(CKObject *Object)
{
    return Object ? (CKDWORD)Object->GetID() : 0u;
}

static CKSTRING CKDrawAnnotationObjectName(CKObject *Object)
{
    return Object && Object->GetName() ? Object->GetName() : (CKSTRING)"";
}

static void CKDrawAnnotationAppendChar(char *Dst, CKDWORD DstSize,
                                       CKDWORD *Offset, char Value)
{
    if (!Dst || !Offset || DstSize == 0)
        return;
    if (*Offset + 1 >= DstSize)
        return;
    Dst[*Offset] = Value;
    ++(*Offset);
    Dst[*Offset] = '\0';
}

static void CKDrawAnnotationAppendText(char *Dst, CKDWORD DstSize,
                                       CKDWORD *Offset, const char *Text)
{
    if (!Dst || !Offset || DstSize == 0 || !Text)
        return;
    while (*Text && *Offset + 1 < DstSize) {
        CKDrawAnnotationAppendChar(Dst, DstSize, Offset, *Text);
        ++Text;
    }
}

static void CKDrawAnnotationAppendSanitized(char *Dst, CKDWORD DstSize,
                                            CKDWORD *Offset, const char *Text)
{
    if (!Dst || !Offset || DstSize == 0 || !Text)
        return;
    while (*Text && *Offset + 1 < DstSize) {
        char c = *Text;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '"' || c == '\'' || c == '=')
            c = '_';
        CKDrawAnnotationAppendChar(Dst, DstSize, Offset, c);
        ++Text;
    }
}

static void CKDrawAnnotationAppendObject(char *Dst, CKDWORD DstSize,
                                         CKDWORD *Offset,
                                         const char *Field,
                                         const CKDrawAnnotationObjectRef *Ref)
{
    char id[32];
    if (!Ref)
        return;
    CKDrawAnnotationAppendText(Dst, DstSize, Offset, " ");
    CKDrawAnnotationAppendText(Dst, DstSize, Offset, Field);
    CKDrawAnnotationAppendText(Dst, DstSize, Offset, "=");
    snprintf(id, sizeof(id), "%u:", (unsigned)Ref->Id);
    CKDrawAnnotationAppendText(Dst, DstSize, Offset, id);
    CKDrawAnnotationAppendSanitized(Dst, DstSize, Offset, Ref->Name);
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

void CKDrawAnnotationInit(CKDrawAnnotation *Annotation,
                          CKDrawAnnotationSource Source)
{
    if (!Annotation)
        return;
    memset(Annotation, 0, sizeof(CKDrawAnnotation));
    Annotation->Source = Source;
    Annotation->GroupIndex = -1;
    Annotation->PrimitiveIndex = -1;
}

void CKDrawAnnotationStateInit(CKDrawAnnotationState *State)
{
    if (!State)
        return;
    memset(State, 0, sizeof(CKDrawAnnotationState));
}

void CKDrawAnnotationCopyText(char *Dst, CKDWORD DstSize, CKSTRING Src)
{
    CKDWORD offset = 0;
    if (!Dst || DstSize == 0)
        return;
    Dst[0] = '\0';
    CKDrawAnnotationAppendSanitized(Dst, DstSize, &offset,
                                    Src ? Src : (CKSTRING)"");
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
                                        CKRenderView View,
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
    Annotation->View = View;
    Annotation->PrimitiveType = PrimitiveType;
    Annotation->IndexCount = IndexCount;
    Annotation->VertexCount = VertexCount;
    CKDrawAnnotationAssignToken(State, Annotation);
}

void CKDrawAnnotationFormatLabel(const CKDrawAnnotation *Annotation,
                                 char *Label,
                                 CKDWORD LabelSize)
{
    CKDWORD offset = 0;
    char value[64];
    if (!Label || LabelSize == 0)
        return;
    Label[0] = '\0';
    if (!Annotation)
        return;

    CKDrawAnnotationAppendText(Label, LabelSize, &offset, "CKDrawV1 source=");
    CKDrawAnnotationAppendText(Label, LabelSize, &offset,
                               CKDrawAnnotationGetSourceName(Annotation->Source));
    snprintf(value, sizeof(value), " token=%u view=%u type=%d indices=%u verts=%u",
             (unsigned)Annotation->Token,
             (unsigned)Annotation->View,
             (int)Annotation->PrimitiveType,
             (unsigned)Annotation->IndexCount,
             (unsigned)Annotation->VertexCount);
    CKDrawAnnotationAppendText(Label, LabelSize, &offset, value);

    if (Annotation->Path[0]) {
        CKDrawAnnotationAppendText(Label, LabelSize, &offset, " path=");
        CKDrawAnnotationAppendSanitized(Label, LabelSize, &offset, Annotation->Path);
    }

    CKDrawAnnotationAppendObject(Label, LabelSize, &offset, "object",
                                 &Annotation->Object);
    CKDrawAnnotationAppendObject(Label, LabelSize, &offset, "entity",
                                 &Annotation->Entity);
    CKDrawAnnotationAppendObject(Label, LabelSize, &offset, "mesh",
                                 &Annotation->Mesh);
    CKDrawAnnotationAppendObject(Label, LabelSize, &offset, "material",
                                 &Annotation->Material);

    if (Annotation->GroupIndex >= 0) {
        snprintf(value, sizeof(value), " group=%d", Annotation->GroupIndex);
        CKDrawAnnotationAppendText(Label, LabelSize, &offset, value);
    }
    if (Annotation->PrimitiveIndex >= 0) {
        snprintf(value, sizeof(value), " prim=%d", Annotation->PrimitiveIndex);
        CKDrawAnnotationAppendText(Label, LabelSize, &offset, value);
    }
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
