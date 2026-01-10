#include <iostream>
#include <cassert>
#include <cmath>
#include "RCKMesh.h"
#include "CKContext.h"
#include "VxMath.h"

// Simple test framework
class SimpleTest {
private:
    int totalTests = 0;
    int passedTests = 0;
    
public:
    void runTest(const std::string& testName, bool (*testFunc)()) {
        totalTests++;
        std::cout << "Running test: " << testName << "... ";
        
        try {
            if (testFunc()) {
                passedTests++;
                std::cout << "PASSED" << std::endl;
            } else {
                std::cout << "FAILED" << std::endl;
            }
        } catch (...) {
            std::cout << "FAILED: Exception" << std::endl;
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

// Helper function to compare vectors with tolerance
bool vectorsEqual(const VxVector& a, const VxVector& b, float tolerance = 0.001f) {
    return std::abs(a.x - b.x) < tolerance &&
           std::abs(a.y - b.y) < tolerance &&
           std::abs(a.z - b.z) < tolerance;
}

// Test 1: Basic mesh creation and properties
bool testMeshCreation() {
    std::cout << "Creating mesh with null context..." << std::endl;
    
    // Test basic mesh creation (without full context setup)
    RCKMesh* mesh = new RCKMesh(nullptr, "TestMesh");
    if (!mesh) {
        std::cout << "ERROR: Failed to create mesh" << std::endl;
        return false;
    }
    
    // Test basic properties
    bool success = true;
    
    // Test class ID
    if (mesh->GetClassID() != CKCID_MESH) {
        std::cout << "ERROR: Wrong class ID" << std::endl;
        success = false;
    }
    
    // Test initial state
    if (mesh->GetVertexCount() != 0) {
        std::cout << "ERROR: Initial vertex count not zero" << std::endl;
        success = false;
    }
    
    if (mesh->GetFaceCount() != 0) {
        std::cout << "ERROR: Initial face count not zero" << std::endl;
        success = false;
    }
    
    if (mesh->GetLineCount() != 0) {
        std::cout << "ERROR: Initial line count not zero" << std::endl;
        success = false;
    }
    
    if (mesh->GetMaterialCount() != 0) {
        std::cout << "ERROR: Initial material count not zero" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 2: Vertex operations
bool testVertexOperations() {
    std::cout << "Testing vertex operations..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "VertexTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Test vertex count setting
    if (!mesh->SetVertexCount(4)) {
        std::cout << "ERROR: Failed to set vertex count" << std::endl;
        success = false;
    }
    
    if (mesh->GetVertexCount() != 4) {
        std::cout << "ERROR: Vertex count mismatch" << std::endl;
        success = false;
    }
    
    // Test vertex position operations
    VxVector pos1(1.0f, 2.0f, 3.0f);
    VxVector pos2(-1.0f, -2.0f, -3.0f);
    VxVector pos3(0.0f, 5.0f, -2.5f);
    VxVector pos4(10.0f, 0.0f, 0.0f);
    
    mesh->SetVertexPosition(0, &pos1);
    mesh->SetVertexPosition(1, &pos2);
    mesh->SetVertexPosition(2, &pos3);
    mesh->SetVertexPosition(3, &pos4);
    
    // Verify positions
    VxVector readPos;
    mesh->GetVertexPosition(0, &readPos);
    if (!vectorsEqual(readPos, pos1)) {
        std::cout << "ERROR: Position 0 mismatch" << std::endl;
        success = false;
    }
    
    mesh->GetVertexPosition(1, &readPos);
    if (!vectorsEqual(readPos, pos2)) {
        std::cout << "ERROR: Position 1 mismatch" << std::endl;
        success = false;
    }
    
    // Test vertex normal operations
    VxVector normal(0.0f, 1.0f, 0.0f);
    mesh->SetVertexNormal(0, &normal);
    
    mesh->GetVertexNormal(0, &readPos);
    if (!vectorsEqual(readPos, normal)) {
        std::cout << "ERROR: Normal 0 mismatch" << std::endl;
        success = false;
    }
    
    // Test vertex color operations
    mesh->SetVertexColor(0, 0xFFFF0000); // Red
    mesh->SetVertexColor(1, 0xFF00FF00); // Green
    mesh->SetVertexColor(2, 0x0000FF00); // Blue
    mesh->SetVertexColor(3, 0xFFFFFFFF); // White
    
    if (mesh->GetVertexColor(0) != 0xFFFF0000) {
        std::cout << "ERROR: Color 0 mismatch" << std::endl;
        success = false;
    }
    
    // Test vertex data pointers
    CKDWORD stride;
    void* positionsPtr = mesh->GetPositionsPtr(&stride);
    if (!positionsPtr || stride == 0) {
        std::cout << "ERROR: Invalid positions pointer" << std::endl;
        success = false;
    }
    
    void* colorsPtr = mesh->GetColorsPtr(&stride);
    if (!colorsPtr || stride == 0) {
        std::cout << "ERROR: Invalid colors pointer" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 3: Face operations
bool testFaceOperations() {
    std::cout << "Testing face operations..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "FaceTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Set up vertices first
    mesh->SetVertexCount(3);
    VxVector pos1(0.0f, 0.0f, 0.0f);
    VxVector pos2(1.0f, 0.0f, 0.0f);
    VxVector pos3(0.5f, 1.0f, 0.0f);
    
    mesh->SetVertexPosition(0, &pos1);
    mesh->SetVertexPosition(1, &pos2);
    mesh->SetVertexPosition(2, &pos3);
    
    // Test face count setting
    if (!mesh->SetFaceCount(2)) {
        std::cout << "ERROR: Failed to set face count" << std::endl;
        success = false;
    }
    
    if (mesh->GetFaceCount() != 2) {
        std::cout << "ERROR: Face count mismatch" << std::endl;
        success = false;
    }
    
    // Test face vertex indices
    mesh->SetFaceVertexIndex(0, 0, 1, 2);
    mesh->SetFaceVertexIndex(1, 1, 2, 0);
    
    int v1, v2, v3;
    mesh->GetFaceVertexIndex(0, v1, v2, v3);
    if (v1 != 0 || v2 != 1 || v3 != 2) {
        std::cout << "ERROR: Face 0 vertex indices mismatch" << std::endl;
        success = false;
    }
    
    mesh->GetFaceVertexIndex(1, v1, v2, v3);
    if (v1 != 1 || v2 != 2 || v3 != 0) {
        std::cout << "ERROR: Face 1 vertex indices mismatch" << std::endl;
        success = false;
    }
    
    // Test face indices array
    CKWORD* faceIndices = mesh->GetFacesIndices();
    if (!faceIndices) {
        std::cout << "ERROR: Invalid face indices pointer" << std::endl;
        success = false;
    }
    
    // Check face indices array content
    if (faceIndices[0] != 0 || faceIndices[1] != 1 || faceIndices[2] != 2 ||
        faceIndices[3] != 1 || faceIndices[4] != 2 || faceIndices[5] != 0) {
        std::cout << "ERROR: Face indices array content mismatch" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 4: Line operations
bool testLineOperations() {
    std::cout << "Testing line operations..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "LineTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Set up vertices
    mesh->SetVertexCount(4);
    VxVector pos1(0.0f, 0.0f, 0.0f);
    VxVector pos2(1.0f, 0.0f, 0.0f);
    VxVector pos3(2.0f, 0.0f, 0.0f);
    VxVector pos4(3.0f, 0.0f, 0.0f);
    
    mesh->SetVertexPosition(0, &pos1);
    mesh->SetVertexPosition(1, &pos2);
    mesh->SetVertexPosition(2, &pos3);
    mesh->SetVertexPosition(3, &pos4);
    
    // Test line count setting
    if (!mesh->SetLineCount(3)) {
        std::cout << "ERROR: Failed to set line count" << std::endl;
        success = false;
    }
    
    if (mesh->GetLineCount() != 3) {
        std::cout << "ERROR: Line count mismatch" << std::endl;
        success = false;
    }
    
    // Test line vertex indices
    mesh->SetLine(0, 0, 1);
    mesh->SetLine(1, 1, 2);
    mesh->SetLine(2, 2, 3);
    
    int v1, v2;
    mesh->GetLine(0, v1, v2);
    if (v1 != 0 || v2 != 1) {
        std::cout << "ERROR: Line 0 vertex indices mismatch" << std::endl;
        success = false;
    }
    
    mesh->GetLine(1, v1, v2);
    if (v1 != 1 || v2 != 2) {
        std::cout << "ERROR: Line 1 vertex indices mismatch" << std::endl;
        success = false;
    }
    
    // Test line indices array
    CKWORD* lineIndices = mesh->GetLineIndices();
    if (!lineIndices) {
        std::cout << "ERROR: Invalid line indices pointer" << std::endl;
        success = false;
    }
    
    // Check line indices array content
    if (lineIndices[0] != 0 || lineIndices[1] != 1 ||
        lineIndices[2] != 2 || lineIndices[3] != 3 ||
        lineIndices[4] != 1 || lineIndices[5] != 2) {
        std::cout << "ERROR: Line indices array content mismatch" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 5: Mesh flags and properties
bool testMeshProperties() {
    std::cout << "Testing mesh properties..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "PropertiesTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Test transparency
    if (mesh->IsTransparent()) {
        std::cout << "ERROR: Initial transparency state" << std::endl;
        success = false;
    }
    
    mesh->SetTransparent(TRUE);
    if (!mesh->IsTransparent()) {
        std::cout << "ERROR: Set transparency failed" << std::endl;
        success = false;
    }
    
    mesh->SetTransparent(FALSE);
    if (mesh->IsTransparent()) {
        std::cout << "ERROR: Clear transparency failed" << std::endl;
        success = false;
    }
    
    // Test flags
    CKDWORD originalFlags = mesh->GetFlags();
    mesh->SetFlags(0x12345678);
    CKDWORD newFlags = mesh->GetFlags();
    if ((newFlags & 0x7FE39A) != (0x12345678 & 0x7FE39A)) {
        std::cout << "ERROR: Flag setting failed" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 6: Bounding volumes
bool testBoundingVolumes() {
    std::cout << "Testing bounding volumes..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "BoundingTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Create a simple cube
    mesh->SetVertexCount(8);
    
    VxVector vertices[8] = {
        VxVector(-1.0f, -1.0f, -1.0f),  // 0
        VxVector( 1.0f, -1.0f, -1.0f),  // 1
        VxVector( 1.0f,  1.0f, -1.0f),  // 2
        VxVector(-1.0f,  1.0f, -1.0f),  // 3
        VxVector( 1.0f,  1.0f,  1.0f),  // 4
        VxVector(-1.0f,  1.0f,  1.0f),  // 5
        VxVector(-1.0f, -1.0f,  1.0f),  // 6
        VxVector( 1.0f, -1.0f,  1.0f)   // 7
    };
    
    for (int i = 0; i < 8; i++) {
        mesh->SetVertexPosition(i, &vertices[i]);
    }
    
    // Test bounding box
    const VxBbox& bbox = mesh->GetLocalBox();
    if (!vectorsEqual(bbox.Min, VxVector(-1.0f, -1.0f, -1.0f)) {
        std::cout << "ERROR: Bounding box min incorrect" << std::endl;
        success = false;
    }
    
    if (!vectorsEqual(bbox.Max, VxVector(1.0f, 1.0f, 1.0f))) {
        std::cout << "ERROR: Bounding box max incorrect" << std::endl;
        success = false;
    }
    
    // Test barycenter
    VxVector barycenter;
    mesh->GetBaryCenter(&barycenter);
    if (!vectorsEqual(barycenter, VxVector(0.0f, 0.0f, 0.0f))) {
        std::cout << "ERROR: Barycenter incorrect" << std::endl;
        success = false;
    }
    
    // Test radius
    float radius = mesh->GetRadius();
    float expectedRadius = std::sqrt(3.0f * 3.0f + 3.0f); // Distance from center to corner
    if (std::abs(radius - expectedRadius) > 0.1f) {
        std::cout << "ERROR: Radius incorrect" << std::endl;
        success = false;
    }
    
    delete mesh;
    return success;
}

// Test 7: Memory management
bool testMemoryManagement() {
    std::cout << "Testing memory management..." << std::endl;
    
    bool success = true;
    
    // Test multiple creation/destruction
    for (int i = 0; i < 10; i++) {
        RCKMesh* mesh = new RCKMesh(nullptr, "MemoryTest");
        if (!mesh) {
            std::cout << "ERROR: Failed to create mesh " << i << std::endl;
            success = false;
            break;
        }
        
        // Add some data
        mesh->SetVertexCount(100);
        mesh->SetFaceCount(50);
        
        // Test memory occupation
        int memoryUsage = mesh->GetMemoryOccupation();
        if (memoryUsage <= sizeof(RCKMesh)) {
            std::cout << "ERROR: Memory usage too low " << i << std::endl;
            success = false;
        }
        
        delete mesh;
    }
    
    return success;
}

// Test 8: Edge cases
bool testEdgeCases() {
    std::cout << "Testing edge cases..." << std::endl;
    
    RCKMesh* mesh = new RCKMesh(nullptr, "EdgeCaseTest");
    if (!mesh) return false;
    
    bool success = true;
    
    // Test zero counts
    if (!mesh->SetVertexCount(0)) {
        std::cout << "ERROR: Failed to set zero vertex count" << std::endl;
        success = false;
    }
    
    if (!mesh->SetFaceCount(0)) {
        std::cout << "ERROR: Failed to set zero face count" << std::endl;
        success = false;
    }
    
    if (!mesh->SetLineCount(0)) {
        std::cout << "ERROR: Failed to set zero line count" << std::endl;
        success = false;
    }
    
    // Test invalid indices (should not crash)
    VxVector pos(1.0f, 2.0f, 3.0f);
    mesh->SetVertexPosition(0, &pos);
    
    mesh->SetVertexPosition(-1, &pos); // Invalid index
    mesh->SetVertexPosition(100, &pos); // Invalid index
    mesh->GetVertexPosition(-1, &pos); // Invalid read
    mesh->GetVertexPosition(100, &pos); // Invalid read
    
    mesh->SetFaceVertexIndex(0, 0, 1, 2);
    mesh->SetFaceVertexIndex(-1, 0, 1, 2); // Invalid face index
    mesh->GetFaceVertexIndex(-1, 0, 1, 2); // Invalid face read
    
    mesh->SetLine(0, 0, 1);
    mesh->SetLine(-1, 0, 1); // Invalid line index
    mesh->GetLine(-1, 0, 1); // Invalid line read
    
    // Test operations on empty mesh
    mesh->SetVertexCount(0);
    mesh->SetFaceCount(0);
    mesh->SetLineCount(0);
    
    // These should not crash
    mesh->GetVertexCount();
    mesh->GetFaceCount();
    mesh->GetLineCount();
    mesh->GetPositionsPtr(nullptr);
    mesh->GetFacesIndices();
    mesh->GetLineIndices();
    
    delete mesh;
    return success;
}

int main() {
    std::cout << "=== CKMesh Simple Test Suite ===" << std::endl;
    std::cout << "Testing CKMesh implementation..." << std::endl << std::endl;
    
    SimpleTest tester;
    
    // Run all tests
    tester.runTest("Mesh Creation", testMeshCreation);
    tester.runTest("Vertex Operations", testVertexOperations);
    tester.runTest("Face Operations", testFaceOperations);
    tester.runTest("Line Operations", testLineOperations);
    tester.runTest("Mesh Properties", testMeshProperties);
    tester.runTest("Bounding Volumes", testBoundingVolumes);
    tester.runTest("Memory Management", testMemoryManagement);
    tester.runTest("Edge Cases", testEdgeCases);
    
    // Print summary
    tester.printSummary();
    
    if (tester.allTestsPassed()) {
        std::cout << "\n🎉 All tests passed! CKMesh implementation is working correctly." << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Some tests failed. Please review the implementation." << std::endl;
        return 1;
    }
}