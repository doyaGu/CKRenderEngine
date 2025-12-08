#include "RCKMesh.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "VxMath.h"
#include "CKContext.h"
#include "CKMaterial.h"
#include "RCKMaterial.h"
#include "CKRenderEngineTypes.h"
#include "CKScene.h"
#include "CKRenderManager.h"
#include "RCKRenderManager.h"
#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"
#include <algorithm>
#include <cmath>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MESH_DEBUG_LOG(msg) CK_LOG("Mesh", msg)
#define MESH_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("Mesh", fmt, __VA_ARGS__)

/**
 * @brief Progressive mesh pre-render callback for LOD processing.
 *
 * Called before rendering to update progressive mesh level based on distance/criteria.
 */
static int SnapPMVertexCount(int target, int maxVertices, int morphStep) {
    int clamped = target;
    if (clamped < 3)
        clamped = 3;
    if (clamped > maxVertices)
        clamped = maxVertices;

    if (morphStep <= 0) {
        int level = maxVertices;
        while ((level / 2) >= 3 && clamped <= (level / 2)) {
            level /= 2;
        }
        return level;
    }

    int step = morphStep;
    int offset = (maxVertices - clamped + step - 1) / step;
    int snapped = maxVertices - offset * step;
    if (snapped < 3)
        snapped = 3;
    return snapped;
}

static void FilterTriangleList(const CKWORD *indices, int indexCount, int vertexLimit, XArray<CKWORD> &out) {
    out.Clear();
    if (!indices || indexCount <= 0 || vertexLimit <= 0)
        return;

    out.Reserve(indexCount);
    for (int i = 0; i + 2 < indexCount; i += 3) {
        CKWORD a = indices[i];
        CKWORD b = indices[i + 1];
        CKWORD c = indices[i + 2];
        if (a < vertexLimit && b < vertexLimit && c < vertexLimit) {
            out.PushBack(a);
            out.PushBack(b);
            out.PushBack(c);
        }
    }
}

static void FilterLineList(const CKWORD *indices, int indexCount, int vertexLimit, XArray<CKWORD> &out) {
    out.Clear();
    if (!indices || indexCount <= 0 || vertexLimit <= 0)
        return;

    out.Reserve(indexCount);
    for (int i = 0; i + 1 < indexCount; i += 2) {
        CKWORD a = indices[i];
        CKWORD b = indices[i + 1];
        if (a < vertexLimit && b < vertexLimit) {
            out.PushBack(a);
            out.PushBack(b);
        }
    }
}

static int ClampPMVertexCount(RCKMesh *mesh, int target) {
    if (!mesh)
        return 0;
    int maxVertices = mesh->GetVertexCount();
    int morphStep = mesh->GetPMGeoMorphStep();
    return SnapPMVertexCount(target, maxVertices, morphStep);
}

void ProgressiveMeshPreRenderCallback(CKRenderContext *ctx, CK3dEntity *entity, CKMesh *meshObj, void *data) {
    RCKMesh *mesh = (RCKMesh *) (meshObj ? meshObj : data);
    if (!mesh || !mesh->IsPM())
        return;

    int maxVertices = mesh->GetVertexCount();
    int current = mesh->GetVerticesRendered();
    if (current <= 0 || current > maxVertices)
        current = maxVertices;

    if (!mesh->IsPMGeoMorphEnabled()) {
        mesh->SetVerticesRendered(ClampPMVertexCount(mesh, current));
        return;
    }

    float radius = mesh->GetRadius();
    float distance = 0.0f;
    if (ctx && entity) {
        CK3dEntity *view = ctx->GetViewpoint();
        if (view) {
            VxVector eyePos;
            VxVector objPos;
            view->GetPosition(&eyePos, nullptr);
            entity->GetPosition(&objPos, nullptr);
            float dx = eyePos.x - objPos.x;
            float dy = eyePos.y - objPos.y;
            float dz = eyePos.z - objPos.z;
            distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }

    int minVertices = (std::max)(3, maxVertices / 8);
    if (radius > 0.0f && distance > 0.0f) {
        float start = radius * 12.0f;
        float end = radius * 72.0f;
        if (distance <= start) {
            current = maxVertices;
        } else if (distance >= end) {
            current = minVertices;
        } else {
            float t = (distance - start) / (end - start);
            float lerp = maxVertices - t * (float) (maxVertices - minVertices);
            current = (int) lerp;
        }
    }

    mesh->SetVerticesRendered(ClampPMVertexCount(mesh, current));
}

// Constructor
RCKMesh::RCKMesh(CKContext *Context, CKSTRING name) : CKMesh(Context, name) {
    // Initialize member variables
    m_BaryCenter.Set(0.0f, 0.0f, 0.0f);
    m_LocalBox.Min.Set(0.0f, 0.0f, 0.0f);
    m_LocalBox.Max.Set(0.0f, 0.0f, 0.0f);
    m_Faces.Clear();
    m_FaceVertexIndices.Clear();
    m_LineIndices.Clear();
    m_Vertices.Clear();
    m_VertexColors.Clear();
    m_MaterialChannels.Clear();
    field_D0.Clear();
    m_MaterialGroups.Clear();

    m_ObjectFlags = 64;
    m_VertexWeights = nullptr;
    m_Flags = 10;
    m_Radius = 0.0f;
    m_ProgressiveMesh = nullptr;

    m_MaterialGroups.Reserve(2);
    // CreateNewMaterialGroup(0); // Commented out - method doesn't exist yet

    // Create vertex and index buffers
    RCKRenderManager *renderManager = (RCKRenderManager *) Context->GetRenderManager();
    // m_VertexBuffer = renderManager->CreateObjectIndex(CKRST_OBJ_VERTEXBUFFER);
    // m_IndexBuffer = renderManager->CreateObjectIndex(CKRST_OBJ_INDEXBUFFER);
    m_VertexBuffer = 0;
    m_IndexBuffer = 0;

    memset(&m_LocalBox, 0, sizeof(m_LocalBox));
    // m_RenderCallbacks = nullptr; // Field doesn't exist in header
    m_SubMeshCallbacks = nullptr;
    m_FaceChannelMask = 0;
    m_Valid = 0;
    field_EC = 0;
}

// Destructor
RCKMesh::~RCKMesh() {
    // Clean up progressive mesh
    if (m_ProgressiveMesh) {
        DestroyPM();
    }

    // Clean up callbacks
    // if (m_RenderCallbacks) {
    //     delete m_RenderCallbacks;
    // }
    // if (m_SubMeshCallbacks) {
    //     delete m_SubMeshCallbacks;
    // }

    // Clean up material channels
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        CKMaterialChannel &channel = m_MaterialChannels[i];
        if (channel.m_uv != nullptr) {
            delete[] channel.m_uv;
            channel.m_uv = nullptr;
        }
    }
}

// Get class ID
CK_CLASSID RCKMesh::GetClassID() {
    return CKCID_MESH;
}

// Show/Hide mesh
void RCKMesh::Show(CK_OBJECT_SHOWOPTION show) {
    CKMesh::Show(show);
}

// Transparency methods
CKBOOL RCKMesh::IsTransparent() {
    return (m_Flags & 0x02) != 0;
}

void RCKMesh::SetTransparent(CKBOOL Transparency) {
    if (Transparency) {
        m_Flags |= 0x02;
    } else {
        m_Flags &= ~0x02;
    }
}

// Wrap mode methods
void RCKMesh::SetWrapMode(VXTEXTURE_WRAPMODE Mode) {
    m_Flags = (m_Flags & ~0x18) | (Mode & 0x18);
}

VXTEXTURE_WRAPMODE RCKMesh::GetWrapMode() {
    return (VXTEXTURE_WRAPMODE) (m_Flags & 0x18);
}

// Lighting mode methods
void RCKMesh::SetLitMode(VXMESH_LITMODE Mode) {
    m_Flags = (m_Flags & ~0x60) | (Mode << 5);
}

VXMESH_LITMODE RCKMesh::GetLitMode() {
    return (VXMESH_LITMODE) ((m_Flags & 0x60) >> 5);
}

// Flags methods
CKDWORD RCKMesh::GetFlags() {
    return m_Flags;
}

void RCKMesh::SetFlags(CKDWORD Flags) {
    m_Flags = Flags & 0x7FE39A;
}

// Vertex data access methods
void *RCKMesh::GetPositionsPtr(CKDWORD *Stride) {
    *Stride = 32;
    return m_Vertices.Begin();
}

void *RCKMesh::GetNormalsPtr(CKDWORD *Stride) {
    *Stride = 32;
    return m_Vertices.Begin();
}

void *RCKMesh::GetColorsPtr(CKDWORD *Stride) {
    *Stride = 4;
    return m_VertexColors.Begin();
}

void *RCKMesh::GetSpecularColorsPtr(CKDWORD *Stride) {
    *Stride = 4;
    return m_VertexColors.Begin();
}

void *RCKMesh::GetTextureCoordinatesPtr(CKDWORD *Stride, int channel) {
    if (channel < 0 || channel >= m_MaterialChannels.Size()) {
        *Stride = 0;
        return nullptr;
    }

    CKMaterialChannel &matChannel = m_MaterialChannels[channel];
    if (matChannel.m_uv != nullptr) {
        *Stride = 8;
        return matChannel.m_uv;
    }

    *Stride = 0;
    return nullptr;
}

// Vertex manipulation notifications
void RCKMesh::VertexMove() {
    m_Flags &= ~0x01;
    // UpdateBoundingVolumes(1); // Commented out - method doesn't exist yet
}

void RCKMesh::UVChanged() {
    // Mark UV data as changed
}

void RCKMesh::NormalChanged() {
    // Mark normal data as changed
}

void RCKMesh::ColorChanged() {
    // Mark color data as changed
}

// Normal building methods
void RCKMesh::BuildNormals() {
    int vertexCount = m_Vertices.Size();
    if (vertexCount == 0) return;

    // Clear existing normals
    for (int i = 0; i < vertexCount; i++) {
        VxVertex &vertex = m_Vertices[i];
        vertex.m_Normal.x = 0.0f;
        vertex.m_Normal.y = 0.0f;
        vertex.m_Normal.z = 0.0f;
    }

    // Calculate face normals and accumulate
    int faceCount = m_Faces.Size();
    for (int i = 0; i < faceCount; i++) {
        CKFace &face = m_Faces[i];
        // Face structure doesn't have vertex indices - need to use face vertex indices array
        // This is a simplified implementation
    }

    // Normalize all vertex normals
    for (int i = 0; i < vertexCount; i++) {
        // m_Vertices[i].m_Normal.Normalize(); // VxVector doesn't have Normalize method
    }
}

void RCKMesh::BuildFaceNormals() {
    int faceCount = m_Faces.Size();
    for (int i = 0; i < faceCount; i++) {
        CKFace &face = m_Faces[i];
        int vertexCount = m_Vertices.Size();

        // Face structure doesn't have vertex indices - simplified implementation
        face.m_Normal.Set(0.0f, 0.0f, 1.0f);
    }
}

// Vertex count management
CKBOOL RCKMesh::SetVertexCount(int Count) {
    if (Count < 0) {
        Count = 0;
    }

    // Align to 4-byte boundary with 3 extra
    int alignedCount = (Count + 3) & ~0x03;

    int currentCount = m_Vertices.Size();
    if (currentCount == Count) {
        return TRUE;
    }

    // Resize vertex arrays
    m_Vertices.Resize(alignedCount);
    m_VertexColors.Resize(alignedCount);
    m_Vertices.Resize(Count);
    m_VertexColors.Resize(Count);

    // Initialize new vertices
    if (currentCount < Count) {
        // Initialize new vertices to zero
        VxVertex *newVertices = &m_Vertices[currentCount];
        memset(newVertices, 0, sizeof(VxVertex) * (Count - currentCount));

        // Initialize new colors to white with alpha 255
        VxColors *newColors = &m_VertexColors[currentCount];
        for (int i = 0; i < (Count - currentCount); i++) {
            m_VertexColors[i].Color = 0xFFFFFFFF;
        }
    }

    // Update material channels UV data
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        CKMaterialChannel &channel = m_MaterialChannels[i];
        // UV data is stored directly in struct, not as pointer
        if (!(channel.m_Flags & 0x800000)) {
            // UV data allocation would go here if needed
        }
    }

    // Update vertex weights if present
    if (m_VertexWeights) {
        // Resize vertex weights array
        int oldSize = (int) m_VertexWeights->Size();
        if (oldSize < Count) {
            m_VertexWeights->Resize(Count);
            // Initialize new weights to 1.0f
            for (int i = oldSize; i < Count; i++) {
                (*m_VertexWeights)[i] = 1.0f;
            }
        }
    }

    m_Flags &= ~0x01; // Clear vertex data valid flag
    return TRUE;
}

int RCKMesh::GetVertexCount() {
    return m_Vertices.Size();
}

// Vertex position methods
void RCKMesh::SetVertexPosition(int Index, VxVector *Vector) {
    if (Index >= 0 && Index < m_Vertices.Size()) {
        m_Vertices[Index].m_Position = *Vector;
        VertexMove();
    }
}

void RCKMesh::GetVertexPosition(int Index, VxVector *Vector) {
    if (Index >= 0 && Index < m_Vertices.Size()) {
        *Vector = m_Vertices[Index].m_Position;
    }
}

// Vertex normal methods
void RCKMesh::SetVertexNormal(int Index, VxVector *Vector) {
    if (Index >= 0 && Index < m_Vertices.Size()) {
        m_Vertices[Index].m_Normal = *Vector;
        NormalChanged();
    }
}

void RCKMesh::GetVertexNormal(int Index, VxVector *Vector) {
    if (Index >= 0 && Index < m_Vertices.Size()) {
        *Vector = m_Vertices[Index].m_Normal;
    }
}

// Vertex color methods
void RCKMesh::SetVertexColor(int Index, CKDWORD Color) {
    if (Index >= 0 && Index < m_VertexColors.Size()) {
        m_VertexColors[Index].Color = Color;
        ColorChanged();
    }
}

CKDWORD RCKMesh::GetVertexColor(int Index) {
    if (Index >= 0 && Index < m_VertexColors.Size()) {
        return m_VertexColors[Index].Color;
    }
    return 0xFFFFFFFF;
}

// Vertex texture coordinate methods
void RCKMesh::SetVertexTextureCoordinates(int Index, float u, float v, int channel) {
    if (Index >= 0 && Index < m_Vertices.Size() &&
        channel >= 0 && channel < m_MaterialChannels.Size()) {
        CKMaterialChannel &matChannel = m_MaterialChannels[channel];
        if (matChannel.m_uv != nullptr) {
            matChannel.m_uv[Index].x = u;
            matChannel.m_uv[Index].y = v;
            UVChanged();
        }
    }
}

void RCKMesh::GetVertexTextureCoordinates(int Index, float *u, float *v, int channel) {
    *u = 0.0f;
    *v = 0.0f;

    if (Index >= 0 && Index < m_Vertices.Size() &&
        channel >= 0 && channel < m_MaterialChannels.Size()) {
        CKMaterialChannel &matChannel = m_MaterialChannels[channel];
        if (matChannel.m_uv != nullptr) {
            *u = matChannel.m_uv[Index].x;
            *v = matChannel.m_uv[Index].y;
        }
    }
}

// Face count management
CKBOOL RCKMesh::SetFaceCount(int Count) {
    if (Count < 0) {
        Count = 0;
    }

    m_Faces.Resize(Count);
    m_FaceVertexIndices.Resize(Count * 3);

    // Initialize new faces
    int currentCount = m_Faces.Size();
    if (currentCount < Count) {
        for (int i = currentCount; i < Count; i++) {
            CKFace &face = m_Faces[i];
            // Face structure doesn't have vertex indices
            // face.m_VertexIndex[0] = 0;
            // face.m_VertexIndex[1] = 0;
            // face.m_VertexIndex[2] = 0;
            face.m_MatIndex = 0;
            face.m_Normal.Set(0.0f, 0.0f, 1.0f);
        }
    }

    return TRUE;
}

int RCKMesh::GetFaceCount() {
    return m_Faces.Size();
}

// Face vertex index methods
void RCKMesh::SetFaceVertexIndex(int FaceIndex, int Vertex1, int Vertex2, int Vertex3) {
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        CKFace &face = m_Faces[FaceIndex];
        // Face structure doesn't have vertex indices directly
        // They are stored in m_FaceVertexIndices array

        // Update face vertex indices array
        if (FaceIndex * 3 + 2 < m_FaceVertexIndices.Size()) {
            m_FaceVertexIndices[FaceIndex * 3] = Vertex1;
            m_FaceVertexIndices[FaceIndex * 3 + 1] = Vertex2;
            m_FaceVertexIndices[FaceIndex * 3 + 2] = Vertex3;
        }
    }
}

void RCKMesh::GetFaceVertexIndex(int FaceIndex, int &Vertex1, int &Vertex2, int &Vertex3) {
    Vertex1 = 0;
    Vertex2 = 0;
    Vertex3 = 0;

    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        CKFace &face = m_Faces[FaceIndex];
        // Face structure doesn't have vertex indices directly
        // They are stored in m_FaceVertexIndices array
        int baseIndex = FaceIndex * 3;
        Vertex1 = m_FaceVertexIndices[baseIndex];
        Vertex2 = m_FaceVertexIndices[baseIndex + 1];
        Vertex3 = m_FaceVertexIndices[baseIndex + 2];
    }
}

// Face material methods
void RCKMesh::SetFaceMaterial(int FaceIndex, CKMaterial *Mat) {
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        int materialIndex = GetChannelByMaterial(Mat);
        if (materialIndex >= 0) {
            m_Faces[FaceIndex].m_MatIndex = materialIndex;
        }
    }
}

CKMaterial *RCKMesh::GetFaceMaterial(int Index) {
    if (Index >= 0 && Index < m_Faces.Size()) {
        int materialIndex = m_Faces[Index].m_MatIndex;
        if (materialIndex >= 0 && materialIndex < m_MaterialChannels.Size()) {
            return (CKMaterial *) m_MaterialChannels[materialIndex].m_Material;
        }
    }
    return nullptr;
}

// Face indices access
CKWORD *RCKMesh::GetFacesIndices() {
    return m_FaceVertexIndices.Begin();
}

// Geometry calculations
float RCKMesh::GetRadius() {
    return m_Radius;
}

const VxBbox &RCKMesh::GetLocalBox() {
    return m_LocalBox;
}

void RCKMesh::GetBaryCenter(VxVector *Vector) {
    *Vector = m_BaryCenter;
}

// Line operations
CKBOOL RCKMesh::SetLineCount(int Count) {
    if (Count < 0) {
        Count = 0;
    }

    m_LineIndices.Resize(Count * 2);
    return TRUE;
}

int RCKMesh::GetLineCount() {
    return m_LineIndices.Size() / 2;
}

void RCKMesh::SetLine(int LineIndex, int VIndex1, int VIndex2) {
    int index = LineIndex * 2;
    if (index + 1 < m_LineIndices.Size()) {
        m_LineIndices[index] = VIndex1;
        m_LineIndices[index + 1] = VIndex2;
    }
}

void RCKMesh::GetLine(int LineIndex, int *VIndex1, int *VIndex2) {
    if (VIndex1) *VIndex1 = 0;
    if (VIndex2) *VIndex2 = 0;

    int index = LineIndex * 2;
    if (index + 1 < m_LineIndices.Size()) {
        if (VIndex1) *VIndex1 = m_LineIndices[index];
        if (VIndex2) *VIndex2 = m_LineIndices[index + 1];
    }
}

CKWORD *RCKMesh::GetLineIndices() {
    return m_LineIndices.Begin();
}

// Vertex weight system
void RCKMesh::SetVertexWeightsCount(int count) {
    if (count <= 0) {
        if (m_VertexWeights) {
            delete m_VertexWeights;
            m_VertexWeights = nullptr;
        }
        return;
    }

    if (!m_VertexWeights) {
        m_VertexWeights = new XArray<float>();
        m_VertexWeights->Resize(count);
        for (int i = 0; i < count; i++) {
            (*m_VertexWeights)[i] = 1.0f;
        }
    } else {
        // Resize existing weights
        int oldCount = (int) m_VertexWeights->Size();
        m_VertexWeights->Resize(count);

        // Initialize new weights to 1.0f
        for (int i = oldCount; i < count; i++) {
            (*m_VertexWeights)[i] = 1.0f;
        }
    }
}

int RCKMesh::GetVertexWeightsCount() {
    return m_VertexWeights ? (int) m_VertexWeights->Size() : 0;
}

void RCKMesh::SetVertexWeight(int index, float w) {
    if (index >= 0 && index < GetVertexWeightsCount() && m_VertexWeights) {
        (*m_VertexWeights)[index] = w;
    }
}

float RCKMesh::GetVertexWeight(int index) {
    if (index >= 0 && index < GetVertexWeightsCount() && m_VertexWeights) {
        return (*m_VertexWeights)[index];
    }
    return 1.0f;
}

// Mesh operations
void RCKMesh::Clean(CKBOOL KeepVertices) {
    // Implementation would remove unused vertices/faces
    // For now, just update bounding volumes
    // UpdateBoundingVolumes(1); // Commented out - method doesn't exist yet
}

void RCKMesh::InverseWinding() {
    int faceCount = m_Faces.Size();
    for (int i = 0; i < faceCount; i++) {
        CKFace &face = m_Faces[i];
        // Swap second and third vertices
        // Face structure doesn't have vertex indices directly
        // They are stored in m_FaceVertexIndices array
        int baseIndex = i * 3;
        int temp = m_FaceVertexIndices[baseIndex + 1];
        m_FaceVertexIndices[baseIndex + 1] = m_FaceVertexIndices[baseIndex + 2];
        m_FaceVertexIndices[baseIndex + 2] = temp;

        // Update face vertex indices array
        if (baseIndex + 2 < m_FaceVertexIndices.Size()) {
            // Face structure doesn't have vertex indices directly
            // They are stored in m_FaceVertexIndices array
        }
    }
}

void RCKMesh::Consolidate() {
    // Implementation would consolidate duplicate vertices
    // For now, just update flags
    m_Flags &= ~0x01;
}

void RCKMesh::UnOptimize() {
    // Implementation would undo optimization
    // For now, just update flags
    m_Flags &= ~0x01;
}

// Callback system methods
CKBOOL RCKMesh::AddPreRenderCallBack(CK_MESHRENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    if (!m_RenderCallbacks) {
        m_RenderCallbacks = new CKCallbacksContainer();
    }
    return m_RenderCallbacks->AddPreCallback((void *) Function, Argument, Temporary, m_Context->GetRenderManager());
}

CKBOOL RCKMesh::RemovePreRenderCallBack(CK_MESHRENDERCALLBACK Function, void *Argument) {
    if (!m_RenderCallbacks) {
        return FALSE;
    }
    return m_RenderCallbacks->RemovePreCallback((void *) Function, Argument);
}

CKBOOL RCKMesh::AddPostRenderCallBack(CK_MESHRENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    if (!m_RenderCallbacks) {
        m_RenderCallbacks = new CKCallbacksContainer();
    }
    return m_RenderCallbacks->AddPostCallback((void *) Function, Argument, Temporary, m_Context->GetRenderManager());
}

CKBOOL RCKMesh::RemovePostRenderCallBack(CK_MESHRENDERCALLBACK Function, void *Argument) {
    if (!m_RenderCallbacks) {
        return FALSE;
    }
    return m_RenderCallbacks->RemovePostCallback((void *) Function, Argument);
}

void RCKMesh::SetRenderCallBack(CK_MESHRENDERCALLBACK Function, void *Argument) {
    if (!m_RenderCallbacks) {
        m_RenderCallbacks = new CKCallbacksContainer();
    }
    m_RenderCallbacks->Clear();
    if (Function) {
        m_RenderCallbacks->AddPreCallback((void *) Function, Argument, FALSE, m_Context->GetRenderManager());
    }
}

void RCKMesh::SetDefaultRenderCallBack() {
    if (m_RenderCallbacks) {
        m_RenderCallbacks->Clear();
    }
}

void RCKMesh::RemoveAllCallbacks() {
    if (m_RenderCallbacks) {
        m_RenderCallbacks->Clear();
    }
    if (m_SubMeshCallbacks) {
        m_SubMeshCallbacks->Clear();
    }
}

// Sub-mesh callback methods
CKBOOL RCKMesh::AddSubMeshPreRenderCallBack(CK_SUBMESHRENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    if (!m_SubMeshCallbacks) {
        m_SubMeshCallbacks = new CKCallbacksContainer();
    }
    return m_SubMeshCallbacks->AddPreCallback((void *) Function, Argument, Temporary, m_Context->GetRenderManager());
}

CKBOOL RCKMesh::RemoveSubMeshPreRenderCallBack(CK_SUBMESHRENDERCALLBACK Function, void *Argument) {
    if (!m_SubMeshCallbacks) {
        return FALSE;
    }
    return m_SubMeshCallbacks->RemovePreCallback((void *) Function, Argument);
}

CKBOOL RCKMesh::AddSubMeshPostRenderCallBack(CK_SUBMESHRENDERCALLBACK Function, void *Argument, CKBOOL Temporary) {
    if (!m_SubMeshCallbacks) {
        m_SubMeshCallbacks = new CKCallbacksContainer();
    }
    return m_SubMeshCallbacks->AddPostCallback((void *) Function, Argument, Temporary, m_Context->GetRenderManager());
}

CKBOOL RCKMesh::RemoveSubMeshPostRenderCallBack(CK_SUBMESHRENDERCALLBACK Function, void *Argument) {
    if (!m_SubMeshCallbacks) {
        return FALSE;
    }
    return m_SubMeshCallbacks->RemovePostCallback((void *) Function, Argument);
}

// Material management
int RCKMesh::GetMaterialCount() {
    return m_MaterialChannels.Size();
}

CKMaterial *RCKMesh::GetMaterial(int index) {
    if (index >= 0 && index < m_MaterialChannels.Size()) {
        return m_MaterialChannels[index].m_Material;
    }
    return nullptr;
}

// Progressive mesh methods (skipped as requested)
void RCKMesh::SetVerticesRendered(int count) {
    if (!m_ProgressiveMesh) {
        if (CreatePM() != CK_OK)
            return;
    }
    m_ProgressiveMesh->m_Field0 = ClampPMVertexCount(this, count);
}

int RCKMesh::GetVerticesRendered() {
    if (!m_ProgressiveMesh)
        return GetVertexCount();
    if (m_ProgressiveMesh->m_Field0 <= 0)
        m_ProgressiveMesh->m_Field0 = GetVertexCount();
    return ClampPMVertexCount(this, m_ProgressiveMesh->m_Field0);
}

void RCKMesh::EnablePMGeoMorph(CKBOOL enable) {
    if (!m_ProgressiveMesh) {
        if (CreatePM() != CK_OK)
            return;
    }
    m_ProgressiveMesh->m_MorphEnabled = enable ? 1 : 0;
}

CKBOOL RCKMesh::IsPMGeoMorphEnabled() {
    return (m_ProgressiveMesh && m_ProgressiveMesh->m_MorphEnabled) ? TRUE : FALSE;
}

void RCKMesh::SetPMGeoMorphStep(int gs) {
    if (!m_ProgressiveMesh) {
        if (CreatePM() != CK_OK)
            return;
    }
    m_ProgressiveMesh->m_MorphStep = gs;
}

int RCKMesh::GetPMGeoMorphStep() {
    return m_ProgressiveMesh ? m_ProgressiveMesh->m_MorphStep : 0;
}

/**
 * @brief Optimized vertex loading using buffer-based approach.
 *
 * Based on IDA decompilation at 0x10027E1E (RCKMesh::ILoadVertices).
 * Uses LockReadBuffer and VxCopyStructure for efficient bulk memory operations.
 *
 * @param chunk The state chunk to read from
 * @param loadFlags Output parameter for save flags
 * @return 0 on success, -1 on error
 */
int RCKMesh::ILoadVertices(CKStateChunk *chunk, CKDWORD *loadFlags) {
    if (chunk->GetDataVersion() < 9)
        return -1;

    if (!chunk->SeekIdentifier(0x20000))
        return 0;

    int vertexCount = chunk->ReadInt();
    SetVertexCount(vertexCount);

    if (vertexCount == 0)
        return 0;

    // Read save flags
    *loadFlags = chunk->ReadDword();

    // Lock read buffer for direct access
    CKDWORD *buffer = (CKDWORD *) chunk->LockReadBuffer();
    CKDWORD *ptr = buffer + 1;

    // Get buffer size and convert endianness
    CKDWORD bufferSize = CKConvertEndian32(*buffer);
    CKConvertEndianArray32(buffer, bufferSize);

    // Read positions (if not skipped via flag 0x10)
    if (!(*loadFlags & 0x10)) {
        VxCopyStructure(vertexCount, m_Vertices.Begin(), 0x20, 0x0C, ptr, 0x0C);
        ptr += 3 * vertexCount;
    }

    // Read diffuse colors
    if (*loadFlags & 0x01) {
        // Only first value - fill all with same color
        m_VertexColors[0].Color = *ptr++;
        VxFillStructure(vertexCount - 1, &m_VertexColors[1].Color, 8, 4, &m_VertexColors[0].Color);
    } else {
        // All colors differ
        VxCopyStructure(vertexCount, &m_VertexColors[0].Color, 8, 4, ptr, 4);
        ptr += vertexCount;
    }

    // Read specular colors
    if (*loadFlags & 0x02) {
        // Only first value - fill all with same color
        m_VertexColors[0].Specular = *ptr++;
        VxFillStructure(vertexCount - 1, &m_VertexColors[1].Specular, 8, 4, &m_VertexColors[0].Specular);
    } else {
        // All specular colors differ
        VxCopyStructure(vertexCount, &m_VertexColors[0].Specular, 8, 4, ptr, 4);
        ptr += vertexCount;
    }

    // Read normals (if not skipped via flag 0x04)
    if (!(*loadFlags & 0x04)) {
        // Copy normals to m_Vertices[i].m_Normal (offset 12 from start of VxVertex)
        char *normalDst = (char *) m_Vertices.Begin() + 12;
        VxCopyStructure(vertexCount, normalDst, 0x20, 0x0C, ptr, 0x0C);
        ptr += 3 * vertexCount;
    }

    // Read UV coordinates
    float *fptr = (float *) ptr;
    if (*loadFlags & 0x08) {
        // Only first value - fill all with same UV
        m_Vertices[0].m_UV.x = *fptr++;
        m_Vertices[0].m_UV.y = *fptr++;
        VxFillStructure(vertexCount - 1, &m_Vertices[1].m_UV, 0x20, 8, &m_Vertices[0].m_UV);
    } else {
        // All UVs differ - copy all
        VxCopyStructure(vertexCount, &m_Vertices[0].m_UV, 0x20, 8, fptr, 8);
    }

    // Restore endianness for buffer and return
    CKConvertEndianArray32(buffer, bufferSize);

    return 0;
}

// CKObject overrides
void RCKMesh::PreSave(CKFile *file, CKDWORD flags) {
    CKBeObject::PreSave(file, flags);
}

CKStateChunk *RCKMesh::Save(CKFile *file, CKDWORD flags) {
    // Call base class save
    CKStateChunk *baseChunk = CKBeObject::Save(file, flags);

    // Early return check - based on IDA at 0x100273bb
    if (!file && (flags & 0xFFF000) == 0)
        return baseChunk;

    // Create mesh-specific chunk
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_MESH, file);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Write mesh flags (chunk 0x2000) - always written
    chunk->WriteIdentifier(0x2000);
    chunk->WriteDword(m_Flags);

    // Only write mesh geometry if not a patch mesh
    if (GetClassID() != CKCID_PATCHMESH) {
        // Write material groups (chunk 0x100000)
        int groupCount = (int) m_MaterialGroups.Size();
        if (groupCount > 0) {
            chunk->WriteIdentifier(0x100000);
            chunk->WriteInt(groupCount);
            for (int i = 0; i < groupCount; i++) {
                CKMaterialGroup *group = m_MaterialGroups[i];
                chunk->WriteObject(group ? group->m_Material : nullptr);
                chunk->WriteInt(0); // Reserved
            }
        }

        // Write face data (chunk 0x10000)
        int faceCount = GetFaceCount();
        if (faceCount > 0) {
            chunk->WriteIdentifier(0x10000);
            chunk->WriteInt(faceCount);
            for (int j = 0; j < faceCount; j++) {
                // Pack vertex indices: first two as DWORD
                WORD idx0 = m_FaceVertexIndices[3 * j];
                WORD idx1 = m_FaceVertexIndices[3 * j + 1];
                CKDWORD indices01 = idx0 | (idx1 << 16);
                chunk->WriteDwordAsWords(indices01);

                // Third index and material index
                WORD idx2 = m_FaceVertexIndices[3 * j + 2];
                WORD matIdx = (WORD) m_Faces[j].m_MatIndex;
                CKDWORD idx2AndMat = idx2 | (matIdx << 16);
                chunk->WriteDwordAsWords(idx2AndMat);
            }
        }

        // Write line data (chunk 0x40000) using bulk write
        int lineCount = GetLineCount();
        if (lineCount > 0) {
            chunk->WriteIdentifier(0x40000);
            chunk->WriteInt(lineCount);
            // Bulk write line indices - based on IDA at 0x100275b5
            chunk->WriteBuffer_LEndian16(lineCount * 2 * sizeof(WORD), m_LineIndices.Begin());
        }

        // Write vertex data (chunk 0x20000) using optimized buffer write
        int vertexCount = GetVertexCount();
        if (vertexCount > 0) {
            chunk->WriteIdentifier(0x20000);
            CKDWORD saveFlags = GetSaveFlags();
            chunk->WriteInt(vertexCount);
            chunk->WriteDword(saveFlags);

            // Lock write buffer for optimized vertex data - 11 dwords max per vertex
            // Based on IDA at 0x10027632
            CKDWORD *buffer = (CKDWORD *) chunk->LockWriteBuffer(11 * vertexCount);
            float *ptr = (float *) (buffer + 1);

            // Write positions (skip if flag 0x10 set)
            if (!(saveFlags & 0x10)) {
                VxVertex *vtx = m_Vertices.Begin();
                for (int k = 0; k < vertexCount; k++) {
                    *ptr++ = vtx->m_Position.x;
                    *ptr++ = vtx->m_Position.y;
                    *ptr++ = vtx->m_Position.z;
                    vtx++;
                }
            }

            // Write diffuse colors - first value always, rest if flag 0x1 not set
            CKDWORD *dptr = (CKDWORD *) ptr;
            *dptr++ = m_VertexColors[0].Color;
            if (!(saveFlags & 0x01)) {
                for (int k = 1; k < vertexCount; k++)
                    *dptr++ = m_VertexColors[k].Color;
            }

            // Write specular colors - first value always, rest if flag 0x2 not set
            *dptr++ = m_VertexColors[0].Specular;
            if (!(saveFlags & 0x02)) {
                for (int k = 1; k < vertexCount; k++)
                    *dptr++ = m_VertexColors[k].Specular;
            }

            // Write normals (skip if flag 0x4 set)
            if (!(saveFlags & 0x04)) {
                ptr = (float *) dptr;
                VxVertex *vtx = m_Vertices.Begin();
                for (int k = 0; k < vertexCount; k++) {
                    *ptr++ = vtx->m_Normal.x;
                    *ptr++ = vtx->m_Normal.y;
                    *ptr++ = vtx->m_Normal.z;
                    vtx++;
                }
                dptr = (CKDWORD *) ptr;
            }

            // Write UV coordinates - first value always, rest if flag 0x8 not set
            ptr = (float *) dptr;
            *ptr++ = m_Vertices[0].m_UV.x;
            *ptr++ = m_Vertices[0].m_UV.y;
            if (!(saveFlags & 0x08)) {
                for (int k = 1; k < vertexCount; k++) {
                    *ptr++ = m_Vertices[k].m_UV.x;
                    *ptr++ = m_Vertices[k].m_UV.y;
                }
            }

            // Calculate written size and finalize buffer
            int written = (int) (((char *) ptr - (char *) buffer) >> 2);
            *buffer = (CKDWORD) written;
            CKConvertEndianArray32(buffer, written);
            chunk->Skip(written);
        }

        // Write material channels (chunk 0x4000)
        int channelCount = GetChannelCount();
        if (channelCount > 0) {
            chunk->WriteIdentifier(0x4000);
            chunk->WriteInt(channelCount);
            for (int ch = 0; ch < channelCount; ch++) {
                CKMaterialChannel &channel = m_MaterialChannels[ch];
                chunk->WriteObject(channel.m_Material);
                chunk->WriteDword(channel.m_Flags);
                chunk->WriteDword((CKDWORD) channel.m_SourceBlend);
                chunk->WriteDword((CKDWORD) channel.m_DestBlend);

                int uvCount = channel.m_uv ? GetVertexCount() : 0;
                chunk->WriteInt(uvCount);
                for (int uv = 0; uv < uvCount; uv++) {
                    chunk->WriteFloat(channel.m_uv[uv].x);
                    chunk->WriteFloat(channel.m_uv[uv].y);
                }
            }
        }
    }

    // Write vertex weights (chunk 0x80000)
    if (m_VertexWeights && m_VertexWeights->Size() > 0) {
        chunk->WriteIdentifier(0x80000);

        // Check if all weights are equal (optimization)
        float *begin = (float *) m_VertexWeights->Begin();
        float *end = (float *) m_VertexWeights->End();
        float *p = begin;
        for (; p < end && *p == *begin; ++p);

        int weightCount = (int) m_VertexWeights->Size();
        chunk->WriteInt(weightCount);

        if (p != end) {
            // Weights differ - write buffer
            chunk->WriteBuffer_LEndian(weightCount * sizeof(float), begin);
        }
        chunk->WriteFloat(*begin); // Always write first weight
    }

    // Write face channel masks if not all 0xFFFF (chunk 0x8000)
    WORD maskAnd = 0xFFFF;
    for (int i = 0; i < GetFaceCount(); i++)
        maskAnd &= m_Faces[i].m_ChannelMask;

    if (maskAnd != 0xFFFF && m_MaterialChannels.Size() > 0) {
        chunk->WriteIdentifier(0x8000);
        int faceCount = GetFaceCount();
        chunk->WriteInt(faceCount);

        // Pack two channel masks per DWORD
        for (int i = 0; i < faceCount / 2; i++) {
            CKDWORD packed = m_Faces[2 * i].m_ChannelMask |
                ((CKDWORD) m_Faces[2 * i + 1].m_ChannelMask << 16);
            chunk->WriteDwordAsWords(packed);
        }
        // Handle odd face count
        if (faceCount & 1)
            chunk->WriteWord(m_Faces[faceCount - 1].m_ChannelMask);
    }

    // Write progressive mesh data (chunk 0x800000)
    if (m_ProgressiveMesh && m_ProgressiveMesh->m_Data.Size() > 0) {
        chunk->WriteIdentifier(0x800000);
        chunk->WriteInt(m_ProgressiveMesh->m_Field0);
        chunk->WriteInt(m_ProgressiveMesh->m_MorphEnabled);
        chunk->WriteInt(m_ProgressiveMesh->m_MorphStep);
        int dataSize = (int) m_ProgressiveMesh->m_Data.Size();
        chunk->WriteBufferNoSize_LEndian(dataSize * sizeof(CKDWORD), m_ProgressiveMesh->m_Data.Begin());
    }

    // Close the chunk
    if (GetClassID() == CKCID_MESH)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

CKERROR RCKMesh::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    MESH_DEBUG_LOG_FMT("Load: Starting for mesh %s", GetName() ? GetName() : "(null)");

    // Call base class load
    CKBeObject::Load(chunk, file);

    // Clear existing channels if not loading from file
    if (!file) {
        while (GetChannelCount() > 0)
            RemoveChannel(0);
    }

    // Get data version for format handling
    int dataVersion = chunk->GetDataVersion();
    CKDWORD loadFlags = 0;

    // Read mesh flags (chunk 0x2000)
    if (chunk->SeekIdentifier(0x2000)) {
        CKDWORD flags = chunk->ReadDword();
        SetFlags(flags & 0x7FE39A);
    }

    // Temporary material group index mapping
    XArray<int> groupIndices;
    groupIndices.Resize(0);

    if (dataVersion >= 9) {
        // New format (version 9+)
        if (GetClassID() != CKCID_PATCHMESH) {
            // Load material groups (chunk 0x100000)
            if (chunk->SeekIdentifier(0x100000)) {
                int groupCount = chunk->ReadInt();

                // Clear existing material groups
                for (int i = 0; i < (int) m_MaterialGroups.Size(); i++) {
                    delete m_MaterialGroups[i];
                }
                m_MaterialGroups.Clear();

                groupIndices.Resize(groupCount);

                for (int i = 0; i < groupCount; i++) {
                    CKMaterial *mat = (CKMaterial *) chunk->ReadObject(m_Context);
                    if (i > 0 && !mat) {
                        // Null material after first - use group 0
                        groupIndices[i] = 0;
                    } else {
                        CKMaterialGroup *group = new CKMaterialGroup();
                        group->m_Material = (RCKMaterial *) mat;
                        groupIndices[i] = (int) m_MaterialGroups.Size();
                        m_MaterialGroups.PushBack(group);
                    }
                    chunk->ReadInt(); // Reserved
                }
            }

            // Load vertex data using optimized buffer read
            ILoadVertices(chunk, &loadFlags);

            // Load face data (chunk 0x10000)
            if (chunk->SeekIdentifier(0x10000)) {
                int faceCount = chunk->ReadInt();
                if (faceCount > 0) {
                    SetFaceCount(faceCount);
                    for (int j = 0; j < faceCount; j++) {
                        // Read packed indices
                        CKDWORD indices01 = chunk->ReadDwordAsWords();
                        m_FaceVertexIndices[3 * j] = (WORD) (indices01 & 0xFFFF);
                        m_FaceVertexIndices[3 * j + 1] = (WORD) (indices01 >> 16);

                        CKDWORD idx2AndMat = chunk->ReadDwordAsWords();
                        m_FaceVertexIndices[3 * j + 2] = (WORD) (idx2AndMat & 0xFFFF);

                        // Map material index through group indices if available
                        int matIdx = (idx2AndMat >> 16);
                        if (groupIndices.Size() > 0 && matIdx < (int) groupIndices.Size())
                            m_Faces[j].m_MatIndex = groupIndices[matIdx];
                        else
                            m_Faces[j].m_MatIndex = matIdx;
                    }
                }
            }

            // Load line data using bulk read (chunk 0x40000)
            if (chunk->SeekIdentifier(0x40000)) {
                int lineCount = chunk->ReadInt();
                SetLineCount(lineCount);
                chunk->ReadAndFillBuffer_LEndian16(m_LineIndices.Begin());
            }

            // Rebuild geometry
            UnOptimize();
            if (loadFlags & 0x04)
                BuildNormals();
            else
                BuildFaceNormals();
        }
    } else {
        // Legacy format (version < 9) - use old loader
        if (GetClassID() != CKCID_PATCHMESH) {
            // Call legacy load function (sub_1007C340 in IDA)
            // For now, fall back to simple element-wise reading
            if (chunk->SeekIdentifier(0x20000)) {
                int vertexCount = chunk->ReadInt();
                CKDWORD saveFlags = chunk->ReadDword();

                if (vertexCount > 0) {
                    SetVertexCount(vertexCount);

                    // Read vertex positions
                    for (int i = 0; i < vertexCount; i++) {
                        VxVector pos;
                        pos.x = chunk->ReadFloat();
                        pos.y = chunk->ReadFloat();
                        pos.z = chunk->ReadFloat();
                        SetVertexPosition(i, &pos);
                    }

                    // Read vertex normals if present
                    if (saveFlags & 0x02) {
                        for (int i = 0; i < vertexCount; i++) {
                            VxVector normal;
                            normal.x = chunk->ReadFloat();
                            normal.y = chunk->ReadFloat();
                            normal.z = chunk->ReadFloat();
                            SetVertexNormal(i, &normal);
                        }
                    }

                    // Read vertex colors if present
                    if (saveFlags & 0x04) {
                        for (int i = 0; i < vertexCount; i++) {
                            CKDWORD color = chunk->ReadDword();
                            SetVertexColor(i, color);
                        }
                    }
                }
            }

            // Load face data (old format)
            if (chunk->SeekIdentifier(0x10000)) {
                int faceCount = chunk->ReadInt();
                if (faceCount > 0) {
                    SetFaceCount(faceCount);
                    for (int i = 0; i < faceCount; i++) {
                        int v1 = chunk->ReadWord();
                        int v2 = chunk->ReadWord();
                        int v3 = chunk->ReadWord();
                        int matIdx = chunk->ReadDword();
                        SetFaceVertexIndex(i, v1, v2, v3);
                        m_Faces[i].m_MatIndex = matIdx;
                    }
                }
            }

            // Load line data (old format)
            if (chunk->SeekIdentifier(0x40000)) {
                int lineCount = chunk->ReadInt();
                SetLineCount(lineCount);
                for (int i = 0; i < lineCount; i++) {
                    int v1 = chunk->ReadWord();
                    int v2 = chunk->ReadWord();
                    SetLine(i, v1, v2);
                }
            }
        }
    }

    // Load material channels (chunk 0x4000) - applies to all versions
    if (GetClassID() != CKCID_PATCHMESH && chunk->SeekIdentifier(0x4000)) {
        int channelCount = chunk->ReadInt();
        for (int k = 0; k < channelCount; k++) {
            CKMaterial *material = (CKMaterial *) chunk->ReadObject(m_Context);
            int channelIndex = AddChannel(material, 1);

            if (channelIndex < 0) {
                // Skip channel data if channel wasn't added
                chunk->ReadDword(); // flags
                chunk->ReadDword(); // source blend
                chunk->ReadDword(); // dest blend
                int uvCount = chunk->ReadInt();
                for (int m = 0; m < uvCount; m++) {
                    chunk->ReadFloat();
                    chunk->ReadFloat();
                }
            } else {
                m_MaterialChannels[channelIndex].m_Flags = chunk->ReadDword();
                m_MaterialChannels[channelIndex].m_SourceBlend = (VXBLEND_MODE) chunk->ReadDword();
                m_MaterialChannels[channelIndex].m_DestBlend = (VXBLEND_MODE) chunk->ReadDword();

                int uvCount = chunk->ReadInt();
                if (uvCount > 0) {
                    CKDWORD stride;
                    float *uvPtr = (float *) GetTextureCoordinatesPtr(&stride, channelIndex);
                    for (int m = 0; m < uvCount; m++) {
                        uvPtr[0] = chunk->ReadFloat();
                        uvPtr[1] = chunk->ReadFloat();
                        uvPtr = (float *) ((char *) uvPtr + stride);
                    }
                }
            }
        }
    }

    // Load vertex weights (chunk 0x80000)
    int weightSize = chunk->SeekIdentifierAndReturnSize(0x80000);
    if (weightSize > 0) {
        int weightCount = chunk->ReadInt();
        if (weightSize > 8) {
            // Full weight array
            SetVertexWeightsCount(weightCount);
            if (m_VertexWeights)
                chunk->ReadAndFillBuffer_LEndian(m_VertexWeights->Begin());
        } else {
            // Constant weight for all vertices
            SetVertexWeightsCount(weightCount);
            float weight = chunk->ReadFloat();
            float *begin = (float *) m_VertexWeights->Begin();
            float *end = (float *) m_VertexWeights->End();
            for (float *p = begin; p < end; ++p)
                *p = weight;
        }
    }

    // Load face channel masks (chunk 0x8000)
    if (chunk->SeekIdentifier(0x8000)) {
        int maskCount = chunk->ReadInt();
        int facesToProcess = maskCount;
        int faceCount = GetFaceCount();
        if (faceCount < maskCount)
            facesToProcess = faceCount & ~1; // Round down to even

        for (int i = 0; i < facesToProcess / 2; i++) {
            CKDWORD packed = chunk->ReadDwordAsWords();
            m_Faces[2 * i].m_ChannelMask = (WORD) (packed & 0xFFFF);
            m_Faces[2 * i + 1].m_ChannelMask = (WORD) (packed >> 16);
        }
        if (maskCount & 1 && faceCount >= maskCount)
            m_Faces[maskCount - 1].m_ChannelMask = chunk->ReadWord();
    }

    // Load progressive mesh data (chunk 0x800000)
    int pmSize = chunk->SeekIdentifierAndReturnSize(0x800000);
    if (pmSize == -1) {
        DestroyPM();
    } else {
        if (!m_ProgressiveMesh)
            m_ProgressiveMesh = new CKProgressiveMesh();

        m_ProgressiveMesh->m_Field0 = chunk->ReadInt();
        m_ProgressiveMesh->m_MorphEnabled = chunk->ReadInt();
        m_ProgressiveMesh->m_MorphStep = chunk->ReadInt();
        pmSize -= 12;

        m_ProgressiveMesh->m_Data.Resize(pmSize / sizeof(CKDWORD));
        chunk->ReadAndFillBuffer_LEndian(pmSize, m_ProgressiveMesh->m_Data.Begin());

        AddPreRenderCallBack((CK_MESHRENDERCALLBACK) ProgressiveMeshPreRenderCallback, this, FALSE);
    }

    return CK_OK;
}

void RCKMesh::CheckPreDeletion() {
    // Clean up before deletion
    RemoveAllCallbacks();

    if (m_ProgressiveMesh) {
        DestroyPM();
    }
}

int RCKMesh::GetMemoryOccupation() {
    int size = sizeof(RCKMesh);
    size += m_Vertices.Size() * sizeof(VxVertex);
    size += m_VertexColors.Size() * sizeof(CKDWORD);
    size += m_Faces.Size() * sizeof(CKFace);
    size += m_FaceVertexIndices.Size() * sizeof(CKWORD);
    size += m_LineIndices.Size() * sizeof(CKWORD);
    size += m_MaterialChannels.Size() * sizeof(CKMaterialChannel);

    // Add UV data size
    for (int i = 0; i < m_MaterialChannels.Size(); ++i) {
        if (m_MaterialChannels[i].m_uv) {
            size += GetVertexCount() * sizeof(Vx2DVector);
        }
    }

    // Add vertex weights size
    if (m_VertexWeights) {
        size += GetVertexWeightsCount() * sizeof(float);
    }

    return size;
}

int RCKMesh::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    // Check if object is used by this mesh
    if (cid == CKCID_MATERIAL) {
        for (int i = 0; i < m_MaterialChannels.Size(); ++i) {
            if (m_MaterialChannels[i].m_Material == (CKMaterial *) o) {
                return 1;
            }
        }
    }
    return 0;
}

// Dependencies Functions
CKERROR RCKMesh::PrepareDependencies(CKDependenciesContext &context) {
    // Add material dependencies
    for (int i = 0; i < m_MaterialChannels.Size(); ++i) {
        if (m_MaterialChannels[i].m_Material) {
            CK_ID id = m_MaterialChannels[i].m_Material->GetID();
            context.AddObjects(&id, 1);
        }
    }
    return CK_OK;
}

CKERROR RCKMesh::RemapDependencies(CKDependenciesContext &context) {
    // Remap material dependencies
    for (int i = 0; i < m_MaterialChannels.Size(); ++i) {
        if (m_MaterialChannels[i].m_Material) {
            m_MaterialChannels[i].m_Material = (CKMaterial *) context.Remap(m_MaterialChannels[i].m_Material);
        }
    }
    return CK_OK;
}

CKERROR RCKMesh::Copy(CKObject &o, CKDependenciesContext &context) {
    // Copy mesh data
    RCKMesh &source = (RCKMesh &) o;

    // Copy basic properties
    m_Flags = source.m_Flags;
    m_BaryCenter = source.m_BaryCenter;
    m_Radius = source.m_Radius;
    m_LocalBox = source.m_LocalBox;

    // Copy vertex data
    SetVertexCount(source.GetVertexCount());
    for (int i = 0; i < source.GetVertexCount(); i++) {
        VxVector pos, normal;
        CKDWORD color;

        source.GetVertexPosition(i, &pos);
        source.GetVertexNormal(i, &normal);
        color = source.GetVertexColor(i);

        SetVertexPosition(i, &pos);
        SetVertexNormal(i, &normal);
        SetVertexColor(i, color);

        // Copy UV coordinates
        for (int channel = 0; channel < source.GetMaterialCount(); channel++) {
            float u, v;
            source.GetVertexTextureCoordinates(i, &u, &v, channel);
            SetVertexTextureCoordinates(i, u, v, channel);
        }
    }

    // Copy face data
    SetFaceCount(source.GetFaceCount());
    for (int i = 0; i < source.GetFaceCount(); i++) {
        int v1, v2, v3;
        source.GetFaceVertexIndex(i, v1, v2, v3);
        SetFaceVertexIndex(i, v1, v2, v3);

        CKMaterial *material = source.GetFaceMaterial(i);
        if (material) {
            SetFaceMaterial(i, material);
        }
    }

    // Copy line data
    SetLineCount(source.GetLineCount());
    for (int i = 0; i < source.GetLineCount(); i++) {
        int v1, v2;
        source.GetLine(i, &v1, &v2);
        SetLine(i, v1, v2);
    }

    // Copy vertex weights
    SetVertexWeightsCount(source.GetVertexWeightsCount());
    for (int i = 0; i < source.GetVertexWeightsCount(); i++) {
        float weight = source.GetVertexWeight(i);
        SetVertexWeight(i, weight);
    }

    return CK_OK;
}

// Scene management
void RCKMesh::AddToScene(CKScene *scene, CKBOOL dependencies) {
    CKBeObject::AddToScene(scene, dependencies);
}

void RCKMesh::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    CKBeObject::RemoveFromScene(scene, dependencies);
}

// Class Registering
CKSTRING RCKMesh::GetClassName() {
    return "3D Mesh";
}

int RCKMesh::GetDependenciesCount(int mode) {
    return 1; // Materials
}

CKSTRING RCKMesh::GetDependencies(int i, int mode) {
    if (i == 0) {
        return "Materials";
    }
    return nullptr;
}

void RCKMesh::Register() {
    // Based on IDA decompilation at 0x10028b53
    CKClassNeedNotificationFrom(m_ClassID, CKCID_MATERIAL);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_MESH);
    CKClassRegisterDefaultOptions(m_ClassID, CK_GENERALOPTIONS_CANUSECURRENTOBJECT);
}

CKMesh *RCKMesh::CreateInstance(CKContext *Context) {
    return new RCKMesh(Context);
}

CK_CLASSID RCKMesh::m_ClassID = CKCID_MESH;

// Helper methods
CKDWORD RCKMesh::GetSaveFlags() {
    CKDWORD flags = 0x01; // Always save positions

    // Check if we have normals
    if (m_Vertices.Size() > 0) {
        flags |= 0x02;
    }

    // Check if we have colors
    if (m_VertexColors.Size() > 0) {
        flags |= 0x04;
    }

    // Check if we have UV coordinates
    for (int i = 0; i < m_MaterialChannels.Size(); ++i) {
        if (m_MaterialChannels[i].m_uv) {
            flags |= 0x08;
            break;
        }
    }

    return flags;
}

void RCKMesh::UpdateBoundingVolumes(CKBOOL force) {
    if (!force && (m_Flags & 0x01)) {
        return; // Already valid
    }

    int vertexCount = m_Vertices.Size();
    if (vertexCount == 0) {
        m_LocalBox.Min = VxVector(0, 0, 0);
        m_LocalBox.Max = VxVector(0, 0, 0);
        m_BaryCenter = VxVector(0.0f, 0.0f, 0.0f);
        m_Radius = 0.0f;
        return;
    }

    // Calculate bounding box
    VxVector minPos = m_Vertices[0].m_Position;
    VxVector maxPos = m_Vertices[0].m_Position;
    VxVector center(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < vertexCount; i++) {
        VxVector &pos = m_Vertices[i].m_Position;
        center += pos;

        if (pos.x < minPos.x) minPos.x = pos.x;
        if (pos.y < minPos.y) minPos.y = pos.y;
        if (pos.z < minPos.z) minPos.z = pos.z;
        if (pos.x > maxPos.x) maxPos.x = pos.x;
        if (pos.y > maxPos.y) maxPos.y = pos.y;
        if (pos.z > maxPos.z) maxPos.z = pos.z;
    }

    // Set bounding box
    m_LocalBox.Min = minPos;
    m_LocalBox.Max = maxPos;

    // Calculate barycenter
    center *= (1.0f / vertexCount);
    m_BaryCenter = center;

    // Calculate radius (distance from barycenter to farthest vertex)
    m_Radius = 0.0f;
    for (int i = 0; i < vertexCount; i++) {
        VxVector diff = m_Vertices[i].m_Position - m_BaryCenter;
        float distance = diff.Magnitude();
        if (distance > m_Radius) {
            m_Radius = distance;
        }
    }

    m_Flags |= 0x01; // Mark as valid
}

// Material channel management
int RCKMesh::AddChannel(CKMaterial *material, int flags) {
    CKMaterialChannel channel;
    channel.m_Material = material;
    channel.m_Flags = flags;
    channel.m_SourceBlend = VXBLEND_SRCALPHA;
    channel.m_DestBlend = VXBLEND_INVSRCALPHA;
    channel.m_uv = nullptr;

    int index = m_MaterialChannels.Size();
    m_MaterialChannels.PushBack(channel);

    // Allocate UV coordinates for this channel
    if (!(flags & 0x800000)) {
        int vertexCount = GetVertexCount();
        if (vertexCount > 0) {
            m_MaterialChannels[index].m_uv = new Vx2DVector[vertexCount];
            memset(m_MaterialChannels[index].m_uv, 0, sizeof(Vx2DVector) * vertexCount);
        }
    }

    return index;
}

void RCKMesh::RemoveChannel(CKMaterial *material) {
    int index = GetChannelByMaterial(material);
    if (index >= 0) {
        RemoveChannel(index);
    }
}

void RCKMesh::RemoveChannel(int index) {
    if (index >= 0 && index < m_MaterialChannels.Size()) {
        CKMaterialChannel &channel = m_MaterialChannels[index];
        if (channel.m_uv) {
            delete[] channel.m_uv;
        }
        m_MaterialChannels.RemoveAt(index);
    }
}

int RCKMesh::GetChannelByMaterial(CKMaterial *material) {
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        if (m_MaterialChannels[i].m_Material == material) {
            return i;
        }
    }
    return -1;
}

void RCKMesh::CreateNewMaterialGroup(int materialIndex) {
    CKMaterialGroup *group = new CKMaterialGroup();
    group->m_Material = nullptr; // Will be set later
    group->field_28 = 0;         // Start vertex
    group->field_2C = 0;         // Vertex count
    group->field_30 = 0;         // VB data ptr
    m_MaterialGroups.PushBack(group);
}

void RCKMesh::DestroyPM() {
    if (!m_ProgressiveMesh)
        return;

    RemovePreRenderCallBack((CK_MESHRENDERCALLBACK) ProgressiveMeshPreRenderCallback, this);
    m_ProgressiveMesh->m_Data.Clear();
    delete m_ProgressiveMesh;
    m_ProgressiveMesh = nullptr;
}

// =============================================
// Missing virtual method implementations
// =============================================

CKBYTE *RCKMesh::GetModifierVertices(CKDWORD *Stride) {
    if (Stride) *Stride = sizeof(VxVertex);
    return (CKBYTE *) m_Vertices.Begin();
}

int RCKMesh::GetModifierVertexCount() {
    return m_Vertices.Size();
}

void RCKMesh::ModifierVertexMove(CKBOOL RebuildNormals, CKBOOL RebuildFaceNormals) {
    if (RebuildNormals) BuildNormals();
    if (RebuildFaceNormals) BuildFaceNormals();
    VertexMove();
}

CKBYTE *RCKMesh::GetModifierUVs(CKDWORD *Stride, int channel) {
    if (channel < 0) channel = 0;
    if (channel >= m_MaterialChannels.Size()) {
        if (Stride) *Stride = 0;
        return nullptr;
    }
    if (Stride) *Stride = sizeof(Vx2DVector);
    return (CKBYTE *) m_MaterialChannels[channel].m_uv;
}

int RCKMesh::GetModifierUVCount(int channel) {
    if (channel < 0) channel = 0;
    if (channel >= m_MaterialChannels.Size()) return 0;
    return m_Vertices.Size();
}

void RCKMesh::ModifierUVMove() {
    UVChanged();
}

void RCKMesh::SetVertexSpecularColor(int Index, CKDWORD Color) {
    if (Index >= 0 && Index < m_VertexColors.Size()) {
        m_VertexColors[Index].Specular = Color;
    }
}

CKDWORD RCKMesh::GetVertexSpecularColor(int Index) {
    if (Index >= 0 && Index < m_VertexColors.Size()) {
        return m_VertexColors[Index].Specular;
    }
    return 0;
}

void RCKMesh::TranslateVertices(VxVector *Vector) {
    if (!Vector) return;
    for (int i = 0; i < m_Vertices.Size(); i++) {
        m_Vertices[i].m_Position.x += Vector->x;
        m_Vertices[i].m_Position.y += Vector->y;
        m_Vertices[i].m_Position.z += Vector->z;
    }
    VertexMove();
}

void RCKMesh::ScaleVertices(VxVector *Vector, VxVector *Pivot) {
    if (!Vector) return;
    VxVector pivot = Pivot ? *Pivot : m_BaryCenter;
    for (int i = 0; i < m_Vertices.Size(); i++) {
        VxVector &pos = m_Vertices[i].m_Position;
        pos.x = pivot.x + (pos.x - pivot.x) * Vector->x;
        pos.y = pivot.y + (pos.y - pivot.y) * Vector->y;
        pos.z = pivot.z + (pos.z - pivot.z) * Vector->z;
    }
    VertexMove();
}

void RCKMesh::ScaleVertices3f(float X, float Y, float Z, VxVector *Pivot) {
    VxVector scale(X, Y, Z);
    ScaleVertices(&scale, Pivot);
}

void RCKMesh::RotateVertices(VxVector *Vector, float Angle) {
    if (!Vector) return;
    VxMatrix rot;
    Vx3DMatrixFromRotation(rot, *Vector, Angle);
    for (int i = 0; i < m_Vertices.Size(); i++) {
        VxVector &pos = m_Vertices[i].m_Position;
        Vx3DRotateVector(&pos, rot, &pos);
    }
    VertexMove();
}

const VxVector &RCKMesh::GetFaceNormal(int Index) {
    static VxVector defaultNormal(0, 0, 1);
    if (Index >= 0 && Index < m_Faces.Size()) {
        return m_Faces[Index].m_Normal;
    }
    return defaultNormal;
}

CKWORD RCKMesh::GetFaceChannelMask(int FaceIndex) {
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        return m_Faces[FaceIndex].m_ChannelMask;
    }
    return 0;
}

VxVector &RCKMesh::GetFaceVertex(int FaceIndex, int VIndex) {
    static VxVector defaultVertex(0, 0, 0);
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        CKFace &face = m_Faces[FaceIndex];
        if (VIndex >= 0 && VIndex < 3) {
            int vertexIndex = face.m_VertexIndex[VIndex];
            if (vertexIndex >= 0 && vertexIndex < m_Vertices.Size()) {
                return m_Vertices[vertexIndex].m_Position;
            }
        }
    }
    return defaultVertex;
}

CKBYTE *RCKMesh::GetFaceNormalsPtr(CKDWORD *Stride) {
    if (Stride) *Stride = sizeof(CKFace);
    if (m_Faces.Size() > 0) {
        return (CKBYTE *) &m_Faces[0].m_Normal;
    }
    return nullptr;
}

void RCKMesh::SetFaceMaterialEx(int *FaceIndices, int FaceCount, CKMaterial *Mat) {
    if (!FaceIndices) return;
    for (int i = 0; i < FaceCount; i++) {
        SetFaceMaterial(FaceIndices[i], Mat);
    }
}

void RCKMesh::SetFaceChannelMask(int FaceIndex, CKWORD ChannelMask) {
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        m_Faces[FaceIndex].m_ChannelMask = ChannelMask;
    }
}

void RCKMesh::ReplaceMaterial(CKMaterial *oldMat, CKMaterial *newMat) {
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        if (m_MaterialChannels[i].m_Material == oldMat) {
            m_MaterialChannels[i].m_Material = newMat;
        }
    }
}

void RCKMesh::ChangeFaceChannelMask(int FaceIndex, CKWORD AddChannelMask, CKWORD RemoveChannelMask) {
    if (FaceIndex >= 0 && FaceIndex < m_Faces.Size()) {
        m_Faces[FaceIndex].m_ChannelMask |= AddChannelMask;
        m_Faces[FaceIndex].m_ChannelMask &= ~RemoveChannelMask;
    }
}

void RCKMesh::ApplyGlobalMaterial(CKMaterial *Mat) {
    for (int i = 0; i < m_Faces.Size(); i++) {
        SetFaceMaterial(i, Mat);
    }
}

void RCKMesh::DissociateAllFaces() {
    // Create separate vertices for each face
    int faceCount = m_Faces.Size();
    if (faceCount == 0) return;

    XArray<VxVertex> newVertices;
    XArray<VxColors> newColors;

    for (int i = 0; i < faceCount; i++) {
        CKFace &face = m_Faces[i];
        for (int j = 0; j < 3; j++) {
            int oldIndex = face.m_VertexIndex[j];
            if (oldIndex >= 0 && oldIndex < m_Vertices.Size()) {
                newVertices.PushBack(m_Vertices[oldIndex]);
                if (oldIndex < m_VertexColors.Size()) {
                    newColors.PushBack(m_VertexColors[oldIndex]);
                }
            }
            face.m_VertexIndex[j] = i * 3 + j;
        }
    }

    // Swap contents
    m_Vertices.Clear();
    m_VertexColors.Clear();
    for (int i = 0; i < newVertices.Size(); i++) {
        m_Vertices.PushBack(newVertices[i]);
    }
    for (int i = 0; i < newColors.Size(); i++) {
        m_VertexColors.PushBack(newColors[i]);
    }
}

void RCKMesh::CreateLineStrip(int StartingLine, int Count, int StartingVertexIndex) {
    if (Count <= 0) return;
    for (int i = 0; i < Count; i++) {
        SetLine(StartingLine + i, StartingVertexIndex + i, StartingVertexIndex + i + 1);
    }
}

int RCKMesh::GetChannelCount() {
    return m_MaterialChannels.Size();
}

void RCKMesh::RemoveChannelByMaterial(CKMaterial *Mat) {
    int index = GetChannelByMaterial(Mat);
    if (index >= 0) {
        RemoveChannel(index);
    }
}

void RCKMesh::ActivateChannel(int Index, CKBOOL Active) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        if (Active) {
            m_MaterialChannels[Index].m_Flags |= 0x01;
        } else {
            m_MaterialChannels[Index].m_Flags &= ~0x01;
        }
    }
}

CKBOOL RCKMesh::IsChannelActive(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return (m_MaterialChannels[Index].m_Flags & 0x01) != 0;
    }
    return FALSE;
}

void RCKMesh::ActivateAllChannels(CKBOOL Active) {
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        ActivateChannel(i, Active);
    }
}

void RCKMesh::LitChannel(int Index, CKBOOL Lit) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        if (Lit) {
            m_MaterialChannels[Index].m_Flags |= 0x02;
        } else {
            m_MaterialChannels[Index].m_Flags &= ~0x02;
        }
    }
}

CKBOOL RCKMesh::IsChannelLit(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return (m_MaterialChannels[Index].m_Flags & 0x02) != 0;
    }
    return FALSE;
}

CKDWORD RCKMesh::GetChannelFlags(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return m_MaterialChannels[Index].m_Flags;
    }
    return 0;
}

void RCKMesh::SetChannelFlags(int Index, CKDWORD Flags) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        m_MaterialChannels[Index].m_Flags = Flags;
    }
}

CKMaterial *RCKMesh::GetChannelMaterial(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return m_MaterialChannels[Index].m_Material;
    }
    return nullptr;
}

VXBLEND_MODE RCKMesh::GetChannelSourceBlend(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return m_MaterialChannels[Index].m_SourceBlend;
    }
    return VXBLEND_ONE;
}

VXBLEND_MODE RCKMesh::GetChannelDestBlend(int Index) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        return m_MaterialChannels[Index].m_DestBlend;
    }
    return VXBLEND_ZERO;
}

void RCKMesh::SetChannelMaterial(int Index, CKMaterial *Mat) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        m_MaterialChannels[Index].m_Material = Mat;
    }
}

void RCKMesh::SetChannelSourceBlend(int Index, VXBLEND_MODE BlendMode) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        m_MaterialChannels[Index].m_SourceBlend = BlendMode;
    }
}

void RCKMesh::SetChannelDestBlend(int Index, VXBLEND_MODE BlendMode) {
    if (Index >= 0 && Index < m_MaterialChannels.Size()) {
        m_MaterialChannels[Index].m_DestBlend = BlendMode;
    }
}

CKERROR RCKMesh::Render(CKRenderContext *Dev, CK3dEntity *Mov) {
    // Basic render implementation - actual rendering done by rasterizer
    return CK_OK;
}

float *RCKMesh::GetVertexWeightsPtr() {
    return m_VertexWeights ? m_VertexWeights->Begin() : nullptr;
}

void RCKMesh::LoadVertices(CKStateChunk *chunk) {
    CKDWORD loadFlags = 0;
    ILoadVertices(chunk, &loadFlags);
    if (loadFlags & 0x04)
        BuildNormals();
    else
        BuildFaceNormals();
}

CKERROR RCKMesh::CreatePM() {
    if (m_ProgressiveMesh)
        return CKERR_ALREADYPRESENT;

    m_ProgressiveMesh = new CKProgressiveMesh();
    m_ProgressiveMesh->m_Field0 = GetVertexCount();
    m_ProgressiveMesh->m_MorphEnabled = 0;
    m_ProgressiveMesh->m_MorphStep = 0;
    m_ProgressiveMesh->m_Data.Clear();
    m_ProgressiveMesh->m_Data.PushBack((CKDWORD) GetVertexCount());

    AddPreRenderCallBack((CK_MESHRENDERCALLBACK) ProgressiveMeshPreRenderCallback, this, FALSE);
    return CK_OK;
}

CKBOOL RCKMesh::IsPM() {
    return m_ProgressiveMesh != nullptr;
}

//--------------------------------------------
// DefaultRender - Main mesh rendering function
//--------------------------------------------
int RCKMesh::DefaultRender(RCKRenderContext *rc, RCK3dEntity *ent) {
    CKRasterizerContext *rstContext = rc->m_RasterizerContext;

    // Check if we have vertices
    int vertexCount = m_Vertices.Size();
    if (vertexCount <= 0)
        return 0;

    // Get face and line counts
    int faceCount = m_Faces.Size();
    int lineCount = GetLineCount();

    if (m_ProgressiveMesh)
        ProgressiveMeshPreRenderCallback((CKRenderContext *) rc, (CK3dEntity *) ent, this, this);

    int renderVertexCount = m_ProgressiveMesh ? ClampPMVertexCount(this, GetVerticesRendered()) : vertexCount;
    bool useLod = m_ProgressiveMesh && renderVertexCount < vertexCount;

    XArray<CKWORD> lodFaceIndices;
    XArray<CKWORD> lodLineIndices;
    CKWORD *faceIndices = m_FaceVertexIndices.Begin();
    CKWORD *lineIndices = m_LineIndices.Begin();
    int faceIndexCount = m_FaceVertexIndices.Size();
    int lineIndexCount = m_LineIndices.Size();

    if (useLod) {
        FilterTriangleList(m_FaceVertexIndices.Begin(), m_FaceVertexIndices.Size(), renderVertexCount, lodFaceIndices);
        FilterLineList(m_LineIndices.Begin(), m_LineIndices.Size(), renderVertexCount, lodLineIndices);
        faceIndices = lodFaceIndices.Begin();
        lineIndices = lodLineIndices.Begin();
        faceIndexCount = lodFaceIndices.Size();
        lineIndexCount = lodLineIndices.Size();
        faceCount = faceIndexCount / 3;
        lineCount = lineIndexCount / 2;
    }

    if (!faceCount && !lineCount)
        return 0;

    CKDWORD zbufOnly = 0;
    CKDWORD stencilOnly = 0;
    CKBOOL hasChannels = (m_MaterialChannels.Size() > 0) && (m_Flags & 8) != 0;
    CKBOOL renderChannels = hasChannels;

    m_DrawFlags = 4;

    if (ent) {
        zbufOnly = ent->GetMoveableFlags() & VX_MOVEABLE_ZBUFONLY;
        stencilOnly = ent->GetMoveableFlags() & VX_MOVEABLE_STENCILONLY;
        renderChannels = ((ent->GetMoveableFlags() & 8) != 0) & renderChannels;
    }

    // Setup VxDrawPrimitiveData
    VxDrawPrimitiveData dpData;
    memset(&dpData, 0, sizeof(dpData));

    dpData.VertexCount = renderVertexCount;
    dpData.PositionStride = 32;
    dpData.NormalStride = 32;
    dpData.TexCoordStride = 32;
    dpData.ColorStride = 8;
    dpData.SpecularColorStride = 8;

    // Setup vertex pointers
    if (m_Vertices.Size() > 0) {
        VxVertex *vertices = &m_Vertices[0];
        dpData.PositionPtr = &vertices->m_Position;
        dpData.NormalPtr = &vertices->m_Normal;
        dpData.TexCoordPtr = &vertices->m_UV;
    }

    // Setup color pointers
    if (m_VertexColors.Size() > 0) {
        VxColors *colors = &m_VertexColors[0];
        dpData.ColorPtr = &colors->Color;
        dpData.SpecularColorPtr = &colors->Specular;
    }

    // Update stats
    rc->m_Stats.NbObjectDrawn++;
    rc->m_Stats.NbVerticesProcessed += dpData.VertexCount;

    // Variables that need to be declared before goto
    CKBOOL hasAlphaMaterial = FALSE;
    RCKMaterial *firstMat = nullptr;

    // Skip if no faces
    if (!faceCount)
        goto RenderLines;

    // Set wrap mode
    {
        CKDWORD wrapMode = ((m_Flags & 0x200) != 0 ? 2 : 0) | ((m_Flags & 0x100) != 0);
        rstContext->SetRenderState(VXRENDERSTATE_WRAP0, wrapMode);
    }

    // Create render groups if needed
    if ((m_Flags & 4) == 0)
        CreateRenderGroups();

    // Update channel mask
    m_FaceChannelMask = (CKWORD) m_FaceChannelMask;
    if (m_FaceChannelMask)
        UpdateChannelIndices();

    // Check if any material has alpha
    for (int i = 0; i < m_MaterialGroups.Size(); i++) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group && group->m_Material) {
            firstMat = group->m_Material;
            // Check alpha bits in material flags
            if ((*(CKDWORD *) ((char *) group->m_Material + 216) << 18) >> 26) {
                hasAlphaMaterial = TRUE;
                break;
            }
        }
    }

    // Z-buffer only rendering mode
    if (zbufOnly) {
        dpData.Flags = m_DrawFlags | 1;
        rstContext->SetVertexShader(0);
        rstContext->SetTexture(0, 0);
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
        rstContext->SetRenderState(VXRENDERSTATE_SPECULARENABLE, 0);
        rstContext->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, 0);
        rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
        rstContext->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
        rstContext->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
        rstContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 1);
        rstContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 1);
        rstContext->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ZERO);
        rstContext->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ONE);

        if (faceIndexCount > 0) {
            rstContext->DrawPrimitive(VX_TRIANGLELIST, faceIndices, faceIndexCount, &dpData);
        }
        goto UpdateStats;
    }

    // Stencil only rendering mode
    if (stencilOnly) {
        dpData.Flags = m_DrawFlags | 1;
        rstContext->SetVertexShader(0);
        rstContext->SetTexture(0, 0);
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
        rstContext->SetRenderState(VXRENDERSTATE_SPECULARENABLE, 0);
        rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
        rstContext->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
        rstContext->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
        rstContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 0);
        rstContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 1);
        rstContext->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ZERO);
        rstContext->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ONE);

        if (faceIndexCount > 0) {
            rstContext->DrawPrimitive(VX_TRIANGLELIST, faceIndices, faceIndexCount, &dpData);
        }
        renderChannels = FALSE;
        goto UpdateStats;
    }

    // Clear channel tracking
    field_D0.Clear();

    // Skip multi-texture channel setup (complex logic - simplified here)
    if (!hasAlphaMaterial && field_D0.Size() == 0) {
        rstContext->SetTexture(0, 1);
        rstContext->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0);
    }

    // Setup draw flags
    {
        CKDWORD flags = m_DrawFlags | 1;
        flags |= (512 << field_D0.Size());
        dpData.Flags = flags;
    }

    rstContext->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 0);

    // Check for vertex colors mode
    if ((m_Flags & 0x80) != 0) {
        dpData.Flags |= 0x30; // Use vertex colors
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
        rstContext->SetRenderState(VXRENDERSTATE_SPECULARENABLE, 1);
    } else {
        dpData.Flags |= 2; // Use normals
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 1);
    }

    // Check HW vertex buffer
    {
        VxDrawPrimitiveData *dp = &dpData;
        m_Valid++;
        if (m_Valid > 3 && (rstContext->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & 0x44) == 0x44) {
            // Check rasterizer capabilities for VB support and attempt HW vertex buffer
            if (CheckHWVertexBuffer(rstContext, dp)) {
                dp = nullptr; // Use HW vertex buffer instead
                field_EC = 1;
            }
        } else {
            field_EC = 0;
        }

        int renderedTriangles = 0;

        // Render non-transparent material groups first
        for (int i = 0; i < m_MaterialGroups.Size(); i++) {
            CKMaterialGroup *group = m_MaterialGroups[i];
            if (group && group->field_30 && dp) {
                // Update vertex buffer data for remapped groups
                // VBuffer::Update((CKMemoryPool *)group->field_30, (VxMemoryPool *)this, 0);
            }
            if (group && group->m_Material) {
                if (!group->m_Material->IsAlphaTransparent()) {
                    renderedTriangles += RenderGroup(rc, group, ent, dp, renderVertexCount);
                }
            }
        }

        // Render transparent material groups
        for (int i = 0; i < m_MaterialGroups.Size(); i++) {
            CKMaterialGroup *group = m_MaterialGroups[i];
            if (group && group->m_Material) {
                if (group->m_Material->IsAlphaTransparent()) {
                    renderedTriangles += RenderGroup(rc, group, ent, dp, renderVertexCount);
                }
            }
        }

        faceCount = renderedTriangles;
    }

    // Clear some flags
    m_Flags &= 0xFFFC3FFF;

    // Wireframe overlay
    if (rc->m_DisplayWireframe) {
        // Get projection matrix and offset Z slightly
        VxMatrix projMat;
        memcpy(&projMat, rc->GetProjectionTransformationMatrix(), sizeof(VxMatrix));
        float origZ = projMat[3][2];
        projMat[3][2] = origZ * 1.003f;
        rc->SetProjectionTransformationMatrix(projMat);

        rstContext->SetTexture(0, 0);
        rstContext->SetVertexShader(0);
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
        rstContext->SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_WIREFRAME);

        dpData.Flags = m_DrawFlags | 1;
        if (m_FaceVertexIndices.Size() > 0) {
            rstContext->DrawPrimitive(VX_TRIANGLELIST, m_FaceVertexIndices.Begin(), m_FaceVertexIndices.Size(),
                                      &dpData);
        }

        projMat[3][2] = origZ;
        rc->SetProjectionTransformationMatrix(projMat);
    }

UpdateStats:
    rc->m_Stats.NbTrianglesDrawn += faceCount;

    // Render material channels if needed
    if (renderChannels) {
        CKDWORD fogEnable = 0;
        rstContext->GetRenderState(VXRENDERSTATE_FOGENABLE, &fogEnable);
        RenderChannels(rc, ent, &dpData, fogEnable, faceIndices, faceIndexCount);
        rstContext->SetRenderState(VXRENDERSTATE_FOGENABLE, fogEnable);
    }

RenderLines:
    // Render lines
    if (lineCount) {
        int linesVertCount = renderVertexCount;

        dpData.VertexCount = linesVertCount;
        dpData.PositionStride = 32;
        dpData.NormalStride = 32;
        dpData.TexCoordStride = 32;
        dpData.ColorStride = 8;
        dpData.SpecularColorStride = 8;

        if (m_Vertices.Size() > 0) {
            dpData.PositionPtr = &m_Vertices[0].m_Position;
            dpData.NormalPtr = nullptr;
            dpData.TexCoordPtr = nullptr;
        }

        if (m_VertexColors.Size() > 0) {
            dpData.ColorPtr = &m_VertexColors[0].Color;
            dpData.SpecularColorPtr = &m_VertexColors[0].Specular;
        }

        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, 0);
        rstContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 1);
        rstContext->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_LESSEQUAL);
        rstContext->SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
        rstContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 0);
        rstContext->SetTexture(0, 0);

        dpData.Flags = m_DrawFlags | 0x11;
        if (lineIndexCount > 0) {
            rstContext->DrawPrimitive(VX_LINELIST, lineIndices, lineIndexCount, &dpData);
        }
        rc->m_Stats.NbLinesDrawn += lineCount;
    }

    // Reset wrap state
    rstContext->SetRenderState(VXRENDERSTATE_WRAP0, 0);

    // Clear texture stages
    if (hasAlphaMaterial || field_D0.Size() > 0) {
        for (int i = 1; i <= (int) field_D0.Size(); i++) {
            rstContext->SetTextureStageState(i, CKRST_TSS_STAGEBLEND, 0);
        }
    }

    return 1;
}

//--------------------------------------------
// RenderGroup - Render a single material group
//--------------------------------------------

// Mesh render callback with material parameter (5 args, not 4 like SDK typedef)
typedef void (*CK_MESHRENDERCALLBACK_EX)(CKRenderContext *Dev, CK3dEntity *Mov, CKMesh *Object, CKMaterial *Mat,
                                         void *Argument);

int RCKMesh::RenderGroup(RCKRenderContext *dev, CKMaterialGroup *group, RCK3dEntity *ent, VxDrawPrimitiveData *data,
                         int vertexLimit) {
    CKRasterizerContext *rstContext = dev->m_RasterizerContext;
    RCKMaterial *mat = group->m_Material;
    int trianglesDrawn = 0;
    bool useLod = vertexLimit > 0 && vertexLimit < m_Vertices.Size();

    // Execute pre-render submesh callbacks (if any exist)
    if (m_SubMeshCallbacks && m_SubMeshCallbacks->m_PreCallBacks.Size() > 0) {
        dev->m_ObjectsCallbacksTimeProfiler.Reset();
        rstContext->SetVertexShader(0);

        for (XClassArray<VxCallBack>::Iterator it = m_SubMeshCallbacks->m_PreCallBacks.Begin();
             it != m_SubMeshCallbacks->m_PreCallBacks.End(); ++it) {
            CK_MESHRENDERCALLBACK_EX func = (CK_MESHRENDERCALLBACK_EX) it->callback;
            if (func) {
                func((CKRenderContext *) dev, (CK3dEntity *) ent, (CKMesh *) this, (CKMaterial *) mat, it->argument);
            }
        }

        dev->m_Stats.ObjectsCallbacksTime += dev->m_ObjectsCallbacksTimeProfiler.Current();

        if (!mat)
            mat = (RCKMaterial *) dev->m_RenderManager->m_DefaultMat;
    } else if (mat) {
        mat->SetAsCurrent((CKRenderContext *) dev, (m_Flags & 0x80) == 0, 0);

        if (!mat->GetTexture(0) && field_D0.Size() > 0) {
            rstContext->SetTextureStageState(0, CKRST_TSS_OP, 2);
            rstContext->SetTextureStageState(0, CKRST_TSS_ARG1, 0);
            rstContext->SetTextureStageState(0, CKRST_TSS_AOP, 2);
            rstContext->SetTextureStageState(0, CKRST_TSS_AARG1, 0);
        }
    } else {
        mat = (RCKMaterial *) dev->m_RenderManager->m_DefaultMat;
        if (mat)
            mat->SetAsCurrent((CKRenderContext *) dev, (m_Flags & 0x80) == 0, 0);

        if ((m_Flags & 0x1080) == 0x1080) {
            rstContext->SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA);
            rstContext->SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA);
            rstContext->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, 1);
        }
    }

    if (ent) {
        CKDWORD moveFlags = ent->GetMoveableFlags();
        if (moveFlags & 0x200000)
            rstContext->SetRenderState(VXRENDERSTATE_ZFUNC, VXCMP_ALWAYS);
        if (moveFlags & 0x80000)
            rstContext->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, 0);
    }

    auto drawEntry = [&](CKPrimitiveEntry *entry, bool countStats) {
        CKWORD *indices = entry->m_Indices.Begin();
        int indexCount = entry->m_Indices.Size();
        XArray<CKWORD> filtered;

        if (useLod) {
            if (entry->m_Type == VX_TRIANGLELIST) {
                FilterTriangleList(indices, indexCount, vertexLimit, filtered);
            } else if (entry->m_Type == VX_LINELIST) {
                FilterLineList(indices, indexCount, vertexLimit, filtered);
            } else {
                filtered.Clear();
            }
            indices = filtered.Begin();
            indexCount = filtered.Size();
        }

        if (indexCount <= 0)
            return;

        if (countStats && entry->m_Type == VX_TRIANGLELIST)
            trianglesDrawn += indexCount / 3;

        if (data) {
            rstContext->DrawPrimitive(entry->m_Type, indices, indexCount, data);
            return;
        }

        if (useLod || (int) entry->m_IndexBufferOffset < 0) {
            int vbCount = group->field_2C;
            if (useLod) {
                int available = vertexLimit - (int) group->field_28;
                if (available <= 0)
                    return;
                if (available < vbCount)
                    vbCount = available;
            }
            rstContext->DrawPrimitiveVB(entry->m_Type, m_VertexBuffer, group->field_28, vbCount, indices, indexCount);
            return;
        }

        rstContext->DrawPrimitiveVBIB(entry->m_Type, m_VertexBuffer, m_IndexBuffer,
                                      group->field_28, group->field_2C,
                                      entry->m_IndexBufferOffset, indexCount);
    };

    if (data) {
        if (mat && mat->GetFillMode() == VXFILL_SOLID && mat->IsTwoSided() && mat->IsAlphaTransparent()) {
            rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
            for (int i = 0; i < group->field_4.Size(); i++) {
                drawEntry(&group->field_4[i], false);
            }
            rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
        }

        for (int i = 0; i < group->field_4.Size(); i++) {
            drawEntry(&group->field_4[i], true);
        }
    } else {
        if (mat && mat->GetFillMode() == VXFILL_SOLID && mat->IsTwoSided() && mat->IsAlphaTransparent()) {
            rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CW);
            for (int i = 0; i < group->field_4.Size(); i++) {
                drawEntry(&group->field_4[i], false);
            }
            rstContext->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
        }

        for (int i = 0; i < group->field_4.Size(); i++) {
            drawEntry(&group->field_4[i], true);
        }
    }

    if (m_SubMeshCallbacks && m_SubMeshCallbacks->m_PostCallBacks.Size() > 0) {
        dev->m_ObjectsCallbacksTimeProfiler.Reset();
        rstContext->SetVertexShader(0);

        for (XClassArray<VxCallBack>::Iterator it = m_SubMeshCallbacks->m_PostCallBacks.Begin();
             it != m_SubMeshCallbacks->m_PostCallBacks.End(); ++it) {
            CK_MESHRENDERCALLBACK_EX func = (CK_MESHRENDERCALLBACK_EX) it->callback;
            if (func) {
                func((CKRenderContext *) dev, (CK3dEntity *) ent, (CKMesh *) this, (CKMaterial *) mat, it->argument);
            }
        }

        dev->m_Stats.ObjectsCallbacksTime += dev->m_ObjectsCallbacksTimeProfiler.Current();
    }

    return trianglesDrawn;
}

//--------------------------------------------
// RenderChannels - Render material channels (multi-texture passes)
//--------------------------------------------
int RCKMesh::RenderChannels(RCKRenderContext *dev, RCK3dEntity *ent, VxDrawPrimitiveData *data, int fogEnable,
                            CKWORD *indices, int indexCount) {
    CKRasterizerContext *rstContext = dev->m_RasterizerContext;

    // Setup flags for channel rendering
    CKDWORD flags = m_DrawFlags | 0x201;
    data->Flags = flags;

    // Save original color pointers
    void *origColorPtr = data->ColorPtr;
    void *origSpecularPtr = data->SpecularColorPtr;

    // Get and modify projection matrix for Z offset
    VxMatrix projMat;
    memcpy(&projMat, dev->GetProjectionTransformationMatrix(), sizeof(VxMatrix));
    float origZ = projMat[3][2];
    projMat[3][2] = origZ * 1.001f;
    dev->SetProjectionTransformationMatrix(projMat);

    // Iterate through material channels
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        CKMaterialChannel *channel = &m_MaterialChannels[i];

        // Skip inactive or already processed channels
        if (!channel->m_Material)
            continue;
        if ((channel->m_Flags & 1) == 0)
            continue;
        if ((channel->m_Flags & 0x2000000) != 0)
            continue;

        RCKMaterial *mat = (RCKMaterial *) channel->m_Material;

        // Save and modify material blend modes
        VXBLEND_MODE origSrcBlend = mat->GetSourceBlend();
        VXBLEND_MODE origDestBlend = mat->GetDestBlend();

        mat->SetSourceBlend(channel->m_SourceBlend);
        mat->SetDestBlend(channel->m_DestBlend);

        // Set fog enable based on blend mode
        if (channel->m_SourceBlend == VXBLEND_SRCALPHA && channel->m_DestBlend == VXBLEND_INVSRCALPHA) {
            rstContext->SetRenderState(VXRENDERSTATE_FOGENABLE, fogEnable);
        } else {
            rstContext->SetRenderState(VXRENDERSTATE_FOGENABLE, 0);
        }

        rstContext->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 0);

        // Check for additive blending (mark as unlit)
        if ((channel->m_SourceBlend == VXBLEND_ZERO && channel->m_DestBlend == VXBLEND_SRCCOLOR) ||
            (channel->m_SourceBlend == VXBLEND_DESTCOLOR && channel->m_DestBlend == VXBLEND_ZERO)) {
            channel->m_Flags |= 0x1000000;
        }

        CKBOOL useLighting = (m_Flags & 0x80) == 0 && (channel->m_Flags & 0x1000000) == 0;
        mat->SetAsCurrent((CKRenderContext *) dev, useLighting, 0);
        rstContext->SetRenderState(VXRENDERSTATE_LIGHTING, useLighting);

        // Handle unlit channel
        if ((channel->m_Flags & 0x1000000) != 0) {
            data->ColorPtr = nullptr;
            data->SpecularColorPtr = nullptr;
            data->Flags &= ~2;
        } else if ((m_Flags & 0x80) != 0) {
            data->Flags &= ~2;
            data->Flags |= 0x30;
            data->ColorPtr = origColorPtr;
            data->SpecularColorPtr = origSpecularPtr;
        } else {
            data->Flags |= 2;
        }

        // Setup texture coordinates
        if ((channel->m_Flags & 0x800000) != 0) {
            // Use main vertex UVs
            if (m_Vertices.Size() > 0) {
                data->TexCoordPtr = &m_Vertices[0].m_UV;
                data->TexCoordStride = 32;
            }
        } else {
            // Use channel-specific UVs
            data->TexCoordPtr = channel->m_uv;
            data->TexCoordStride = 8;
        }

        // Draw the channel
        CKWORD *channelIndices = indices ? indices : m_FaceVertexIndices.Begin();
        int channelIndexCount = indexCount > 0 ? indexCount : m_FaceVertexIndices.Size();
        if (channelIndexCount > 0 && channelIndices) {
            rstContext->DrawPrimitive(VX_TRIANGLELIST, channelIndices, channelIndexCount, data);
        }

        // Restore material blend modes
        mat->SetSourceBlend(origSrcBlend);
        mat->SetDestBlend(origDestBlend);
    }

    // Restore texture coordinate pointer
    if (m_Vertices.Size() > 0) {
        data->TexCoordPtr = &m_Vertices[0].m_UV;
        data->TexCoordStride = 32;
    }
    data->ColorPtr = origColorPtr;
    data->SpecularColorPtr = origSpecularPtr;

    // Restore projection matrix
    projMat[3][2] = origZ;
    dev->SetProjectionTransformationMatrix(projMat);

    return 1;
}

//--------------------------------------------
// CreateRenderGroups - Build material groups for rendering
//--------------------------------------------
void RCKMesh::CreateRenderGroups() {
    // Mark as mono-material initially and invalidate
    m_Flags |= VXMESH_MONOMATERIAL;
    m_Valid = 0;

    // Check for valid geometry
    int vertexCount = m_Vertices.Size();
    int faceCount = m_Faces.Size();
    if (vertexCount <= 0 || faceCount <= 0) {
        m_Flags |= 4;
        return;
    }

    // Clear face indices from existing groups (don't delete the groups themselves)
    for (int i = 0; i < m_MaterialGroups.Size(); i++) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group) {
            // Clear primitive entries
            for (int j = 0; j < group->field_4.Size(); j++) {
                group->field_4[j].m_Indices.Clear();
            }
            group->field_10.Clear();
            group->field_2C = 0; // Reset vertex count
        }
    }

    // Track min/max material indices used
    int maxMatIndex = 0;
    int minMatIndex = 2048;

    // First pass: count faces per material and build face index lists
    CKWORD *faceIndices = m_FaceVertexIndices.Begin();

    for (int faceIdx = 0; faceIdx < faceCount; faceIdx++) {
        CKFace &face = m_Faces[faceIdx];
        int matIdx = face.m_MatIndex;

        // Track min/max material index
        if (matIdx > maxMatIndex) maxMatIndex = matIdx;
        if (matIdx < minMatIndex) minMatIndex = matIdx;

        // Ensure material group exists
        while (m_MaterialGroups.Size() <= matIdx) {
            CKMaterialGroup *newGroup = new CKMaterialGroup();
            memset(newGroup, 0, sizeof(CKMaterialGroup));
            m_MaterialGroups.PushBack(newGroup);
        }

        CKMaterialGroup *group = m_MaterialGroups[matIdx];
        if (group) {
            // Add face index to the group's face list
            CKWORD faceWord = (CKWORD) faceIdx;
            group->field_10.PushBack((void *) (intptr_t) faceWord);
        }

        faceIndices += 3; // Move to next face
    }

    // Check if single material
    if (minMatIndex == maxMatIndex && m_MaterialGroups.Size() > 0) {
        // Single material case - copy all face indices directly
        CKMaterialGroup *group = m_MaterialGroups[minMatIndex];
        if (group) {
            // Ensure we have at least one primitive entry
            if (group->field_4.Size() == 0) {
                CKPrimitiveEntry entry;
                entry.m_Type = VX_TRIANGLELIST;
                entry.m_IndexBufferOffset = -1;
                group->field_4.PushBack(entry);
            }

            // Copy all indices to the primitive entry
            CKPrimitiveEntry &entry = group->field_4[0];
            entry.m_Indices.Resize(faceCount * 3);
            memcpy(entry.m_Indices.Begin(), m_FaceVertexIndices.Begin(), faceCount * 3 * sizeof(CKWORD));
            entry.m_IndexBufferOffset = -1;

            // Set vertex range
            group->field_20 = 0;
            group->field_24 = vertexCount;
            group->field_2C = vertexCount;
            group->field_28 = 0;
        }
    } else {
        // Multiple materials - need to build per-material index lists
        m_Flags &= ~VXMESH_MONOMATERIAL;

        // Allocate tracking array for vertex -> material mapping
        CKDWORD *vertexMaterialMask = new CKDWORD[vertexCount];

        for (int matIdx = 0; matIdx < m_MaterialGroups.Size(); matIdx++) {
            CKMaterialGroup *group = m_MaterialGroups[matIdx];
            if (!group || group->field_10.Size() == 0)
                continue;

            // Clear vertex tracking
            memset(vertexMaterialMask, 0, vertexCount * sizeof(CKDWORD));

            // Ensure primitive entry exists
            if (group->field_4.Size() == 0) {
                CKPrimitiveEntry entry;
                entry.m_Type = VX_TRIANGLELIST;
                entry.m_IndexBufferOffset = -1;
                group->field_4.PushBack(entry);
            }

            CKPrimitiveEntry &entry = group->field_4[0];
            int faceListCount = group->field_10.Size();
            entry.m_Indices.Resize(faceListCount * 3);

            // Build local index list for this material group
            CKWORD *dstIndices = entry.m_Indices.Begin();
            int localVertexCount = 0;

            // Track which vertices we've seen and their local indices
            XArray<int> vertexRemap;
            vertexRemap.Resize(vertexCount);
            for (int v = 0; v < vertexCount; v++)
                vertexRemap[v] = -1;

            for (int f = 0; f < faceListCount; f++) {
                int faceIdx = (int) (intptr_t) group->field_10[f];
                CKWORD *srcIndices = m_FaceVertexIndices.Begin() + faceIdx * 3;

                for (int v = 0; v < 3; v++) {
                    CKWORD vidx = srcIndices[v];
                    if (vertexRemap[vidx] < 0) {
                        vertexRemap[vidx] = localVertexCount++;
                    }
                    *dstIndices++ = (CKWORD) vertexRemap[vidx];
                }
            }

            // Allocate local vertex buffer for this group
            if (localVertexCount > 0) {
                // Store vertex remap table in field_30
                int *remapTable = new int[localVertexCount];
                int remapIdx = 0;
                for (int v = 0; v < vertexCount && remapIdx < localVertexCount; v++) {
                    if (vertexRemap[v] >= 0) {
                        remapTable[vertexRemap[v]] = v;
                    }
                }
                group->field_30 = (CKDWORD) remapTable;
            }

            entry.m_IndexBufferOffset = -1;
            group->field_20 = 0;
            group->field_24 = localVertexCount;
            group->field_2C = localVertexCount;
            group->field_28 = 0; // Will be set later for combined VB
        }

        delete[] vertexMaterialMask;

        // Update vertex buffer offsets for combined buffer
        CKDWORD totalOffset = 0;
        for (int i = 0; i < m_MaterialGroups.Size(); i++) {
            CKMaterialGroup *group = m_MaterialGroups[i];
            if (group && group->field_2C > 0) {
                group->field_28 = totalOffset;
                totalOffset += group->field_2C;
            }
        }
    }

    // Remove empty material groups
    for (int i = m_MaterialGroups.Size() - 1; i > 0; i--) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group && group->field_10.Size() == 0) {
            // Check if any face references this material
            bool hasReference = false;
            for (int j = 0; j < m_MaterialGroups.Size() && !hasReference; j++) {
                if (i != j && m_MaterialGroups[j] && m_MaterialGroups[j]->m_Material == group->m_Material) {
                    hasReference = true;
                }
            }

            if (!hasReference) {
                delete group;
                m_MaterialGroups.RemoveAt(i);

                // Update face material indices
                for (int f = 0; f < m_Faces.Size(); f++) {
                    if (m_Faces[f].m_MatIndex > i)
                        m_Faces[f].m_MatIndex--;
                }
            }
        }
    }

    // Clear face channel mask for material channels
    m_FaceChannelMask = 0xFFFF;

    // Clear channel index data
    for (int i = 0; i < m_MaterialChannels.Size(); i++) {
        // Channel face indices would be rebuilt by UpdateChannelIndices if needed
    }

    // Mark render groups as built
    m_Flags |= 4;
}

//--------------------------------------------
// UpdateChannelIndices - Update channel face indices
// Builds per-channel index lists for faces that use each material channel
//--------------------------------------------
void RCKMesh::UpdateChannelIndices() {
    // Compute the intersection of all face channel masks
    // (which channel bits are set in ALL faces)
    CKWORD allFacesMask = 0xFFFF;
    for (int i = 0; i < m_Faces.Size(); i++) {
        allFacesMask &= m_Faces[i].m_ChannelMask;
    }

    // Channel bit for iteration
    CKWORD channelBit = 1;

    // Process each material channel
    for (int c = 0; c < m_MaterialChannels.Size(); c++) {
        CKMaterialChannel &channel = m_MaterialChannels[c];

        if ((channelBit & allFacesMask) != 0) {
            // This channel is used by ALL faces - no need for per-channel indices
            // Free the index array if it exists
            if (channel.m_FaceIndices) {
                delete channel.m_FaceIndices;
                channel.m_FaceIndices = nullptr;
            }
        } else if (!channel.m_FaceIndices || (channelBit & m_FaceChannelMask) != 0) {
            // Need to build index list for faces that use this channel

            // Create index array if needed
            if (channel.m_FaceIndices) {
                channel.m_FaceIndices->Clear();
            } else {
                channel.m_FaceIndices = new XVoidArray();
            }

            // Iterate through faces and collect vertex indices for faces using this channel
            CKWORD *faceIndices = m_FaceVertexIndices.Begin();
            for (int f = 0; f < m_Faces.Size(); f++) {
                if ((channelBit & m_Faces[f].m_ChannelMask) != 0) {
                    // This face uses this channel - add its 3 vertex indices
                    channel.m_FaceIndices->PushBack((void *) (intptr_t) faceIndices[0]);
                    channel.m_FaceIndices->PushBack((void *) (intptr_t) faceIndices[1]);
                    channel.m_FaceIndices->PushBack((void *) (intptr_t) faceIndices[2]);
                }
                faceIndices += 3;
            }
        }

        // Move to next channel bit
        channelBit <<= 1;
    }

    // Clear the face channel mask to indicate indices are up to date
    m_FaceChannelMask = 0;
}

//--------------------------------------------
// CheckHWVertexBuffer - Check and setup hardware vertex buffer
// Returns TRUE if hardware VB was created/updated successfully
//--------------------------------------------
CKBOOL RCKMesh::CheckHWVertexBuffer(CKRasterizerContext *rst, VxDrawPrimitiveData *data) {
    if (!rst || !data)
        return FALSE;

    CKBOOL needNewBuffer = FALSE;

    // Calculate vertex format and size
    CKDWORD vertexSize = 0;
    CKDWORD vertexFormat = CKRSTGetVertexFormat((CKRST_DPFLAGS) data->Flags, vertexSize);

    // Count total vertices needed
    CKDWORD totalVertexCount = 0;
    CKBOOL hasRemappedVertices = FALSE;
    CKBOOL hasDirectVertices = FALSE;

    for (int i = 0; i < m_MaterialGroups.Size(); i++) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group && group->m_Material) {
            if (group->field_30) {
                hasRemappedVertices = TRUE;
                totalVertexCount += group->field_2C;
            } else {
                hasDirectVertices = TRUE;
            }
        }
    }

    // If direct vertices, add base vertex count
    if (hasDirectVertices) {
        totalVertexCount += data->VertexCount;
    }

    // Check if existing vertex buffer is sufficient
    CKVertexBufferDesc *vbDesc = rst->GetVertexBufferData(m_VertexBuffer);
    if (vbDesc) {
        // Check if format or size changed
        if (vbDesc->m_MaxVertexCount < totalVertexCount || vbDesc->m_VertexFormat != vertexFormat) {
            rst->DeleteObject(m_VertexBuffer, CKRST_OBJ_VERTEXBUFFER);
            rst->DeleteObject(m_IndexBuffer, CKRST_OBJ_INDEXBUFFER);
            needNewBuffer = TRUE;
        }
    } else {
        needNewBuffer = TRUE;
    }

    // Create new vertex buffer if needed
    if (needNewBuffer) {
        CKVertexBufferDesc newDesc;
        newDesc.m_Flags = 21; // CKRST_VB_WRITEONLY | CKRST_VB_DYNAMIC
        newDesc.m_MaxVertexCount = totalVertexCount;
        newDesc.m_VertexFormat = vertexFormat;
        newDesc.m_VertexSize = vertexSize;

        if (!rst->CreateObject(m_VertexBuffer, CKRST_OBJ_VERTEXBUFFER, &newDesc)) {
            return FALSE;
        }
        field_EC = 0; // Mark as needing update
    }

    // If buffer is up to date, just check index buffer and return
    if (field_EC != 0) {
        RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
        if (rm->m_UseIndexBuffers.Value) {
            CheckHWIndexBuffer(rst);
        }
        return TRUE;
    }

    // Lock and fill vertex buffer
    CKBYTE *vbData = (CKBYTE *) rst->LockVertexBuffer(m_VertexBuffer, 0, totalVertexCount, (CKRST_LOCKFLAGS) 0);
    if (!vbData) {
        return FALSE;
    }

    CKDWORD currentOffset = 0;

    // Copy direct vertices first (if any)
    if (hasDirectVertices) {
        CKDWORD count = m_Vertices.Size();
        vbData = CKRSTLoadVertexBuffer(vbData, vertexFormat, vertexSize, data);
        currentOffset += count;
    }

    // Copy remapped vertices for each material group
    if (hasRemappedVertices) {
        VxDrawPrimitiveData localData;
        memcpy(&localData, data, sizeof(VxDrawPrimitiveData));

        for (int i = 0; i < m_MaterialGroups.Size(); i++) {
            CKMaterialGroup *group = m_MaterialGroups[i];
            if (group && group->m_Material) {
                if (group->field_30) {
                    // This group has remapped vertices
                    // field_30 points to remapped vertex array
                    VxVertex *remappedVerts = (VxVertex *) group->field_30;
                    VxColors *remappedColors = (VxColors *) ((CKBYTE *) group->field_30 + 12);

                    // Update local data for this group
                    localData.VertexCount = group->field_2C;
                    localData.PositionPtr = &remappedVerts[0].m_Position;
                    localData.NormalPtr = &remappedVerts[0].m_Normal;
                    localData.TexCoordPtr = &remappedVerts[0].m_UV;
                    localData.ColorPtr = &remappedColors[0].Color;
                    localData.SpecularColorPtr = &remappedColors[0].Specular;

                    // Set up extra tex coords from field_D0
                    for (int t = 0; t < (int) field_D0.Size(); t++) {
                        int channelIdx = (int) (intptr_t) field_D0[t];
                        if (channelIdx >= 0 && channelIdx < m_MaterialChannels.Size()) {
                            // Get UV data for this channel from the remapped vertex array
                            // This assumes the remapped data contains per-channel UVs at offset 24 + channelIdx * sizeof(UV array)
                            localData.TexCoordPtrs[t] = m_MaterialChannels[channelIdx].m_uv;
                            localData.TexCoordStrides[t] = 8;
                        }
                    }

                    // Set the start vertex offset for this group
                    group->field_28 = currentOffset;
                    currentOffset += group->field_2C;

                    // Load this group's vertices into the buffer
                    vbData = CKRSTLoadVertexBuffer(vbData, vertexFormat, vertexSize, &localData);
                } else {
                    // No remapped vertices for this group
                    group->field_28 = 0;
                }
            }
        }
    }

    rst->UnlockVertexBuffer(m_VertexBuffer);

    // Check if we should create index buffer
    RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
    if (rm->m_UseIndexBuffers.Value) {
        CheckHWIndexBuffer(rst);
    }

    return TRUE;
}

//--------------------------------------------
// CheckHWIndexBuffer - Check and setup hardware index buffer
// Returns TRUE if hardware IB was created/updated successfully
//--------------------------------------------
CKBOOL RCKMesh::CheckHWIndexBuffer(CKRasterizerContext *rst) {
    if (!rst)
        return FALSE;

    // Check if rasterizer supports index buffers
    if ((rst->m_Driver->m_3DCaps.CKRasterizerSpecificCaps & 0x10000) == 0)
        return FALSE;

    CKBOOL needNewBuffer = FALSE;
    CKBOOL needUpdate = FALSE;
    CKDWORD totalIndexCount = 0;

    // Count total indices needed and check if any group needs update
    for (int i = 0; i < m_MaterialGroups.Size(); i++) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group && group->m_Material) {
            // Iterate through primitive entries
            for (int p = 0; p < group->field_4.Size(); p++) {
                CKPrimitiveEntry *prim = &group->field_4[p];
                totalIndexCount += prim->m_Indices.Size();
                if (prim->m_IndexBufferOffset < 0) {
                    needUpdate = TRUE;
                }
            }
        }
    }

    // Check if existing index buffer is sufficient
    CKIndexBufferDesc *ibDesc = rst->GetIndexBufferData(m_IndexBuffer);
    CKBOOL needResize = !ibDesc || ibDesc->m_MaxIndexCount < totalIndexCount;

    if (needResize) {
        rst->DeleteObject(m_IndexBuffer, CKRST_OBJ_INDEXBUFFER);

        CKIndexBufferDesc newDesc;
        newDesc.m_Flags = 21; // CKRST_IB_WRITEONLY | CKRST_IB_DYNAMIC
        newDesc.m_MaxIndexCount = totalIndexCount;

        if (!rst->CreateObject(m_IndexBuffer, CKRST_OBJ_INDEXBUFFER, &newDesc)) {
            // Failed to create index buffer - mark all primitives as software
            for (int i = 0; i < m_MaterialGroups.Size(); i++) {
                CKMaterialGroup *group = m_MaterialGroups[i];
                if (group && group->m_Material) {
                    for (int p = 0; p < group->field_4.Size(); p++) {
                        group->field_4[p].m_IndexBufferOffset = -1;
                    }
                }
            }
            return FALSE;
        }
        needUpdate = TRUE;
    }

    if (!needUpdate)
        return TRUE;

    // Lock and fill index buffer
    CKWORD *ibData = (CKWORD *) rst->LockIndexBuffer(m_IndexBuffer, 0, totalIndexCount, CKRST_LOCK_DISCARD);
    if (!ibData) {
        // Failed to lock - mark all primitives as software
        for (int i = 0; i < m_MaterialGroups.Size(); i++) {
            CKMaterialGroup *group = m_MaterialGroups[i];
            if (group && group->m_Material) {
                for (int p = 0; p < group->field_4.Size(); p++) {
                    group->field_4[p].m_IndexBufferOffset = -1;
                }
            }
        }
        return FALSE;
    }

    CKDWORD currentOffset = 0;

    // Copy indices for each material group
    for (int i = 0; i < m_MaterialGroups.Size(); i++) {
        CKMaterialGroup *group = m_MaterialGroups[i];
        if (group && group->m_Material) {
            for (int p = 0; p < group->field_4.Size(); p++) {
                CKPrimitiveEntry *prim = &group->field_4[p];

                // Set the index buffer offset for this primitive
                prim->m_IndexBufferOffset = currentOffset;

                // Copy indices
                CKDWORD indexCount = prim->m_Indices.Size();
                if (indexCount > 0) {
                    memcpy(ibData, prim->m_Indices.Begin(), indexCount * sizeof(CKWORD));
                    ibData += indexCount;
                    currentOffset += indexCount;
                }
            }
        }
    }

    rst->UnlockIndexBuffer(m_IndexBuffer);

    return TRUE;
}
