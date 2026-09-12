#ifndef CKRASTERIZERDRAWMARKER_H
#define CKRASTERIZERDRAWMARKER_H

#include "CKTypes.h"

#define CKDRAW_ANNOTATION_NAME_SIZE 96
#define CKDRAW_ANNOTATION_PATH_SIZE 32
#define CKDRAW_ANNOTATION_LABEL_SIZE 512
#define CKDRAW_ANNOTATION_SOURCE_SIZE 24

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

struct CKDrawAnnotationParsed {
    CKBOOL Valid;
    CKDWORD Token;
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

const char *CKDrawAnnotationGetSourceName(CKDrawAnnotationSource Source);
CKDrawAnnotationSource CKDrawAnnotationParseSourceName(CKSTRING Source);
void CKDrawAnnotationInit(CKDrawAnnotation *Annotation,
                          CKDrawAnnotationSource Source);
void CKDrawAnnotationCopyText(char *Dst, CKDWORD DstSize, CKSTRING Src);
void CKDrawAnnotationFormatLabel(const CKDrawAnnotation *Annotation,
                                 char *Label,
                                 CKDWORD LabelSize);
CKBOOL CKDrawAnnotationParseLabel(CKSTRING Label,
                                  CKDrawAnnotationParsed *Parsed);

#endif
