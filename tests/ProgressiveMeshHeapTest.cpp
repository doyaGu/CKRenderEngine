#include <gtest/gtest.h>
#include <map>
#include <set>
#include <vector>
#include "CKRenderEngineTypes.h"

namespace {

struct OwnedHeap {
    PMEdgeCollapseData Data;
    std::vector<PMVertexEx *> Created;
    std::map<int, float> Active;

    PMVertexEx *Add(float cost) {
        const int label = static_cast<int>(Created.size());
        PMVertexEx *vertex = new PMVertexEx(VxVector(float(label), 0.0f, 0.0f), label);
        vertex->collapseCost = cost;
        vertex->arrayIndex = Data.vertices.Size();
        Data.vertices.PushBack(vertex);
        Created.push_back(vertex);
        Active[label] = cost;
        Data.AddToHeap(vertex);
        return vertex;
    }

    void Change(PMVertexEx *vertex, float cost) {
        vertex->collapseCost = cost;
        Active[vertex->originalIndex] = cost;
        Data.UpdateHeapPosition(vertex);
    }

    float Minimum() const {
        std::map<int, float>::const_iterator it = Active.begin();
        float result = it->second;
        for (; it != Active.end(); ++it) {
            if (it->second < result)
                result = it->second;
        }
        return result;
    }
};

// The oracle is a linear minimum over a separate label/cost map. It neither
// repeats the production heap algorithm nor requires a particular tie order.
void ExpectConsistent(const OwnedHeap &owner) {
    std::set<const PMVertexEx *> owned;
    for (int i = 0; i < owner.Data.vertices.Size(); ++i) {
        const PMVertexEx *vertex = owner.Data.vertices[i];
        ASSERT_NE(nullptr, vertex);
        EXPECT_EQ(i, vertex->arrayIndex);
        EXPECT_TRUE(owned.insert(vertex).second);
    }

    std::set<int> seen;
    for (int i = 0; i < owner.Data.heap.Size(); ++i) {
        const PMVertexEx *vertex = owner.Data.heap[i];
        if (!vertex) {
            // A hole has lower priority than every finite, live entry.
            const int left = 2 * i + 1;
            const int right = left + 1;
            if (left < owner.Data.heap.Size()) EXPECT_EQ(nullptr, owner.Data.heap[left]);
            if (right < owner.Data.heap.Size()) EXPECT_EQ(nullptr, owner.Data.heap[right]);
            continue;
        }
        ASSERT_EQ(1u, owned.count(vertex));
        EXPECT_EQ(i, vertex->heapIndex);
        ASSERT_EQ(1u, owner.Active.count(vertex->originalIndex));
        EXPECT_FLOAT_EQ(owner.Active.find(vertex->originalIndex)->second, vertex->collapseCost);
        EXPECT_TRUE(seen.insert(vertex->originalIndex).second);
        if (i > 0) {
            const PMVertexEx *parent = owner.Data.heap[(i - 1) / 2];
            ASSERT_NE(nullptr, parent);
            EXPECT_LE(parent->collapseCost, vertex->collapseCost);
        }
    }
    EXPECT_EQ(owner.Active.size(), seen.size());
    for (int i = 0; i < owner.Data.vertices.Size(); ++i) {
        const PMVertexEx *vertex = owner.Data.vertices[i];
        if (owner.Active.count(vertex->originalIndex)) {
            ASSERT_GE(vertex->heapIndex, 0);
            ASSERT_LT(vertex->heapIndex, owner.Data.heap.Size());
            EXPECT_EQ(vertex, owner.Data.heap[vertex->heapIndex]);
        } else {
            EXPECT_EQ(-1, vertex->heapIndex);
        }
    }
}

void DrainAgainstOracle(OwnedHeap &owner) {
    while (!owner.Active.empty()) {
        const float expected = owner.Minimum();
        PMVertexEx *vertex = owner.Data.PopMinFromHeap();
        ASSERT_NE(nullptr, vertex);
        ASSERT_EQ(1u, owner.Active.count(vertex->originalIndex));
        EXPECT_FLOAT_EQ(expected, vertex->collapseCost);
        EXPECT_EQ(-1, vertex->heapIndex);
        owner.Active.erase(vertex->originalIndex);
        ExpectConsistent(owner);
    }
    EXPECT_EQ(nullptr, owner.Data.PopMinFromHeap());
}

void RegisterDefaultFace(PMEdgeCollapseData &owner, PMFace *face,
                         PMVertexEx *a, PMVertexEx *b, PMVertexEx *c) {
    // The default constructor intentionally leaves an inert face. Build this
    // graph explicitly so the test is independent of the parameterized
    // constructor's registration contract.
    face->vertices[0] = a;
    face->vertices[1] = b;
    face->vertices[2] = c;
    face->arrayIndex = owner.faces.Size();
    owner.faces.PushBack(face);
    PMVertexEx *vertices[] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        vertices[i]->faces.PushBack(face);
        vertices[i]->AddNeighbor(vertices[(i + 1) % 3]);
        vertices[i]->AddNeighbor(vertices[(i + 2) % 3]);
    }
}

} // namespace

TEST(ProgressiveMeshHeapTest, EmptyAndSingleEntryExposeCostsAndBorrowedPopWithoutDeletingTheOwner) {
    OwnedHeap owner;
    EXPECT_EQ(0, owner.Data.vertices.Size());
    EXPECT_EQ(0, owner.Data.faces.Size());
    EXPECT_EQ(0, owner.Data.heap.Size());
    EXPECT_EQ(nullptr, owner.Data.PopMinFromHeap());
    const float missing = owner.Data.GetCost(0);
    EXPECT_FLOAT_EQ(9.9999998e12f, missing);

    PMVertexEx *vertex = owner.Add(3.5f);
    EXPECT_FLOAT_EQ(3.5f, owner.Data.GetCost(0));
    EXPECT_FLOAT_EQ(missing, owner.Data.GetCost(1));
    ASSERT_EQ(vertex, owner.Data.PopMinFromHeap());
    owner.Active.clear();
    EXPECT_EQ(1, owner.Data.vertices.Size());
    EXPECT_EQ(1, owner.Data.heap.Size());
    EXPECT_EQ(nullptr, owner.Data.heap[0]);
    EXPECT_FLOAT_EQ(missing, owner.Data.GetCost(0));
    EXPECT_EQ(0, vertex->originalIndex);
    EXPECT_EQ(0, vertex->arrayIndex);
    EXPECT_EQ(-1, vertex->heapIndex);
    ExpectConsistent(owner);
}

TEST(ProgressiveMeshHeapTest, MixedFiniteCostsAndTiesExtractAccordingToAnIndependentMinimumOracle) {
    OwnedHeap owner;
    const float costs[] = {4.0f, -2.0f, 7.5f, 0.0f, 4.0f, -2.0f, 1.25f, 9.0f, 0.5f};
    for (float cost : costs) {
        owner.Add(cost);
        ExpectConsistent(owner);
    }
    DrainAgainstOracle(owner);
    EXPECT_EQ(9, owner.Data.vertices.Size());
    EXPECT_EQ(9, owner.Data.heap.Size());
}

TEST(ProgressiveMeshHeapTest, ChangedPrioritiesRepairUpAndDownAndKeepBothIndexDomainsSeparate) {
    OwnedHeap owner;
    const float costs[] = {2, 5, 3, 9, 8, 4, 6};
    for (float cost : costs) owner.Add(cost);
    PMVertexEx *decreased = owner.Created[3];
    decreased->collapseCost = -3.0f;
    owner.Active[decreased->originalIndex] = -3.0f;
    owner.Data.HeapifyUp(decreased->heapIndex);
    ASSERT_EQ(decreased, owner.Data.heap[0]);
    ExpectConsistent(owner);

    decreased->collapseCost = 12.0f;
    owner.Active[decreased->originalIndex] = 12.0f;
    owner.Data.HeapifyDown(decreased->heapIndex);
    EXPECT_NE(decreased, owner.Data.heap[0]);
    ExpectConsistent(owner);
    owner.Change(owner.Created[4], -4.0f);
    ASSERT_EQ(owner.Created[4], owner.Data.heap[0]);
    owner.Change(owner.Created[4], 15.0f);
    ExpectConsistent(owner);
    DrainAgainstOracle(owner);
}

TEST(ProgressiveMeshHeapTest, RemovingLegalInternalPositionsRepairsTowardTheParentAndTowardTheChildren) {
    const float costs[] = {1, 10, 2, 11, 12, 3, 4};
    {
        OwnedHeap owner;
        for (float cost : costs) owner.Add(cost);
        PMVertexEx *removed = owner.Created[3];
        PMVertexEx *replacement = owner.Created[6];
        ASSERT_EQ(3, removed->heapIndex);
        owner.Data.RemoveFromHeap(removed);
        owner.Active.erase(removed->originalIndex);
        EXPECT_EQ(-1, removed->heapIndex);
        EXPECT_EQ(1, replacement->heapIndex); // 4 must move above its old parent 10.
        EXPECT_EQ(7, owner.Data.vertices.Size());
        ExpectConsistent(owner);
        DrainAgainstOracle(owner);
    }
    {
        OwnedHeap owner;
        for (float cost : costs) owner.Add(cost);
        PMVertexEx *removed = owner.Data.heap[0];
        owner.Data.RemoveFromHeap(removed);
        owner.Active.erase(removed->originalIndex);
        EXPECT_EQ(-1, removed->heapIndex);
        ASSERT_NE(nullptr, owner.Data.heap[0]);
        EXPECT_FLOAT_EQ(2.0f, owner.Data.heap[0]->collapseCost);
        ExpectConsistent(owner);
        DrainAgainstOracle(owner);
    }
}

TEST(ProgressiveMeshHeapTest, ExtractedHolesAllowRemovalReinsertionAndLargerFiniteCostsRemainLive) {
    {
        OwnedHeap owner;
        const float costs[] = {1, 4, 2, 6, 5, 3};
        for (float cost : costs) owner.Add(cost);
        PMVertexEx *popped = owner.Data.PopMinFromHeap();
        ASSERT_NE(nullptr, popped);
        owner.Active.erase(popped->originalIndex);
        PMVertexEx *removed = owner.Created[1];
        owner.Data.RemoveFromHeap(removed);
        owner.Active.erase(removed->originalIndex);
        ExpectConsistent(owner);
        popped->collapseCost = -1.0f;
        owner.Active[popped->originalIndex] = -1.0f;
        owner.Data.AddToHeap(popped);
        removed->collapseCost = 0.5f;
        owner.Active[removed->originalIndex] = 0.5f;
        owner.Data.AddToHeap(removed);
        ExpectConsistent(owner);
        DrainAgainstOracle(owner);
    }
    {
        OwnedHeap owner;
        PMVertexEx *first = owner.Add(2.0e13f);
        PMVertexEx *second = owner.Add(3.0e13f);
        ASSERT_EQ(first, owner.Data.PopMinFromHeap());
        owner.Active.erase(first->originalIndex);
        // GetCost's finite sentinel is an interface detail, not an upper
        // bound on real priorities. A null hole must not hide the live child.
        ASSERT_EQ(second, owner.Data.PopMinFromHeap());
        owner.Active.erase(second->originalIndex);
        ExpectConsistent(owner);
        second->collapseCost = 4.0e13f;
        owner.Active[second->originalIndex] = second->collapseCost;
        owner.Data.AddToHeap(second);
        ASSERT_EQ(second, owner.Data.PopMinFromHeap());
        owner.Active.clear();
        ExpectConsistent(owner);
    }
}

TEST(ProgressiveMeshHeapTest, RemovingPoppedVerticesCompactsOwnedStorageWithoutDeletingHeapAliasesTwice) {
    OwnedHeap owner;
    const float costs[] = {5, 1, 4, 2, 3};
    for (float cost : costs) owner.Add(cost);
    while (!owner.Active.empty()) {
        const float expected = owner.Minimum();
        PMVertexEx *vertex = owner.Data.PopMinFromHeap();
        ASSERT_NE(nullptr, vertex);
        EXPECT_FLOAT_EQ(expected, vertex->collapseCost);
        const int label = vertex->originalIndex;
        const int formerSlot = vertex->arrayIndex;
        PMVertexEx *last = owner.Data.vertices[owner.Data.vertices.Size() - 1];
        owner.Active.erase(label);
        owner.Data.RemoveVertex(vertex);
        owner.Created[label] = nullptr;
        if (formerSlot < owner.Data.vertices.Size()) {
            EXPECT_EQ(last, owner.Data.vertices[formerSlot]);
            EXPECT_EQ(formerSlot, last->arrayIndex);
        }
        ExpectConsistent(owner);
    }
    EXPECT_EQ(0, owner.Data.vertices.Size());
    EXPECT_EQ(5, owner.Data.heap.Size());
}

TEST(ProgressiveMeshHeapTest, FaceDetachAndDeletePreserveOwnerIndicesAndCleanActualBorrowedAdjacency) {
    PMVertexEx a, b, c, d;
    PMEdgeCollapseData owner;
    PMFace *detached = new PMFace;
    PMFace *retained = new PMFace;
    RegisterDefaultFace(owner, detached, &a, &b, &c);
    RegisterDefaultFace(owner, retained, &a, &c, &d);
    ASSERT_EQ(2, owner.faces.Size());
    owner.RemoveFace(detached);
    EXPECT_EQ(-1, detached->arrayIndex);
    ASSERT_EQ(1, owner.faces.Size());
    EXPECT_EQ(retained, owner.faces[0]);
    EXPECT_EQ(0, retained->arrayIndex);
    // Detachment transfers face ownership; the object and its graph links
    // remain alive until the caller actually destroys it.
    EXPECT_TRUE(detached->ContainsVertex(&b));
    EXPECT_EQ(1, b.faces.Size());
    EXPECT_TRUE(a.HasNeighbor(&b));
    delete detached;
    EXPECT_EQ(0, b.faces.Size());
    EXPECT_FALSE(a.HasNeighbor(&b));
    EXPECT_TRUE(a.HasNeighbor(&c));
    owner.DeleteFace(retained);
    EXPECT_EQ(0, owner.faces.Size());
    EXPECT_EQ(0, a.faces.Size());
    EXPECT_EQ(0, c.faces.Size());
    EXPECT_EQ(0, d.faces.Size());
    EXPECT_EQ(0, a.neighbors.Size());
    EXPECT_EQ(0, c.neighbors.Size());
    EXPECT_EQ(0, d.neighbors.Size());
}

TEST(ProgressiveMeshHeapTest, IndependentOwnersAndRealDestructorLeaveBorrowedVerticesWithoutDeadFaceLinks) {
    PMVertexEx a, b, c;
    OwnedHeap survivor;
    PMVertexEx *surviving = survivor.Add(7.0f);
    {
        OwnedHeap temporary;
        temporary.Add(-2.0f);
        temporary.Add(1.0f);
        EXPECT_NE(temporary.Data.vertices.Begin(), survivor.Data.vertices.Begin());
        RegisterDefaultFace(temporary.Data, new PMFace, &a, &b, &c);
        ASSERT_EQ(1, a.faces.Size());
        ASSERT_TRUE(a.HasNeighbor(&b));
        DrainAgainstOracle(temporary);
        ExpectConsistent(survivor);
    }
    // Only the temporary owner contained the allocated face. Its destructor
    // must run that real face destructor; borrowed stack vertices stay alive.
    EXPECT_EQ(0, a.faces.Size());
    EXPECT_EQ(0, b.faces.Size());
    EXPECT_EQ(0, c.faces.Size());
    EXPECT_EQ(0, a.neighbors.Size());
    EXPECT_EQ(0, b.neighbors.Size());
    EXPECT_EQ(0, c.neighbors.Size());
    ASSERT_EQ(surviving, survivor.Data.PopMinFromHeap());
    survivor.Active.clear();
    EXPECT_EQ(0, surviving->originalIndex);
    ExpectConsistent(survivor);
}
