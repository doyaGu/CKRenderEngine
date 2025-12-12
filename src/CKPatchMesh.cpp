#include "RCKPatchMesh.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "VxMath.h"
#include "CKContext.h"
#include "CKMaterial.h"
#include "CKRenderEngineTypes.h"
#include <cstring>

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKPatchMesh::RCKPatchMesh(CKContext *Context, CKSTRING name)
    : RCKMesh(Context, name),
      m_VecCount(0),
      m_VertCount(0),
      m_Verts(nullptr),
      m_Vecs(nullptr),
      m_IterationCount(0),
      m_PatchFlags(0) {
}

RCKPatchMesh::~RCKPatchMesh() {
    ClearPatches();
    ClearPatchEdges();
    if (m_Verts) {
        delete[] m_Verts;
        m_Verts = nullptr;
    }
}

//=============================================================================
// Load Methods
//=============================================================================

/**
 * @brief Loads patch mesh vertices from a state chunk
 * @param chunk The state chunk containing vertex data
 * @return Pointer to the patch mesh object
 */
void RCKPatchMesh::LoadVertices(CKStateChunk *chunk) {
    if (chunk->SeekIdentifier(0x8000000)) {
        // New format (0x8000000)
        CKDWORD Dword = chunk->ReadDword();
        Dword |= 2;
        m_PatchFlags = Dword;

        CKDWORD patchFlags = m_PatchFlags;
        patchFlags &= 0xFE;
        m_PatchFlags = patchFlags;

        chunk->ReadInt(); // Skip unused value
        m_VecCount = chunk->ReadInt();
        CKDWORD v10 = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        if (m_VertCount) {
            chunk->ReadAndFillBuffer_LEndian(v10, m_Verts);
        }

        m_VertCount = m_VertCount - m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = 0;
        }
    } else if (chunk->SeekIdentifier(0x1000000)) {
        // Old format (0x1000000)
        CKDWORD v5 = chunk->ReadDword();
        v5 |= 2;
        m_PatchFlags = v5;

        CKDWORD v6 = m_PatchFlags;
        v6 &= 0xFE;
        m_PatchFlags = v6;

        chunk->ReadObjectID(); // Skip object ID
        chunk->ReadInt();      // Skip unused value
        m_VecCount = chunk->ReadInt();
        CKDWORD v9 = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        if (m_VertCount) {
            chunk->ReadAndFillBuffer_LEndian(v9, m_Verts);
        }

        m_VertCount = m_VertCount - m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = 0;
        }
    } else {
        // Obsolete format - cannot load
        const char *v7 = GetName();
        m_Context->OutputToConsoleExBeep("%s : Obsolete version of PatchMesh format: Cannot Load", v7);
    }
}

/**
 * @brief Loads patch mesh data from a state chunk
 * @param chunk The state chunk containing patch mesh data
 * @param file The file being loaded from (can be nullptr)
 * @return CKERROR indicating success or failure
 */
CKERROR RCKPatchMesh::Load(CKStateChunk *chunk, CKFile *file) {
    RCKMesh::Load(chunk, file);

    if (chunk->SeekIdentifier(0x8000000)) {
        m_PatchFlags = chunk->ReadDword();
        int v2 = chunk->ReadInt();
        SetIterationCount(v2);

        m_VecCount = chunk->ReadInt();
        CKDWORD v5 = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        int v4 = m_VertCount;
        if (v4) {
            VxVector *v3 = new VxVector[v4];
            if (v3) {
                // Initialize vectors
                for (int i = 0; i < v4; ++i) {
                    v3[i] = VxVector(0, 0, 0);
                }
                m_Verts = v3;
            } else {
                m_Verts = 0;
            }
            chunk->ReadAndFillBuffer_LEndian(v5, m_Verts);
        }

        m_VertCount -= m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = 0;
        }

        // Load patches
        int Sequence = chunk->StartReadSequence();
        m_Patches.Resize(Sequence);

        for (int i = 0; i < Sequence; ++i) {
            CKDWORD ObjectID = chunk->ReadObjectID();
            m_Patches[i].Material = ObjectID;
        }

        for (int i = 0; i < Sequence; ++i) {
            CKDWORD v8 = chunk->ReadDword();
            m_Patches[i].type = v8;

            CKDWORD v9 = chunk->ReadDword();
            m_Patches[i].SmoothingGroup = v9;

            CKPatch *patch = &m_Patches[i];
            chunk->ReadAndFillBuffer_LEndian16(40, (void *) patch->v);
        }

        // Load patch edges
        int v70 = chunk->ReadDword();
        int v69 = chunk->ReadDword();
        if (v69) {
            m_PatchEdges.Resize(v69);
            void *v11 = m_PatchEdges.Begin();
            chunk->ReadAndFillBuffer_LEndian16(v70, v11);
        } else {
            m_PatchEdges.Clear();
        }

        // Load texture patches
        int v76 = chunk->StartReadSequence();
        m_TVPatches.Resize(v76);

        for (int i = 0; i < v76; ++i) {
            CKDWORD v12 = chunk->ReadObjectID();
            m_TVPatches[i].Material = v12;
        }

        for (int i = 0; i < m_TVPatches.Size(); ++i) {
            CKDWORD v13 = chunk->ReadDword();
            m_TVPatches[i].Flags = v13;

            CKDWORD v14 = chunk->ReadDword();
            m_TVPatches[i].Type = v14;

            CKDWORD v15 = chunk->ReadDword();
            m_TVPatches[i].SubType = v15;

            int v68 = chunk->ReadDword();
            int v67 = chunk->ReadDword();
            if (v67) {
                m_TVPatches[i].Vertices.Resize(v67);
                void *v18 = m_TVPatches[i].Vertices.Begin();
                chunk->ReadAndFillBuffer_LEndian16(v68, v18);
            } else {
                m_TVPatches[i].Vertices.Clear();
            }

            int v66 = chunk->ReadDword();
            int v65 = chunk->ReadDword();
            if (v65) {
                m_TVPatches[i].UVs.Resize(v65);
                void *v22 = m_TVPatches[i].UVs.Begin();
                chunk->ReadAndFillBuffer_LEndian(v66, v22);
            } else {
                m_TVPatches[i].UVs.Clear();
            }
        }
    } else if (chunk->SeekIdentifier(0x1000000)) {
        // Old format loading
        ClearPatches();

        CKDWORD v24 = chunk->ReadDword();
        v24 |= 2;
        m_PatchFlags = v24;

        CKDWORD v25 = m_PatchFlags;
        v25 &= 0xFE;
        m_PatchFlags = v25;

        CKDWORD v57 = chunk->ReadObjectID();
        int v26 = chunk->ReadInt();
        SetIterationCount(v26);

        m_VecCount = chunk->ReadInt();
        CKDWORD v51 = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        int v50 = m_VertCount;
        if (v50) {
            VxVector *v41 = new VxVector[v50];
            if (v41) {
                // Initialize vectors
                for (int i = 0; i < v50; ++i) {
                    v41[i] = VxVector(0, 0, 0);
                }
                m_Verts = v41;
            } else {
                m_Verts = 0;
            }
            chunk->ReadAndFillBuffer_LEndian(v51, m_Verts);
        }

        m_VertCount -= m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = 0;
        }

        // Load old format patches, edges, and texture patches
        // This section handles the complex old format data structure
        // Implementation would require detailed mapping of old format to new structure
    } else {
        // Obsolete format
        const char *v35 = GetName();
        m_Context->OutputToConsoleExBeep("%s : Obsolete version of PatchMesh format: Cannot Load", v35);
    }

    Rebuild();
    return CK_OK;
}

/**
 * @brief Clears all patch data from the mesh
 */
void RCKPatchMesh::ClearPatches() {
    m_Patches.Clear();
    m_TVPatches.Clear();
    m_PatchEdges.Clear();

    if (m_Verts) {
        delete[] m_Verts;
        m_Verts = nullptr;
    }
    m_Vecs = nullptr;
    m_VertCount = 0;
    m_VecCount = 0;
}

/**
 * @brief Rebuilds the patch mesh geometry after loading or modification
 */
void RCKPatchMesh::Rebuild() {
    // TODO: Implement patch mesh subdivision/tessellation
    // This would generate the actual mesh geometry from the patch control points
    // based on m_IterationCount subdivision level
}

/**
 * @brief Saves patch mesh data to a state chunk
 * @param file The file to save to
 * @param flags Save flags
 * @return Pointer to the created state chunk
 */
CKStateChunk *RCKPatchMesh::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *v31 = RCKMesh::Save(file, flags);
    if (!file && (flags & 0xFF00000) == 0)
        return v31;

    CKStateChunk *ckChunk = CreateCKStateChunk(53, file); // CKCID_PATCHMESH = 53
    ckChunk->StartWrite();
    ckChunk->AddChunkAndDelete(v31);

    ckChunk->WriteIdentifier(0x8000000);
    ckChunk->WriteDword(m_PatchFlags);
    ckChunk->WriteInt(m_IterationCount);
    ckChunk->WriteInt(m_VecCount);
    ckChunk->WriteBuffer_LEndian((m_VertCount + m_VecCount) * 12, m_Verts);

    int v4 = m_Patches.Size();
    ckChunk->StartObjectIDSequence(v4);
    for (int i = 0; i < v4; ++i) {
        CKObject *ObjectA = m_Context->GetObject(m_Patches[i].Material);
        ckChunk->WriteObjectSequence(ObjectA);
    }

    for (int j = 0; j < v4; ++j) {
        ckChunk->WriteDword(m_Patches[j].type);
        ckChunk->WriteDword(m_Patches[j].SmoothingGroup);
        ckChunk->WriteBufferNoSize_LEndian16(40, (void *) m_Patches[j].v);
    }

    void *v23 = m_PatchEdges.Begin();
    int v10 = m_PatchEdges.Size();
    ckChunk->WriteBuffer_LEndian16(v10 * 12, v23);

    int v11 = m_TVPatches.Size();
    ckChunk->StartObjectIDSequence(v11);
    for (int k = 0; k < v11; ++k) {
        CKObject *v13 = m_Context->GetObject(m_TVPatches[k].Material);
        ckChunk->WriteObjectSequence(v13);
    }

    for (int m = 0; m < v11; ++m) {
        ckChunk->WriteDword(m_TVPatches[m].Flags);
        ckChunk->WriteDword(m_TVPatches[m].Type);
        ckChunk->WriteDword(m_TVPatches[m].SubType);

        void *v24 = m_TVPatches[m].Vertices.Begin();
        int v19 = m_TVPatches[m].Vertices.Size();
        ckChunk->WriteBuffer_LEndian16(v19 * 2, v24);

        void *v25 = m_TVPatches[m].UVs.Begin();
        int v22 = m_TVPatches[m].UVs.Size();
        ckChunk->WriteBuffer_LEndian(v22 * 4, v25);
    }

    if (GetClassID() == CKCID_PATCHMESH)
        ckChunk->CloseChunk();
    else
        ckChunk->UpdateDataSize();

    return ckChunk;
}

/**
 * @brief Prepares patch mesh for saving by saving referenced objects
 * @param file The file to save to
 * @param flags Save flags
 */
void RCKPatchMesh::PreSave(CKFile *file, CKDWORD flags) {
    RCKMesh::PreSave(file, flags);

    for (int i = 0; i < m_Patches.Size(); ++i) {
        CKObject *v3 = m_Context->GetObject(m_Patches[i].Material);
        file->SaveObject(v3, flags);
    }

    for (int j = 0; j < m_TVPatches.Size(); ++j) {
        CKObject *ObjectA = m_Context->GetObject(m_TVPatches[j].Material);
        file->SaveObject(ObjectA, flags);
    }
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCKPatchMesh::m_ClassID = CKCID_PATCHMESH;

CKSTRING RCKPatchMesh::GetClassName() {
    return (CKSTRING) "Patch Mesh";
}

int RCKPatchMesh::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKPatchMesh::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKPatchMesh::Register() {
    // Based on IDA analysis
    CKClassNeedNotificationFrom(RCKPatchMesh::m_ClassID, CKCID_MATERIAL);

    // Register associated parameter GUID: {0x76058DB5, 0x59E3A24B}
    CKClassRegisterAssociatedParameter(RCKPatchMesh::m_ClassID, CKPGUID_PATCHMESH);

    // Register default options
    CKClassRegisterDefaultOptions(RCKPatchMesh::m_ClassID, 2);
}

CKPatchMesh *RCKPatchMesh::CreateInstance(CKContext *Context) {
    // Object size is 0x1AC (428 bytes)
    RCKPatchMesh *patchMesh = new RCKPatchMesh(Context, nullptr);
    return reinterpret_cast<CKPatchMesh *>(patchMesh);
}

//=============================================================================
// CKPatchMesh Virtual Methods Implementation
//=============================================================================

CK_CLASSID RCKPatchMesh::GetClassID() {
    return m_ClassID;
}

int RCKPatchMesh::GetMemoryOccupation() {
    return sizeof(RCKPatchMesh);
}

CKERROR RCKPatchMesh::Copy(CKObject &o, CKDependenciesContext &context) {
    return CK_OK;
}

CKERROR RCKPatchMesh::FromMesh(CKMesh *m) {
    return CKERR_NOTIMPLEMENTED;
}

CKERROR RCKPatchMesh::ToMesh(CKMesh *m, int stepcount) {
    return CKERR_NOTIMPLEMENTED;
}

void RCKPatchMesh::SetIterationCount(int count) {
    m_IterationCount = count;
}

int RCKPatchMesh::GetIterationCount() {
    return m_IterationCount;
}

void RCKPatchMesh::BuildRenderMesh() {
    // TODO: Implement tessellation
}

void RCKPatchMesh::CleanRenderMesh() {
}

void RCKPatchMesh::Clear() {
    ClearPatches();
}

void RCKPatchMesh::ComputePatchAux(int index) {
}

void RCKPatchMesh::ComputePatchInteriors(int index) {
}

CKDWORD RCKPatchMesh::GetPatchFlags() {
    return m_PatchFlags;
}

void RCKPatchMesh::SetPatchFlags(CKDWORD Flags) {
    m_PatchFlags = Flags;
}

void RCKPatchMesh::SetVertVecCount(int VertCount, int VecCount) {
    if (m_Verts) {
        delete[] m_Verts;
        m_Verts = nullptr;
    }
    m_VertCount = VertCount;
    m_VecCount = VecCount;
    int totalCount = VertCount + VecCount;
    if (totalCount > 0) {
        m_Verts = new VxVector[totalCount];
        for (int i = 0; i < totalCount; ++i) {
            m_Verts[i] = VxVector(0, 0, 0);
        }
        m_Vecs = (VecCount > 0) ? &m_Verts[VertCount] : nullptr;
    }
}

int RCKPatchMesh::GetVertCount() {
    return m_VertCount;
}

void RCKPatchMesh::SetVert(int index, VxVector *cp) {
    if (m_Verts && index >= 0 && index < m_VertCount && cp)
        m_Verts[index] = *cp;
}

void RCKPatchMesh::GetVert(int index, VxVector *cp) {
    if (m_Verts && index >= 0 && index < m_VertCount && cp)
        *cp = m_Verts[index];
}

VxVector *RCKPatchMesh::GetVerts() {
    return m_Verts;
}

int RCKPatchMesh::GetVecCount() {
    return m_VecCount;
}

void RCKPatchMesh::SetVec(int index, VxVector *cp) {
    if (m_Vecs && index >= 0 && index < m_VecCount && cp)
        m_Vecs[index] = *cp;
}

void RCKPatchMesh::GetVec(int index, VxVector *cp) {
    if (m_Vecs && index >= 0 && index < m_VecCount && cp)
        *cp = m_Vecs[index];
}

VxVector *RCKPatchMesh::GetVecs() {
    return m_Vecs;
}

void RCKPatchMesh::SetEdgeCount(int count) {
    m_PatchEdges.Resize(count);
}

int RCKPatchMesh::GetEdgeCount() {
    return m_PatchEdges.Size();
}

void RCKPatchMesh::SetEdge(int index, CKPatchEdge *edge) {
    if (index >= 0 && index < m_PatchEdges.Size() && edge)
        m_PatchEdges[index] = *edge;
}

void RCKPatchMesh::GetEdge(int index, CKPatchEdge *edge) {
    if (index >= 0 && index < m_PatchEdges.Size() && edge)
        *edge = m_PatchEdges[index];
}

CKPatchEdge *RCKPatchMesh::GetEdges() {
    return m_PatchEdges.Begin();
}

void RCKPatchMesh::SetPatchCount(int count) {
    m_Patches.Resize(count);
}

int RCKPatchMesh::GetPatchCount() {
    return m_Patches.Size();
}

void RCKPatchMesh::SetPatch(int index, CKPatch *p) {
    if (index >= 0 && index < m_Patches.Size() && p)
        m_Patches[index] = *p;
}

void RCKPatchMesh::GetPatch(int index, CKPatch *p) {
    if (index >= 0 && index < m_Patches.Size() && p)
        *p = m_Patches[index];
}

CKDWORD RCKPatchMesh::GetPatchSM(int index) {
    if (index >= 0 && index < m_Patches.Size())
        return m_Patches[index].SmoothingGroup;
    return 0;
}

void RCKPatchMesh::SetPatchSM(int index, CKDWORD smoothing) {
    if (index >= 0 && index < m_Patches.Size())
        m_Patches[index].SmoothingGroup = smoothing;
}

CKMaterial *RCKPatchMesh::GetPatchMaterial(int index) {
    if (index >= 0 && index < m_Patches.Size())
        return (CKMaterial *) m_Context->GetObject(m_Patches[index].Material);
    return nullptr;
}

void RCKPatchMesh::SetPatchMaterial(int index, CKMaterial *mat) {
    if (index >= 0 && index < m_Patches.Size())
        m_Patches[index].Material = mat ? mat->GetID() : 0;
}

CKPatch *RCKPatchMesh::GetPatches() {
    return m_Patches.Begin();
}

void RCKPatchMesh::SetTVPatchCount(int count, int Channel) {
    // For now, only default channel (-1) is supported
    if (Channel == -1)
        m_TVPatches.Resize(count);
}

int RCKPatchMesh::GetTVPatchCount(int Channel) {
    if (Channel == -1)
        return m_TVPatches.Size();
    return 0;
}

void RCKPatchMesh::SetTVPatch(int index, CKTVPatch *tvpatch, int Channel) {
    // Stub - RCKTVPatch is different from CKTVPatch
}

void RCKPatchMesh::GetTVPatch(int index, CKTVPatch *tvpatch, int Channel) {
    // Stub - RCKTVPatch is different from CKTVPatch
}

CKTVPatch *RCKPatchMesh::GetTVPatches(int Channel) {
    return nullptr; // RCKTVPatch is different from CKTVPatch
}

void RCKPatchMesh::SetTVCount(int count, int Channel) {
    // Stub
}

int RCKPatchMesh::GetTVCount(int Channel) {
    return 0;
}

void RCKPatchMesh::SetTV(int index, float u, float v, int Channel) {
    // Stub
}

void RCKPatchMesh::GetTV(int index, float *u, float *v, int Channel) {
    if (u) *u = 0.0f;
    if (v) *v = 0.0f;
}

VxUV *RCKPatchMesh::GetTVs(int Channel) {
    return nullptr;
}

//=============================================================================
// Internal Helper Methods
//=============================================================================

void *RCKPatchMesh::GetPatchEdgesBegin() {
    return m_PatchEdges.Begin();
}

void RCKPatchMesh::ClearPatchEdges() {
    m_PatchEdges.Clear();
}

void *RCKPatchMesh::GetTVPatchVerticesBegin(int index) {
    if (index >= 0 && index < m_TVPatches.Size())
        return m_TVPatches[index].Vertices.Begin();
    return nullptr;
}

void RCKPatchMesh::ClearTVPatchVertices(int index) {
    if (index >= 0 && index < m_TVPatches.Size())
        m_TVPatches[index].Vertices.Clear();
}

void *RCKPatchMesh::GetTVPatchUVsBegin(int index) {
    if (index >= 0 && index < m_TVPatches.Size())
        return m_TVPatches[index].UVs.Begin();
    return nullptr;
}

void RCKPatchMesh::ClearTVPatchUVs(int index) {
    if (index >= 0 && index < m_TVPatches.Size())
        m_TVPatches[index].UVs.Clear();
}
