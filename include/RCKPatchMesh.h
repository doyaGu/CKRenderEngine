#ifndef RCKPATCHMESH_H
#define RCKPATCHMESH_H

#include "CKRenderEngineTypes.h"

#include "RCKMesh.h"
#include "CKPatchMesh.h"

/**
 * @brief Internal structure for texture patch channel data
 * 
 * Each channel contains arrays of texture patch indices and UVs,
 * plus material and flags information.
 * Native/SDL Win32: 40 bytes. SDL x64: 64 bytes.
 * Offset comments below describe the native Win32 layout.
 */
struct CKPatchChannel {
    XClassArray<CKTVPatch> Patches;  // 0x00 - Texture patch indices (12 bytes)
    XClassArray<VxUV> UVs;           // 0x0C - Texture coordinates (12 bytes)
    CK_ID Material;                  // 0x18 - Material ID (4 bytes)
    CKDWORD Flags;                   // 0x1C - Flags (4 bytes)
    CKDWORD Type;                    // 0x20 - Type (4 bytes)
    CKDWORD SubType;                 // 0x24 - SubType (4 bytes)
    // Total: 40 bytes
    
    CKPatchChannel() : Material(0), Flags(0), Type(0), SubType(0) {}
    ~CKPatchChannel() {
        Patches.Clear();
        UVs.Clear();
    }
};

/**
 * @brief Patch mesh implementation
 *
 * CKPatchMesh represents a parametric surface mesh using Bezier patches.
 * It supports both triangular (3-vertex) and quadrilateral (4-vertex) patches.
 * The mesh can be tessellated at runtime with configurable iteration count.
 *
 * Native Win32: 428 bytes (0x1AC), including the 260-byte Mesh base.
 * SDL: 460 bytes on Win32, 728 bytes on x64.
 * Offset comments below describe the native Win32 layout, not the SDL ABI.
 */
class RCKPatchMesh : public RCKMesh {
public:

#undef CK_PURE
#define CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKPatchMesh.h"
#undef CK_3DIMPLEMENTATION
#undef CK_PURE

    explicit RCKPatchMesh(CKContext *Context, CKSTRING name = nullptr);
    ~RCKPatchMesh() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;
    void PreSave(CKFile *file, CKDWORD flags) override;
    void LoadVertices(CKStateChunk *chunk) override;

    CKBYTE *GetModifierVertices(CKDWORD *Stride) override;
    int GetModifierVertexCount() override;
    void ModifierVertexMove(CKBOOL RebuildNormals, CKBOOL RebuildFaceNormals) override;
    CKBYTE *GetModifierUVs(CKDWORD *Stride, int channel = -1) override;
    int GetModifierUVCount(int channel = -1) override;
    void ModifierUVMove() override;

    using RCKMesh::RemoveChannel;
    int GetChannelCount() override;
    int AddChannel(CKMaterial *Mat, CKBOOL CopySrcUv = TRUE) override;
    void RemoveChannel(int Index) override;
    void RemoveChannelByMaterial(CKMaterial *Mat) override;
    int GetChannelByMaterial(CKMaterial *Mat) override;
    void ActivateChannel(int Index, CKBOOL Active = TRUE) override;
    CKBOOL IsChannelActive(int Index) override;
    void ActivateAllChannels(CKBOOL Active = TRUE) override;
    void LitChannel(int Index, CKBOOL Lit = TRUE) override;
    CKBOOL IsChannelLit(int Index) override;
    CKDWORD GetChannelFlags(int Index) override;
    void SetChannelFlags(int Index, CKDWORD Flags) override;
    CKMaterial *GetChannelMaterial(int Index) override;
    VXBLEND_MODE GetChannelSourceBlend(int Index) override;
    VXBLEND_MODE GetChannelDestBlend(int Index) override;
    void SetChannelMaterial(int Index, CKMaterial *Mat) override;
    void SetChannelSourceBlend(int Index, VXBLEND_MODE BlendMode) override;
    void SetChannelDestBlend(int Index, VXBLEND_MODE BlendMode) override;

    int GetMemoryOccupation() override;

    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPatchMesh *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    struct EdgeTessInfo {
        int BasePatch1;
        int BasePatch2;
        CKBOOL Hard;
    };

    CKBOOL IsEdgeHard(int edgeIndex) const;
    int GetPatchCornerForVertex(const CKPatch &patch, int vertexIndex) const;
    void *GetTextureChannelPtr(int textureChannel, CKDWORD *strideOut) const;
    void WriteTextureCoordinate(void *base, CKDWORD stride, int vertexIndex, float u, float v) const;
    CKBOOL GetCornerTextureCoordinate(int textureChannel, int patchIndex, int cornerIndex, float &outU, float &outV) const;
    CKBOOL DoPatchesShareUVOnEdge(int edgeIndex) const;
    void EnsureCornerVertexMapAllocated(int patchCount);
    int ComputeQuadVertexIndex(int patchIndex, int steps, int i, int j, const XArray<int> &interiorBase, const XArray<EdgeTessInfo> &edgeTess) const;
    int ComputeTriVertexIndex(int patchIndex, int steps, int uSteps, int vSteps, const XArray<int> &interiorBase, const XArray<EdgeTessInfo> &edgeTess) const;
    int TriInteriorOffset(int steps, int row, int col) const;

    // Helper methods for tessellation
    void EvaluateTriPatch(CKPatch *patch, float u, float v, float w, VxVector *result);
    void EvaluateQuadPatch(CKPatch *patch, float u, float v, VxVector *result);

    //=========================================================================
    // Patch vertex/vector data (Offset 0x104)
    //=========================================================================
    
    /**
     * @brief Combined vertex and vector array
     * Layout: [vertices (m_VertCount)] [vectors (m_VecCount)]
     * Vertices are corner points, vectors are control points for Bezier curves
     */
    VxVector *m_Verts;                         // 0x104
    
    /**
     * @brief Pointer to vector portion of m_Verts array
     * Points to &m_Verts[m_VertCount], nullptr if no vectors
     */
    VxVector *m_Vecs;                          // 0x108
    
    //=========================================================================
    // Counts (Offset 0x10C)
    //=========================================================================
    
    int m_VertCount;                       // 0x10C - Number of patch vertices (corner points)
    int m_VecCount;                        // 0x110 - Number of vectors (control points)

    //=========================================================================
    // Patch structure arrays (Offset 0x114)
    //=========================================================================
    
    XArray<CKPatchEdge> m_PatchEdges;          // 0x114 - Edge connectivity data (12 bytes)
    XClassArray<CKPatch> m_Patches;            // 0x120 - Patch definitions (12 bytes)
    XClassArray<CKPatchChannel> m_TexturePatches; // 0x12C - Texture channel data (12 bytes)
    
    //=========================================================================
    // Flags and tessellation state (Offset 0x138)
    //=========================================================================
    
    CKDWORD m_PatchFlags;                      // 0x138 - CK_PATCHMESH_FLAGS bitmask
    
    /**
     * @brief Active texture channel count
     * Tracks the number of texture channels currently in use.
     * Set to 1 in constructor and Clear().
     */
    CKDWORD m_TextureChannelCount;             // 0x13C
    
    /**
     * @brief Subdivision iteration count for tessellation
     * 0 = no subdivision (patches become 1-2 triangles)
     * Higher values = smoother curves with more triangles
     */
    int m_IterationCount;                      // 0x140
    
    //=========================================================================
    // Tessellation working data (Offset 0x144)
    // These fields are used during BuildRenderMesh() tessellation
    //=========================================================================
    
    CKDWORD m_TessVertexBase;                  // 0x144 - Base vertex index for current tessellation
    CKDWORD m_TessFaceBase;                    // 0x148 - Base face index for current tessellation
    CKDWORD m_TessEdgeVertexCount;             // 0x14C - Vertices per edge in tessellation
    CKDWORD m_TessInteriorVertexCount;         // 0x150 - Interior vertices per patch
    CKDWORD m_TessTotalVertices;               // 0x154 - Total vertices after tessellation
    
    /**
     * @brief Working vectors for tessellation calculations
     * Used for temporary storage during patch evaluation
     */
    VxVector m_TessWorkVectors[3];             // 0x158 (36 bytes)
    
    CKDWORD m_TessWorkData0;                   // 0x17C - Tessellation working data
    CKDWORD m_TessWorkData1;                   // 0x180 - Tessellation working data
    CKDWORD m_TessWorkData2;                   // 0x184 - Tessellation working data
    
    //=========================================================================
    // Runtime state (Offset 0x188)
    //=========================================================================
    
    /**
     * @brief Flag indicating patch data has changed
     * When TRUE, BuildRenderMesh() will regenerate the mesh.
     * Set by methods that modify patch topology or geometry.
     */
    CKBOOL m_PatchChanged;                     // 0x188
    
    /**
     * @brief Vertex index mapping for patch corners
     * Array of size 4 * patchCount, maps each patch corner to
     * its corresponding vertex in the tessellated mesh.
     * Used during tessellation to share vertices between patches.
     */
    CKDWORD *m_CornerVertexMap;                // 0x18C
    
    /**
     * @brief Source vertex indices for shared smooth vertices
     * Each entry maps a shared vertex to its source vertex
     * for normal sharing across patch boundaries.
     */
    XVoidArray m_SharedVertexSources;          // 0x190 (12 bytes)
    
    /**
     * @brief Bit flags for smooth edges
     * Marks edges that should have smooth normal interpolation.
     * Used during normal generation in BuildRenderMesh().
     */
    XSArray<CKBYTE> m_SmoothEdgeFlags;         // 0x19C (8 bytes)
    
    /**
     * @brief Bit flags for hard edges
     * Marks edges that should have distinct normals (hard edge).
     * Used during normal generation in BuildRenderMesh().
     */
    XSArray<CKBYTE> m_HardEdgeFlags;           // 0x1A4 (8 bytes)
    
    // Total size: 0x1AC (428 bytes)
};

#endif // RCKPATCHMESH_H
