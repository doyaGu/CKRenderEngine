#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "CKContext.h"
#include "RCKMesh.h"
#include "TestTriangleMultiset.h"

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f) {
    return fabsf(lhs - rhs) <= epsilon;
}

bool VectorsEqual(const VxVector &lhs, const VxVector &rhs, float epsilon = 0.001f) {
    return NearlyEqual(lhs.x, rhs.x, epsilon)
        && NearlyEqual(lhs.y, rhs.y, epsilon)
        && NearlyEqual(lhs.z, rhs.z, epsilon);
}

void RunBasicGeometryChecks() {
    CKContext context(nullptr, 0, 0);
    RCKMesh mesh(&context, "GeometryMesh");

    TestCheck(mesh.SetVertexCount(4) == TRUE, "SetVertexCount failed");

    VxVector normal(0.0f, 1.0f, 0.0f);
    const VxVector vertices[4] = {
        VxVector(0.0f, 0.0f, 0.0f),
        VxVector(2.0f, 0.0f, 0.0f),
        VxVector(0.0f, 2.0f, 0.0f),
        VxVector(0.0f, 0.0f, 2.0f),
    };

    for (int i = 0; i < 4; ++i) {
        mesh.SetVertexPosition(i, const_cast<VxVector *>(&vertices[i]));
        mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexColor(i, 0xFF000000u | static_cast<CKDWORD>(i));
        mesh.SetVertexTextureCoordinates(i, static_cast<float>(i), static_cast<float>(i + 1), -1);
    }

    CKDWORD stride = 0;
    TestCheck(mesh.GetPositionsPtr(&stride) != nullptr && stride != 0, "Positions buffer unavailable");
    TestCheck(mesh.GetNormalsPtr(&stride) != nullptr && stride != 0, "Normals buffer unavailable");
    TestCheck(mesh.GetColorsPtr(&stride) != nullptr && stride != 0, "Colors buffer unavailable");
    TestCheck(mesh.GetTextureCoordinatesPtr(&stride, -1) != nullptr && stride != 0, "UV buffer unavailable");

    TestCheck(mesh.SetFaceCount(2) == TRUE, "SetFaceCount failed");
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceVertexIndex(1, 0, 2, 3);

    int v0 = -1;
    int v1 = -1;
    int v2 = -1;
    mesh.GetFaceVertexIndex(0, v0, v1, v2);
    TestCheck(v0 == 0 && v1 == 1 && v2 == 2, "Unexpected face winding");

    CKWORD *faceIndices = mesh.GetFacesIndices();
    TestCheck(faceIndices != nullptr, "Face index buffer unavailable");
    TestCheck(faceIndices[0] == 0 && faceIndices[1] == 1 && faceIndices[2] == 2, "Unexpected face indices");

    TestCheck(mesh.SetLineCount(2) == TRUE, "SetLineCount failed");
    mesh.SetLine(0, 0, 1);
    mesh.SetLine(1, 2, 3);

    int line0 = -1;
    int line1 = -1;
    mesh.GetLine(0, &line0, &line1);
    TestCheck(line0 == 0 && line1 == 1, "Unexpected line indices");

    const VxBbox &box = mesh.GetLocalBox();
    TestCheck(VectorsEqual(box.Min, VxVector(0.0f, 0.0f, 0.0f)), "Unexpected bounding box minimum");
    TestCheck(VectorsEqual(box.Max, VxVector(2.0f, 2.0f, 2.0f)), "Unexpected bounding box maximum");

    VxVector barycenter;
    mesh.GetBaryCenter(&barycenter);
    TestCheck(VectorsEqual(barycenter, VxVector(0.5f, 0.5f, 0.5f)), "Unexpected barycenter");
    TestCheck(mesh.GetRadius() > 0.0f, "Radius should be positive");
}

void RunChannelAndWeightChecks() {
    CKContext context(nullptr, 0, 0);
    RCKMesh mesh(&context, "ChannelMesh");

    TestCheck(mesh.SetVertexCount(3) == TRUE, "SetVertexCount failed");
    mesh.SetVertexWeightsCount(3);
    TestCheck(mesh.GetVertexWeightsCount() == 3, "Unexpected weight count");

    mesh.SetVertexWeight(0, 1.0f);
    mesh.SetVertexWeight(1, 0.5f);
    mesh.SetVertexWeight(2, 0.25f);
    TestCheck(NearlyEqual(mesh.GetVertexWeight(0), 1.0f), "Unexpected first weight");
    TestCheck(NearlyEqual(mesh.GetVertexWeight(1), 0.5f), "Unexpected second weight");
    TestCheck(mesh.GetVertexWeightsPtr() != nullptr, "Weights buffer unavailable");

    CKMaterial *material0 = reinterpret_cast<CKMaterial *>(static_cast<uintptr_t>(0x11111111u));
    CKMaterial *material1 = reinterpret_cast<CKMaterial *>(static_cast<uintptr_t>(0x22222222u));
    TestCheck(mesh.AddChannel(material0, TRUE) == 0, "First channel was not inserted at index 0");
    TestCheck(mesh.AddChannel(material1, TRUE) == 1, "Second channel was not inserted at index 1");
    TestCheck(mesh.GetChannelCount() == 2, "Unexpected channel count");
    TestCheck(mesh.GetChannelByMaterial(material1) == 1, "Channel lookup failed");

    mesh.RemoveChannel(0);
    TestCheck(mesh.GetChannelCount() == 1, "RemoveChannel did not shrink the channel list");
    TestCheck(mesh.GetChannelMaterial(0) == material1, "Unexpected remaining channel after removal");

    mesh.SetFlags(0x12345678u);
    TestCheck((mesh.GetFlags() & 0x7FE39Au) == (0x12345678u & 0x7FE39Au), "Flags were not masked as expected");
    mesh.SetTransparent(TRUE);
    TestCheck(mesh.IsTransparent() == TRUE, "SetTransparent(TRUE) failed");
    mesh.SetTransparent(FALSE);
    TestCheck(mesh.IsTransparent() == FALSE, "SetTransparent(FALSE) failed");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Basic geometry checks", &RunBasicGeometryChecks);
    tests.Run("Channel and weight checks", &RunChannelAndWeightChecks);
    return tests.ExitCode();
}
