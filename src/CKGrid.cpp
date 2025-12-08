#include "RCKGrid.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCKLayer.h"
#include "RCKMesh.h"

/**
 * @brief RCKGrid constructor
 * @param Context The CKContext instance
 * @param name Optional name for the grid
 */
RCKGrid::RCKGrid(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_Width(0),
      m_Length(0),
      m_Priority(0),
      m_OrientationMode(0),
      m_Mesh(nullptr) {
    // Initialize layers array
}

/**
 * @brief RCKGrid destructor
 */
RCKGrid::~RCKGrid() {
    // Cleanup resources if needed
}

/**
 * @brief Get the class ID for RCKGrid
 * @return The class ID (50)
 */
CK_CLASSID RCKGrid::GetClassID() {
    return 50;
}


/**
 * @brief Save the grid data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKGrid::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // If no file and no special flags, return base chunk only
    if (!file && (flags & 0xFC00000) == 0)
        return baseChunk;

    // Create a new state chunk for grid data
    CKStateChunk *chunk = CreateCKStateChunk(50, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write grid specific data
    chunk->WriteIdentifier(0x400000u); // Grid identifier

    // Write grid properties
    chunk->WriteInt(m_Width);
    chunk->WriteInt(m_Length);
    chunk->WriteInt(0); // Reserved field
    chunk->WriteInt(m_Priority);
    chunk->WriteDword(m_OrientationMode);

    // Write file context flag if saving to file
    if (file) {
        chunk->WriteInt(1);
    }

    // Save layers array
    // Convert CKLayer array to XObjectPointerArray for saving
    XObjectPointerArray objectArray;
    for (int i = 0; i < m_Layers.Size(); ++i) {
        objectArray.PushBack(reinterpret_cast<CKObject *>(m_Layers[i]));
    }
    objectArray.Save(chunk);

    // If not saving to file, save layer sub-chunks
    if (!file) {
        for (int i = 0; i < m_Layers.Size(); ++i) {
            CKObject *layerObject = reinterpret_cast<CKObject *>(m_Layers[i]);
            if (layerObject) {
                CKStateChunk *layerChunk = layerObject->Save(nullptr, flags);
                chunk->WriteSubChunk(layerChunk);
                DeleteCKStateChunk(layerChunk);
            }
        }
    }

    // Close or update the chunk based on class ID
    if (GetClassID() == 50)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Load grid data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKGrid::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    // Load grid data if identifier is found
    if (chunk->SeekIdentifier(0x400000u)) {
        // Read grid properties
        m_Width = chunk->ReadInt();
        m_Length = chunk->ReadInt();
        chunk->ReadInt(); // Skip reserved field
        m_Priority = chunk->ReadInt();
        m_OrientationMode = chunk->ReadDword();

        // Read file context flag if loading from file
        if (file) {
            chunk->ReadInt();
        }

        // Load layers array
        XObjectPointerArray objectArray;
        objectArray.Load(m_Context, chunk);

        // Convert back to CKLayer array
        m_Layers.Clear();
        for (int i = 0; i < objectArray.Size(); ++i) {
            CKLayer *layer = reinterpret_cast<CKLayer *>(objectArray[i]);
            if (layer) {
                m_Layers.PushBack(layer);
            }
        }

        // If not loading from file, load layer sub-chunks
        if (!file) {
            for (int i = 0; i < m_Layers.Size(); ++i) {
                CKObject *layerObject = reinterpret_cast<CKObject *>(m_Layers[i]);
                CKStateChunk *subChunk = chunk->ReadSubChunk();
                if (layerObject) {
                    layerObject->Load(subChunk, nullptr);
                }
                DeleteCKStateChunk(subChunk);
            }
        }

        // Check and validate loaded layers
        objectArray.Check();
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the grid
 * @return Memory size in bytes
 */
int RCKGrid::GetMemoryOccupation() {
    return sizeof(RCKGrid) + m_Layers.Size() * sizeof(CKLayer *);
}

/**
 * @brief Copy grid data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKGrid::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    RCK3dEntity::Copy(o, context);

    // Copy grid specific data
    RCKGrid &target = (RCKGrid &) o;
    target.m_Width = m_Width;
    target.m_Length = m_Length;
    target.m_Priority = m_Priority;
    target.m_OrientationMode = m_OrientationMode;
    target.m_Mesh = m_Mesh;

    // Copy layers (shallow copy)
    target.m_Layers = m_Layers;

    return CK_OK;
}

// Static class registration methods
CKSTRING RCKGrid::GetClassName() {
    return "Grid";
}

int RCKGrid::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKGrid::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKGrid::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_LAYER);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_3DENTITY); // Grid uses 3DENTITY GUID
    CKClassRegisterDefaultDependencies(m_ClassID, 1, CK_DEPENDENCIES_COPY);
}

CKGrid *RCKGrid::CreateInstance(CKContext *Context) {
    return reinterpret_cast<CKGrid *>(new RCKGrid(Context));
}

// =====================================================
// Additional Grid Methods
// =====================================================

void RCKGrid::ConstructMeshTexture(float scale) {
    // Construct the mesh texture for the grid
}

void RCKGrid::DestroyMeshTexture() {
    // Destroy the mesh texture
}

CKBOOL RCKGrid::IsActive() {
    return TRUE;
}

void RCKGrid::SetHeightValidity(float val) {
    // Set height validity threshold
}

float RCKGrid::GetHeightValidity() {
    return 0.0f;
}

int RCKGrid::GetWidth() {
    return m_Width;
}

int RCKGrid::GetLength() {
    return m_Length;
}

void RCKGrid::SetDimensions(int width, int length, float cellWidth, float cellLength) {
    m_Width = width;
    m_Length = length;
}

float RCKGrid::Get2dCoordsFrom3dPos(const VxVector *pos, int *x, int *y) {
    if (!pos || !x || !y) return 0.0f;
    *x = 0;
    *y = 0;
    return 0.0f;
}

void RCKGrid::Get3dPosFrom2dCoords(VxVector *pos, int x, int y) {
    if (pos) {
        pos->x = (float) x;
        pos->y = 0.0f;
        pos->z = (float) y;
    }
}

CKERROR RCKGrid::AddClassification(int classType) {
    return CK_OK;
}

CKERROR RCKGrid::AddClassificationByName(char *name) {
    return CK_OK;
}

CKERROR RCKGrid::RemoveClassification(int classType) {
    return CK_OK;
}

CKERROR RCKGrid::RemoveClassificationByName(char *name) {
    return CK_OK;
}

CKBOOL RCKGrid::HasCompatibleClass(CK3dEntity *entity) {
    return TRUE;
}

void RCKGrid::SetGridPriority(int priority) {
    m_Priority = priority;
}

int RCKGrid::GetGridPriority() {
    return m_Priority;
}

void RCKGrid::SetOrientationMode(CK_GRIDORIENTATION mode) {
    m_OrientationMode = mode;
}

CK_GRIDORIENTATION RCKGrid::GetOrientationMode() {
    return static_cast<CK_GRIDORIENTATION>(m_OrientationMode);
}

CKLayer *RCKGrid::AddLayer(int type, int format) {
    return nullptr;
}

CKLayer *RCKGrid::AddLayerByName(char *name, int format) {
    return nullptr;
}

CKLayer *RCKGrid::GetLayer(int type) {
    return nullptr;
}

CKLayer *RCKGrid::GetLayerByName(char *name) {
    return nullptr;
}

int RCKGrid::GetLayerCount() {
    return m_Layers.Size();
}

CKLayer *RCKGrid::GetLayerByIndex(int index) {
    if (index >= 0 && index < m_Layers.Size()) {
        return m_Layers[index];
    }
    return nullptr;
}

CKERROR RCKGrid::RemoveLayer(int type) {
    return CK_OK;
}

CKERROR RCKGrid::RemoveLayerByName(char *name) {
    return CK_OK;
}

CKERROR RCKGrid::RemoveAllLayers() {
    m_Layers.Clear();
    return CK_OK;
}

// Static class ID
CK_CLASSID RCKGrid::m_ClassID = CKCID_GRID;
