#include "CKDrawAnnotation.h"
#include "TestTriangleMultiset.h"

#include <cstring>

static void AnnotationFormatterWritesStableMeshLabel()
{
    CKDrawAnnotation annotation;
    CKDrawAnnotationInit(&annotation, CKDRAW_SOURCE_MESH);
    annotation.View = 3;
    annotation.PrimitiveType = VX_TRIANGLELIST;
    annotation.IndexCount = 36;
    annotation.VertexCount = 24;
    annotation.Entity.Id = 10;
    CKDrawAnnotationCopyText(annotation.Entity.Name, sizeof(annotation.Entity.Name), "Entity A");
    annotation.Mesh.Id = 20;
    CKDrawAnnotationCopyText(annotation.Mesh.Name, sizeof(annotation.Mesh.Name), "Mesh/B");
    annotation.Material.Id = 30;
    CKDrawAnnotationCopyText(annotation.Material.Name, sizeof(annotation.Material.Name), "Mat\"C");
    annotation.GroupIndex = 2;
    annotation.PrimitiveIndex = 5;
    CKDrawAnnotationCopyText(annotation.Path, sizeof(annotation.Path), "HW");

    char label[CKDRAW_ANNOTATION_LABEL_SIZE];
    CKDrawAnnotationFormatLabel(&annotation, label, sizeof(label));

    TestCheck(strstr(label, "CKDrawV1") == label,
              "annotation label must use the versioned CKDrawV1 prefix");
    TestCheck(strstr(label, "source=Mesh") != NULL,
              "mesh annotation must include source kind");
    TestCheck(strstr(label, "entity=10:Entity_A") != NULL,
              "annotation formatter must sanitize entity names");
    TestCheck(strstr(label, "mesh=20:Mesh/B") != NULL,
              "annotation formatter must preserve safe mesh name characters");
    TestCheck(strstr(label, "material=30:Mat_C") != NULL,
              "annotation formatter must sanitize quotes");
    TestCheck(strstr(label, "group=2") != NULL,
              "mesh annotation must include group index");
    TestCheck(strstr(label, "prim=5") != NULL,
              "mesh annotation must include primitive index");
}

static void AnnotationLifecycleConsumesPendingOnce()
{
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);

    CKDrawAnnotation annotation;
    CKDrawAnnotationInit(&annotation, CKDRAW_SOURCE_SPRITE);
    annotation.View = 6;
    annotation.Object.Id = 44;
    CKDrawAnnotationCopyText(annotation.Object.Name, sizeof(annotation.Object.Name), "Sprite");

    CKDrawAnnotationStateSetPending(&state, &annotation);

    CKDrawAnnotation consumed;
    TestCheck(CKDrawAnnotationStateConsume(&state, &consumed) == TRUE,
              "pending annotation must be consumed");
    TestCheck(consumed.Source == CKDRAW_SOURCE_SPRITE,
              "consumed annotation must preserve source");
    TestCheck(CKDrawAnnotationStateConsume(&state, &consumed) == FALSE,
              "pending annotation must be consumed only once");
}

static void AnnotationParserRoundTripsStructuredFields()
{
    CKDrawAnnotation annotation;
    CKDrawAnnotationInit(&annotation, CKDRAW_SOURCE_MESH);
    annotation.Token = 123;
    annotation.View = 3;
    annotation.PrimitiveType = VX_TRIANGLELIST;
    annotation.IndexCount = 36;
    annotation.VertexCount = 24;
    annotation.Entity.Id = 10;
    CKDrawAnnotationCopyText(annotation.Entity.Name, sizeof(annotation.Entity.Name), "Entity A");
    annotation.Mesh.Id = 20;
    CKDrawAnnotationCopyText(annotation.Mesh.Name, sizeof(annotation.Mesh.Name), "Mesh/B");
    annotation.Material.Id = 30;
    CKDrawAnnotationCopyText(annotation.Material.Name, sizeof(annotation.Material.Name), "Mat C");
    annotation.GroupIndex = 2;
    annotation.PrimitiveIndex = 5;
    CKDrawAnnotationCopyText(annotation.Path, sizeof(annotation.Path), "HW");

    char label[CKDRAW_ANNOTATION_LABEL_SIZE];
    CKDrawAnnotationFormatLabel(&annotation, label, sizeof(label));

    CKDrawAnnotationParsed parsed;
    TestCheck(CKDrawAnnotationParseLabel(label, &parsed) == TRUE,
              "parser must accept CKDrawV1 labels");
    TestCheck(parsed.Token == 123,
              "parser must preserve token");
    TestCheck(parsed.Source == CKDRAW_SOURCE_MESH,
              "parser must preserve source enum");
    TestCheck(strcmp(parsed.SourceName, "Mesh") == 0,
              "parser must preserve source name");
    TestCheck(parsed.Entity.Id == 10,
              "parser must preserve entity id");
    TestCheck(strcmp(parsed.Entity.Name, "Entity_A") == 0,
              "parser must preserve sanitized entity name");
    TestCheck(parsed.Mesh.Id == 20,
              "parser must preserve mesh id");
    TestCheck(parsed.Material.Id == 30,
              "parser must preserve material id");
    TestCheck(parsed.GroupIndex == 2,
              "parser must preserve group index");
    TestCheck(parsed.PrimitiveIndex == 5,
              "parser must preserve primitive index");
    TestCheck(parsed.IndexCount == 36 && parsed.VertexCount == 24,
              "parser must preserve primitive counts");
}

static void AnnotationParserRejectsMalformedLabel()
{
    CKDrawAnnotationParsed parsed;
    TestCheck(CKDrawAnnotationParseLabel("not a draw annotation", &parsed) == FALSE,
              "parser must reject labels without CKDrawV1 prefix");
    TestCheck(CKDrawAnnotationParseLabel("CKDrawV1 source=Unknown token=1", &parsed) == FALSE,
              "parser must reject unknown source kinds");
}

static void AnnotationLifecycleCountsOverwriteAndExplicit()
{
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);

    CKDrawAnnotation first;
    CKDrawAnnotationInit(&first, CKDRAW_SOURCE_MESH);
    CKDrawAnnotationStateSetPending(&state, &first);

    CKDrawAnnotation second;
    CKDrawAnnotationInit(&second, CKDRAW_SOURCE_SPRITE);
    CKDrawAnnotationStateSetPending(&state, &second);

    CKDrawAnnotation consumed;
    TestCheck(CKDrawAnnotationStateConsume(&state, &consumed) == TRUE,
              "overwritten pending annotation must still consume latest annotation");
    TestCheck(consumed.Source == CKDRAW_SOURCE_SPRITE,
              "pending overwrite must keep newest annotation");

    CKDrawAnnotationCounters counters;
    CKDrawAnnotationStateGetCounters(&state, &counters);
    TestCheck(counters.PendingSetCount == 2,
              "pending set counter must include both annotations");
    TestCheck(counters.PendingOverwriteCount == 1,
              "overwrite counter must detect replaced pending annotation");
    TestCheck(counters.ExplicitCount == 1,
              "explicit counter must track consumed pending annotation");
}

static void AnnotationLifecycleBuildsCallbackFallback()
{
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);
    CKDrawAnnotationObjectRef callbackObject;
    callbackObject.Id = 77;
    CKDrawAnnotationCopyText(callbackObject.Name, sizeof(callbackObject.Name), "Callback Object");
    CKDrawAnnotationStateSetCallbackObject(&state, &callbackObject);

    CKDrawAnnotation fallback;
    CKDrawAnnotationStateBuildFallback(&state, &fallback,
                                       5, VX_TRIANGLEFAN, 4, 4);

    TestCheck(fallback.Source == CKDRAW_SOURCE_CALLBACK,
              "fallback must use callback source when a callback object is active");
    TestCheck(fallback.Object.Id == 77,
              "callback fallback must preserve callback object id");
    TestCheck(fallback.View == 5,
              "callback fallback must preserve view");

    CKDrawAnnotationCounters counters;
    CKDrawAnnotationStateGetCounters(&state, &counters);
    TestCheck(counters.CallbackFallbackCount == 1,
              "callback fallback counter must increment");
}

static void AnnotationLifecycleBuildsRawFallback()
{
    CKDrawAnnotationState state;
    CKDrawAnnotationStateInit(&state);

    CKDrawAnnotation fallback;
    CKDrawAnnotationStateBuildFallback(&state, &fallback,
                                       5, VX_LINESTRIP, 8, 9);

    TestCheck(fallback.Source == CKDRAW_SOURCE_RAW_PRIMITIVE,
              "fallback without callback object must be raw primitive");
    TestCheck(fallback.IndexCount == 8,
              "raw fallback must preserve index count");
    TestCheck(fallback.VertexCount == 9,
              "raw fallback must preserve vertex count");

    CKDrawAnnotationCounters counters;
    CKDrawAnnotationStateGetCounters(&state, &counters);
    TestCheck(counters.RawFallbackCount == 1,
              "raw fallback counter must increment");
}

int main()
{
    TestFramework tests;
    tests.Run("AnnotationFormatterWritesStableMeshLabel",
              AnnotationFormatterWritesStableMeshLabel);
    tests.Run("AnnotationLifecycleConsumesPendingOnce",
              AnnotationLifecycleConsumesPendingOnce);
    tests.Run("AnnotationParserRoundTripsStructuredFields",
              AnnotationParserRoundTripsStructuredFields);
    tests.Run("AnnotationParserRejectsMalformedLabel",
              AnnotationParserRejectsMalformedLabel);
    tests.Run("AnnotationLifecycleCountsOverwriteAndExplicit",
              AnnotationLifecycleCountsOverwriteAndExplicit);
    tests.Run("AnnotationLifecycleBuildsCallbackFallback",
              AnnotationLifecycleBuildsCallbackFallback);
    tests.Run("AnnotationLifecycleBuildsRawFallback",
              AnnotationLifecycleBuildsRawFallback);
    return tests.ExitCode();
}
