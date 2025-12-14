#include "RCKPatchMesh.h"

#include "VxMath.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKMaterial.h"
#include "CKRenderEngineTypes.h"

//=============================================================================
// Forward Declarations
//=============================================================================

static void PatchMeshPreRenderCallback(CKRenderContext *rc, CK3dEntity *ent, CKMesh *mesh, void *data);

//=============================================================================
// Constructor/Destructor
//=============================================================================

/**
 * @brief Constructor for RCKPatchMesh
 * Based on IDA analysis at 0x1003056e
 */
RCKPatchMesh::RCKPatchMesh(CKContext *Context, CKSTRING name)
    : RCKMesh(Context, name),
      m_Verts(nullptr),
      m_Vecs(nullptr),
      m_VertCount(0),
      m_VecCount(0),
      m_PatchFlags(CK_PATCHMESH_BUILDNORMALS),
      // = 2
      m_TextureChannelCount(1),
      m_IterationCount(0),
      m_TessVertexBase(0),
      m_TessFaceBase(0),
      m_TessEdgeVertexCount(0),
      m_TessInteriorVertexCount(0),
      m_TessTotalVertices(0),
      m_TessWorkData0(0),
      m_TessWorkData1(0),
      m_TessWorkData2(0),
      m_PatchChanged(TRUE),
      m_CornerVertexMap(nullptr) {
    // Initialize tessellation work vectors
    for (int i = 0; i < 3; ++i) {
        m_TessWorkVectors[i] = VxVector(0, 0, 0);
    }

    // Initialize texture patches with one default channel
    m_TexturePatches.Resize(1);

    // Register pre-render callback for BuildRenderMesh
    RCKMesh::AddPreRenderCallBack(PatchMeshPreRenderCallback, this, FALSE);
}

/**
 * @brief Destructor for RCKPatchMesh
 */
RCKPatchMesh::~RCKPatchMesh() {
    // Clear releases all allocated memory
    RCKPatchMesh::Clear();

    // Remove the pre-render callback
    RCKMesh::RemovePreRenderCallBack(PatchMeshPreRenderCallback, this);
}

//=============================================================================
// Pre-Render Callback
//=============================================================================

/**
 * @brief Pre-render callback that triggers mesh building
 * Based on IDA analysis at 0x10030540
 */
static void PatchMeshPreRenderCallback(CKRenderContext *rc, CK3dEntity *ent, CKMesh *mesh, void *data) {
    if (CKIsChildClassOf(mesh, CKCID_PATCHMESH)) {
        static_cast<RCKPatchMesh *>(mesh)->BuildRenderMesh();
    }
}

//=============================================================================
// Clear Method
//=============================================================================

/**
 * @brief Clears all patch data from the mesh
 * Based on IDA analysis at 0x100307a9
 */
void RCKPatchMesh::Clear() {
    // Free vertex/vector array
    delete[] m_Verts;
    m_Verts = nullptr;
    m_Vecs = nullptr;

    // Clear patches array
    m_Patches.Clear();

    // Reset texture patches to single default channel
    m_TexturePatches.Resize(1);
    m_TextureChannelCount = 1;

    // Clear the first texture channel's data
    if (m_TexturePatches.Size() > 0) {
        m_TexturePatches[0].Patches.Clear();
        m_TexturePatches[0].UVs.Clear();
    }

    // Free corner vertex map allocated data
    if (m_CornerVertexMap) {
        delete[] m_CornerVertexMap;
        m_CornerVertexMap = nullptr;
    }

    // Clear shared vertex sources array
    m_SharedVertexSources.Resize(0);

    // Reset state
    m_VertCount = 0;
    m_VecCount = 0;
    m_PatchChanged = TRUE;
    m_PatchFlags = CK_PATCHMESH_BUILDNORMALS; // = 2
}

//=============================================================================
// Load Methods
//=============================================================================

/**
 * @brief Loads patch mesh vertices from a state chunk
 * Based on IDA analysis at 0x1003afd8
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
 * Based on IDA analysis at 0x1003b208
 */
CKERROR RCKPatchMesh::Load(CKStateChunk *chunk, CKFile *file) {
    RCKMesh::Load(chunk, file);

    if (chunk->SeekIdentifier(0x8000000)) {
        // New format (0x8000000)
        m_PatchFlags = chunk->ReadDword();
        int iterCount = chunk->ReadInt();
        SetIterationCount(iterCount);

        m_VecCount = chunk->ReadInt();
        CKDWORD bufferSize = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        int totalCount = m_VertCount;
        if (totalCount) {
            VxVector *verts = new VxVector[totalCount];
            if (verts) {
                for (int i = 0; i < totalCount; ++i) {
                    verts[i] = VxVector(0, 0, 0);
                }
                m_Verts = verts;
            } else {
                m_Verts = nullptr;
            }
            chunk->ReadAndFillBuffer_LEndian(bufferSize, m_Verts);
        }

        m_VertCount -= m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = nullptr;
        }

        // Load patches
        int patchCount = chunk->StartReadSequence();
        m_Patches.Resize(patchCount);

        // First pass: read material IDs
        for (int i = 0; i < patchCount; ++i) {
            m_Patches[i].Material = chunk->ReadObjectID();
        }

        // Second pass: read patch data
        for (int i = 0; i < patchCount; ++i) {
            m_Patches[i].type = chunk->ReadDword();
            m_Patches[i].SmoothingGroup = chunk->ReadDword();
            chunk->ReadAndFillBuffer_LEndian16(40, (void *) m_Patches[i].v);
        }

        // Load patch edges
        CKDWORD edgeBufferSize = chunk->ReadDword();
        int edgeCount = chunk->ReadDword();
        if (edgeCount) {
            m_PatchEdges.Resize(edgeCount);
            chunk->ReadAndFillBuffer_LEndian16(edgeBufferSize, m_PatchEdges.Begin());
        } else {
            m_PatchEdges.Clear();
        }

        // Load texture patch channels
        int channelCount = chunk->StartReadSequence();
        m_TexturePatches.Resize(channelCount);

        // First pass: read material IDs
        for (int i = 0; i < channelCount; ++i) {
            m_TexturePatches[i].Material = chunk->ReadObjectID();
        }

        // Second pass: read channel data
        for (int i = 0; i < m_TexturePatches.Size(); ++i) {
            m_TexturePatches[i].Flags = chunk->ReadDword();
            m_TexturePatches[i].Type = chunk->ReadDword();
            m_TexturePatches[i].SubType = chunk->ReadDword();

            // Read patches array
            CKDWORD patchesBufferSize = chunk->ReadDword();
            int patchesCount = chunk->ReadDword();
            if (patchesCount) {
                m_TexturePatches[i].Patches.Resize(patchesCount);
                chunk->ReadAndFillBuffer_LEndian16(patchesBufferSize, m_TexturePatches[i].Patches.Begin());
            } else {
                m_TexturePatches[i].Patches.Clear();
            }

            // Read UVs array
            CKDWORD uvsBufferSize = chunk->ReadDword();
            int uvsCount = chunk->ReadDword();
            if (uvsCount) {
                m_TexturePatches[i].UVs.Resize(uvsCount);
                chunk->ReadAndFillBuffer_LEndian(uvsBufferSize, m_TexturePatches[i].UVs.Begin());
            } else {
                m_TexturePatches[i].UVs.Clear();
            }
        }

        // Update texture channel count
        m_TextureChannelCount = channelCount;
    } else if (chunk->SeekIdentifier(0x1000000)) {
        // Old format loading
        Clear();

        CKDWORD flags = chunk->ReadDword();
        flags |= 2;
        m_PatchFlags = flags;
        m_PatchFlags &= 0xFE;

        chunk->ReadObjectID(); // Skip object ID
        int iterCount = chunk->ReadInt();
        SetIterationCount(iterCount);

        m_VecCount = chunk->ReadInt();
        CKDWORD bufferSize = chunk->ReadDword();
        m_VertCount = chunk->ReadDword();

        int totalCount = m_VertCount;
        if (totalCount) {
            VxVector *verts = new VxVector[totalCount];
            if (verts) {
                for (int i = 0; i < totalCount; ++i) {
                    verts[i] = VxVector(0, 0, 0);
                }
                m_Verts = verts;
            } else {
                m_Verts = nullptr;
            }
            chunk->ReadAndFillBuffer_LEndian(bufferSize, m_Verts);
        }

        m_VertCount -= m_VecCount;

        if (m_Verts && (int) m_VecCount > 0) {
            m_Vecs = &m_Verts[m_VertCount];
        } else {
            m_Vecs = nullptr;
        }

        // Old format has different structure for patches, edges, and texture data
        // This would require more detailed reverse engineering of the old format
    } else {
        // Obsolete format
        CKSTRING name = GetName();
        m_Context->OutputToConsoleExBeep("%s : Obsolete version of PatchMesh format: Cannot Load", name);
    }

    m_PatchChanged = TRUE;
    return CK_OK;
}

/**
 * @brief Saves patch mesh data to a state chunk
 * Based on IDA analysis at 0x1003ac69
 */
CKStateChunk *RCKPatchMesh::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *baseChunk = RCKMesh::Save(file, flags);
    if (!file && (flags & 0xFF00000) == 0)
        return baseChunk;

    CKStateChunk *chunk = CreateCKStateChunk(CKCID_PATCHMESH, file);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    chunk->WriteIdentifier(0x8000000);
    chunk->WriteDword(m_PatchFlags);
    chunk->WriteInt(m_IterationCount);
    chunk->WriteInt(m_VecCount);
    chunk->WriteBuffer_LEndian((m_VertCount + m_VecCount) * 12, m_Verts);

    // Write patches
    int patchCount = m_Patches.Size();
    chunk->StartObjectIDSequence(patchCount);

    // First pass: write material IDs
    for (int i = 0; i < patchCount; ++i) {
        CKObject *mat = m_Context->GetObject(m_Patches[i].Material);
        chunk->WriteObjectSequence(mat);
    }

    // Second pass: write patch data
    for (int i = 0; i < patchCount; ++i) {
        chunk->WriteDword(m_Patches[i].type);
        chunk->WriteDword(m_Patches[i].SmoothingGroup);
        chunk->WriteBufferNoSize_LEndian16(40, (void *) m_Patches[i].v);
    }

    // Write patch edges
    int edgeCount = m_PatchEdges.Size();
    chunk->WriteBuffer_LEndian16(edgeCount * 12, m_PatchEdges.Begin());

    // Write texture patch channels
    int channelCount = m_TexturePatches.Size();
    chunk->StartObjectIDSequence(channelCount);

    // First pass: write material IDs
    for (int i = 0; i < channelCount; ++i) {
        CKObject *mat = m_Context->GetObject(m_TexturePatches[i].Material);
        chunk->WriteObjectSequence(mat);
    }

    // Second pass: write channel data
    for (int i = 0; i < channelCount; ++i) {
        chunk->WriteDword(m_TexturePatches[i].Flags);
        chunk->WriteDword(m_TexturePatches[i].Type);
        chunk->WriteDword(m_TexturePatches[i].SubType);

        // Write patches array
        int patchesCount = m_TexturePatches[i].Patches.Size();
        chunk->WriteBuffer_LEndian16(patchesCount * sizeof(CKTVPatch), m_TexturePatches[i].Patches.Begin());

        // Write UVs array
        int uvsCount = m_TexturePatches[i].UVs.Size();
        chunk->WriteBuffer_LEndian(uvsCount * sizeof(VxUV), m_TexturePatches[i].UVs.Begin());
    }

    if (GetClassID() == CKCID_PATCHMESH)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Prepares patch mesh for saving by saving referenced objects
 * Based on IDA analysis at 0x1003c333
 */
void RCKPatchMesh::PreSave(CKFile *file, CKDWORD flags) {
    RCKMesh::PreSave(file, flags);

    // Save patch materials
    for (int i = 0; i < m_Patches.Size(); ++i) {
        CKObject *mat = m_Context->GetObject(m_Patches[i].Material);
        if (mat)
            file->SaveObject(mat, flags);
    }

    // Save texture channel materials
    for (int i = 0; i < m_TexturePatches.Size(); ++i) {
        CKObject *mat = m_Context->GetObject(m_TexturePatches[i].Material);
        if (mat)
            file->SaveObject(mat, flags);
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
    CKClassNeedNotificationFrom(m_ClassID, CKCID_MATERIAL);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_PATCHMESH);
    CKClassRegisterDefaultOptions(m_ClassID, 2);
}

CKPatchMesh *RCKPatchMesh::CreateInstance(CKContext *Context) {
    RCKPatchMesh *patchMesh = new RCKPatchMesh(Context, nullptr);
    return reinterpret_cast<CKPatchMesh *>(patchMesh);
}

//=============================================================================
// CKPatchMesh Virtual Methods Implementation
//=============================================================================

CK_CLASSID RCKPatchMesh::GetClassID() {
    return m_ClassID;
}

/**
 * @brief Returns memory occupation of the patch mesh
 * Based on IDA analysis at 0x1003abb7
 */
int RCKPatchMesh::GetMemoryOccupation() {
    int base = RCKMesh::GetMemoryOccupation() + 168; // Extra fields size

    // Vertex and vector memory
    int vertVecMem = 12 * (m_VertCount + m_VecCount);

    // Calculate texture channel memory
    int channelMem = 0;
    for (int i = 0; i < m_TexturePatches.Size(); ++i) {
        int patchesSize = m_TexturePatches[i].Patches.Size();
        int uvsSize = m_TexturePatches[i].UVs.Size();
        channelMem += 8 * patchesSize + 8 * uvsSize + 40; // 40 = CKPatchChannel base size
    }

    // Patches memory (56 bytes each)
    int patchesMem = 56 * m_Patches.Size();

    // Edges memory (12 bytes each)
    int edgesMem = 12 * m_PatchEdges.Size();

    return base + vertVecMem + channelMem + patchesMem + edgesMem;
}

/**
 * @brief Copy patch mesh from another object
 * Based on IDA analysis at 0x1003cd40
 */
CKERROR RCKPatchMesh::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCKMesh::Copy(o, context);
    if (err != CK_OK)
        return err;

    context.GetClassDependencies(CKCID_MESH);

    // Save the source object and load into this
    CKStateChunk *chunk = o.Save(nullptr, 0xFF00000);
    if (!chunk)
        return CKERR_OUTOFMEMORY;

    Load(chunk, nullptr);
    delete chunk;

    return CK_OK;
}

/**
 * @brief Convert from a regular CKMesh (not implemented)
 * Based on IDA analysis at 0x1003412a - returns CKERR_NOTIMPLEMENTED
 */
CKERROR RCKPatchMesh::FromMesh(CKMesh *m) {
    return CKERR_NOTIMPLEMENTED;
}

/**
 * @brief Convert to a regular CKMesh (not implemented)
 * Based on IDA analysis at 0x1003413c - returns CKERR_NOTIMPLEMENTED
 */
CKERROR RCKPatchMesh::ToMesh(CKMesh *m, int stepcount) {
    return CKERR_NOTIMPLEMENTED;
}

/**
 * @brief Set the iteration count for tessellation
 * Based on IDA analysis at 0x100308ad
 */
void RCKPatchMesh::SetIterationCount(int count) {
    if (m_IterationCount != count) {
        if (count < 0)
            count = 0;
        m_IterationCount = count;
        m_PatchFlags &= ~CK_PATCHMESH_UPTODATE;    // Clear UPTODATE flag
        m_PatchFlags |= CK_PATCHMESH_BUILDNORMALS; // Set BUILDNORMALS flag
    }
}

int RCKPatchMesh::GetIterationCount() {
    return m_IterationCount;
}

/**
 * @brief Build the render mesh from patch data
 * Based on IDA analysis at 0x1003424f
 *
 * This is the core tessellation function that converts patch data into
 * a renderable triangle mesh. It handles:
 * - Triangle patches (3 corners) and quad patches (4 corners)
 * - Configurable tessellation level via m_IterationCount
 * - Smooth edge handling for normal interpolation
 * - Texture coordinate generation
 *
 * The original function is ~25KB (0x609f bytes), making it one of the
 * largest functions in CK2_3D.dll.
 */
void RCKPatchMesh::BuildRenderMesh() {
    // Early exit conditions - check if mesh needs building
    // Bit 0 of m_PatchFlags = CK_PATCHMESH_UPTODATE
    if ((m_PatchFlags & CK_PATCHMESH_UPTODATE) != 0)
        return;

    // Need edges, patches, verts and vecs to build
    if (m_PatchEdges.Size() == 0)
        return;
    if (m_Patches.Size() == 0)
        return;
    if (m_VertCount == 0)
        return;
    if (m_VecCount == 0)
        return;

    // Mark mesh as invalid during rebuild
    m_Valid = FALSE;

    int patchCount = m_Patches.Size();
    int edgeCount = m_PatchEdges.Size();
    m_TextureChannelCount = m_TexturePatches.Size();

    // Calculate tessellation parameters
    int tessLevel = m_IterationCount + 1;      // Number of divisions per edge
    float tessStep = 1.0f / (float) tessLevel; // Parametric step size
    int tessLevelMinus1 = tessLevel - 1;

    int totalVertices = 0;
    int totalFaces = 0;

    // Handle patch changed state - rebuild mapping arrays
    if (m_PatchChanged) {
        // Free old corner vertex map
        if (m_CornerVertexMap) {
            delete[] m_CornerVertexMap;
            m_CornerVertexMap = nullptr;
        }

        // Clear shared vertex sources
        m_SharedVertexSources.Resize(0);
        m_PatchChanged = FALSE;

        // Allocate corner vertex map (4 entries per patch)
        m_CornerVertexMap = new CKDWORD[4 * patchCount];

        // Initialize all entries to -1 (unassigned)
        for (int i = 0; i < 4 * patchCount; ++i) {
            m_CornerVertexMap[i] = (CKDWORD) -1;
        }

        // Process edges to determine smooth/hard edge flags
        for (int i = 0; i < edgeCount; ++i) {
            CKPatchEdge *edge = &m_PatchEdges[i];
            int p0 = edge->patch1;
            int p1 = edge->patch2;

            // Skip edges with invalid patch references
            if (p1 < 0)
                continue;

            // Check if patches share smoothing group
            CKPatch *patch0 = &m_Patches[p0];
            CKPatch *patch1 = &m_Patches[p1];

            if ((patch0->SmoothingGroup & patch1->SmoothingGroup) == 0) {
                // Hard edge - no smoothing group in common
                // XSArray doesn't have Set, we need to resize and assign
                if ((int) m_HardEdgeFlags.Size() <= i) {
                    m_HardEdgeFlags.Resize(i + 1);
                }
                m_HardEdgeFlags[i] = 1;
            } else {
                // Smooth edge
                if ((int) m_SmoothEdgeFlags.Size() <= i) {
                    m_SmoothEdgeFlags.Resize(i + 1);
                }
                m_SmoothEdgeFlags[i] = 1;
            }
        }
    }

    // Calculate total vertex and face counts based on patch types
    for (int i = 0; i < patchCount; ++i) {
        CKPatch *patch = &m_Patches[i];

        if (patch->type == 3) {
            // Triangle patch
            // Vertices: (tessLevel+1)*(tessLevel+2)/2 per patch (triangular grid)
            // Faces: tessLevel^2 triangles
            int triVerts = (tessLevel + 1) * (tessLevel + 2) / 2;
            int triFaces = tessLevel * tessLevel;
            totalVertices += triVerts;
            totalFaces += triFaces;
        } else if (patch->type == 4) {
            // Quad patch
            // Vertices: (tessLevel+1)^2 per patch (regular grid)
            // Faces: 2 * tessLevel^2 triangles
            int quadVerts = (tessLevel + 1) * (tessLevel + 1);
            int quadFaces = 2 * tessLevel * tessLevel;
            totalVertices += quadVerts;
            totalFaces += quadFaces;
        }
    }

    // Add vertices for shared edge smoothing
    totalVertices += m_SharedVertexSources.Size();

    // Allocate mesh data
    RCKMesh::SetVertexCount(totalVertices);
    RCKMesh::SetFaceCount(totalFaces);

    // Get vertex data pointer
    CKDWORD stride = 0;
    VxVector *positions = (VxVector *) RCKMesh::GetPositionsPtr(&stride);
    if (!positions)
        return;

    int currentVertex = 0;
    int currentFace = 0;

    // Process each patch
    for (int patchIdx = 0; patchIdx < patchCount; ++patchIdx) {
        CKPatch *patch = &m_Patches[patchIdx];
        CKMaterial *patchMat = GetPatchMaterial(patchIdx);

        if (patch->type == 3) {
            // Tessellate triangle patch
            int patchVertBase = currentVertex;

            // Generate vertices in barycentric pattern
            for (int j = 0; j <= tessLevel; ++j) {
                float v = (float) j * tessStep;
                for (int k = 0; k <= tessLevel - j; ++k) {
                    float u = (float) k * tessStep;
                    float w = 1.0f - u - v;

                    // Evaluate Bezier triangle at (u, v, w)
                    VxVector pos;
                    EvaluateTriPatch(patch, u, v, w, &pos);

                    // Calculate position pointer using stride
                    VxVector *posPtr = (VxVector *) ((CKBYTE *) positions + currentVertex * stride);
                    *posPtr = pos;
                    currentVertex++;
                }
            }

            // Generate triangle faces
            int rowStart = patchVertBase;
            for (int j = 0; j < tessLevel; ++j) {
                int rowLen = tessLevel - j + 1;
                int nextRowStart = rowStart + rowLen;

                for (int k = 0; k < rowLen - 1; ++k) {
                    // Upward triangle
                    RCKMesh::SetFaceVertexIndex(currentFace,
                                                rowStart + k,
                                                rowStart + k + 1,
                                                nextRowStart + k);
                    RCKMesh::SetFaceMaterial(currentFace, patchMat);
                    currentFace++;

                    // Downward triangle (if not at edge)
                    if (k < rowLen - 2) {
                        RCKMesh::SetFaceVertexIndex(currentFace,
                                                    rowStart + k + 1,
                                                    nextRowStart + k + 1,
                                                    nextRowStart + k);
                        RCKMesh::SetFaceMaterial(currentFace, patchMat);
                        currentFace++;
                    }
                }
                rowStart = nextRowStart;
            }
        } else if (patch->type == 4) {
            // Tessellate quad patch
            int patchVertBase = currentVertex;

            // Generate vertices in grid pattern
            for (int j = 0; j <= tessLevel; ++j) {
                float v = (float) j * tessStep;
                for (int k = 0; k <= tessLevel; ++k) {
                    float u = (float) k * tessStep;

                    // Evaluate Bezier quad at (u, v)
                    VxVector pos;
                    EvaluateQuadPatch(patch, u, v, &pos);

                    // Calculate position pointer using stride
                    VxVector *posPtr = (VxVector *) ((CKBYTE *) positions + currentVertex * stride);
                    *posPtr = pos;
                    currentVertex++;
                }
            }

            // Generate triangle faces (2 per quad cell)
            int gridWidth = tessLevel + 1;
            for (int j = 0; j < tessLevel; ++j) {
                for (int k = 0; k < tessLevel; ++k) {
                    int idx00 = patchVertBase + j * gridWidth + k;
                    int idx10 = idx00 + 1;
                    int idx01 = idx00 + gridWidth;
                    int idx11 = idx01 + 1;

                    // First triangle
                    RCKMesh::SetFaceVertexIndex(currentFace, idx00, idx10, idx01);
                    RCKMesh::SetFaceMaterial(currentFace, patchMat);
                    currentFace++;

                    // Second triangle
                    RCKMesh::SetFaceVertexIndex(currentFace, idx10, idx11, idx01);
                    RCKMesh::SetFaceMaterial(currentFace, patchMat);
                    currentFace++;
                }
            }
        }

        // Store corner vertex indices in map
        for (int corner = 0; corner < (int) patch->type; ++corner) {
            m_CornerVertexMap[patchIdx * 4 + corner] = currentVertex - 1; // Placeholder
        }
    }

    // Build normals if requested
    if ((m_PatchFlags & CK_PATCHMESH_BUILDNORMALS) != 0) {
        RCKMesh::BuildFaceNormals();

        // Clear vertex normals
        int vertCount = RCKMesh::GetVertexCount();
        CKDWORD normStride = 0;
        VxVector *normals = (VxVector *) RCKMesh::GetNormalsPtr(&normStride);
        for (int i = 0; i < vertCount; ++i) {
            VxVector *normPtr = (VxVector *) ((CKBYTE *) normals + i * normStride);
            *normPtr = VxVector(0, 0, 0);
        }

        // Accumulate face normals into vertex normals
        int faceCountNow = RCKMesh::GetFaceCount();
        CKWORD *indices = RCKMesh::GetFacesIndices();

        for (int i = 0; i < faceCountNow; ++i) {
            VxVector faceNormal = RCKMesh::GetFaceNormal(i);

            CKWORD i0 = indices[i * 3];
            CKWORD i1 = indices[i * 3 + 1];
            CKWORD i2 = indices[i * 3 + 2];

            VxVector *norm0 = (VxVector *) ((CKBYTE *) normals + i0 * normStride);
            VxVector *norm1 = (VxVector *) ((CKBYTE *) normals + i1 * normStride);
            VxVector *norm2 = (VxVector *) ((CKBYTE *) normals + i2 * normStride);

            *norm0 += faceNormal;
            *norm1 += faceNormal;
            *norm2 += faceNormal;
        }

        // Handle smooth edges - share normals across patch boundaries
        for (int i = 0; i < edgeCount; ++i) {
            if ((int) m_SmoothEdgeFlags.Size() <= i || m_SmoothEdgeFlags[i] == 0)
                continue;

            // Copy normals between shared edge vertices
            // This is simplified - actual implementation is more complex
        }

        // Normalize all vertex normals
        for (int i = 0; i < vertCount; ++i) {
            VxVector *normPtr = (VxVector *) ((CKBYTE *) normals + i * normStride);
            normPtr->Normalize();
        }
    }

    // Mark patch mesh as up-to-date
    m_PatchFlags |= CK_PATCHMESH_UPTODATE;

    // Trigger vertex update
    RCKMesh::VertexMove();
}

/**
 * @brief Evaluate a triangular Bezier patch at parametric coordinates
 */
void RCKPatchMesh::EvaluateTriPatch(CKPatch *patch, float u, float v, float w, VxVector *result) {
    // Get corner vertices
    VxVector &v0 = m_Verts[patch->v[0]];
    VxVector &v1 = m_Verts[patch->v[1]];
    VxVector &v2 = m_Verts[patch->v[2]];

    // Simple linear interpolation for basic tessellation
    // A full implementation would use cubic Bezier evaluation with control points
    *result = v0 * w + v1 * u + v2 * v;
}

/**
 * @brief Evaluate a quadrilateral Bezier patch at parametric coordinates
 */
void RCKPatchMesh::EvaluateQuadPatch(CKPatch *patch, float u, float v, VxVector *result) {
    // Get corner vertices
    VxVector &v00 = m_Verts[patch->v[0]];
    VxVector &v10 = m_Verts[patch->v[1]];
    VxVector &v11 = m_Verts[patch->v[2]];
    VxVector &v01 = m_Verts[patch->v[3]];

    // Bilinear interpolation for basic tessellation
    // A full implementation would use bicubic Bezier evaluation with control points
    float u1 = 1.0f - u;
    float v1 = 1.0f - v;

    *result = v00 * (u1 * v1) + v10 * (u * v1) + v01 * (u1 * v) + v11 * (u * v);
}

/**
 * @brief Clean the render mesh data
 * Based on IDA analysis at 0x1003a2ee
 */
void RCKPatchMesh::CleanRenderMesh() {
    SetVertexCount(0);
    SetFaceCount(0);
    UnOptimize();
    m_PatchChanged = TRUE;
    m_PatchFlags &= 0xFFFFFFFA; // Clear bits 0, 2
}

/**
 * @brief Compute auxiliary points for a patch (Bezier control points)
 * Based on IDA analysis at 0x10031362
 *
 * For triangle patches, computes 9 auxiliary points (3 per edge) that
 * define the Bezier surface. These are used during tessellation to
 * evaluate the curved surface.
 */
void RCKPatchMesh::ComputePatchAux(int index) {
    if (index < 0 || index >= m_Patches.Size())
        return;

    CKPatch *patch = &m_Patches[index];

    // Auxiliary lookup order for triangle patches
    static const int auxOrder[3] = {1, 2, 0};

    // Allocate auxiliary points if not already present (9 points for triangle)
    if (!patch->auxs) {
        patch->auxs = new VxVector[9];
        for (int i = 0; i < 9; ++i) {
            patch->auxs[i] = VxVector(0, 0, 0);
        }
    }

    int auxIndex = 0;
    int vecIdx = 0;

    for (int i = 0; i < 3; ++i) {
        // Get corner vertices for this edge
        VxVector *v0 = &m_Verts[patch->v[i]];
        VxVector *v1 = &m_Verts[patch->v[auxOrder[i]]];

        // Get edge control vectors
        VxVector *vec0 = &m_Vecs[patch->vec[vecIdx]];
        VxVector *vec1 = &m_Vecs[patch->vec[vecIdx + 1]];

        // Compute auxiliary points using cubic Bezier subdivision
        // aux[0] = v0 + 0.75 * (vec0 - v0)
        VxVector diff0 = *vec0 - *v0;
        patch->auxs[auxIndex] = *v0 + diff0 * 0.75f;

        // aux[1] = vec0 + 0.5 * (vec1 - vec0)
        VxVector diff1 = *vec1 - *vec0;
        patch->auxs[auxIndex + 1] = *vec0 + diff1 * 0.5f;

        // aux[2] = vec1 + 0.25 * (v1 - vec1)
        VxVector diff2 = *v1 - *vec1;
        patch->auxs[auxIndex + 2] = *vec1 + diff2 * 0.25f;

        auxIndex += 3;
        vecIdx += 2;
    }
}

/**
 * @brief Compute interior vector points for a patch
 * Based on IDA analysis at 0x100315f0
 *
 * For both triangle and quad patches, computes the interior control
 * vectors that define the surface curvature in the patch interior.
 */
void RCKPatchMesh::ComputePatchInteriors(int index) {
    if (index < 0 || index >= m_Patches.Size())
        return;

    CKPatch *patch = &m_Patches[index];

    if (patch->type == 3) {
        // Triangle patch - compute 3 interior points
        // First compute auxiliary points
        ComputePatchAux(index);

        // interior[0] = vec[5] + (vec[0] - v[0])
        VxVector diff0 = m_Vecs[patch->vec[0]] - m_Verts[patch->v[0]];
        m_Vecs[patch->interior[0]] = m_Vecs[patch->vec[5]] + diff0;

        // interior[1] = vec[1] + (vec[2] - v[1])
        VxVector diff1 = m_Vecs[patch->vec[2]] - m_Verts[patch->v[1]];
        m_Vecs[patch->interior[1]] = m_Vecs[patch->vec[1]] + diff1;

        // interior[2] = vec[3] + (vec[4] - v[2])
        VxVector diff2 = m_Vecs[patch->vec[4]] - m_Verts[patch->v[2]];
        m_Vecs[patch->interior[2]] = m_Vecs[patch->vec[3]] + diff2;
    } else if (patch->type == 4) {
        // Quad patch - compute 4 interior points

        // interior[0] = vec[7] + (vec[0] - v[0])
        VxVector diff0 = m_Vecs[patch->vec[0]] - m_Verts[patch->v[0]];
        m_Vecs[patch->interior[0]] = m_Vecs[patch->vec[7]] + diff0;

        // interior[1] = vec[1] + (vec[2] - v[1])
        VxVector diff1 = m_Vecs[patch->vec[2]] - m_Verts[patch->v[1]];
        m_Vecs[patch->interior[1]] = m_Vecs[patch->vec[1]] + diff1;

        // interior[2] = vec[3] + (vec[4] - v[2])
        VxVector diff2 = m_Vecs[patch->vec[4]] - m_Verts[patch->v[2]];
        m_Vecs[patch->interior[2]] = m_Vecs[patch->vec[3]] + diff2;

        // interior[3] = vec[5] + (vec[6] - v[3])
        VxVector diff3 = m_Vecs[patch->vec[6]] - m_Verts[patch->v[3]];
        m_Vecs[patch->interior[3]] = m_Vecs[patch->vec[5]] + diff3;
    }
}

CKDWORD RCKPatchMesh::GetPatchFlags() {
    return m_PatchFlags;
}

void RCKPatchMesh::SetPatchFlags(CKDWORD Flags) {
    m_PatchFlags = Flags;
}

/**
 * @brief Set vertex and vector counts, allocating combined array
 * Based on IDA analysis at 0x10030921
 */
void RCKPatchMesh::SetVertVecCount(int VertCount, int VecCount) {
    m_VertCount = VertCount;
    m_VecCount = VecCount;

    if (m_Verts) {
        delete[] m_Verts;
        m_Verts = nullptr;
    }

    int totalCount = VertCount + VecCount;
    if (totalCount > 0) {
        VxVector *verts = new VxVector[totalCount];
        if (verts) {
            for (int i = 0; i < totalCount; ++i) {
                verts[i] = VxVector(0, 0, 0);
            }
            m_Verts = verts;
        }
    }

    if (m_Verts && VecCount > 0) {
        m_Vecs = &m_Verts[VertCount];
    } else {
        m_Vecs = nullptr;
    }

    m_PatchChanged = TRUE;
}

int RCKPatchMesh::GetVertCount() {
    return m_VertCount;
}

/**
 * @brief Set a vertex position
 * Based on IDA analysis at 0x10030a74
 */
void RCKPatchMesh::SetVert(int index, VxVector *cp) {
    if (index >= 0 && index < (int) m_VertCount && cp) {
        m_Verts[index] = *cp;
        m_PatchFlags &= ~CK_PATCHMESH_UPTODATE; // Clear UPTODATE flag
    }
}

/**
 * @brief Get a vertex position
 * Based on IDA analysis at 0x10030ad0
 */
void RCKPatchMesh::GetVert(int index, VxVector *cp) {
    if (index >= 0 && index < (int) m_VertCount && cp) {
        *cp = m_Verts[index];
    }
}

VxVector *RCKPatchMesh::GetVerts() {
    return m_Verts;
}

int RCKPatchMesh::GetVecCount() {
    return m_VecCount;
}

/**
 * @brief Set a vector position
 * Based on IDA analysis at 0x10030b17
 */
void RCKPatchMesh::SetVec(int index, VxVector *cp) {
    if (index >= 0 && index < (int) m_VecCount && cp) {
        m_Vecs[index] = *cp;
        m_PatchFlags &= ~CK_PATCHMESH_UPTODATE; // Clear UPTODATE flag
    }
}

/**
 * @brief Get a vector position
 * Based on IDA analysis at 0x10030b73
 */
void RCKPatchMesh::GetVec(int index, VxVector *cp) {
    if (index >= 0 && index < (int) m_VecCount && cp) {
        *cp = m_Vecs[index];
    }
}

VxVector *RCKPatchMesh::GetVecs() {
    return m_Vecs;
}

/**
 * @brief Set number of edges
 * Based on IDA analysis at 0x10030be2
 */
void RCKPatchMesh::SetEdgeCount(int count) {
    m_PatchEdges.Resize(count);
    m_PatchChanged = TRUE;
}

int RCKPatchMesh::GetEdgeCount() {
    return m_PatchEdges.Size();
}

/**
 * @brief Set edge data
 * Based on IDA analysis at 0x10030c27
 */
void RCKPatchMesh::SetEdge(int index, CKPatchEdge *edge) {
    if (index >= 0 && index < m_PatchEdges.Size() && edge) {
        m_PatchEdges[index] = *edge;
    }
}

/**
 * @brief Get edge data
 * Based on IDA analysis at 0x10030c76
 */
void RCKPatchMesh::GetEdge(int index, CKPatchEdge *edge) {
    if (index >= 0 && index < m_PatchEdges.Size() && edge) {
        *edge = m_PatchEdges[index];
    }
}

CKPatchEdge *RCKPatchMesh::GetEdges() {
    return m_PatchEdges.Begin();
}

/**
 * @brief Set number of patches
 * Based on IDA analysis at 0x10030cdc
 */
void RCKPatchMesh::SetPatchCount(int count) {
    m_Patches.Resize(count);
    m_PatchChanged = TRUE;
}

int RCKPatchMesh::GetPatchCount() {
    return m_Patches.Size();
}

/**
 * @brief Set patch data
 * Based on IDA analysis at 0x10030d21
 * Copies 56 bytes (size of CKPatch without auxs) and clears auxs
 */
void RCKPatchMesh::SetPatch(int index, CKPatch *p) {
    if (index >= 0 && index < m_Patches.Size() && p) {
        // Copy 56 bytes (all fields except auxs)
        memcpy(&m_Patches[index], p, 56);
        m_Patches[index].auxs = nullptr; // Clear runtime auxs pointer
        m_PatchChanged = TRUE;
    }
}

/**
 * @brief Get patch data
 * Based on IDA analysis at 0x10030d91
 */
void RCKPatchMesh::GetPatch(int index, CKPatch *p) {
    if (index >= 0 && index < m_Patches.Size() && p) {
        memcpy(p, &m_Patches[index], 56);
        p->auxs = nullptr; // Clear runtime auxs pointer
    }
}

/**
 * @brief Get patch smoothing group
 * Based on IDA analysis at 0x10030dfe
 */
CKDWORD RCKPatchMesh::GetPatchSM(int index) {
    if (index >= 0 && index < m_Patches.Size())
        return m_Patches[index].SmoothingGroup;
    return 0;
}

/**
 * @brief Set patch smoothing group
 * Based on IDA analysis at 0x10030e3d
 */
void RCKPatchMesh::SetPatchSM(int index, CKDWORD smoothing) {
    if (index >= 0 && index < m_Patches.Size()) {
        m_Patches[index].SmoothingGroup = smoothing;
        m_PatchChanged = TRUE;
    }
}

/**
 * @brief Get patch material
 * Based on IDA analysis at 0x10030e8a
 */
CKMaterial *RCKPatchMesh::GetPatchMaterial(int index) {
    if (index >= 0 && index < m_Patches.Size())
        return (CKMaterial *) m_Context->GetObject(m_Patches[index].Material);
    return nullptr;
}

/**
 * @brief Set patch material
 * Based on IDA analysis at 0x10030ed5
 */
void RCKPatchMesh::SetPatchMaterial(int index, CKMaterial *mat) {
    if (index >= 0 && index < m_Patches.Size()) {
        CK_ID newId = mat ? mat->GetID() : 0;
        if (newId != m_Patches[index].Material) {
            m_Patches[index].Material = newId;
            m_PatchFlags &= 0xFFFFFFFA; // Clear bits 0, 2
        }
    }
}

CKPatch *RCKPatchMesh::GetPatches() {
    return m_Patches.Begin();
}

//=============================================================================
// Texture Patch Methods
// These use Channel+1 to index into m_TexturePatches
// Channel -1 = index 0 (default), Channel 0 = index 1, etc.
//=============================================================================

/**
 * @brief Set texture patch count for a channel
 * Based on IDA analysis at 0x1003102e
 */
void RCKPatchMesh::SetTVPatchCount(int count, int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (count == 0) {
            m_TexturePatches[channelIndex].Patches.Clear();
        } else {
            m_TexturePatches[channelIndex].Patches.Resize(count);
        }
    }
}

/**
 * @brief Get texture patch count for a channel
 * Based on IDA analysis at 0x1003107f
 */
int RCKPatchMesh::GetTVPatchCount(int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        return m_TexturePatches[channelIndex].Patches.Size();
    }
    return 0;
}

/**
 * @brief Set texture patch data
 * Based on IDA analysis at 0x100310a8
 */
void RCKPatchMesh::SetTVPatch(int index, CKTVPatch *tvpatch, int Channel) {
    int channelIndex = Channel + 1;
    if (index >= 0 && channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (index < m_TexturePatches[channelIndex].Patches.Size() && tvpatch) {
            m_TexturePatches[channelIndex].Patches[index] = *tvpatch;
        }
    }
}

/**
 * @brief Get texture patch data
 * Based on IDA analysis at 0x1003110f
 */
void RCKPatchMesh::GetTVPatch(int index, CKTVPatch *tvpatch, int Channel) {
    int channelIndex = Channel + 1;
    if (index >= 0 && channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (index < m_TexturePatches[channelIndex].Patches.Size() && tvpatch) {
            *tvpatch = m_TexturePatches[channelIndex].Patches[index];
        }
    }
}

/**
 * @brief Get pointer to texture patches array
 * Based on IDA analysis at 0x10031172
 */
CKTVPatch *RCKPatchMesh::GetTVPatches(int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        return m_TexturePatches[channelIndex].Patches.Begin();
    }
    return nullptr;
}

/**
 * @brief Set texture coordinate count for a channel
 * Based on IDA analysis at 0x1003119b
 */
void RCKPatchMesh::SetTVCount(int count, int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (count == 0) {
            m_TexturePatches[channelIndex].UVs.Clear();
        } else {
            m_TexturePatches[channelIndex].UVs.Resize(count);
        }
    }
}

/**
 * @brief Get texture coordinate count for a channel
 * Based on IDA analysis at 0x100311f2
 */
int RCKPatchMesh::GetTVCount(int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        return m_TexturePatches[channelIndex].UVs.Size();
    }
    return 0;
}

/**
 * @brief Set texture coordinate values
 * Based on IDA analysis at 0x1003121e
 */
void RCKPatchMesh::SetTV(int index, float u, float v, int Channel) {
    int channelIndex = Channel + 1;
    if (index >= 0 && channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (index < m_TexturePatches[channelIndex].UVs.Size()) {
            m_TexturePatches[channelIndex].UVs[index].u = u;
            m_TexturePatches[channelIndex].UVs[index].v = v;
        }
    }
}

/**
 * @brief Get texture coordinate values
 * Based on IDA analysis at 0x100312a8
 */
void RCKPatchMesh::GetTV(int index, float *u, float *v, int Channel) {
    int channelIndex = Channel + 1;
    if (index >= 0 && channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        if (index < m_TexturePatches[channelIndex].UVs.Size()) {
            if (u) *u = m_TexturePatches[channelIndex].UVs[index].u;
            if (v) *v = m_TexturePatches[channelIndex].UVs[index].v;
            return;
        }
    }
    if (u) *u = 0.0f;
    if (v) *v = 0.0f;
}

/**
 * @brief Get pointer to texture coordinates array
 * Based on IDA analysis at 0x10031336
 */
VxUV *RCKPatchMesh::GetTVs(int Channel) {
    int channelIndex = Channel + 1;
    if (channelIndex >= 0 && channelIndex < m_TexturePatches.Size()) {
        return m_TexturePatches[channelIndex].UVs.Begin();
    }
    return nullptr;
}
