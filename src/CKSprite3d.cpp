#include "RCKSprite3D.h"
#include "RCK3dEntity.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKSceneGraph.h"
#include "CKRasterizerTypes.h"
#include "VxMath.h"

/*************************************************
Summary: PreSave method for RCKSprite3D.
Purpose: Prepares sprite for saving by handling material dependencies.
Remarks:
- Calls base class RCK3dEntity::PreSave() first to handle entity dependencies
- Saves the material object for later serialization
- Ensures material is properly saved with the sprite data

Implementation based on decompilation at 0x1004326A:
- Simple implementation that only saves the material object
- Uses CKFile::SaveObject for dependency handling

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior and dependency handling
*************************************************/
void RCKSprite3D::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first to handle entity dependencies
    RCK3dEntity::PreSave(file, flags);

    // Save material object
    file->SaveObject((CKObject *) m_Material, flags);
}

/*************************************************
Summary: Save method for RCKSprite3D.
Purpose: Saves sprite data to a state chunk including mode, size, offset, UV mapping, and material.
Remarks:
- Calls base class RCK3dEntity::Save() first to handle entity data
- Creates sprite-specific state chunk with identifier 0x400000
- Saves sprite mode (billboard, fixed, etc.)
- Calculates and saves sprite size from bounding box
- Saves offset position for sprite placement
- Saves UV mapping rectangle coordinates
- Saves material object reference

Implementation based on decompilation at 0x1004329D:
- Uses chunk identifier 0x400000 for sprite-specific data
- Calculates size from local bounding box dimensions
- Saves offset as 2D vector
- Saves UV mapping rectangle as four floats
- Saves material object reference

Arguments:
- file: The file context for saving (may be NULL for memory operations)
- flags: Save flags controlling behavior and data inclusion
Return Value:
- CKStateChunk*: The created state chunk containing sprite data
*************************************************/
CKStateChunk *RCKSprite3D::Save(CKFile *file, CKDWORD flags) {
    // Call base class save first to handle entity data
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // Return early if no file context and not in specific save modes
    if (!file && (flags & 0xFC00000) == 0) {
        return baseChunk;
    }

    // Create sprite-specific state chunk
    CKStateChunk *spriteChunk = CreateCKStateChunk(CKCID_SPRITE3D, file);
    if (!spriteChunk) {
        return baseChunk;
    }

    spriteChunk->StartWrite();
    spriteChunk->AddChunkAndDelete(baseChunk);

    // Write sprite-specific data with identifier 0x400000
    spriteChunk->WriteIdentifier(0x400000);
    spriteChunk->WriteDword(m_Mode);

    // Calculate size from bounding box (half width and height)
    float halfWidth = (m_LocalBoundingBox.Max.x - m_LocalBoundingBox.Min.x) * 0.5f;
    float halfHeight = (m_LocalBoundingBox.Max.y - m_LocalBoundingBox.Min.y) * 0.5f;
    spriteChunk->WriteFloat(halfWidth);
    spriteChunk->WriteFloat(halfHeight);

    // Save offset position
    spriteChunk->WriteFloat(m_Offset.x);
    spriteChunk->WriteFloat(m_Offset.y);

    // Save UV mapping rectangle
    spriteChunk->WriteFloat(m_Rect.left);
    spriteChunk->WriteFloat(m_Rect.top);
    spriteChunk->WriteFloat(m_Rect.right);
    spriteChunk->WriteFloat(m_Rect.bottom);

    // Save material object
    spriteChunk->WriteObject((CKObject *) m_Material);

    // Close chunk appropriately based on class type
    if (GetClassID() == CKCID_SPRITE3D) {
        spriteChunk->CloseChunk();
    } else {
        spriteChunk->UpdateDataSize();
    }

    return spriteChunk;
}

/*************************************************
Summary: Load method for RCKSprite3D.
Purpose: Loads sprite data from a state chunk including mode, size, offset, UV mapping, and material.
Remarks:
- Calls base class RCK3dEntity::Load() first to handle entity data
- Loads sprite mode and calculates full size from half dimensions
- Sets sprite size and offset using appropriate methods
- Loads UV mapping rectangle and applies it to the sprite
- Loads material object reference
- Sets object flags to mark sprite as modified after loading

Implementation based on decompilation at 0x1004340F:
- Uses chunk identifier 0x400000 for sprite-specific data
- Converts half dimensions to full size for SetSize method
- Uses SetOffset and SetUVMapping methods for proper initialization
- Sets object flag 0x400 to indicate sprite modification

Arguments:
- chunk: The state chunk containing sprite data
- file: The file context for loading (may be NULL)
Return Value:
- CKERROR: 0 for success, -1 for invalid chunk
*************************************************/
CKERROR RCKSprite3D::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) {
        return CKERR_INVALIDPARAMETER;
    }

    // Call base class load first to handle entity data
    RCK3dEntity::Load(chunk, file);

    if (chunk->SeekIdentifier(0x400000)) {
        // Load sprite mode
        m_Mode = chunk->ReadDword();

        // Load half dimensions and calculate full size
        float halfWidth = chunk->ReadFloat();
        float halfHeight = chunk->ReadFloat();
        Vx2DVector size(halfWidth * 2.0f, halfHeight * 2.0f);
        SetSize(size);

        // Load offset position
        m_Offset.x = chunk->ReadFloat();
        m_Offset.y = chunk->ReadFloat();
        Vx2DVector offset(m_Offset.x, m_Offset.y);
        SetOffset(offset);

        // Load UV mapping rectangle
        m_Rect.left = chunk->ReadFloat();
        m_Rect.top = chunk->ReadFloat();
        m_Rect.right = chunk->ReadFloat();
        m_Rect.bottom = chunk->ReadFloat();
        SetUVMapping(m_Rect);

        // Load material object
        m_Material = (RCKMaterial *) chunk->ReadObject(m_Context);

        // Mark sprite as modified after loading
        ModifyObjectFlags(0, 0x400);
    }

    return CK_OK;
}

RCKSprite3D::RCKSprite3D(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name), m_Material(nullptr), m_Mode(0) {
    // Initialize offset to center
    m_Offset.x = 0.0f;
    m_Offset.y = 0.0f;

    // Initialize local bounding box for 2x2 sprite centered at origin
    m_LocalBoundingBox.Min.x = -1.0f;
    m_LocalBoundingBox.Min.y = -1.0f;
    m_LocalBoundingBox.Min.z = 0.0f;
    m_LocalBoundingBox.Max.x = 1.0f;
    m_LocalBoundingBox.Max.y = 1.0f;
    m_LocalBoundingBox.Max.z = 0.0f;

    // Initialize UV mapping to full texture
    m_Rect.left = 0.0f;
    m_Rect.top = 0.0f;
    m_Rect.right = 1.0f;
    m_Rect.bottom = 1.0f;
}

RCKSprite3D::~RCKSprite3D() {
}

CK_CLASSID RCKSprite3D::m_ClassID = CKCID_SPRITE3D;

CK_CLASSID RCKSprite3D::GetClassID() {
    return m_ClassID;
}

int RCKSprite3D::GetMemoryOccupation() {
    // Base class memory + Sprite3D specific members (32 bytes)
    // m_Material (4) + m_Mode (4) + m_Offset (8) + m_Rect (16) = 32
    return RCK3dEntity::GetMemoryOccupation() + 32;
}

CKERROR RCKSprite3D::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCK3dEntity::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKSprite3D *src = (RCKSprite3D *) &o;

    m_Material = src->m_Material;
    m_LocalBoundingBox = src->m_LocalBoundingBox;
    m_Offset = src->m_Offset;
    m_Rect = src->m_Rect;
    m_Mode = src->m_Mode;

    return CK_OK;
}

void RCKSprite3D::SetMaterial(CKMaterial *mat) {
    m_Material = (RCKMaterial *) mat;
}

CKMaterial *RCKSprite3D::GetMaterial() {
    return (CKMaterial *) m_Material;
}

void RCKSprite3D::SetSize(Vx2DVector &size) {
    // Reset Z bounds
    m_LocalBoundingBox.Max.z = 0.0f;
    m_LocalBoundingBox.Min.z = 0.0f;

    // Calculate bounds based on offset and size
    // Offset is in range [-1, 1], where 0 means centered
    float halfWidth = size.x * 0.5f;
    float halfHeight = size.y * 0.5f;

    m_LocalBoundingBox.Min.x = (m_Offset.x - 1.0f) * halfWidth;
    m_LocalBoundingBox.Min.y = (m_Offset.y - 1.0f) * halfHeight;
    m_LocalBoundingBox.Max.x = (m_Offset.x + 1.0f) * halfWidth;
    m_LocalBoundingBox.Max.y = (m_Offset.y + 1.0f) * halfHeight;

    // Invalidate scene graph bounding box
    if (m_SceneGraphNode)
        m_SceneGraphNode->InvalidateBox(TRUE);
}

void RCKSprite3D::GetSize(Vx2DVector &size) {
    size.x = m_LocalBoundingBox.Max.x - m_LocalBoundingBox.Min.x;
    size.y = m_LocalBoundingBox.Max.y - m_LocalBoundingBox.Min.y;
}

void RCKSprite3D::SetOffset(Vx2DVector &offset) {
    m_Offset = offset;

    // Calculate current half size
    float halfWidth = (m_LocalBoundingBox.Max.x - m_LocalBoundingBox.Min.x) * 0.5f;
    float halfHeight = (m_LocalBoundingBox.Max.y - m_LocalBoundingBox.Min.y) * 0.5f;

    // Reset Z bounds
    m_LocalBoundingBox.Max.z = 0.0f;
    m_LocalBoundingBox.Min.z = 0.0f;

    // Recalculate bounds based on new offset
    m_LocalBoundingBox.Min.x = (m_Offset.x - 1.0f) * halfWidth;
    m_LocalBoundingBox.Min.y = (m_Offset.y - 1.0f) * halfHeight;
    m_LocalBoundingBox.Max.x = (m_Offset.x + 1.0f) * halfWidth;
    m_LocalBoundingBox.Max.y = (m_Offset.y + 1.0f) * halfHeight;

    // Invalidate scene graph bounding box
    if (m_SceneGraphNode)
        m_SceneGraphNode->InvalidateBox(TRUE);
}

void RCKSprite3D::GetOffset(Vx2DVector &offset) {
    offset = m_Offset;
}

void RCKSprite3D::SetUVMapping(VxRect &rect) {
    m_Rect = rect;
}

void RCKSprite3D::GetUVMapping(VxRect &rect) {
    rect = m_Rect;
}

void RCKSprite3D::SetMode(VXSPRITE3D_TYPE mode) {
    m_Mode = mode;
}

VXSPRITE3D_TYPE RCKSprite3D::GetMode() {
    return (VXSPRITE3D_TYPE) m_Mode;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CKSTRING RCKSprite3D::GetClassName() {
    return (CKSTRING) "3D Sprite";
}

int RCKSprite3D::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKSprite3D::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKSprite3D::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(RCKSprite3D::m_ClassID, CKCID_MATERIAL);

    // Register associated parameter GUID: CKPGUID_SPRITE3D
    CKClassRegisterAssociatedParameter(RCKSprite3D::m_ClassID, CKPGUID_SPRITE3D);

    // Register default dependencies
    CKClassRegisterDefaultDependencies(RCKSprite3D::m_ClassID, 1, CK_DEPENDENCIES_COPY);
}

CKSprite3D *RCKSprite3D::CreateInstance(CKContext *Context) {
    RCKSprite3D *sprite = new RCKSprite3D(Context, nullptr);
    return reinterpret_cast<CKSprite3D *>(sprite);
}

//=============================================================================
// FillBatch - Fills a sprite batch with vertex data
// Based on IDA decompilation at 0x100428A8
//=============================================================================

void RCKSprite3D::FillBatch(CKSprite3DBatch *batch) {
    if (!batch)
        return;
    
    // Check if clipping is needed from scene graph node
    // If the node's flag bit 0 is not set, we need clipping (from IDA: m_Flags & 1)
    CKSceneGraphNode *node = (CKSceneGraphNode *)m_SceneGraphNode;
    if (node) {
        // Set batch flag if clipping needed (flag bit 0 == 0 means not inside frustum)
        batch->m_Flags |= ((node->m_Flags & 1) == 0) ? 1 : 0;
    }
    
    // Expand vertex array by 4 vertices (one quad for the sprite)
    // This adds 4 vertices (128 bytes since CKVertex is 32 bytes)
    int currentSize = batch->m_Vertices.Size();
    batch->m_Vertices.Resize(currentSize + 4);
    
    // Get pointer to the 4 new vertices at the end
    CKVertex *vertices = batch->m_Vertices.Begin() + currentSize;
    
    // Get sprite dimensions from local bounding box
    float width = m_LocalBoundingBox.Max.x - m_LocalBoundingBox.Min.x;
    float height = m_LocalBoundingBox.Max.y - m_LocalBoundingBox.Min.y;
    
    // Get world matrix vectors (columns 0, 1, 3)
    VxVector xAxis(m_WorldMatrix[0][0], m_WorldMatrix[0][1], m_WorldMatrix[0][2]);
    VxVector yAxis(m_WorldMatrix[1][0], m_WorldMatrix[1][1], m_WorldMatrix[1][2]);
    VxVector position(m_WorldMatrix[3][0], m_WorldMatrix[3][1], m_WorldMatrix[3][2]);
    
    // Scale axes by dimensions
    VxVector scaledX = xAxis * width;
    VxVector scaledY = yAxis * height;
    
    // Calculate offset multipliers (offset goes from -1 to 1, convert to 0 to 1 range)
    float offsetX = (m_Offset.x - 1.0f) * 0.5f;
    float offsetY = (m_Offset.y - 1.0f) * 0.5f;
    
    // Calculate corner offset vectors
    VxVector xOffset = scaledX * offsetX;
    VxVector yOffset = scaledY * offsetY;
    
    // Calculate base corner position (bottom-left based on offset)
    VxVector basePos = position + xOffset + yOffset;
    
    // Calculate 4 corner positions
    // Vertex 0: base position (bottom-left)
    vertices[0].V.x = basePos.x;
    vertices[0].V.y = basePos.y;
    vertices[0].V.z = basePos.z;
    vertices[0].V.w = 1.0f;
    
    // Vertex 1: base + Y (top-left)
    VxVector pos1 = basePos + scaledY;
    vertices[1].V.x = pos1.x;
    vertices[1].V.y = pos1.y;
    vertices[1].V.z = pos1.z;
    vertices[1].V.w = 1.0f;
    
    // Vertex 2: base + X + Y (top-right)
    VxVector pos2 = pos1 + scaledX;
    vertices[2].V.x = pos2.x;
    vertices[2].V.y = pos2.y;
    vertices[2].V.z = pos2.z;
    vertices[2].V.w = 1.0f;
    
    // Vertex 3: base + X (bottom-right)
    VxVector pos3 = basePos + scaledX;
    vertices[3].V.x = pos3.x;
    vertices[3].V.y = pos3.y;
    vertices[3].V.z = pos3.z;
    vertices[3].V.w = 1.0f;
    
    // Set texture coordinates from m_Rect
    // Vertex 0: left, bottom
    vertices[0].tu = m_Rect.left;
    vertices[0].tv = m_Rect.bottom;
    
    // Vertex 1: left, top
    vertices[1].tu = m_Rect.left;
    vertices[1].tv = m_Rect.top;
    
    // Vertex 2: right, top
    vertices[2].tu = m_Rect.right;
    vertices[2].tv = m_Rect.top;
    
    // Vertex 3: right, bottom
    vertices[3].tu = m_Rect.right;
    vertices[3].tv = m_Rect.bottom;
}
