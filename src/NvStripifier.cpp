// NvStripifier.cpp
// Portable NvStripifier implementation
// Based on IDA analysis of NvTriStripObjects.obj (MD5: b2c4553e1045fe3bf3a222aa328bcefc)

#include "NvStripifier.h"
#include "MeshAdjacency.h"

#include <cstdio>
#include <cstring>

// ============================================================================
// Static helper: NvStripifier::FindEdgeInfo (0x24)
// ============================================================================
NvEdgeInfo *NvStripifier::FindEdgeInfo(XArray<NvEdgeInfo *> &edgeBuckets, int v0, int v1) {
    if (v0 < 0 || v0 >= edgeBuckets.Size())
        return nullptr;

    NvEdgeInfo *edge = edgeBuckets[v0];
    while (edge) {
        const int a0 = edge->Vertex0;
        const int a1 = edge->Vertex1;

        if ((a0 == v0 && a1 == v1) || (a0 == v1 && a1 == v0))
            return edge;

        // Advance along the bucket list for v0.
        if (a0 == v0)
            edge = edge->NextV0;
        else
            edge = edge->NextV1;
    }
    return nullptr;
}

// ============================================================================
// Static helper: NvStripifier::FindOtherFace (0x64)
// ============================================================================
NvFaceInfo *NvStripifier::FindOtherFace(XArray<NvEdgeInfo *> &edgeBuckets, int v0, int v1, NvFaceInfo *notThisFace) {
    // Find the edge, then return the other face
    NvEdgeInfo *edge = FindEdgeInfo(edgeBuckets, v0, v1);
    if (!edge)
        return nullptr;

    // Check if edge matches (v0, v1) or (v1, v0)
    if ((edge->Vertex0 == v0 && edge->Vertex1 == v1) ||
        (edge->Vertex0 == v1 && edge->Vertex1 == v0)) {
        if (edge->Face0 == notThisFace)
            return edge->Face1;
        return edge->Face0;
    }
    return nullptr;
}

// ============================================================================
// Static helper: NvStripifier::AlreadyExists (0x124)
// ============================================================================
bool NvStripifier::AlreadyExists(NvFaceInfo *face, const XArray<NvFaceInfo *> &faces) {
    int count = faces.Size();
    for (int i = 0; i < count; i++) {
        NvFaceInfo *f = faces[i];
        if (f->V[0] == face->V[0] && f->V[1] == face->V[1] && f->V[2] == face->V[2])
            return true;
    }
    return false;
}

// ============================================================================
// Static helper: NvStripifier::NextIsCW (0x1A24)
// ============================================================================
bool NvStripifier::NextIsCW(int numIndices) {
    return (numIndices % 2) == 0;
}

// ============================================================================
// Static helper: NvStripifier::GetUniqueVertexInB
// ============================================================================
int NvStripifier::GetUniqueVertexInB(NvFaceInfo *a, NvFaceInfo *b) {
    // Find the vertex in b that is not in a
    for (int i = 0; i < 3; i++) {
        int v = b->V[i];
        if (v != a->V[0] && v != a->V[1] && v != a->V[2])
            return v;
    }
    return -1;
}

// ============================================================================
// Static helper: NvStripifier::GetSharedVertex
// ============================================================================
int NvStripifier::GetSharedVertex(NvFaceInfo *a, NvFaceInfo *b) {
    // Find a vertex that exists in both faces
    for (int i = 0; i < 3; i++) {
        int v = b->V[i];
        if (v == a->V[0] || v == a->V[1] || v == a->V[2])
            return v;
    }
    return -1;
}

// ============================================================================
// Static helper: NvStripifier::NumNeighbors (0x2CA4)
// ============================================================================
int NvStripifier::NumNeighbors(NvFaceInfo *face, XArray<NvEdgeInfo *> &edgeBuckets) {
    int count = 0;
    if (face->Neighbor[0]) count++;
    if (face->Neighbor[1]) count++;
    if (face->Neighbor[2]) count++;
    return count;
}

// ============================================================================
// Static helper: NvStripifier::FindStartPoint (0x44C)
// ============================================================================
int NvStripifier::FindStartPoint(XArray<NvFaceInfo *> &allFaces, XArray<NvEdgeInfo *> &edgeBuckets) {
    int count = allFaces.Size();
    for (int i = 0; i < count; i++) {
        NvFaceInfo *face = allFaces[i];
        int numNeighbors = 0;
        if (face->Neighbor[0]) numNeighbors++;
        if (face->Neighbor[1]) numNeighbors++;
        if (face->Neighbor[2]) numNeighbors++;

        // Return face with more than 1 boundary edge (less than 2 neighbors)
        if (numNeighbors < 2)
            return i;
    }
    return -1;
}

// ============================================================================
// Static helper: NvStripifier::UpdateCacheStrip (0x29B4)
// ============================================================================
void NvStripifier::UpdateCacheStrip(VertexCache *cache, NvStripInfo *strip) {
    int count = strip->Faces.Size();
    for (int i = 0; i < count; i++) {
        NvFaceInfo *face = strip->Faces[i];
        if (!cache->InCache(face->V[0]))
            cache->AddEntry(face->V[0]);
        if (!cache->InCache(face->V[1]))
            cache->AddEntry(face->V[1]);
        if (!cache->InCache(face->V[2]))
            cache->AddEntry(face->V[2]);
    }
}

// ============================================================================
// Static helper: NvStripifier::UpdateCacheFace (0x2AB4)
// ============================================================================
void NvStripifier::UpdateCacheFace(VertexCache *cache, NvFaceInfo *face) {
    if (!cache->InCache(face->V[0]))
        cache->AddEntry(face->V[0]);
    if (!cache->InCache(face->V[1]))
        cache->AddEntry(face->V[1]);
    if (!cache->InCache(face->V[2]))
        cache->AddEntry(face->V[2]);
}

// ============================================================================
// Static helper: NvStripifier::CalcNumHitsStrip (0x2B64)
// ============================================================================
float NvStripifier::CalcNumHitsStrip(VertexCache *cache, NvStripInfo *strip) {
    int hits = 0;
    int faceCount = 0;
    int count = strip->Faces.Size();
    for (int i = 0; i < count; i++) {
        NvFaceInfo *face = strip->Faces[i];
        if (cache->InCache(face->V[0])) hits++;
        if (cache->InCache(face->V[1])) hits++;
        if (cache->InCache(face->V[2])) hits++;
        faceCount++;
    }
    return (float)hits / (float)faceCount;
}

// ============================================================================
// Static helper: NvStripifier::CalcNumHitsFace (0x2C34)
// ============================================================================
int NvStripifier::CalcNumHitsFace(VertexCache *cache, NvFaceInfo *face) {
    int hits = 0;
    if (cache->InCache(face->V[0])) hits++;
    if (cache->InCache(face->V[1])) hits++;
    if (cache->InCache(face->V[2])) hits++;
    return hits;
}

// ============================================================================
// Static helper: NvStripifier::AvgStripSize (0x2D44)
// ============================================================================
float NvStripifier::AvgStripSize(const XArray<NvStripInfo *> &strips) {
    int totalFaces = 0;
    int stripCount = strips.Size();
    if (stripCount <= 0)
        return 0.0f;
    for (int i = 0; i < stripCount; i++) {
        totalFaces += strips[i]->Faces.Size();
    }
    return (float)totalFaces / (float)stripCount;
}

// ============================================================================
// Static helper: NvStripifier::FindTraversal (0x1538)
// ============================================================================
bool NvStripifier::FindTraversal(
    XArray<NvFaceInfo *> &allFaces,
    XArray<NvEdgeInfo *> &edgeBuckets,
    NvStripInfo *strip,
    NvFaceInfo *&outFace,
    NvEdgeInfo *&outEdge,
    bool &outCW) {
    outFace = nullptr;
    outEdge = nullptr;
    outCW = false;
    if (!strip->StartEdge)
        return false;

    const int vertex = strip->StartCW ? strip->StartEdge->Vertex1 : strip->StartEdge->Vertex0;
    NvEdgeInfo *edge = edgeBuckets[vertex];
    while (edge) {
        NvFaceInfo *a = edge->Face0;
        NvFaceInfo *b = edge->Face1;
        const bool aBelongs = a && (strip->ExperimentId < 0 ? a->MarkA : a->MarkB) == strip->StripId;
        const bool bBelongs = b && (strip->ExperimentId < 0 ? b->MarkA : b->MarkB) == strip->StripId;
        if (aBelongs && b && b->MarkA < 0 &&
            (strip->ExperimentId < 0 || b->Experiment != strip->ExperimentId))
            outFace = b;
        else if (bBelongs && a && a->MarkA < 0 &&
                 (strip->ExperimentId < 0 || a->Experiment != strip->ExperimentId))
            outFace = a;

        if (outFace) {
            outEdge = edge;
            bool touchesStrip = false;
            for (int i = 0; i < 3; ++i) {
                NvFaceInfo *neighbor = FindOtherFace(edgeBuckets, outFace->V[i], outFace->V[(i + 1) % 3], outFace);
                if (neighbor && (strip->ExperimentId < 0 ? neighbor->MarkA : neighbor->MarkB) == strip->StripId)
                    touchesStrip = true;
            }
            outCW = touchesStrip ? edge->Vertex0 == vertex : edge->Vertex1 == vertex;
            return true;
        }
        edge = edge->Vertex0 == vertex ? edge->NextV0 : edge->NextV1;
    }
    return false;
}

static bool FaceContainsVertex(const NvFaceInfo *face, int v) {
    return face && (face->V[0] == v || face->V[1] == v || face->V[2] == v);
}

static int FaceUniqueVertexNotInOther(const NvFaceInfo *a, const NvFaceInfo *b) {
    if (!a || !b)
        return -1;
    for (int i = 0; i < 3; i++) {
        const int v = a->V[i];
        if (!FaceContainsVertex(b, v))
            return v;
    }
    return -1;
}

static int FaceUniqueVertexNotEq(const NvFaceInfo *face, int a, int b) {
    if (!face)
        return -1;
    for (int i = 0; i < 3; i++) {
        const int v = face->V[i];
        if (v != a && v != b)
            return v;
    }
    return -1;
}

static int FaceSharedVertices(const NvFaceInfo *a, const NvFaceInfo *b, int &out0, int &out1) {
    out0 = -1;
    out1 = -1;
    if (!a || !b)
        return 0;

    int count = 0;
    for (int i = 0; i < 3; i++) {
        const int v = a->V[i];
        if (FaceContainsVertex(b, v)) {
            if (count == 0)
                out0 = v;
            else if (count == 1)
                out1 = v;
            count++;
        }
    }
    return count;
}

static void AppendJoinedSequence(XArray<CKWORD> &outIndices, const XArray<CKWORD> &seq);

static bool FaceHasWinding(const NvFaceInfo *face, int a, int b, int c) {
    for (int i = 0; i < 3; ++i) {
        if (face->V[i] == a && face->V[(i + 1) % 3] == b && face->V[(i + 2) % 3] == c)
            return true;
    }
    return false;
}

static bool BuildStripIndexSequenceFromFaces(const XArray<NvFaceInfo *> &faces, XArray<CKWORD> &seq) {
    seq.Resize(0);
    if (faces.Size() == 0)
        return false;

    const NvFaceInfo *first = faces[0];
    if (!first)
        return false;
    int firstVertex = 0;
    if (faces.Size() > 1) {
        int shared0, shared1;
        if (FaceSharedVertices(first, faces[1], shared0, shared1) == 2) {
            const int unique = FaceUniqueVertexNotInOther(first, faces[1]);
            for (int i = 0; i < 3; ++i) {
                if (first->V[i] == unique)
                    firstVertex = i;
            }
        }
    }
    // A cyclic rotation preserves the authored winding of the first face.
    for (int i = 0; i < 3; ++i)
        seq.PushBack((CKWORD)first->V[(firstVertex + i) % 3]);

    for (int i = 1; i < faces.Size(); ++i) {
        const NvFaceInfo *face = faces[i];
        if (!face)
            return false;
        const int a = seq[seq.Size() - 2];
        const int b = seq[seq.Size() - 1];
        const int c = FaceUniqueVertexNotEq(face, a, b);
        if (c >= 0 && FaceContainsVertex(face, a) && FaceContainsVertex(face, b) &&
            ((seq.Size() % 2) ? FaceHasWinding(face, b, a, c) : FaceHasWinding(face, a, b, c))) {
            seq.PushBack((CKWORD)c);
        } else {
            // A different edge or winding starts a new strip, joined only by
            // degenerate triangles. Never turn a real face into a connector.
            XArray<CKWORD> triangle;
            for (int v = 0; v < 3; ++v)
                triangle.PushBack((CKWORD)face->V[v]);
            AppendJoinedSequence(seq, triangle);
        }
    }
    return true;
}

static bool SequenceContainsRestartValue(const XArray<CKWORD> &seq) {
    for (int i = 0; i < seq.Size(); ++i) {
        if (seq[i] == (CKWORD)0xFFFF)
            return true;
    }
    return false;
}

static void AppendJoinedSequence(XArray<CKWORD> &outIndices, const XArray<CKWORD> &seq) {
    if (seq.Size() == 0)
        return;

    if (outIndices.Size() > 0) {
        const CKWORD last = outIndices[outIndices.Size() - 1];
        if ((outIndices.Size() % 2) != 0)
            outIndices.PushBack(last);
        outIndices.PushBack(last);
        outIndices.PushBack(seq[0]);
    }

    for (int i = 0; i < seq.Size(); ++i)
        outIndices.PushBack(seq[i]);
}

// ============================================================================
// NvStripInfo::Build (0x834) - Uses STATIC local variables!
// ============================================================================
void NvStripInfo::Build(XArray<NvEdgeInfo *> &edgeBuckets, XArray<NvFaceInfo *> &allFaces) {
    // CRITICAL: Binary uses static local variables
    static XArray<NvFaceInfo *> forwardFaces;
    static XArray<NvFaceInfo *> backwardFaces;
    static XArray<CKWORD> scratchIndices;
    static XArray<NvFaceInfo *> tempAllFaces;

    Faces.Resize(0);

    if (!StartFace || !StartEdge)
        return;

    // Initialize scratch arrays
    forwardFaces.Resize(0);
    backwardFaces.Resize(0);
    scratchIndices.Resize(0);

    // Determine starting vertices based on StartCW
    int v0, v1;
    if (StartCW) {
        v0 = StartEdge->Vertex0;
        v1 = StartEdge->Vertex1;
    } else {
        v0 = StartEdge->Vertex1;
        v1 = StartEdge->Vertex0;
    }

    // Find the third vertex
    int v2 = -1;
    if (StartFace->V[0] != v0 && StartFace->V[0] != v1)
        v2 = StartFace->V[0];
    else if (StartFace->V[1] != v0 && StartFace->V[1] != v1)
        v2 = StartFace->V[1];
    else
        v2 = StartFace->V[2];

    // Build forward direction
    scratchIndices.PushBack((CKWORD)v0);
    scratchIndices.PushBack((CKWORD)v1);
    scratchIndices.PushBack((CKWORD)v2);

    forwardFaces.PushBack(StartFace);
    MarkTriangle(StartFace);

    NvFaceInfo *curFace = StartFace;
    while (true) {
        if (scratchIndices.Size() < 3)
            break;

        int a = (int)scratchIndices[scratchIndices.Size() - 2];
        int b = (int)scratchIndices[scratchIndices.Size() - 1];

        // Find neighbor through edge (a, b)
        NvFaceInfo *next = NvStripifier::FindOtherFace(edgeBuckets, a, b, curFace);
        if (!next)
            break;

        // Check if already marked
        if (next->MarkA >= 0)
            break;
        if (ExperimentId >= 0 && next->Experiment == ExperimentId)
            break;

        // Find next vertex
        int nextV = -1;
        if (next->V[0] != a && next->V[0] != b)
            nextV = next->V[0];
        else if (next->V[1] != a && next->V[1] != b)
            nextV = next->V[1];
        else if (next->V[2] != a && next->V[2] != b)
            nextV = next->V[2];
        else
            break;

        scratchIndices.PushBack((CKWORD)nextV);
        forwardFaces.PushBack(next);
        MarkTriangle(next);
        curFace = next;
    }

    // Build backward direction (reverse winding from start)
    scratchIndices.Resize(0);
    scratchIndices.PushBack((CKWORD)v2);
    scratchIndices.PushBack((CKWORD)v1);
    scratchIndices.PushBack((CKWORD)v0);

    curFace = StartFace;
    while (true) {
        if (scratchIndices.Size() < 3)
            break;

        int a = (int)scratchIndices[scratchIndices.Size() - 2];
        int b = (int)scratchIndices[scratchIndices.Size() - 1];

        NvFaceInfo *next = NvStripifier::FindOtherFace(edgeBuckets, a, b, curFace);
        if (!next)
            break;

        if (next->MarkA >= 0)
            break;
        if (ExperimentId >= 0 && next->Experiment == ExperimentId)
            break;

        int nextV = -1;
        if (next->V[0] != a && next->V[0] != b)
            nextV = next->V[0];
        else if (next->V[1] != a && next->V[1] != b)
            nextV = next->V[1];
        else if (next->V[2] != a && next->V[2] != b)
            nextV = next->V[2];
        else
            break;

        scratchIndices.PushBack((CKWORD)nextV);
        backwardFaces.PushBack(next);
        MarkTriangle(next);
        curFace = next;
    }

    // Combine forward and backward
    Combine(forwardFaces, backwardFaces);
}

// ============================================================================
// NvStripInfo::Combine (0x11F4)
// ============================================================================
void NvStripInfo::Combine(XArray<NvFaceInfo *> &forward, XArray<NvFaceInfo *> &backward) {
    Faces.Resize(0);

    // Add backward faces in reverse order
    for (int i = backward.Size() - 1; i >= 0; i--) {
        Faces.PushBack(backward[i]);
    }

    // Add forward faces
    for (int i = 0; i < forward.Size(); i++) {
        Faces.PushBack(forward[i]);
    }
}

// ============================================================================
// NvStripifier::FindGoodResetPoint
// ============================================================================
NvFaceInfo *NvStripifier::FindGoodResetPoint(XArray<NvFaceInfo *> &allFaces, XArray<NvEdgeInfo *> &edgeBuckets) {
    const int count = allFaces.Size();
    if (count == 0)
        return nullptr;
    int start = -1;
    if (m_FirstTime) {
        start = FindStartPoint(allFaces, edgeBuckets);
        m_FirstTime = 0;
    }
    if (start < 0)
        start = (int)((count - 1) * m_Ratio);
    m_Ratio += 0.1f;
    if (m_Ratio > 1.0f)
        m_Ratio = 0.05f;
    for (int n = 0; n < count; ++n) {
        NvFaceInfo *face = allFaces[(start + n) % count];
        if (face->MarkA < 0)
            return face;
    }
    return nullptr;
}

// ============================================================================
// NvStripifier::CommitStrips (0x13E8)
// ============================================================================
void NvStripifier::CommitStrips(XArray<NvStripInfo *> &outStrips, XArray<NvStripInfo *> *candidateSet) {
    int count = candidateSet->Size();
    for (int i = 0; i < count; i++) {
        NvStripInfo *strip = (*candidateSet)[i];
        strip->ExperimentId = -1;

        // Mark all faces as committed
        for (int j = 0; j < strip->Faces.Size(); j++) {
            strip->MarkTriangle(strip->Faces[j]);
        }

        outStrips.PushBack(strip);
    }
}

// ============================================================================
// NvStripifier::RemoveSmallStrips (0x1648)
// ============================================================================
void NvStripifier::RemoveSmallStrips(
    XArray<NvStripInfo *> &allStrips,
    XArray<NvFaceInfo *> &allFaces,
    XArray<NvStripInfo *> &outStrips,
    XArray<NvEdgeInfo *> &edgeBuckets) {
    allFaces.Resize(0);
    outStrips.Resize(0);
    for (int i = 0; i < allStrips.Size(); ++i) {
        NvStripInfo *strip = allStrips[i];
        if (strip->Faces.Size() < m_MinStripLength) {
            for (int j = 0; j < strip->Faces.Size(); ++j)
                allFaces.PushBack(strip->Faces[j]);
            delete strip;
        } else {
            outStrips.PushBack(strip);
        }
    }
    allStrips.Resize(0);

    // This API has no separate triangle-list output. Retain short faces as
    // singleton strips, ordered with the same FIFO cache metric as native.
    VertexCache cache(m_CacheSize);
    while (allFaces.Size() > 0) {
        int best = 0;
        int bestHits = -1;
        for (int i = 0; i < allFaces.Size(); ++i) {
            const int hits = CalcNumHitsFace(&cache, allFaces[i]);
            if (hits > bestHits) {
                bestHits = hits;
                best = i;
            }
        }
        NvFaceInfo *face = allFaces[best];
        UpdateCacheFace(&cache, face);
        NvStripInfo *strip = new NvStripInfo();
        strip->StartFace = face;
        strip->Faces.PushBack(face);
        outStrips.PushBack(strip);
        allFaces.RemoveAt(best);
    }
}

// ============================================================================
// NvStripifier::BuildStripifyInfo (0x174)
// ============================================================================
void NvStripifier::BuildStripifyInfo(
    const XArray<CKWORD> &indices,
    CKWORD vertexCount,
    XArray<NvFaceInfo *> &outFaces,
    XArray<NvEdgeInfo *> &outEdgeBuckets,
    XArray<NvEdgeInfo *> &outEdges) {

    outFaces.Resize(0);
    outEdgeBuckets.Resize(0);
    outEdges.Resize(0);
    if (indices.Size() < 3 || vertexCount == 0)
        return;

    int triCount = indices.Size() / 3;

    // Create face array
    outFaces.Reserve(triCount);
    for (int i = 0; i < triCount; i++) {
        NvFaceInfo *face = new NvFaceInfo();
        face->V[0] = (int)indices[i * 3 + 0];
        face->V[1] = (int)indices[i * 3 + 1];
        face->V[2] = (int)indices[i * 3 + 2];
        outFaces.PushBack(face);
    }

    // Create edge buckets
    outEdgeBuckets.Resize(vertexCount + 1);
    for (int i = 0; i < outEdgeBuckets.Size(); i++)
        outEdgeBuckets[i] = nullptr;

    // Use MeshAdjacency to compute neighbors and edges
    MeshAdjacency adj;
    adj.Init((CKWORD *)indices.Begin(), triCount);
    if (!adj.Compute(true, true)) {
        for (int i = 0; i < outFaces.Size(); ++i)
            delete outFaces[i];
        outFaces.Resize(0);
        outEdgeBuckets.Resize(0);
        return;
    }

    // Set neighbor links
    const XArray<MeshAdjacency::Face> &adjFaces = adj.GetFaces();
    for (int i = 0; i < triCount; i++) {
        const MeshAdjacency::Face &af = adjFaces[i];
        NvFaceInfo *face = outFaces[i];
        for (int e = 0; e < 3; e++) {
            CKDWORD link = af.faces[e];
            if (IS_BOUNDARY(link)) {
                face->Neighbor[e] = nullptr;
            } else {
                int nface = (int)MAKE_ADJ_TRI(link);
                if (nface >= 0 && nface < triCount)
                    face->Neighbor[e] = outFaces[nface];
                else
                    face->Neighbor[e] = nullptr;
            }
        }
    }

    // Create edge info and buckets
    const XArray<MeshAdjacency::Edge> &adjEdges = adj.GetEdges();
    for (int i = 0; i < adjEdges.Size(); i++) {
        const MeshAdjacency::Edge &e = adjEdges[i];
        NvEdgeInfo *edge = new NvEdgeInfo();
        outEdges.PushBack(edge);
        edge->Vertex0 = (int)e.vertices[0];
        edge->Vertex1 = (int)e.vertices[1];

        CKDWORD f0 = e.faces[0];
        CKDWORD f1 = e.faces[1];
        edge->Face0 = (f0 != 0xFFFFFFFFu && (int)f0 < triCount) ? outFaces[(int)f0] : nullptr;
        edge->Face1 = (f1 != 0xFFFFFFFFu && (int)f1 < triCount) ? outFaces[(int)f1] : nullptr;

        // Insert into buckets
        if (edge->Vertex0 >= 0 && edge->Vertex0 < outEdgeBuckets.Size()) {
            edge->NextV0 = outEdgeBuckets[edge->Vertex0];
            outEdgeBuckets[edge->Vertex0] = edge;
        }
        if (edge->Vertex1 >= 0 && edge->Vertex1 < outEdgeBuckets.Size()) {
            edge->NextV1 = outEdgeBuckets[edge->Vertex1];
            outEdgeBuckets[edge->Vertex1] = edge;
        }
    }
}

// ============================================================================
// NvStripifier::FindAllStrips (0x2D94)
// ============================================================================
bool NvStripifier::FindAllStrips(
    XArray<NvStripInfo *> &outAllStrips,
    XArray<NvFaceInfo *> &allFaces,
    XArray<NvEdgeInfo *> &edgeBuckets,
    int numSamples,
    int minStripLength) {
    int stripId = 0;
    int experimentId = 0;
    NvFaceInfo *resetPoint;
    // numSamples limits a selection round, not the number of output strips.
    // Minimum length is applied after splitting against the cache budget.
    while ((resetPoint = FindGoodResetPoint(allFaces, edgeBuckets)) != nullptr) {
        XClassArray<XArray<NvStripInfo *> > experiments;
        XArray<NvFaceInfo *> usedResetPoints;
        for (int sample = 0; sample < numSamples; ++sample) {
            if (sample > 0)
                resetPoint = FindGoodResetPoint(allFaces, edgeBuckets);
            if (!resetPoint || usedResetPoints.Find(resetPoint) != usedResetPoints.End())
                continue;
            usedResetPoints.PushBack(resetPoint);
            for (int e = 0; e < 3; ++e) {
                NvEdgeInfo *edge = FindEdgeInfo(edgeBuckets, resetPoint->V[e], resetPoint->V[(e + 1) % 3]);
                if (!edge)
                    continue;
                for (int cw = 0; cw < 2; ++cw) {
                    XArray<NvStripInfo *> candidate;
                    NvStripInfo *strip = new NvStripInfo();
                    strip->StartFace = resetPoint;
                    strip->StartEdge = edge;
                    strip->StartCW = (unsigned char)cw;
                    strip->StripId = stripId++;
                    strip->ExperimentId = experimentId++;
                    while (true) {
                        strip->Build(edgeBuckets, allFaces);
                        candidate.PushBack(strip);
                        NvFaceInfo *nextFace;
                        NvEdgeInfo *nextEdge;
                        bool nextCW;
                        if (!FindTraversal(allFaces, edgeBuckets, strip, nextFace, nextEdge, nextCW))
                            break;
                        NvStripInfo *next = new NvStripInfo();
                        next->StartFace = nextFace;
                        next->StartEdge = nextEdge;
                        next->StartCW = (unsigned char)nextCW;
                        next->StripId = stripId++;
                        next->ExperimentId = strip->ExperimentId;
                        strip = next;
                    }
                    experiments.PushBack(candidate);
                }
            }
        }
        int best = -1;
        float bestScore = -1.0f;
        for (int i = 0; i < experiments.Size(); ++i) {
            const float score = AvgStripSize(experiments[i]) * (1.0f + experiments[i].Size());
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0)
            return false;
        CommitStrips(outAllStrips, &experiments[best]);
        for (int i = 0; i < experiments.Size(); ++i) {
            if (i != best) {
                for (int j = 0; j < experiments[i].Size(); ++j)
                    delete experiments[i][j];
            }
        }
    }
    return true;
}

// ============================================================================
// NvStripifier::SplitUpStripsAndOptimize (0x21AC)
// ============================================================================
void NvStripifier::SplitUpStripsAndOptimize(
    XArray<NvStripInfo *> &allStrips,
    bool joinStrips,
    XArray<NvEdgeInfo *> &edgeBuckets) {
    XArray<NvStripInfo *> pieces;
    for (int i = 0; i < allStrips.Size(); ++i) {
        NvStripInfo *source = allStrips[i];
        for (int start = 0; start < source->Faces.Size();) {
            NvStripInfo *piece = new NvStripInfo();
            for (int n = 0; n < m_CacheSize && start < source->Faces.Size(); ++n, ++start)
                piece->Faces.PushBack(source->Faces[start]);
            piece->StartFace = piece->Faces[0];
            pieces.PushBack(piece);
        }
        delete source;
    }
    allStrips.Resize(0);
    XArray<NvFaceInfo *> shortFaces;
    XArray<NvStripInfo *> retained;
    RemoveSmallStrips(pieces, shortFaces, retained, edgeBuckets);
    VertexCache cache(m_CacheSize);
    while (allStrips.Size() < retained.Size()) {
        int best = -1;
        float bestScore = -1.0f;
        for (int i = 0; i < retained.Size(); ++i) {
            NvStripInfo *strip = retained[i];
            if (strip->field_20)
                continue;
            float score;
            if (allStrips.Size() == 0) {
                int neighbors = 0;
                for (int j = 0; j < strip->Faces.Size(); ++j)
                    neighbors += NumNeighbors(strip->Faces[j], edgeBuckets);
                score = 3.0f - (float)neighbors / strip->Faces.Size();
            } else {
                score = CalcNumHitsStrip(&cache, strip);
            }
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        NvStripInfo *strip = retained[best];
        strip->field_20 = 1;
        strip->StripId = allStrips.Size();
        for (int i = 0; i < strip->Faces.Size(); ++i)
            strip->MarkTriangle(strip->Faces[i]);
        UpdateCacheStrip(&cache, strip);
        allStrips.PushBack(strip);
    }
}

// ============================================================================
// NvStripifier::CreateStrips (0x1A94)
// ============================================================================
void NvStripifier::CreateStrips(
    const XArray<NvStripInfo *> &strips,
    XArray<CKWORD> &outIndices,
    bool joinStrips,
    CKDWORD &outStripCount) {

    outIndices.Resize(0);
    outStripCount = 0;
    XClassArray<XArray<CKWORD> > sequences;
    sequences.Reserve(strips.Size());
    bool requiresJoinedFallback = false;

    for (int s = 0; s < strips.Size(); s++) {
        NvStripInfo *strip = strips[s];
        if (!strip || strip->Faces.Size() == 0)
            continue;

        // Build index sequence for this strip from the ordered face list.
        XArray<CKWORD> seq;
        if (!BuildStripIndexSequenceFromFaces(strip->Faces, seq))
            continue;

        if (seq.Size() == 0)
            continue;

        if (!joinStrips && SequenceContainsRestartValue(seq))
            requiresJoinedFallback = true;

        sequences.PushBack(seq);
    }

    if (joinStrips || requiresJoinedFallback) {
        for (int i = 0; i < sequences.Size(); ++i)
            AppendJoinedSequence(outIndices, sequences[i]);
        outStripCount = (outIndices.Size() > 0) ? 1 : 0;
        return;
    }

    for (int i = 0; i < sequences.Size(); ++i) {
        if (outIndices.Size() > 0)
            outIndices.PushBack((CKWORD)0xFFFF);

        const XArray<CKWORD> &seq = sequences[i];
        for (int j = 0; j < seq.Size(); ++j)
            outIndices.PushBack(seq[j]);
        if (seq.Size() > 0) {
            outStripCount++;
        }
    }
}

// ============================================================================
// NvStripifier::DestroyStrips - Cleanup method
// ============================================================================
void NvStripifier::DestroyStrips(XArray<NvStripInfo *> &strips) {
    XArray<NvFaceInfo *> deletedFaces;
    for (int i = 0; i < strips.Size(); i++) {
        NvStripInfo *strip = strips[i];
        if (!strip)
            continue;

        for (int j = 0; j < strip->Faces.Size(); ++j) {
            NvFaceInfo *face = strip->Faces[j];
            if (!face)
                continue;
            if (deletedFaces.Find(face) != deletedFaces.End())
                continue;
            deletedFaces.PushBack(face);
            delete face;
        }

        delete strip;
    }
    strips.Resize(0);
}

// ============================================================================
// NvStripifier constructor/destructor
// ============================================================================
NvStripifier::NvStripifier()
    : m_MinStripLength(0), m_CacheSize(0), m_Ratio(0.0f), m_FirstTime(1) {
}

NvStripifier::~NvStripifier() {
}

// ============================================================================
// NvStripifier::Stripify - Main API (0x1F24)
// ============================================================================
void NvStripifier::Stripify(
    const XArray<CKWORD> &inIndices,
    int minStripLength,
    int cacheSize,
    CKWORD vertexCount,
    XArray<NvStripInfo *> &outStrips) {

    outStrips.Resize(0);
    if (inIndices.Size() < 3 || vertexCount == 0)
        return;

    // Native reserves six entries from the hardware cache. Minimum length
    // is a separate face threshold, evaluated after cache-sized splitting.
    m_CacheSize = cacheSize > 6 ? cacheSize - 6 : 1;
    m_MinStripLength = minStripLength;
    m_Ratio = 0.0f;
    m_FirstTime = 1;

    // Copy indices to scratch
    m_Scratch.Resize(0);
    m_Scratch.Reserve(inIndices.Size());
    for (int i = 0; i < inIndices.Size(); i++)
        m_Scratch.PushBack(inIndices[i]);

    // Build adjacency info
    XArray<NvFaceInfo *> faces;
    XArray<NvEdgeInfo *> edgeBuckets;
    XArray<NvEdgeInfo *> edges;
    BuildStripifyInfo(m_Scratch, vertexCount, faces, edgeBuckets, edges);

    // Find all strips (10 samples as in binary)
    FindAllStrips(outStrips, faces, edgeBuckets, 10, m_MinStripLength);

    // Split into cache-sized pieces and order them against the vertex FIFO.
    SplitUpStripsAndOptimize(outStrips, false, edgeBuckets);

    // Add remaining unmarked faces as single-triangle strips
    for (int i = 0; i < faces.Size(); i++) {
        NvFaceInfo *face = faces[i];
        if (face->MarkA < 0) {
            NvStripInfo *strip = new NvStripInfo();
            strip->StartFace = face;
            strip->StartEdge = FindEdgeInfo(edgeBuckets, face->V[0], face->V[1]);
            strip->StartCW = 1;
            strip->ExperimentId = -1;
            strip->StripId = outStrips.Size();
            strip->Faces.PushBack(face);
            strip->MarkTriangle(face);
            outStrips.PushBack(strip);
        }
    }

    for (int i = 0; i < outStrips.Size(); ++i)
        outStrips[i]->StartEdge = nullptr; // Build-only edge storage ends here.
    for (int i = 0; i < edges.Size(); ++i)
        delete edges[i];
}

// ============================================================================
// NvStripifier::Stripify - Convenience one-shot API
// ============================================================================
void NvStripifier::Stripify(
    const XArray<CKWORD> &inIndices,
    int minStripLength,
    int cacheSize,
    CKWORD vertexCount,
    bool joinStrips,
    XArray<CKWORD> &outIndices,
    CKDWORD &outStripCount) {

    XArray<NvStripInfo *> strips;
    Stripify(inIndices, minStripLength, cacheSize, vertexCount, strips);
    CreateStrips(strips, outIndices, joinStrips, outStripCount);

    // Cleanup strips
    DestroyStrips(strips);
}
