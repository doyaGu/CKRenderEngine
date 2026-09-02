#include "CKDrawAnnotation.h"

#include <cstring>

const char *CKDrawAnnotationGetSourceName(CKDrawAnnotationSource Source)
{
    switch (Source) {
    case CKDRAW_SOURCE_MESH:          return "Mesh";
    case CKDRAW_SOURCE_2D_ENTITY:     return "2D";
    case CKDRAW_SOURCE_SPRITE:        return "Sprite";
    case CKDRAW_SOURCE_CALLBACK:      return "Callback";
    case CKDRAW_SOURCE_RAW_PRIMITIVE: return "RawPrimitive";
    default:                          return "None";
    }
}

static int CKDrawAnnotationTextEquals(CKSTRING A, CKSTRING B)
{
    if (!A || !B)
        return 0;
    return strcmp(A, B) == 0;
}

CKDrawAnnotationSource CKDrawAnnotationParseSourceName(CKSTRING Source)
{
    if (CKDrawAnnotationTextEquals(Source, "Mesh"))
        return CKDRAW_SOURCE_MESH;
    if (CKDrawAnnotationTextEquals(Source, "2D"))
        return CKDRAW_SOURCE_2D_ENTITY;
    if (CKDrawAnnotationTextEquals(Source, "Sprite"))
        return CKDRAW_SOURCE_SPRITE;
    if (CKDrawAnnotationTextEquals(Source, "Callback"))
        return CKDRAW_SOURCE_CALLBACK;
    if (CKDrawAnnotationTextEquals(Source, "RawPrimitive"))
        return CKDRAW_SOURCE_RAW_PRIMITIVE;
    return CKDRAW_SOURCE_NONE;
}

static void CKDrawAnnotationParsedInit(CKDrawAnnotationParsed *Parsed)
{
    if (!Parsed)
        return;
    memset(Parsed, 0, sizeof(CKDrawAnnotationParsed));
    Parsed->PrimitiveIndex = -1;
    Parsed->GroupIndex = -1;
}

static CKBOOL CKDrawAnnotationParseUnsigned(CKSTRING Text, CKDWORD *Value)
{
    CKDWORD result = 0;
    if (!Text || !Value || !*Text)
        return FALSE;
    while (*Text) {
        if (*Text < '0' || *Text > '9')
            return FALSE;
        result = result * 10u + (CKDWORD)(*Text - '0');
        ++Text;
    }
    *Value = result;
    return TRUE;
}

static CKBOOL CKDrawAnnotationParseSigned(CKSTRING Text, int *Value)
{
    int sign = 1;
    int result = 0;
    if (!Text || !Value || !*Text)
        return FALSE;
    if (*Text == '-') {
        sign = -1;
        ++Text;
    }
    if (!*Text)
        return FALSE;
    while (*Text) {
        if (*Text < '0' || *Text > '9')
            return FALSE;
        result = result * 10 + (int)(*Text - '0');
        ++Text;
    }
    *Value = result * sign;
    return TRUE;
}

static void CKDrawAnnotationCopyParsedText(char *Dst, CKDWORD DstSize,
                                           CKSTRING Src)
{
    CKDWORD offset = 0;
    if (!Dst || DstSize == 0)
        return;
    Dst[0] = '\0';
    if (!Src)
        return;
    while (*Src && offset + 1 < DstSize) {
        Dst[offset] = *Src;
        ++offset;
        ++Src;
    }
    Dst[offset] = '\0';
}

static void CKDrawAnnotationParseObjectRef(CKSTRING Text,
                                           CKDrawAnnotationObjectRef *Ref)
{
    char idText[32];
    CKDWORD idOffset = 0;
    CKDWORD id = 0;
    CKSTRING name = NULL;
    if (!Text || !Ref)
        return;

    memset(Ref, 0, sizeof(CKDrawAnnotationObjectRef));
    while (*Text && *Text != ':' && idOffset + 1 < sizeof(idText)) {
        idText[idOffset] = *Text;
        ++idOffset;
        ++Text;
    }
    idText[idOffset] = '\0';
    if (*Text == ':')
        name = Text + 1;
    if (CKDrawAnnotationParseUnsigned(idText, &id))
        Ref->Id = id;
    CKDrawAnnotationCopyParsedText(Ref->Name, sizeof(Ref->Name), name ? name : "");
}

static void CKDrawAnnotationParsePair(CKDrawAnnotationParsed *Parsed,
                                      CKSTRING Key,
                                      CKSTRING Value)
{
    CKDWORD unsignedValue = 0;
    int signedValue = 0;
    if (!Parsed || !Key || !Value)
        return;

    if (CKDrawAnnotationTextEquals(Key, "source")) {
        CKDrawAnnotationCopyParsedText(Parsed->SourceName,
                                       sizeof(Parsed->SourceName), Value);
        Parsed->Source = CKDrawAnnotationParseSourceName(Value);
    } else if (CKDrawAnnotationTextEquals(Key, "token")) {
        if (CKDrawAnnotationParseUnsigned(Value, &unsignedValue))
            Parsed->Token = unsignedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "view")) {
        if (CKDrawAnnotationParseUnsigned(Value, &unsignedValue))
            Parsed->View = (CKRenderView)unsignedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "type")) {
        if (CKDrawAnnotationParseSigned(Value, &signedValue))
            Parsed->PrimitiveType = (VXPRIMITIVETYPE)signedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "indices")) {
        if (CKDrawAnnotationParseUnsigned(Value, &unsignedValue))
            Parsed->IndexCount = unsignedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "verts")) {
        if (CKDrawAnnotationParseUnsigned(Value, &unsignedValue))
            Parsed->VertexCount = unsignedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "path")) {
        CKDrawAnnotationCopyParsedText(Parsed->Path, sizeof(Parsed->Path), Value);
    } else if (CKDrawAnnotationTextEquals(Key, "object")) {
        CKDrawAnnotationParseObjectRef(Value, &Parsed->Object);
    } else if (CKDrawAnnotationTextEquals(Key, "entity")) {
        CKDrawAnnotationParseObjectRef(Value, &Parsed->Entity);
    } else if (CKDrawAnnotationTextEquals(Key, "mesh")) {
        CKDrawAnnotationParseObjectRef(Value, &Parsed->Mesh);
    } else if (CKDrawAnnotationTextEquals(Key, "material")) {
        CKDrawAnnotationParseObjectRef(Value, &Parsed->Material);
    } else if (CKDrawAnnotationTextEquals(Key, "group")) {
        if (CKDrawAnnotationParseSigned(Value, &signedValue))
            Parsed->GroupIndex = signedValue;
    } else if (CKDrawAnnotationTextEquals(Key, "prim")) {
        if (CKDrawAnnotationParseSigned(Value, &signedValue))
            Parsed->PrimitiveIndex = signedValue;
    }
}

CKBOOL CKDrawAnnotationParseLabel(CKSTRING Label,
                                  CKDrawAnnotationParsed *Parsed)
{
    char text[CKDRAW_ANNOTATION_LABEL_SIZE];
    char *cursor;
    if (!Label || !Parsed)
        return FALSE;
    CKDrawAnnotationParsedInit(Parsed);
    strncpy(text, Label, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    cursor = text;

    if (strncmp(cursor, "CKDrawV1", 8) != 0)
        return FALSE;
    cursor += 8;
    while (*cursor == ' ')
        ++cursor;

    while (*cursor) {
        char *key = cursor;
        char *value = NULL;
        while (*cursor && *cursor != '=' && *cursor != ' ')
            ++cursor;
        if (*cursor != '=')
            break;
        *cursor = '\0';
        ++cursor;
        value = cursor;
        while (*cursor && *cursor != ' ')
            ++cursor;
        if (*cursor) {
            *cursor = '\0';
            ++cursor;
        }
        CKDrawAnnotationParsePair(Parsed, key, value);
        while (*cursor == ' ')
            ++cursor;
    }

    Parsed->Valid = Parsed->Source != CKDRAW_SOURCE_NONE;
    return Parsed->Valid;
}
