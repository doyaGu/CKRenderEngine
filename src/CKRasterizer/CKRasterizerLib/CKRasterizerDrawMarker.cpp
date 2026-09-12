#include "CKRasterizerDrawMarker.h"

#include <cstdio>
#include <cstring>

namespace {

void AppendCharacter(char *destination, CKDWORD destinationSize,
                     CKDWORD *offset, char value)
{
    if (!destination || !offset || destinationSize == 0 ||
        *offset + 1 >= destinationSize)
        return;
    destination[*offset] = value;
    ++(*offset);
    destination[*offset] = '\0';
}

void AppendText(char *destination, CKDWORD destinationSize,
                CKDWORD *offset, const char *text)
{
    if (!destination || !offset || destinationSize == 0 || !text)
        return;
    while (*text && *offset + 1 < destinationSize) {
        AppendCharacter(destination, destinationSize, offset, *text);
        ++text;
    }
}

void AppendSanitizedText(char *destination, CKDWORD destinationSize,
                         CKDWORD *offset, const char *text)
{
    if (!destination || !offset || destinationSize == 0 || !text)
        return;
    while (*text && *offset + 1 < destinationSize) {
        char value = *text;
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
            value == '"' || value == '\'' || value == '=')
            value = '_';
        AppendCharacter(destination, destinationSize, offset, value);
        ++text;
    }
}

void AppendObjectReference(char *destination, CKDWORD destinationSize,
                           CKDWORD *offset, const char *field,
                           const CKDrawAnnotationObjectRef *reference)
{
    if (!reference)
        return;

    char id[32];
    AppendText(destination, destinationSize, offset, " ");
    AppendText(destination, destinationSize, offset, field);
    AppendText(destination, destinationSize, offset, "=");
    std::snprintf(id, sizeof(id), "%u:", (unsigned)reference->Id);
    AppendText(destination, destinationSize, offset, id);
    AppendSanitizedText(destination, destinationSize, offset, reference->Name);
}

} // namespace

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

void CKDrawAnnotationInit(CKDrawAnnotation *Annotation,
                          CKDrawAnnotationSource Source)
{
    if (!Annotation)
        return;
    std::memset(Annotation, 0, sizeof(CKDrawAnnotation));
    Annotation->Source = Source;
    Annotation->GroupIndex = -1;
    Annotation->PrimitiveIndex = -1;
}

void CKDrawAnnotationCopyText(char *Dst, CKDWORD DstSize, CKSTRING Src)
{
    CKDWORD offset = 0;
    if (!Dst || DstSize == 0)
        return;
    Dst[0] = '\0';
    AppendSanitizedText(Dst, DstSize, &offset, Src ? Src : (CKSTRING)"");
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

    AppendText(Label, LabelSize, &offset, "CKDrawV1 source=");
    AppendText(Label, LabelSize, &offset,
               CKDrawAnnotationGetSourceName(Annotation->Source));
    std::snprintf(value, sizeof(value),
                  " token=%u type=%d indices=%u verts=%u",
                  (unsigned)Annotation->Token,
                  (int)Annotation->PrimitiveType,
                  (unsigned)Annotation->IndexCount,
                  (unsigned)Annotation->VertexCount);
    AppendText(Label, LabelSize, &offset, value);

    if (Annotation->Path[0]) {
        AppendText(Label, LabelSize, &offset, " path=");
        AppendSanitizedText(Label, LabelSize, &offset, Annotation->Path);
    }

    AppendObjectReference(Label, LabelSize, &offset, "object",
                          &Annotation->Object);
    AppendObjectReference(Label, LabelSize, &offset, "entity",
                          &Annotation->Entity);
    AppendObjectReference(Label, LabelSize, &offset, "mesh",
                          &Annotation->Mesh);
    AppendObjectReference(Label, LabelSize, &offset, "material",
                          &Annotation->Material);

    if (Annotation->GroupIndex >= 0) {
        std::snprintf(value, sizeof(value), " group=%d",
                      Annotation->GroupIndex);
        AppendText(Label, LabelSize, &offset, value);
    }
    if (Annotation->PrimitiveIndex >= 0) {
        std::snprintf(value, sizeof(value), " prim=%d",
                      Annotation->PrimitiveIndex);
        AppendText(Label, LabelSize, &offset, value);
    }
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
