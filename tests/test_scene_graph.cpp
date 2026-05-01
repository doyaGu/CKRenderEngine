#include <stdio.h>
#include <stdlib.h>

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

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Deleting parent detaches child nodes", &DeleteParentNodeDetachesChildNodes);
    return tests.ExitCode();
}
