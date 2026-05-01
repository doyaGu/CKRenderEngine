#include <stdio.h>
#include <stdlib.h>

#include "CKContext.h"
#include "CKStateChunk.h"
#include "RCKMesh.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

void RunSerializationSmokeTest() {
    CKContext context(nullptr, 0, 0);
    RCKMesh source(&context, "SourceMesh");
    RCKMesh loaded(&context, "LoadedMesh");

    TestCheck(source.SetVertexCount(3) == TRUE, "SetVertexCount failed");
    TestCheck(source.SetFaceCount(1) == TRUE, "SetFaceCount failed");

    VxVector a(0.0f, 0.0f, 0.0f);
    VxVector b(1.0f, 0.0f, 0.0f);
    VxVector c(0.0f, 1.0f, 0.0f);
    source.SetVertexPosition(0, &a);
    source.SetVertexPosition(1, &b);
    source.SetVertexPosition(2, &c);
    source.SetVertexColor(0, 0xFFFF0000u);
    source.SetVertexColor(1, 0xFF00FF00u);
    source.SetVertexColor(2, 0xFF0000FFu);
    source.SetFaceVertexIndex(0, 0, 1, 2);

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_MESHONLY);
    TestCheck(chunk != nullptr, "Save returned a null chunk");
    TestCheck(loaded.Load(chunk, nullptr) == CK_OK, "Load failed");
    delete chunk;

    TestCheck(loaded.GetVertexCount() == 3, "Loaded vertex count mismatch");
    TestCheck(loaded.GetFaceCount() == 1, "Loaded face count mismatch");
    TestCheck(loaded.GetVertexColor(0) == 0xFFFF0000u, "Loaded vertex color mismatch");
}

void RunTopologyMutationSmokeTest() {
    CKContext context(nullptr, 0, 0);
    RCKMesh mesh(&context, "MutationMesh");

    TestCheck(mesh.SetVertexCount(3) == TRUE, "SetVertexCount failed");
    TestCheck(mesh.SetFaceCount(1) == TRUE, "SetFaceCount failed");

    VxVector a(0.0f, 0.0f, 0.0f);
    VxVector b(1.0f, 0.0f, 0.0f);
    VxVector c(0.0f, 1.0f, 0.0f);
    mesh.SetVertexPosition(0, &a);
    mesh.SetVertexPosition(1, &b);
    mesh.SetVertexPosition(2, &c);
    mesh.SetFaceVertexIndex(0, 0, 1, 2);

    mesh.InverseWinding();

    int v0 = -1;
    int v1 = -1;
    int v2 = -1;
    mesh.GetFaceVertexIndex(0, v0, v1, v2);
    TestCheck(v0 == 0 && v1 == 2 && v2 == 1, "InverseWinding did not swap the last two indices");

    mesh.Clean(TRUE);
    mesh.Consolidate();
    mesh.UnOptimize();
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();

    TestFramework tests;
    tests.Run("Mesh serialization smoke", &RunSerializationSmokeTest);
    tests.Run("Mesh topology mutation smoke", &RunTopologyMutationSmokeTest);
    return tests.ExitCode();
}
