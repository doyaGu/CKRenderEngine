#include <math.h>
#include <stdint.h>
#include <fstream>
#include <iterator>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "CKEnums.h"
#include "CKContext.h"
#include "CKStateChunk.h"
#include "RCKMesh.h"
#include "TestTriangleMultiset.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f) {
    return fabsf(lhs - rhs) <= epsilon;
}

bool VectorsEqual(const VxVector &lhs, const VxVector &rhs, float epsilon = 0.001f) {
    return NearlyEqual(lhs.x, rhs.x, epsilon)
        && NearlyEqual(lhs.y, rhs.y, epsilon)
        && NearlyEqual(lhs.z, rhs.z, epsilon);
}

bool UVsEqual(const Vx2DVector &lhs, const Vx2DVector &rhs, float epsilon = 0.001f) {
    return NearlyEqual(lhs.x, rhs.x, epsilon)
        && NearlyEqual(lhs.y, rhs.y, epsilon);
}

std::string ReadSourceText(const char *relativePath) {
    std::ifstream file(std::string(CKRE_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
    if (!file)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

VxVertex MakePMTestVertex(const VxVector &position, const VxVector &normal, const Vx2DVector &uv) {
    VxVertex vertex;
    vertex.m_Position = position;
    vertex.m_Normal = normal;
    vertex.m_UV = uv;
    return vertex;
}

void FillPMTestMesh(RCKMesh &mesh) {
    TestCheck(mesh.SetVertexCount(4) == TRUE, "SetVertexCount failed");

    VxVector normal(0.0f, 0.0f, 1.0f);
    const VxVector vertices[4] = {
        VxVector(0.0f, 0.0f, 0.0f),
        VxVector(1.0f, 0.0f, 0.0f),
        VxVector(0.0f, 1.0f, 0.0f),
        VxVector(0.0f, 0.0f, 1.0f),
    };

    for (int i = 0; i < 4; ++i) {
        mesh.SetVertexPosition(i, const_cast<VxVector *>(&vertices[i]));
        mesh.SetVertexNormal(i, &normal);
        mesh.SetVertexTextureCoordinates(i, static_cast<float>(i), static_cast<float>(i + 1), -1);
        mesh.SetVertexColor(i, 0xFF000000u | static_cast<CKDWORD>(i));
        mesh.SetVertexSpecularColor(i, 0x00000010u | static_cast<CKDWORD>(i));
    }

    TestCheck(mesh.SetFaceCount(2) == TRUE, "SetFaceCount failed");
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.SetFaceVertexIndex(1, 0, 2, 3);
}

class TestablePMMesh : public RCKMesh {
public:
    explicit TestablePMMesh(CKContext *context, CKSTRING name)
        : RCKMesh(context, name) {}

    void AttachSyntheticProgressiveMesh(int renderedVertexCount) {
        TestCheck(m_ProgressiveMesh == nullptr, "Progressive mesh already attached");
        m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = renderedVertexCount;
    }

    void SetSyntheticPMFields(int renderedVertexCount, int morphEnabled, int morphStep, const CKDWORD *parents, int parentCount) {
        if (!m_ProgressiveMesh)
            AttachSyntheticProgressiveMesh(renderedVertexCount);
        m_ProgressiveMesh->m_VertexCount = renderedVertexCount;
        m_ProgressiveMesh->m_MorphEnabled = morphEnabled;
        m_ProgressiveMesh->m_MorphStep = morphStep;
        m_ProgressiveMesh->m_Data.Resize(parentCount);
        for (int i = 0; i < parentCount; ++i)
            m_ProgressiveMesh->m_Data[i] = parents[i];
    }

    bool HasSyntheticPM() const {
        return m_ProgressiveMesh != nullptr;
    }

    int PMMorphEnabled() const {
        return m_ProgressiveMesh ? m_ProgressiveMesh->m_MorphEnabled : 0;
    }

    int PMMorphStep() const {
        return m_ProgressiveMesh ? m_ProgressiveMesh->m_MorphStep : 0;
    }

    int PMParentCount() const {
        return m_ProgressiveMesh ? m_ProgressiveMesh->m_Data.Size() : 0;
    }

    CKDWORD PMParentAt(int index) const {
        return m_ProgressiveMesh->m_Data[index];
    }

    int PreRenderCallbackCount() const {
        return m_RenderCallbacks ? m_RenderCallbacks->m_PreCallBacks.Size() : 0;
    }

    void ClearRenderCallbacksForTest() {
        delete m_RenderCallbacks;
        m_RenderCallbacks = nullptr;
    }
};

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

void RunPMGeoMorphInterpolationChecks() {
    VxVertex current = MakePMTestVertex(
        VxVector(1.0f, 2.0f, 3.0f),
        VxVector(0.0f, 0.0f, 1.0f),
        Vx2DVector(0.25f, 0.5f)
    );
    VxVertex target = MakePMTestVertex(
        VxVector(5.0f, 10.0f, 15.0f),
        VxVector(0.0f, 1.0f, 0.0f),
        Vx2DVector(0.75f, 1.5f)
    );
    VxVertex result;

    CKRETestInterpolatePMGeoMorphVertex(&result, 0.0f, &current, &target);
    TestCheck(VectorsEqual(result.m_Position, current.m_Position), "t=0 should keep current position");
    TestCheck(VectorsEqual(result.m_Normal, current.m_Normal), "t=0 should keep current normal");
    TestCheck(UVsEqual(result.m_UV, current.m_UV), "t=0 should keep current UV");

    CKRETestInterpolatePMGeoMorphVertex(&result, 1.0f, &current, &target);
    TestCheck(VectorsEqual(result.m_Position, target.m_Position), "t=1 should reach target position");
    TestCheck(VectorsEqual(result.m_Normal, target.m_Normal), "t=1 should reach target normal");
    TestCheck(UVsEqual(result.m_UV, target.m_UV), "t=1 should reach target UV");

    CKRETestInterpolatePMGeoMorphVertex(&result, 0.5f, &current, &target);
    TestCheck(VectorsEqual(result.m_Position, VxVector(3.0f, 6.0f, 9.0f)), "Unexpected midpoint position");
    TestCheck(VectorsEqual(result.m_Normal, VxVector(0.0f, 0.5f, 0.5f)), "Unexpected midpoint normal");
    TestCheck(UVsEqual(result.m_UV, Vx2DVector(0.5f, 1.0f)), "Unexpected midpoint UV");
}

void RunPMParameterChecks() {
    CKContext context(nullptr, 0, 0);
    TestablePMMesh mesh(&context, "PMParameterMesh");
    FillPMTestMesh(mesh);

    mesh.AttachSyntheticProgressiveMesh(4);
    TestCheck(mesh.IsPM() == TRUE, "Mesh should have progressive mesh data");
    TestCheck(mesh.GetVerticesRendered() == 4, "Synthetic PM should initialize rendered vertex count");

    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    TestCheck(mesh.IsUpToDate() != FALSE, "Test setup failed to mark mesh up to date");
    mesh.SetVerticesRendered(2);
    TestCheck(mesh.GetVerticesRendered() == 2, "SetVerticesRendered did not store requested count");
    TestCheck(mesh.IsUpToDate() == FALSE, "SetVerticesRendered should dirty the mesh");

    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    mesh.SetVerticesRendered(-5);
    TestCheck(mesh.GetVerticesRendered() == 0, "SetVerticesRendered should clamp below zero");
    TestCheck(mesh.IsUpToDate() == FALSE, "SetVerticesRendered clamp should dirty the mesh");

    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    mesh.SetVerticesRendered(99);
    TestCheck(mesh.GetVerticesRendered() == 4, "SetVerticesRendered should clamp above vertex count");
    TestCheck(mesh.IsUpToDate() == FALSE, "SetVerticesRendered high clamp should dirty the mesh");

    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    mesh.EnablePMGeoMorph(TRUE);
    TestCheck(mesh.IsPMGeoMorphEnabled() == TRUE, "EnablePMGeoMorph(TRUE) failed");
    TestCheck(mesh.IsUpToDate() == FALSE, "EnablePMGeoMorph should dirty the mesh");

    mesh.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    mesh.SetPMGeoMorphStep(2);
    TestCheck(mesh.GetPMGeoMorphStep() == 2, "SetPMGeoMorphStep failed");
    TestCheck(mesh.IsUpToDate() == FALSE, "SetPMGeoMorphStep should dirty the mesh");
}

void RunPMDegenerateCollapseChecks() {
    const CKDWORD parents[4] = {0, 1, 2, 2};
    TestCheck(!CKRETestPMRemappedTriangleIsDegenerate(3, parents, 4, 0, 1, 2),
              "Uncollapsed triangle should remain renderable");
    TestCheck(CKRETestPMRemappedTriangleIsDegenerate(3, parents, 4, 0, 2, 3),
              "Collapsed triangle should be detected as degenerate");
}

void RunPMModifierCountChecks() {
    CKContext context(nullptr, 0, 0);
    TestablePMMesh mesh(&context, "PMModifierCountMesh");
    FillPMTestMesh(mesh);
    mesh.AttachSyntheticProgressiveMesh(2);

    TestCheck(mesh.GetModifierVertexCount() == 2, "PM modifier vertex count must use rendered vertex count");
    TestCheck(mesh.GetModifierUVCount(-1) == 2, "PM modifier UV count must follow rendered vertex count");

    CKDWORD stride = 0;
    TestCheck(mesh.GetModifierVertices(&stride) == mesh.GetPositionsPtr(&stride),
              "PM modifier vertices should still address the position stream");
}

void RunPMModifierMutationBoundsChecks() {
    CKContext context(nullptr, 0, 0);

    {
        TestablePMMesh mesh(&context, "PMTranslateBoundsMesh");
        FillPMTestMesh(mesh);
        mesh.AttachSyntheticProgressiveMesh(2);

        VxVector original[4];
        for (int i = 0; i < 4; ++i)
            mesh.GetVertexPosition(i, &original[i]);

        VxVector translation(10.0f, 20.0f, 30.0f);
        mesh.TranslateVertices(&translation);

        VxVector position;
        mesh.GetVertexPosition(0, &position);
        TestCheck(VectorsEqual(position, VxVector(10.0f, 20.0f, 30.0f)),
                  "TranslateVertices should mutate rendered PM vertex 0");
        mesh.GetVertexPosition(1, &position);
        TestCheck(VectorsEqual(position, VxVector(11.0f, 20.0f, 30.0f)),
                  "TranslateVertices should mutate rendered PM vertex 1");
        mesh.GetVertexPosition(2, &position);
        TestCheck(VectorsEqual(position, original[2]),
                  "TranslateVertices must not mutate collapsed PM vertex 2");
        mesh.GetVertexPosition(3, &position);
        TestCheck(VectorsEqual(position, original[3]),
                  "TranslateVertices must not mutate collapsed PM vertex 3");
    }

    {
        TestablePMMesh mesh(&context, "PMScaleBoundsMesh");
        FillPMTestMesh(mesh);
        mesh.AttachSyntheticProgressiveMesh(2);

        VxVector original2;
        VxVector original3;
        mesh.GetVertexPosition(2, &original2);
        mesh.GetVertexPosition(3, &original3);

        VxVector scale(2.0f, 3.0f, 4.0f);
        mesh.ScaleVertices(&scale, nullptr);

        VxVector position;
        mesh.GetVertexPosition(1, &position);
        TestCheck(VectorsEqual(position, VxVector(2.0f, 0.0f, 0.0f)),
                  "ScaleVertices should mutate rendered PM vertex 1");
        mesh.GetVertexPosition(2, &position);
        TestCheck(VectorsEqual(position, original2),
                  "ScaleVertices must not mutate collapsed PM vertex 2");
        mesh.GetVertexPosition(3, &position);
        TestCheck(VectorsEqual(position, original3),
                  "ScaleVertices must not mutate collapsed PM vertex 3");
    }

    {
        TestablePMMesh mesh(&context, "PMRotateBoundsMesh");
        FillPMTestMesh(mesh);
        mesh.AttachSyntheticProgressiveMesh(2);

        VxVector original1;
        VxVector original2;
        VxVector original3;
        mesh.GetVertexPosition(1, &original1);
        mesh.GetVertexPosition(2, &original2);
        mesh.GetVertexPosition(3, &original3);

        VxVector axis(0.0f, 0.0f, 1.0f);
        mesh.RotateVertices(&axis, 1.57079632679f);

        VxVector position;
        mesh.GetVertexPosition(1, &position);
        TestCheck(!VectorsEqual(position, original1),
                  "RotateVertices should mutate rendered PM vertex 1");
        mesh.GetVertexPosition(2, &position);
        TestCheck(VectorsEqual(position, original2),
                  "RotateVertices must not mutate collapsed PM vertex 2");
        mesh.GetVertexPosition(3, &position);
        TestCheck(VectorsEqual(position, original3),
                  "RotateVertices must not mutate collapsed PM vertex 3");
    }
}

void RunPMGeoMorphSkinBranchSourceChecks() {
    const std::string source = ReadSourceText("src/CK3dEntity.cpp");
    TestCheck(!source.empty(), "CK3dEntity source must be readable");
    TestCheck(source.find("mesh->IsPM() && mesh->IsPMGeoMorphEnabled()") != std::string::npos,
              "UpdateSkin must keep the PM GeoMorph branch");
    TestCheck(source.find("modifierVertexCount = m_Skin->GetVertexCount()") != std::string::npos,
              "PM GeoMorph skin branch must use the full skin vertex count");
    TestCheck(source.find("mesh->ModifyObjectFlags(0, CK_OBJECT_UPTODATE)") != std::string::npos,
              "PM GeoMorph skin branch must dirty the mesh");
}

void RunPMSaveLoadRoundTripChecks() {
    CKContext context(nullptr, 0, 0);
    TestablePMMesh source(&context, "PMSaveSource");
    TestablePMMesh loaded(&context, "PMSaveLoaded");
    FillPMTestMesh(source);

    const CKDWORD parents[4] = {0, 0, 1, 2};
    source.SetSyntheticPMFields(2, TRUE, 3, parents, 4);

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_MESHONLY);
    TestCheck(chunk != nullptr, "PM mesh save returned a null chunk");

    chunk->StartRead();
    int pmSize = chunk->SeekIdentifierAndReturnSize(CK_STATESAVE_PROGRESSIVEMESH);
    TestCheck(pmSize == 12 + 4 * (int) sizeof(CKDWORD), "Unexpected progressive mesh chunk size");
    TestCheck(chunk->ReadInt() == 2, "PM chunk vertex count mismatch");
    TestCheck(chunk->ReadInt() == TRUE, "PM chunk morph enabled mismatch");
    TestCheck(chunk->ReadInt() == 3, "PM chunk morph step mismatch");
    CKDWORD savedParents[4] = {};
    chunk->ReadAndFillBuffer_LEndian(sizeof(savedParents), savedParents);
    for (int i = 0; i < 4; ++i)
        TestCheck(savedParents[i] == parents[i], "PM chunk parent map mismatch");

    chunk->StartRead();
    TestCheck(loaded.Load(chunk, nullptr) == CK_OK, "PM mesh load failed");
    delete chunk;

    TestCheck(loaded.HasSyntheticPM(), "Load should restore progressive mesh data");
    TestCheck(loaded.GetVerticesRendered() == 2, "Loaded PM vertex count mismatch");
    TestCheck(loaded.IsPMGeoMorphEnabled() == TRUE, "Loaded PM morph enabled mismatch");
    TestCheck(loaded.PMMorphStep() == 3, "Loaded PM morph step mismatch");
    TestCheck(loaded.PMParentCount() == 4, "Loaded PM parent count mismatch");
    for (int i = 0; i < 4; ++i)
        TestCheck(loaded.PMParentAt(i) == parents[i], "Loaded PM parent map mismatch");
    TestCheck(loaded.PreRenderCallbackCount() > 0, "PM load should register a pre-render callback");
    loaded.ClearRenderCallbacksForTest();
}

void RunNoPMLoadSourceChecks() {
    CKContext context(nullptr, 0, 0);
    TestablePMMesh source(&context, "NoPMSource");
    FillPMTestMesh(source);

    CKStateChunk *chunk = source.Save(nullptr, CK_STATESAVE_MESHONLY);
    TestCheck(chunk != nullptr, "Non-PM mesh save returned a null chunk");
    chunk->StartRead();
    TestCheck(chunk->SeekIdentifierAndReturnSize(CK_STATESAVE_PROGRESSIVEMESH) == -1,
              "Non-PM source should not save a progressive mesh chunk");
    delete chunk;

    const std::string sourceText = ReadSourceText("src/CKMesh.cpp");
    TestCheck(!sourceText.empty(), "CKMesh source must be readable");
    TestCheck(sourceText.find("if (pmSize == -1)") != std::string::npos &&
                  sourceText.find("DestroyPM();") != std::string::npos,
              "Load without a PM chunk must keep destroying existing PM data");
}

} // namespace

int main() {
    SetProcessorSpecific_FunctionsPtr();

    TestFramework tests;
    tests.Run("Basic geometry checks", &RunBasicGeometryChecks);
    tests.Run("Channel and weight checks", &RunChannelAndWeightChecks);
    tests.Run("PM GeoMorph interpolation checks", &RunPMGeoMorphInterpolationChecks);
    tests.Run("PM parameter checks", &RunPMParameterChecks);
    tests.Run("PM degenerate collapse checks", &RunPMDegenerateCollapseChecks);
    tests.Run("PM modifier count checks", &RunPMModifierCountChecks);
    tests.Run("PM modifier mutation bounds checks", &RunPMModifierMutationBoundsChecks);
    tests.Run("PM GeoMorph skin branch source checks", &RunPMGeoMorphSkinBranchSourceChecks);
    tests.Run("PM save/load round trip checks", &RunPMSaveLoadRoundTripChecks);
    tests.Run("No-PM load source checks", &RunNoPMLoadSourceChecks);
    return tests.ExitCode();
}
