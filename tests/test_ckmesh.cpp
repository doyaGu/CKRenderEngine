#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include "RCKMesh.h"
#include "CKContext.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "VxMath.h"

// Simple test framework
class TestFramework {
private:
    int totalTests = 0;
    int passedTests = 0;
    
public:
    void runTest(const std::string& testName, std::function<void()> testFunc) {
        totalTests++;
        std::cout << "Running test: " << testName << "... ";
        
        try {
            testFunc();
            passedTests++;
            std::cout << "PASSED" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "FAILED: " << e.what() << std::endl;
        } catch (...) {
            std::cout << "FAILED: Unknown exception" << std::endl;
        }
    }
    
    void printSummary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total tests: " << totalTests << std::endl;
        std::cout << "Passed: " << passedTests << std::endl;
        std::cout << "Failed: " << (totalTests - passedTests) << std::endl;
        std::cout << "Success rate: " << (passedTests * 100.0 / totalTests) << "%" << std::endl;
    }
    
    bool allTestsPassed() {
        return passedTests == totalTests;
    }
};

// Global test framework instance
TestFramework g_testFramework;

// Helper function to compare vectors with tolerance
bool vectorsEqual(const VxVector& a, const VxVector& b, float tolerance = 0.001f) {
    return std::abs(a.x - b.x) < tolerance &&
           std::abs(a.y - b.y) < tolerance &&
           std::abs(a.z - b.z) < tolerance;
}

// Test 1: Basic mesh creation and initialization
void testMeshCreation() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "TestMesh");
    
    // Test basic properties
    assert(mesh.GetClassID() == CKCID_MESH);
    assert(mesh.GetVertexCount() == 0);
    assert(mesh.GetFaceCount() == 0);
    assert(mesh.GetLineCount() == 0);
    assert(mesh.GetMaterialCount() == 0);
    
    // Test flags
    CKDWORD flags = mesh.GetFlags();
    mesh.SetFlags(0x12345678);
    assert((mesh.GetFlags() & 0x7FE39A) == (0x12345678 & 0x7FE39A));
    
    std::cout << "Mesh creation and basic properties verified" << std::endl;
}

// Test 2: Vertex operations
void testVertexOperations() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "VertexTest");
    
    // Test vertex count setting
    assert(mesh.SetVertexCount(4) == TRUE);
    assert(mesh.GetVertexCount() == 4);
    
    // Test vertex position setting/getting
    VxVector pos1(1.0f, 2.0f, 3.0f);
    VxVector pos2(-1.0f, -2.0f, -3.0f);
    VxVector pos3(0.0f, 5.0f, -2.5f);
    VxVector pos4(10.0f, 0.0f, 0.0f);
    
    mesh.SetVertexPosition(0, &pos1);
    mesh.SetVertexPosition(1, &pos2);
    mesh.SetVertexPosition(2, &pos3);
    mesh.SetVertexPosition(3, &pos4);
    
    VxVector readPos;
    mesh.GetVertexPosition(0, &readPos);
    assert(vectorsEqual(readPos, pos1));
    
    mesh.GetVertexPosition(1, &readPos);
    assert(vectorsEqual(readPos, pos2));
    
    // Test vertex normal setting/getting
    VxVector normal(0.0f, 1.0f, 0.0f);
    mesh.SetVertexNormal(0, &normal);
    
    VxVector readNormal;
    mesh.GetVertexNormal(0, &readNormal);
    assert(vectorsEqual(readNormal, normal));
    
    // Test vertex color setting/getting
    CKDWORD color = 0xFF00FF00; // ARGB format
    mesh.SetVertexColor(0, color);
    assert(mesh.GetVertexColor(0) == color);
    
    // Test vertex data pointers
    CKDWORD stride;
    void* positionsPtr = mesh.GetPositionsPtr(&stride);
    assert(positionsPtr != nullptr);
    assert(stride == 32); // Expected stride for VxVertex
    
    void* colorsPtr = mesh.GetColorsPtr(&stride);
    assert(colorsPtr != nullptr);
    assert(stride == 4); // Expected stride for CKDWORD
    
    std::cout << "Vertex operations completed successfully" << std::endl;
}

// Test 3: Face operations
void testFaceOperations() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "FaceTest");
    
    // Create vertices first
    mesh.SetVertexCount(4);
    VxVector pos1(0.0f, 0.0f, 0.0f);
    VxVector pos2(1.0f, 0.0f, 0.0f);
    VxVector pos3(0.0f, 1.0f, 0.0f);
    VxVector pos4(1.0f, 1.0f, 0.0f);
    
    mesh.SetVertexPosition(0, &pos1);
    mesh.SetVertexPosition(1, &pos2);
    mesh.SetVertexPosition(2, &pos3);
    mesh.SetVertexPosition(3, &pos4);
    
    // Test face creation
    assert(mesh.SetFaceCount(2) == TRUE);
    assert(mesh.GetFaceCount() == 2);
    
    // Set face vertex indices
    mesh.SetFaceVertexIndex(0, 0, 1, 2); // First triangle
    mesh.SetFaceVertexIndex(1, 1, 3, 2); // Second triangle
    
    // Verify face vertex indices
    int v1, v2, v3;
    mesh.GetFaceVertexIndex(0, v1, v2, v3);
    assert(v1 == 0 && v2 == 1 && v3 == 2);
    
    mesh.GetFaceVertexIndex(1, v1, v2, v3);
    assert(v1 == 1 && v2 == 3 && v3 == 2);
    
    // Test face indices array
    CKWORD* faceIndices = mesh.GetFacesIndices();
    assert(faceIndices != nullptr);
    assert(faceIndices[0] == 0 && faceIndices[1] == 1 && faceIndices[2] == 2);
    assert(faceIndices[3] == 1 && faceIndices[4] == 3 && faceIndices[5] == 2);
    
    std::cout << "Face operations completed successfully" << std::endl;
}

// Test 4: Line operations
void testLineOperations() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "LineTest");
    
    // Create vertices
    mesh.SetVertexCount(4);
    for (int i = 0; i < 4; i++) {
        VxVector pos((float)i, (float)i, 0.0f);
        mesh.SetVertexPosition(i, &pos);
    }
    
    // Test line creation
    assert(mesh.SetLineCount(3) == TRUE);
    assert(mesh.GetLineCount() == 3);
    
    // Set line vertex indices
    mesh.SetLine(0, 0, 1);
    mesh.SetLine(1, 1, 2);
    mesh.SetLine(2, 2, 3);
    
    // Verify line vertex indices
    int v1, v2;
    mesh.GetLine(0, v1, v2);
    assert(v1 == 0 && v2 == 1);
    
    mesh.GetLine(1, v1, v2);
    assert(v1 == 1 && v2 == 2);
    
    mesh.GetLine(2, v1, v2);
    assert(v1 == 2 && v2 == 3);
    
    // Test line indices array
    CKWORD* lineIndices = mesh.GetLineIndices();
    assert(lineIndices != nullptr);
    assert(lineIndices[0] == 0 && lineIndices[1] == 1);
    assert(lineIndices[2] == 1 && lineIndices[3] == 2);
    assert(lineIndices[4] == 2 && lineIndices[5] == 3);
    
    std::cout << "Line operations completed successfully" << std::endl;
}

// Test 5: Material channel operations
void testMaterialChannels() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "MaterialTest");
    
    // Test adding material channels
    CKMaterial* material1 = (CKMaterial*)0x12345678; // Fake material pointer for testing
    CKMaterial* material2 = (CKMaterial*)0x87654321; // Another fake material
    
    int channel1 = mesh.AddChannel(material1, TRUE);
    assert(channel1 >= 0);
    assert(mesh.GetMaterialCount() == 1);
    assert(mesh.GetMaterial(0) == material1);
    
    int channel2 = mesh.AddChannel(material2, TRUE);
    assert(channel2 >= 1);
    assert(mesh.GetMaterialCount() == 2);
    assert(mesh.GetMaterial(1) == material2);
    
    // Test getting channel by material
    int foundChannel = mesh.GetChannelByMaterial(material1);
    assert(foundChannel == 0);
    
    foundChannel = mesh.GetChannelByMaterial(material2);
    assert(foundChannel == 1);
    
    // Test UV coordinates
    mesh.SetVertexCount(2);
    mesh.SetVertexTextureCoordinates(0, 0.0f, 0.0f, 0);
    mesh.SetVertexTextureCoordinates(1, 1.0f, 1.0f, 0);
    
    float u, v;
    mesh.GetVertexTextureCoordinates(0, &u, &v, 0);
    assert(u == 0.0f && v == 0.0f);
    
    mesh.GetVertexTextureCoordinates(1, &u, &v, 0);
    assert(u == 1.0f && v == 1.0f);
    
    // Test removing channels
    mesh.RemoveChannel(0);
    assert(mesh.GetMaterialCount() == 1);
    assert(mesh.GetMaterial(0) == material2);
    
    std::cout << "Material channel operations completed successfully" << std::endl;
}

// Test 6: Vertex weights
void testVertexWeights() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "WeightTest");
    
    // Test vertex weights
    mesh.SetVertexCount(4);
    mesh.SetVertexWeightsCount(4);
    assert(mesh.GetVertexWeightsCount() == 4);
    
    // Set vertex weights
    mesh.SetVertexWeight(0, 1.0f);
    mesh.SetVertexWeight(1, 0.5f);
    mesh.SetVertexWeight(2, 0.75f);
    mesh.SetVertexWeight(3, 0.25f);
    
    // Verify vertex weights
    assert(std::abs(mesh.GetVertexWeight(0) - 1.0f) < 0.001f);
    assert(std::abs(mesh.GetVertexWeight(1) - 0.5f) < 0.001f);
    assert(std::abs(mesh.GetVertexWeight(2) - 0.75f) < 0.001f);
    assert(std::abs(mesh.GetVertexWeight(3) - 0.25f) < 0.001f);
    
    // Test vertex weights pointer
    float* weightsPtr = mesh.GetVertexWeightsPtr();
    assert(weightsPtr != nullptr);
    
    std::cout << "Vertex weight operations completed successfully" << std::endl;
}

// Test 7: Bounding volume calculations
void testBoundingVolumes() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "BoundingTest");
    
    // Create a simple cube
    mesh.SetVertexCount(8);
    
    // Define cube vertices
    VxVector vertices[8] = {
        VxVector(-1.0f, -1.0f, -1.0f),
        VxVector( 1.0f, -1.0f, -1.0f),
        VxVector( 1.0f,  1.0f, -1.0f),
        VxVector(-1.0f,  1.0f, -1.0f),
        VxVector(-1.0f, -1.0f,  1.0f),
        VxVector( 1.0f, -1.0f,  1.0f),
        VxVector( 1.0f,  1.0f,  1.0f),
        VxVector(-1.0f,  1.0f,  1.0f)
    };
    
    for (int i = 0; i < 8; i++) {
        mesh.SetVertexPosition(i, &vertices[i]);
    }
    
    // Test bounding box
    const VxBbox& bbox = mesh.GetLocalBox();
    assert(vectorsEqual(bbox.min, VxVector(-1.0f, -1.0f, -1.0f)));
    assert(vectorsEqual(bbox.max, VxVector(1.0f, 1.0f, 1.0f)));
    
    // Test barycenter
    VxVector barycenter;
    mesh.GetBaryCenter(&barycenter);
    assert(vectorsEqual(barycenter, VxVector(0.0f, 0.0f, 0.0f)));
    
    // Test radius (should be sqrt(3) for unit cube)
    float radius = mesh.GetRadius();
    assert(std::abs(radius - std::sqrt(3.0f)) < 0.1f);
    
    std::cout << "Bounding volume calculations completed successfully" << std::endl;
}

// Test 8: Mesh flags and properties
void testMeshFlags() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "FlagsTest");
    
    // Test transparency
    assert(mesh.IsTransparent() == FALSE);
    mesh.SetTransparent(TRUE);
    assert(mesh.IsTransparent() == TRUE);
    mesh.SetTransparent(FALSE);
    assert(mesh.IsTransparent() == FALSE);
    
    // Test wrap mode
    mesh.SetWrapMode(VXTEXTURE_WRAP);
    assert(mesh.GetWrapMode() == VXTEXTURE_WRAP);
    
    mesh.SetWrapMode(VXTEXTURE_CLAMP);
    assert(mesh.GetWrapMode() == VXTEXTURE_CLAMP);
    
    // Test lighting mode
    mesh.SetLitMode(VX_LITMESH);
    assert(mesh.GetLitMode() == VX_LITMESH);
    
    mesh.SetLitMode(VX_PRELITMESH);
    assert(mesh.GetLitMode() == VX_PRELITMESH);
    
    std::cout << "Mesh flags and properties completed successfully" << std::endl;
}

// Test 9: Mesh operations
void testMeshOperations() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "OperationsTest");
    
    // Create a simple mesh
    mesh.SetVertexCount(3);
    mesh.SetFaceCount(1);
    
    VxVector pos1(0.0f, 0.0f, 0.0f);
    VxVector pos2(1.0f, 0.0f, 0.0f);
    VxVector pos3(0.0f, 1.0f, 0.0f);
    
    mesh.SetVertexPosition(0, &pos1);
    mesh.SetVertexPosition(1, &pos2);
    mesh.SetVertexPosition(2, &pos3);
    
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    
    // Test inverse winding
    mesh.InverseWinding();
    
    int v1, v2, v3;
    mesh.GetFaceVertexIndex(0, v1, v2, v3);
    assert(v1 == 0 && v2 == 2 && v3 == 1); // Second and third vertices should be swapped
    
    // Test clean operation
    mesh.Clean(TRUE); // Keep vertices
    
    // Test consolidate
    mesh.Consolidate();
    
    // Test unoptimize
    mesh.UnOptimize();
    
    std::cout << "Mesh operations completed successfully" << std::endl;
}

// Test 10: Serialization (basic test)
void testSerialization() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "SerializationTest");
    
    // Create a test mesh
    mesh.SetVertexCount(3);
    mesh.SetFaceCount(1);
    
    VxVector pos1(0.0f, 0.0f, 0.0f);
    VxVector pos2(1.0f, 0.0f, 0.0f);
    VxVector pos3(0.0f, 1.0f, 0.0f);
    
    mesh.SetVertexPosition(0, &pos1);
    mesh.SetVertexPosition(1, &pos2);
    mesh.SetVertexPosition(2, &pos3);
    
    mesh.SetVertexColor(0, 0xFFFF0000);
    mesh.SetVertexColor(1, 0xFF00FF00);
    mesh.SetVertexColor(2, 0xFF0000FF);
    
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    
    // Test save (basic test - we can't easily test full serialization without a CKFile)
    CKStateChunk* chunk = mesh.Save(nullptr, 0);
    assert(chunk != nullptr);
    
    // Test load
    RCKMesh loadedMesh(&context, "LoadedMesh");
    CKERROR result = loadedMesh.Load(chunk, nullptr);
    assert(result == CK_OK);
    
    // Verify loaded data
    assert(loadedMesh.GetVertexCount() == 3);
    assert(loadedMesh.GetFaceCount() == 1);
    
    VxVector loadedPos;
    loadedMesh.GetVertexPosition(0, &loadedPos);
    assert(vectorsEqual(loadedPos, pos1));
    
    assert(loadedMesh.GetVertexColor(0) == 0xFFFF0000);
    
    // Cleanup
    delete chunk;
    
    std::cout << "Serialization test completed successfully" << std::endl;
}

// Test 11: Memory management
void testMemoryManagement() {
    CKContext context(nullptr);
    
    // Test multiple mesh creation and destruction
    for (int i = 0; i < 100; i++) {
        RCKMesh* mesh = new RCKMesh(&context, "MemoryTest");
        
        // Add some data
        mesh->SetVertexCount(10);
        mesh->SetFaceCount(5);
        mesh->SetLineCount(3);
        
        // Add material channels
        CKMaterial* fakeMaterial = (CKMaterial*)0x12345678;
        mesh->AddChannel(fakeMaterial, TRUE);
        
        // Add vertex weights
        mesh->SetVertexWeightsCount(10);
        
        // Test memory occupation
        int memoryUsage = mesh->GetMemoryOccupation();
        assert(memoryUsage > sizeof(RCKMesh));
        
        delete mesh;
    }
    
    std::cout << "Memory management test completed successfully" << std::endl;
}

// Test 12: Edge cases and error handling
void testEdgeCases() {
    CKContext context(nullptr);
    RCKMesh mesh(&context, "EdgeCaseTest");
    
    // Test invalid indices
    mesh.SetVertexCount(5);
    
    VxVector pos(1.0f, 2.0f, 3.0f);
    mesh.SetVertexPosition(-1, &pos); // Should not crash
    mesh.SetVertexPosition(10, &pos); // Should not crash
    
    VxVector readPos;
    mesh.GetVertexPosition(-1, &readPos); // Should not crash
    mesh.GetVertexPosition(10, &readPos); // Should not crash
    
    // Test empty mesh operations
    RCKMesh emptyMesh(&context, "EmptyMesh");
    assert(emptyMesh.GetVertexCount() == 0);
    assert(emptyMesh.GetFaceCount() == 0);
    assert(emptyMesh.GetLineCount() == 0);
    
    // Test operations on empty mesh
    emptyMesh.BuildNormals();
    emptyMesh.BuildFaceNormals();
    
    // Test invalid face operations
    mesh.SetFaceCount(2);
    mesh.SetFaceVertexIndex(-1, 0, 1, 2); // Should not crash
    mesh.SetFaceVertexIndex(10, 0, 1, 2); // Should not crash
    
    // Test invalid material operations
    CKMaterial* nullMaterial = nullptr;
    int channelIndex = mesh.GetChannelByMaterial(nullMaterial);
    assert(channelIndex == -1);
    
    // Test zero counts
    assert(mesh.SetVertexCount(0) == TRUE);
    assert(mesh.GetVertexCount() == 0);
    
    assert(mesh.SetFaceCount(0) == TRUE);
    assert(mesh.GetFaceCount() == 0);
    
    assert(mesh.SetLineCount(0) == TRUE);
    assert(mesh.GetLineCount() == 0);
    
    std::cout << "Edge cases and error handling completed successfully" << std::endl;
}

int main() {
    std::cout << "=== CKMesh Comprehensive Test Suite ===" << std::endl;
    std::cout << "Testing CKMesh implementation..." << std::endl << std::endl;
    
    // Run all tests
    g_testFramework.runTest("Mesh Creation", testMeshCreation);
    g_testFramework.runTest("Vertex Operations", testVertexOperations);
    g_testFramework.runTest("Face Operations", testFaceOperations);
    g_testFramework.runTest("Line Operations", testLineOperations);
    g_testFramework.runTest("Material Channels", testMaterialChannels);
    g_testFramework.runTest("Vertex Weights", testVertexWeights);
    g_testFramework.runTest("Bounding Volumes", testBoundingVolumes);
    g_testFramework.runTest("Mesh Flags", testMeshFlags);
    g_testFramework.runTest("Mesh Operations", testMeshOperations);
    g_testFramework.runTest("Serialization", testSerialization);
    g_testFramework.runTest("Memory Management", testMemoryManagement);
    g_testFramework.runTest("Edge Cases", testEdgeCases);
    
    // Print summary
    g_testFramework.printSummary();
    
    if (g_testFramework.allTestsPassed()) {
        std::cout << "\n🎉 All tests passed! CKMesh implementation is working correctly." << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Some tests failed. Please review the implementation." << std::endl;
        return 1;
    }
}