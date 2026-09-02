#ifndef CKDRAWANNOTATION_H
#define CKDRAWANNOTATION_H

#include "CKRasterizer.h"

#define CKDRAW_ANNOTATION_NAME_SIZE 96
#define CKDRAW_ANNOTATION_PATH_SIZE 32
#define CKDRAW_ANNOTATION_LABEL_SIZE 512
#define CKDRAW_ANNOTATION_SOURCE_SIZE 24

class CKObject;
class CKRenderObject;
class RCKRenderContext;

typedef enum CKDrawAnnotationSource {
    CKDRAW_SOURCE_NONE = 0,
    CKDRAW_SOURCE_MESH,
    CKDRAW_SOURCE_2D_ENTITY,
    CKDRAW_SOURCE_SPRITE,
    CKDRAW_SOURCE_CALLBACK,
    CKDRAW_SOURCE_RAW_PRIMITIVE,
    CKDRAW_SOURCE_COUNT
} CKDrawAnnotationSource;

struct CKDrawAnnotationObjectRef {
    CKDWORD Id;
    char Name[CKDRAW_ANNOTATION_NAME_SIZE];
};

struct CKDrawAnnotation {
    CKDrawAnnotationSource Source;
    CKRenderView View;
    VXPRIMITIVETYPE PrimitiveType;
    CKDWORD IndexCount;
    CKDWORD VertexCount;
    CKDWORD Token;
    CKDrawAnnotationObjectRef Object;
    CKDrawAnnotationObjectRef Entity;
    CKDrawAnnotationObjectRef Mesh;
    CKDrawAnnotationObjectRef Material;
    int GroupIndex;
    int PrimitiveIndex;
    char Path[CKDRAW_ANNOTATION_PATH_SIZE];
};

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

struct CKDrawAnnotationParsed {
    CKBOOL Valid;
    CKDWORD Token;
    CKRenderView View;
    VXPRIMITIVETYPE PrimitiveType;
    CKDWORD IndexCount;
    CKDWORD VertexCount;
    CKDrawAnnotationSource Source;
    CKDrawAnnotationObjectRef Object;
    CKDrawAnnotationObjectRef Entity;
    CKDrawAnnotationObjectRef Mesh;
    CKDrawAnnotationObjectRef Material;
    int GroupIndex;
    int PrimitiveIndex;
    char SourceName[CKDRAW_ANNOTATION_SOURCE_SIZE];
    char Path[CKDRAW_ANNOTATION_PATH_SIZE];
};

struct CKDrawAnnotationCounters {
    CKDWORD PendingSetCount;
    CKDWORD ExplicitCount;
    CKDWORD CallbackFallbackCount;
    CKDWORD RawFallbackCount;
    CKDWORD PendingOverwriteCount;
};

const char *CKDrawAnnotationGetSourceName(CKDrawAnnotationSource Source);
CKDrawAnnotationSource CKDrawAnnotationParseSourceName(CKSTRING Source);
void CKDrawAnnotationInit(CKDrawAnnotation *Annotation,
                          CKDrawAnnotationSource Source);
void CKDrawAnnotationStateInit(CKDrawAnnotationState *State);
void CKDrawAnnotationCopyText(char *Dst, CKDWORD DstSize, CKSTRING Src);
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
                                        CKRenderView View,
                                        VXPRIMITIVETYPE PrimitiveType,
                                        CKDWORD IndexCount,
                                        CKDWORD VertexCount);
void CKDrawAnnotationFormatLabel(const CKDrawAnnotation *Annotation,
                                 char *Label,
                                 CKDWORD LabelSize);
CKBOOL CKDrawAnnotationParseLabel(CKSTRING Label,
                                  CKDrawAnnotationParsed *Parsed);

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
