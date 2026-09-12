#ifndef CKRENDERDRAWANNOTATION_H
#define CKRENDERDRAWANNOTATION_H

#include "CKRasterizerDrawMarker.h"

class CKObject;
class CKRenderObject;
class RCKRenderContext;

struct CKDrawAnnotationState {
    CKBOOL HasPending;
    CKBOOL HasCallbackObject;
    CKDWORD NextToken;
    CKDrawAnnotation Pending;
    CKDrawAnnotationObjectRef CallbackObject;
    CKDWORD PendingSetCount;
    CKDWORD ExplicitCount;
    CKDWORD CallbackFallbackCount;
    CKDWORD RawFallbackCount;
    CKDWORD PendingOverwriteCount;
};

struct CKDrawAnnotationCounters {
    CKDWORD PendingSetCount;
    CKDWORD ExplicitCount;
    CKDWORD CallbackFallbackCount;
    CKDWORD RawFallbackCount;
    CKDWORD PendingOverwriteCount;
};

void CKDrawAnnotationStateInit(CKDrawAnnotationState *State);
void CKDrawAnnotationSetObject(CKDrawAnnotationObjectRef *Ref,
                               CKObject *Object);
void CKDrawAnnotationStateSetPending(CKDrawAnnotationState *State,
                                     const CKDrawAnnotation *Annotation);
CKBOOL CKDrawAnnotationStateConsume(CKDrawAnnotationState *State,
                                    CKDrawAnnotation *Annotation);
CKBOOL CKDrawAnnotationStateHasPending(CKDrawAnnotationState *State);
void CKDrawAnnotationStateClearPending(CKDrawAnnotationState *State);
void CKDrawAnnotationStateGetCounters(CKDrawAnnotationState *State,
                                      CKDrawAnnotationCounters *Counters);
void CKDrawAnnotationStateSetCallbackObject(CKDrawAnnotationState *State,
                                            const CKDrawAnnotationObjectRef *Object);
CKBOOL CKDrawAnnotationStateGetCallbackObject(CKDrawAnnotationState *State,
                                              CKDrawAnnotationObjectRef *Object);
void CKDrawAnnotationStateBuildFallback(CKDrawAnnotationState *State,
                                        CKDrawAnnotation *Annotation,
                                        VXPRIMITIVETYPE PrimitiveType,
                                        CKDWORD IndexCount,
                                        CKDWORD VertexCount);

class CKScopedDrawAnnotation {
public:
    CKScopedDrawAnnotation(RCKRenderContext *Context, CKRenderObject *Object);
    ~CKScopedDrawAnnotation();

private:
    RCKRenderContext *m_Context;
    CKBOOL m_HadPrevious;
    CKDrawAnnotationObjectRef m_Previous;
};

#endif
