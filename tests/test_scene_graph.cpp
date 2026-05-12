#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "CKSceneGraph.h"
#include "TestTriangleMultiset.h"

namespace {

void DeleteParentNodeDetachesChildNodes() {
    CKSceneGraphNode *parent = new CKSceneGraphNode(nullptr);
    CKSceneGraphNode child(nullptr);

    parent->AddNode(&child);
    TestCheck(child.m_Parent == parent, "Child was not attached to parent");

    delete parent;

    TestCheck(child.m_Parent == nullptr, "Deleting a parent node should detach child nodes");
    TestCheck(child.m_Index == 0, "Detached child index should be reset");
}

void TransparentObjectLayoutMatchesOriginalDllOffsets() {
    TestCheck(offsetof(CKTransparentObject, m_Node) == 0,
              "CKTransparentObject node pointer must stay at offset +0");
    TestCheck(offsetof(CKTransparentObject, m_ZhMin) == sizeof(CKSceneGraphNode *),
              "CKTransparentObject ZhMin must stay immediately after the node pointer");
    TestCheck(offsetof(CKTransparentObject, m_ZhMax) == sizeof(CKSceneGraphNode *) + sizeof(float),
              "CKTransparentObject ZhMax must stay immediately after ZhMin");
#if !defined(_WIN64)
    TestCheck(sizeof(CKTransparentObject) == 12,
              "Win32 CKTransparentObject must remain 12 bytes like the original CK2_3D.dll");
#endif
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Deleting parent detaches child nodes", &DeleteParentNodeDetachesChildNodes);
    tests.Run("Transparent object layout matches original DLL offsets", &TransparentObjectLayoutMatchesOriginalDllOffsets);
    return tests.ExitCode();
}
