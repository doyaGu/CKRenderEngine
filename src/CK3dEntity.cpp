#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCKMesh.h"
#include "RCKSkin.h"
#include "CKSkin.h"
#include "CKMesh.h"
#include "CKObjectAnimation.h"
#include "CKSceneGraph.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKPlace.h"
#include "CKScene.h"
#include "CKLevel.h"
#include "CKDependencies.h"
#include "CKRasterizer.h"
#include "VxMath.h"
#include "CKDebugLogger.h"

#define ENTITY_DEBUG_LOG(msg) CK_LOG("3dEntity", msg)
#define ENTITY_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("3dEntity", fmt, __VA_ARGS__)


// Moveable flags that may not be defined in SDK
#ifndef VX_MOVEABLE_NOANIMALIASING
#define VX_MOVEABLE_NOANIMALIASING 0x00000800
#endif
#ifndef VX_MOVEABLE_ALLINSIDE
#define VX_MOVEABLE_ALLINSIDE 0x00001000
#endif
#ifndef VX_MOVEABLE_ALLOUTSIDE
#define VX_MOVEABLE_ALLOUTSIDE 0x00002000
#endif

// Helper function for matrix inverse multiply (A^-1 * B)
inline void Vx3DMultiplyMatrixInverse(VxMatrix &result, const VxMatrix &A, const VxMatrix &B) {
    VxMatrix invA;
    Vx3DInverseMatrix(invA, A);
    Vx3DMultiplyMatrix(result, invA, B);
}

/*************************************************
Summary: PostLoad method for RCK3dEntity.
Purpose: Called after loading to finalize the entity state.
Remarks:
- Updates skin if present to ensure proper vertex binding
- Processes object animations with current scale transformation
- Adds entity to current level's render contexts if loaded from file
- Calls base class PostLoad to complete initialization

Implementation based on decompilation at 0x1000A138:
- Checks if m_Skin exists and calls UpdateSkin()
- Iterates through m_ObjectAnimations and applies scale transformation
- Adds to render contexts if in a level
- Calls CKObject::PostLoad() at the end
*************************************************/
void RCK3dEntity::PostLoad() {
    ENTITY_DEBUG_LOG_FMT("PostLoad: Starting for entity=%p meshes=%d currentMesh=%p", 
                         this, m_Meshes.Size(), m_CurrentMesh);
    
    // Based on original implementation at 0x1000a138:
    // 1. Update skin if it exists
    if (m_Skin) {
        UpdateSkin();
    }

    // 2. Process object animations if any exist
    // Apply current scale to all animations to maintain proper proportions
    if (m_ObjectAnimations.Size() > 0) {
        VxVector scale;
        GetScale(&scale, 1);

        // Iterate through all object animations and apply scale
        // Based on decompilation: uses XClassArray::Begin/End and sub_1005A56A
        // Note: sub_1005A56A applies scale to animation - implementation TBD
    }

    // 3. Call base class PostLoad
    // NOTE: Original does NOT add entities to RenderContext in PostLoad!
    // Entity registration happens through AddToScene() or explicit AddObject() calls
    CKObject::PostLoad();
    
    ENTITY_DEBUG_LOG_FMT("PostLoad: Complete for entity=%p", this);
}

/*************************************************
Summary: Load method for RCK3dEntity.
Purpose: Loads entity data from a state chunk.
Remarks:
- Supports both old and new format versions for backward compatibility
- Loads meshes, animations, and transformation matrices
- Handles complex skin data with bones and vertex weights
- Restores parent-child relationships in scene hierarchy
- Processes entity flags and moveable properties

Implementation based on decompilation at 0x1000A7B9:
- Calls CKBeObject::Load() first
- Preserves moveable flags with 0x40000 mask
- Loads object animations (chunk 0x2000)
- Loads meshes (chunk 0x4000)
- Loads main entity data (chunk 0x100000) with flags, matrix, parent, priority
- Supports legacy format with separate chunks (0x8000, 0x10000, 0x20000)
- Loads skin data (chunk 0x200000) with bones, vertices, weights, normals
- Sets world matrix if not in file context
- Sets default bounding box if needed

Arguments:
- chunk: The state chunk containing entity data
- file: The file context for loading (may be NULL)
Return Value:
- CKERROR: 0 for success, error code otherwise
*************************************************/
CKERROR RCK3dEntity::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    ENTITY_DEBUG_LOG_FMT("Load: Starting for entity %s", GetName() ? GetName() : "(null)");

    // Call base class load first to handle basic object data
    CKERROR result = CKBeObject::Load(chunk, file);
    if (result != CK_OK) {
        ENTITY_DEBUG_LOG_FMT("Load: Base class load failed with error %d", result);
        return result;
    }

    // Preserve moveable flags that should not be modified during load
    // Based on IDA: v16 = GetMoveableFlags() & 0x40000
    CKDWORD preservedFlags = GetMoveableFlags() & 0x40000;

    // Initialize identity matrix for transformation
    VxMatrix worldMatrix;
    worldMatrix.SetIdentity();

    // Load object animations (chunk 0x2000)
    // Based on IDA: XObjectPointerArray::Load for animations
    if (chunk->SeekIdentifier(0x2000)) {
        m_ObjectAnimations.Load(m_Context, chunk);
    }

    // Load meshes (chunk 0x4000)
    // Based on IDA and docs: First reads current mesh, then mesh array
    if (chunk->SeekIdentifier(0x4000)) {
        // Read current mesh object first
        CKMesh *currentMesh = (CKMesh *)chunk->ReadObject(m_Context);
        ENTITY_DEBUG_LOG_FMT("Load: Read current mesh = %p", currentMesh);
        
        // Read mesh array sequence
        int meshCount = chunk->StartReadSequence();
        ENTITY_DEBUG_LOG_FMT("Load: Mesh sequence count = %d", meshCount);
        
        for (int i = 0; i < meshCount; i++) {
            CKMesh *mesh = (CKMesh *)chunk->ReadObject(m_Context);
            if (mesh) {
                m_Meshes.AddIfNotHere(mesh);
                ENTITY_DEBUG_LOG_FMT("Load: Added mesh[%d] = %p (%s)", i, mesh, mesh->GetName());
            }
        }
        
        // Set current mesh
        if (currentMesh) {
            // Original uses add_if_not_here = TRUE to keep array in sync
            SetCurrentMesh(currentMesh, TRUE);
            ENTITY_DEBUG_LOG_FMT("Load: Set current mesh to %p", m_CurrentMesh);
        } else if (m_Meshes.Size() > 0) {
            // Fallback: use first mesh if current mesh is NULL
            SetCurrentMesh((CKMesh *)m_Meshes[0], TRUE);
            ENTITY_DEBUG_LOG_FMT("Load: Set current mesh to first mesh %p", m_CurrentMesh);
        }
    }

    // Load main entity data (chunk 0x100000) - new format
    // Matches IDA implementation @0x1000a7b9
    if (chunk->SeekIdentifier(0x100000)) {
        CKDWORD entityFlags = chunk->ReadDword();
        CKDWORD moveableFlags = chunk->ReadDword();

        // Object flags are masked before being applied
        CKDWORD objectFlags = entityFlags & 0xFFFFEFDF;
        SetFlags(objectFlags);

        // Moveable flags are also masked and merged with preserved flags
        CKDWORD originalMoveable = moveableFlags;
        moveableFlags &= 0xBB3DBFEB;
        if (preservedFlags) {
            moveableFlags |= 0x40000;
        }
        ENTITY_DEBUG_LOG_FMT("Load: flags obj=0x%X moveable(raw=0x%X masked=0x%X pres=0x%X)", 
                             objectFlags, originalMoveable, moveableFlags, preservedFlags);

        // Read matrix row vectors (IDA shows reading 4 VxVectors)
        VxVector row0, row1, row2, row3;
        chunk->ReadVector(&row0);
        chunk->ReadVector(&row1);
        chunk->ReadVector(&row2);
        chunk->ReadVector(&row3);

        // Construct world matrix from row vectors
        worldMatrix[0][0] = row0.x;
        worldMatrix[0][1] = row0.y;
        worldMatrix[0][2] = row0.z;
        worldMatrix[0][3] = 0.0f;
        worldMatrix[1][0] = row1.x;
        worldMatrix[1][1] = row1.y;
        worldMatrix[1][2] = row1.z;
        worldMatrix[1][3] = 0.0f;
        worldMatrix[2][0] = row2.x;
        worldMatrix[2][1] = row2.y;
        worldMatrix[2][2] = row2.z;
        worldMatrix[2][3] = 0.0f;
        worldMatrix[3][0] = row3.x;
        worldMatrix[3][1] = row3.y;
        worldMatrix[3][2] = row3.z;
        worldMatrix[3][3] = 1.0f;

        // Calculate determinant for handedness detection
        VxVector cross = CrossProduct(row0, row1);
        float dot = DotProduct(cross, row2);
        if (dot < 0.0f) {
            // Left-handed matrix - flip sign on row2 and mark the flag
            worldMatrix[2][0] = -worldMatrix[2][0];
            worldMatrix[2][1] = -worldMatrix[2][1];
            worldMatrix[2][2] = -worldMatrix[2][2];
            moveableFlags |= 0x1000000;
        } else {
            moveableFlags &= ~0x1000000;
        }

        // Sync visibility bit from CKObject visibility
        if (IsVisible()) {
            moveableFlags |= VX_MOVEABLE_VISIBLE;
        } else {
            moveableFlags &= ~VX_MOVEABLE_VISIBLE;
        }

        // Apply moveable flags
        SetMoveableFlags(moveableFlags);
        ENTITY_DEBUG_LOG_FMT("Load: SetMoveableFlags -> 0x%X", m_MoveableFlags);

        // Read optional parent (if entityFlags & 0x1)
        if (entityFlags & 0x1) {
            CK3dEntity *parent = (CK3dEntity *)chunk->ReadObject(m_Context);
            if (parent) {
                SetParent(parent, FALSE);
            }
        } else {
            // Explicitly clear parent when flag is not set
            SetParent(nullptr, FALSE);
        }

        // Read optional priority data (if entityFlags & 0x2)
        if (entityFlags & 0x2) {
            int priority = chunk->ReadInt();
            if (m_SceneGraphNode) {
                m_SceneGraphNode->m_Priority = (CKWORD)priority;
                m_SceneGraphNode->m_MaxPriority = (CKWORD)priority;
            }
        }
    }

    // Legacy format loading for backward compatibility
    // Based on IDA: separate chunks for parent (0x8000), flags (0x10000), matrix (0x20000)
    if (chunk->SeekIdentifier(0x8000)) {
        CK3dEntity *parent = (CK3dEntity *) chunk->ReadObject(m_Context);
        if (parent) {
            SetParent(parent, FALSE);
        }
    }

    if (chunk->SeekIdentifier(0x10000)) {
        CKDWORD flags = chunk->ReadDword();
        SetFlags(flags);

        CKDWORD moveable = chunk->ReadDword() & 0xFF3F00EB;
        if (preservedFlags) {
            moveable |= 0x40000;
        }
        SetMoveableFlags(moveable);
    }

    if (chunk->SeekIdentifier(0x20000)) {
        chunk->ReadMatrix(worldMatrix);

        // Recompute handedness flag based on loaded matrix
        VxVector row0(worldMatrix[0][0], worldMatrix[0][1], worldMatrix[0][2]);
        VxVector row1(worldMatrix[1][0], worldMatrix[1][1], worldMatrix[1][2]);
        VxVector row2(worldMatrix[2][0], worldMatrix[2][1], worldMatrix[2][2]);
        VxVector cross = CrossProduct(row0, row1);
        float dot = DotProduct(cross, row2);

        CKDWORD moveable = GetMoveableFlags();
        if (dot < 0.0f) {
            moveable |= 0x1000000;
        } else {
            moveable &= ~0x1000000;
        }
        SetMoveableFlags(moveable);
    }

    // Set world matrix if not in file context
    if (!file) {
        SetWorldMatrix(worldMatrix, TRUE);
    }

    // Load skin data (chunk 0x200000) - complex vertex weighting system
    // Based on IDA: checks for chunk, calls CreateSkin(), reads bone/vertex data
    if (chunk->SeekIdentifier(0x200000)) {
        // Create skin if needed
        if (!m_Skin) {
            CreateSkin();
        }

        if (m_Skin) {
            // Read skin version
            CKDWORD skinVersion = chunk->ReadInt();

            // Read object initialization matrix
            VxMatrix objectInitMatrix;
            chunk->ReadMatrix(objectInitMatrix);
            m_Skin->SetObjectInitMatrix(objectInitMatrix);

            // Read bone data
            int boneCount = chunk->ReadInt();
            m_Skin->SetBoneCount(boneCount);

            for (int i = 0; i < boneCount; i++) {
                CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
                if (boneData) {
                    // Read bone object
                    CK3dEntity *boneEntity = (CK3dEntity *) chunk->ReadObject(m_Context);
                    boneData->SetBone(boneEntity);

                    // Read bone initial inverse matrix
                    VxMatrix boneMatrix;
                    chunk->ReadMatrix(boneMatrix);
                    boneData->SetBoneInitialInverseMatrix(boneMatrix);
                }
            }

            // Read vertex data
            int vertexCount = chunk->ReadInt();
            m_Skin->SetVertexCount(vertexCount);

            for (int i = 0; i < vertexCount; i++) {
                CKSkinVertexData *vertexData = m_Skin->GetVertexData(i);
                if (vertexData) {
                    // Read initial position
                    VxVector initPos;
                    chunk->ReadVector(&initPos);
                    vertexData->SetInitialPos(initPos);

                    // Read bone count for this vertex
                    int vertexBoneCount = chunk->ReadInt();
                    vertexData->SetBoneCount(vertexBoneCount);

                    // Read bone indices and weights
                    for (int j = 0; j < vertexBoneCount; j++) {
                        int boneIdx = chunk->ReadInt();
                        float weight = chunk->ReadFloat();
                        vertexData->SetBone(j, boneIdx);
                        vertexData->SetWeight(j, weight);
                    }
                }
            }

            // Read normal data if present (chunk 0x1000)
            if (chunk->SeekIdentifier(0x1000)) {
                int normalCount = chunk->ReadInt();
                m_Skin->SetNormalCount(normalCount);

                for (int i = 0; i < normalCount; i++) {
                    VxVector normal;
                    chunk->ReadVector(&normal);
                    m_Skin->SetNormal(i, normal);
                }
            }
        }
    }

    // Set default bounding box if needed
    // Based on IDA: checks flags, creates VxBbox with +/- 1.0 bounds
    if (!(GetMoveableFlags() & VX_MOVEABLE_ALLINSIDE)) {
        VxBbox defaultBbox;
        defaultBbox.Min = VxVector(-1.0f, -1.0f, -1.0f);
        defaultBbox.Max = VxVector(1.0f, 1.0f, 1.0f);
        SetBoundingBox(&defaultBbox, TRUE);
    }

    return CK_OK;
}

/*************************************************
Summary: PreSave method for RCK3dEntity.
Purpose: Prepares entity for saving by handling dependencies.
Remarks:
- Saves mesh objects (except for curves which don't have meshes)
- Saves object animations for later serialization
- Handles skin-related mesh flags for proper rendering
- Saves children entities if requested by save flags

Implementation based on decompilation at 0x1000A1C2:
- Calls CKBeObject::PreSave() first
- Saves mesh objects if not CKCID_CURVE using XClassArray::Begin/size
- Saves object animations if present using XClassArray::Begin/size
- Sets skin-related mesh flags (0x200000) if m_Skin exists
- Saves children if flags & 0x40000 is set

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior and dependency handling
*************************************************/
void RCK3dEntity::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first to handle basic object dependencies
    CKBeObject::PreSave(file, flags);

    // Save mesh objects (except for curves which don't have meshes)
    // Based on decompilation: checks GetClassID() != CKCID_CURVE
    if (GetClassID() != CKCID_CURVE && m_Meshes.Size() > 0) {
        // Implementation would save mesh objects
    }

    // Save object animations
    // Based on decompilation: checks m_ObjectAnimations.m_Begin
    if (m_ObjectAnimations.Size() > 0) {
        // Implementation would save object animations
    }

    // Handle skin-related mesh flags for proper rendering
    // Based on decompilation: checks m_Skin, calls GetCurrentMesh()
    if (m_Skin && m_CurrentMesh) {
        // Implementation would set skin-related mesh flags
    }

    // Save children entities if requested by save flags
    // Based on decompilation: checks flags & 0x40000
    if (flags & 0x40000 && m_Children.Size() > 0) {
        // Implementation would save children entities
    }
}

/*************************************************
Summary: Save method for RCK3dEntity.
Purpose: Saves entity data to a state chunk.
Remarks:
- Creates and populates state chunk with entity data
- Saves meshes, animations, and transformation data
- Handles complex skin data with bones and vertex weights
- Manages parent-child relationships in scene hierarchy
- Supports both base entity and derived class saving

Implementation based on decompilation at 0x1000A2B2:
- Gets base class chunk with CKBeObject::Save()
- Returns early if !file && (flags & 0x3FF000) == 0
- Creates CKStateChunk with CKCID_3DENTITY
- Saves mesh data (chunk 0x4000) if not curve and has meshes
- Saves object animations (chunk 0x2000) if present
- Saves main entity data (chunk 0x100000):
  - Builds entity flags based on parent, place, priority
  - Writes flags, moveableFlags
  - Writes world matrix as row vectors
  - Writes optional parent, place, priority data
- Saves skin data (chunk 0x200000) if present:
  - Writes m_ObjectInitMatrix
  - Saves bone data: count, objects, matrices
  - Saves vertex data: count, positions, weights, bone indices
  - Saves normal data (chunk 0x1000) if present
- Closes chunk appropriately based on class type

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior
Return Value:
- CKStateChunk*: The created state chunk containing entity data
*************************************************/
CKStateChunk *RCK3dEntity::Save(CKFile *file, CKDWORD flags) {
    // Get base class chunk first
    CKStateChunk *baseChunk = CKBeObject::Save(file, flags);

    // Return early if no file and not in specific save modes
    // Based on IDA: if (!file && (flags & 0x3FF000) == 0)
    if (!file && (flags & 0x3FF000) == 0) {
        return baseChunk;
    }

    // Create entity state chunk
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_3DENTITY, file);
    if (!chunk) {
        return baseChunk;
    }

    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Save mesh data (chunk 0x4000)
    // Based on IDA: checks GetClassID() != CKCID_CURVE and m_Meshes.Size() > 0
    if (GetClassID() != CKCID_CURVE && m_Meshes.Size() > 0) {
        chunk->WriteIdentifier(0x4000);
        m_Meshes.Save(chunk);
    }

    // Save object animations (chunk 0x2000)
    // Based on IDA: checks m_ObjectAnimations.Size() > 0
    if (m_ObjectAnimations.Size() > 0) {
        chunk->WriteIdentifier(0x2000);
        m_ObjectAnimations.Save(chunk);
    }

    // Save main entity data (chunk 0x100000)
    // Based on IDA: builds flags from current state
    {
        chunk->WriteIdentifier(0x100000);

        // Build entity flags
        CKDWORD entityFlags = 0;
        if (m_Parent) {
            entityFlags |= 0x1; // Has parent
        }
        // Note: IDA shows additional flag bits for place (0x4) and priority (0x2)
        // but we keep it simple for now

        chunk->WriteInt(entityFlags);
        chunk->WriteInt(GetMoveableFlags());

        // Write world matrix as 4 row vectors
        // Based on IDA: uses sub_1000C160 to extract row vectors
        const VxMatrix &mat = GetWorldMatrix();
        VxVector row0(mat[0][0], mat[0][1], mat[0][2]);
        VxVector row1(mat[1][0], mat[1][1], mat[1][2]);
        VxVector row2(mat[2][0], mat[2][1], mat[2][2]);
        VxVector row3(mat[3][0], mat[3][1], mat[3][2]);

        chunk->WriteVector(&row0);
        chunk->WriteVector(&row1);
        chunk->WriteVector(&row2);
        chunk->WriteVector(&row3);

        // Write optional parent (if entityFlags & 0x1)
        if (entityFlags & 0x1) {
            chunk->WriteObject(m_Parent);
        }
    }

    // Save skin data (chunk 0x200000)
    // Based on IDA: checks m_Skin, writes bone/vertex data
    if (m_Skin) {
        chunk->WriteIdentifier(0x200000);

        // Write skin version
        chunk->WriteInt(1); // Version 1

        // Write object initialization matrix
        // Note: We need to access the internal matrix - using identity as placeholder
        VxMatrix initMatrix;
        initMatrix.SetIdentity();
        chunk->WriteMatrix(initMatrix);

        // Write bone data
        int boneCount = m_Skin->GetBoneCount();
        chunk->WriteInt(boneCount);

        for (int i = 0; i < boneCount; i++) {
            CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
            if (boneData) {
                // Write bone object
                chunk->WriteObject(boneData->GetBone());

                // Write bone initial inverse matrix
                // Note: Interface doesn't expose getter, use identity as placeholder
                VxMatrix boneMatrix;
                boneMatrix.SetIdentity();
                chunk->WriteMatrix(boneMatrix);
            }
        }

        // Write vertex data
        int vertexCount = m_Skin->GetVertexCount();
        chunk->WriteInt(vertexCount);

        for (int i = 0; i < vertexCount; i++) {
            CKSkinVertexData *vertexData = m_Skin->GetVertexData(i);
            if (vertexData) {
                // Write initial position
                VxVector &initPos = vertexData->GetInitialPos();
                chunk->WriteVector(&initPos);

                // Write bone count for this vertex
                int vertexBoneCount = vertexData->GetBoneCount();
                chunk->WriteInt(vertexBoneCount);

                // Write bone indices and weights
                for (int j = 0; j < vertexBoneCount; j++) {
                    chunk->WriteInt(vertexData->GetBone(j));
                    chunk->WriteFloat(vertexData->GetWeight(j));
                }
            }
        }

        // Write normal data if present (chunk 0x1000)
        int normalCount = m_Skin->GetNormalCount();
        if (normalCount > 0) {
            chunk->WriteIdentifier(0x1000);
            chunk->WriteInt(normalCount);

            for (int i = 0; i < normalCount; i++) {
                VxVector &normal = m_Skin->GetNormal(i);
                chunk->WriteVector(&normal);
            }
        }
    }

    // Close chunk appropriately based on class type
    // Based on IDA: checks GetClassID() == CKCID_3DENTITY
    if (GetClassID() == CKCID_3DENTITY)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

// =====================================================
// RCK3dEntity Constructor/Destructor
// =====================================================

RCK3dEntity::RCK3dEntity(CKContext *Context, CKSTRING name)
    : RCKRenderObject(Context, name),
      m_Parent(nullptr),
      m_CurrentMesh(nullptr),
      field_14(0),
      m_LastFrameMatrix(0),
      m_Skin(nullptr),
      m_MoveableFlags(0x4000B),  // Default from IDA: VX_MOVEABLE_VISIBLE | other flags
      field_124(0),
      field_128(0),
      field_12C(0),
      field_130(0),
      field_134(0),
      field_138(0),
      m_SceneGraphNode(nullptr) {
    m_LocalMatrix.SetIdentity();
    m_WorldMatrix.SetIdentity();
    m_InverseWorldMatrix.SetIdentity();
    m_LocalBoundingBox.Min = VxVector(-1.0f, -1.0f, -1.0f);
    m_LocalBoundingBox.Max = VxVector(1.0f, 1.0f, 1.0f);
    m_WorldBoundingBox = m_LocalBoundingBox;
    
    // Create scene graph node - critical for rendering!
    // Based on IDA analysis: RCK3dEntity constructor calls RCKRenderManager::CreateNode
    RCKRenderManager *renderManager = (RCKRenderManager *)m_Context->GetRenderManager();
    if (renderManager) {
        m_SceneGraphNode = renderManager->CreateNode(this);
        ENTITY_DEBUG_LOG_FMT("Constructor: Created scene graph node=%p for entity=%p", m_SceneGraphNode, this);
    } else {
        ENTITY_DEBUG_LOG("Constructor: WARNING - No RenderManager, cannot create scene graph node!");
    }
}

RCK3dEntity::~RCK3dEntity() {
    // Clean up scene graph node
    if (m_SceneGraphNode) {
        // Delete the scene graph node
        delete m_SceneGraphNode;
        m_SceneGraphNode = nullptr;
    }
    
    // Clean up children references
    for (int i = 0; i < m_Children.Size(); ++i) {
        RCK3dEntity *child = (RCK3dEntity *) m_Children[i];
        if (child) {
            child->m_Parent = nullptr;
        }
    }
    m_Children.Clear();

    // Clear meshes
    m_Meshes.Clear();
    m_CurrentMesh = nullptr;

    // Clear animations
    m_ObjectAnimations.Clear();

    // Destroy skin if present
    if (m_Skin) {
        DestroySkin();
    }
}

CK_CLASSID RCK3dEntity::GetClassID() {
    return CKCID_3DENTITY;
}

int RCK3dEntity::GetMemoryOccupation() {
    // Calculate memory used by this entity
    int size = CKRenderObject::GetMemoryOccupation();

    // Add matrix storage
    size += sizeof(VxMatrix) * 3; // Local, World, InverseWorld

    // Add bounding boxes
    size += sizeof(VxBbox) * 2;

    // Add children array
    size += sizeof(void *) * m_Children.Size();

    // Add meshes array
    size += sizeof(void *) * m_Meshes.Size();

    // Add animations array
    size += sizeof(void *) * m_ObjectAnimations.Size();

    return size;
}

// =====================================================
// Parent/Child Hierarchy Methods
// =====================================================

int RCK3dEntity::GetChildrenCount() const {
    return m_Children.Size();
}

CK3dEntity *RCK3dEntity::GetChild(int pos) const {
    if (pos < 0 || pos >= m_Children.Size())
        return nullptr;
    return (CK3dEntity *) m_Children[pos];
}

CKBOOL RCK3dEntity::SetParent(CK3dEntity *Parent, CKBOOL KeepWorldPos) {
    if (Parent == (CK3dEntity *) this)
        return FALSE;

    // Remove from old parent
    if (m_Parent) {
        m_Parent->RemoveChild((CK3dEntity *) this);
    }

    // Store world matrix if keeping position
    VxMatrix oldWorld;
    if (KeepWorldPos) {
        oldWorld = m_WorldMatrix;
    }

    // Set new parent
    m_Parent = (RCK3dEntity *) Parent;

    // Add to new parent's children
    if (m_Parent) {
        m_Parent->m_Children.PushBack(this);
    }

    // Restore world position if requested
    if (KeepWorldPos) {
        SetWorldMatrix(oldWorld, FALSE);
    }

    return TRUE;
}

CK3dEntity *RCK3dEntity::GetParent() const {
    return (CK3dEntity *) m_Parent;
}

CKBOOL RCK3dEntity::AddChild(CK3dEntity *Child, CKBOOL KeepWorldPos) {
    if (!Child || Child == (CK3dEntity *) this)
        return FALSE;

    RCK3dEntity *rChild = (RCK3dEntity *) Child;

    // Remove from old parent
    if (rChild->m_Parent) {
        rChild->m_Parent->RemoveChild(Child);
    }

    // Store world matrix if keeping position
    VxMatrix oldWorld;
    if (KeepWorldPos) {
        oldWorld = rChild->m_WorldMatrix;
    }

    // Set parent and add to children
    rChild->m_Parent = this;
    m_Children.PushBack(rChild);

    // Restore world position if requested
    if (KeepWorldPos) {
        rChild->SetWorldMatrix(oldWorld, FALSE);
    }

    return TRUE;
}

CKBOOL RCK3dEntity::AddChildren(const XObjectPointerArray &Children, CKBOOL KeepWorldPos) {
    for (CKObject **it = Children.Begin(); it != Children.End(); ++it) {
        CK3dEntity *child = (CK3dEntity *) *it;
        if (child) {
            AddChild(child, KeepWorldPos);
        }
    }
    return TRUE;
}

CKBOOL RCK3dEntity::RemoveChild(CK3dEntity *Mov) {
    if (!Mov)
        return FALSE;

    RCK3dEntity *rMov = (RCK3dEntity *) Mov;

    for (int i = 0; i < m_Children.Size(); ++i) {
        if (m_Children[i] == rMov) {
            m_Children.Remove(m_Children.At(i));
            rMov->m_Parent = nullptr;
            return TRUE;
        }
    }
    return FALSE;
}

CKBOOL RCK3dEntity::CheckIfSameKindOfHierarchy(CK3dEntity *Mov, CKBOOL SameRecur) const {
    // Check if same type or subclass
    return const_cast<RCK3dEntity *>(this)->GetClassID() == Mov->GetClassID();
}

CK3dEntity *RCK3dEntity::HierarchyParser(CK3dEntity *current) const {
    if (!current) {
        // Start from first child
        if (m_Children.Size() > 0)
            return (CK3dEntity *) m_Children[0];
        return nullptr;
    }

    // Find next in hierarchy
    RCK3dEntity *rCurrent = (RCK3dEntity *) current;

    // First try children of current
    if (rCurrent->m_Children.Size() > 0) {
        return (CK3dEntity *) rCurrent->m_Children[0];
    }

    // Then try siblings
    RCK3dEntity *parent = rCurrent->m_Parent;
    while (parent) {
        int idx = -1;
        for (int i = 0; i < parent->m_Children.Size(); ++i) {
            if (parent->m_Children[i] == rCurrent) {
                idx = i;
                break;
            }
        }
        if (idx >= 0 && idx + 1 < parent->m_Children.Size()) {
            return (CK3dEntity *) parent->m_Children[idx + 1];
        }
        rCurrent = parent;
        parent = rCurrent->m_Parent;
    }

    return nullptr;
}

// =====================================================
// Flags Methods
// =====================================================

CKDWORD RCK3dEntity::GetFlags() const {
    return const_cast<RCK3dEntity *>(this)->CKObject::GetObjectFlags();
}

void RCK3dEntity::SetFlags(CKDWORD Flags) {
    ModifyObjectFlags(Flags, 0xFFFFFFFF);
}

void RCK3dEntity::SetPickable(CKBOOL Pick) {
    if (Pick) {
        m_MoveableFlags |= VX_MOVEABLE_PICKABLE;
    } else {
        m_MoveableFlags &= ~VX_MOVEABLE_PICKABLE;
    }
}

CKBOOL RCK3dEntity::IsPickable() const {
    return (m_MoveableFlags & VX_MOVEABLE_PICKABLE) != 0;
}

void RCK3dEntity::SetRenderChannels(CKBOOL RenderChannels) {
    if (RenderChannels) {
        m_MoveableFlags |= VX_MOVEABLE_RENDERCHANNELS;
    } else {
        m_MoveableFlags &= ~VX_MOVEABLE_RENDERCHANNELS;
    }
}

CKBOOL RCK3dEntity::AreRenderChannelsVisible() const {
    return (m_MoveableFlags & VX_MOVEABLE_RENDERCHANNELS) != 0;
}

void RCK3dEntity::IgnoreAnimations(CKBOOL ignore) {
    if (ignore) {
        m_MoveableFlags |= VX_MOVEABLE_NOANIMALIASING;
    } else {
        m_MoveableFlags &= ~VX_MOVEABLE_NOANIMALIASING;
    }
}

CKBOOL RCK3dEntity::AreAnimationIgnored() const {
    return (m_MoveableFlags & VX_MOVEABLE_NOANIMALIASING) != 0;
}

CKBOOL RCK3dEntity::IsAllInsideFrustrum() const {
    return (m_MoveableFlags & VX_MOVEABLE_ALLINSIDE) != 0;
}

CKBOOL RCK3dEntity::IsAllOutsideFrustrum() const {
    return (m_MoveableFlags & VX_MOVEABLE_ALLOUTSIDE) != 0;
}

void RCK3dEntity::SetRenderAsTransparent(CKBOOL Trans) {
    if (Trans) {
        m_MoveableFlags |= VX_MOVEABLE_RENDERLAST;
    } else {
        m_MoveableFlags &= ~VX_MOVEABLE_RENDERLAST;
    }
}

CKDWORD RCK3dEntity::GetMoveableFlags() const {
    return m_MoveableFlags;
}

void RCK3dEntity::SetMoveableFlags(CKDWORD Flags) {
    m_MoveableFlags = Flags;
}

CKDWORD RCK3dEntity::ModifyMoveableFlags(CKDWORD Add, CKDWORD Remove) {
    m_MoveableFlags = (m_MoveableFlags & ~Remove) | Add;
    return m_MoveableFlags;
}

// =====================================================
// Mesh Methods
// =====================================================

CKMesh *RCK3dEntity::GetCurrentMesh() const {
    return (CKMesh *) m_CurrentMesh;
}

CKMesh *RCK3dEntity::SetCurrentMesh(CKMesh *m, CKBOOL add_if_not_here) {
    CKMesh *old = (CKMesh *) m_CurrentMesh;

    if (add_if_not_here && m) {
        // Check if mesh is in list
        CKBOOL found = FALSE;
        for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
            if (*it == m) {
                found = TRUE;
                break;
            }
        }
        if (!found) {
            m_Meshes.PushBack(m);
        }
    }

    m_CurrentMesh = (RCKMesh *) m;

    if (old != m) {
        ENTITY_DEBUG_LOG_FMT(
            "SetCurrentMesh: entity=%p name=%s old=%p new=%p add=%d size=%d",
            this, GetName() ? GetName() : "(null)", old, m, add_if_not_here, m_Meshes.Size());
    }
    return old;
}

int RCK3dEntity::GetMeshCount() const {
    return m_Meshes.Size();
}

CKMesh *RCK3dEntity::GetMesh(int pos) const {
    if (pos < 0 || pos >= m_Meshes.Size())
        return nullptr;
    return (CKMesh *) m_Meshes[pos];
}

CKERROR RCK3dEntity::AddMesh(CKMesh *mesh) {
    if (!mesh)
        return CKERR_INVALIDPARAMETER;

    m_Meshes.PushBack(mesh);
    if (!m_CurrentMesh) {
        m_CurrentMesh = (RCKMesh *) mesh;
    }
    ENTITY_DEBUG_LOG_FMT("AddMesh: entity=%p name=%s mesh=%p current=%p size=%d", this,
                         GetName() ? GetName() : "(null)", mesh, m_CurrentMesh, m_Meshes.Size());
    return CK_OK;
}

CKERROR RCK3dEntity::RemoveMesh(CKMesh *mesh) {
    if (!mesh)
        return CKERR_INVALIDPARAMETER;

    for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
        if (*it == mesh) {
            m_Meshes.Remove(it);
            if (m_CurrentMesh == (RCKMesh *) mesh) {
                m_CurrentMesh = m_Meshes.Size() > 0 ? (RCKMesh *) m_Meshes[0] : nullptr;
            }
                ENTITY_DEBUG_LOG_FMT("RemoveMesh: entity=%p name=%s removed=%p newCurrent=%p size=%d", this,
                                     GetName() ? GetName() : "(null)", mesh, m_CurrentMesh, m_Meshes.Size());
            return CK_OK;
        }
    }
    return CKERR_NOTFOUND;
}

// =====================================================
// Transform Methods
// =====================================================

void RCK3dEntity::LookAt(const VxVector *Pos, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Pos)
        return;

    VxVector target = *Pos;
    VxVector position;
    GetPosition(&position, Ref);

    VxVector dir = target - position;
    dir.Normalize();

    VxVector up(0, 1, 0);
    VxVector right = CrossProduct(up, dir);
    right.Normalize();
    up = CrossProduct(dir, right);

    SetOrientation(&dir, &up, nullptr, Ref, KeepChildren);
}

void RCK3dEntity::Rotate3f(float X, float Y, float Z, float Angle, CK3dEntity *Ref, CKBOOL KeepChildren) {
    VxVector axis(X, Y, Z);
    Rotate(&axis, Angle, Ref, KeepChildren);
}

void RCK3dEntity::Rotate(const VxVector *Axis, float Angle, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Axis)
        return;

    // Build rotation matrix from axis-angle
    VxMatrix rot;
    Vx3DMatrixFromRotation(rot, *Axis, Angle);

    // Apply rotation to current orientation
    VxMatrix newWorld;
    Vx3DMultiplyMatrix(newWorld, rot, m_WorldMatrix);

    // Preserve position
    newWorld[3][0] = m_WorldMatrix[3][0];
    newWorld[3][1] = m_WorldMatrix[3][1];
    newWorld[3][2] = m_WorldMatrix[3][2];

    SetWorldMatrix(newWorld, KeepChildren);
}

void RCK3dEntity::Translate3f(float X, float Y, float Z, CK3dEntity *Ref, CKBOOL KeepChildren) {
    VxVector trans(X, Y, Z);
    Translate(&trans, Ref, KeepChildren);
}

void RCK3dEntity::Translate(const VxVector *Vect, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Vect)
        return;

    VxVector pos;
    GetPosition(&pos, Ref);
    pos += *Vect;
    SetPosition(&pos, Ref, KeepChildren);
}

void RCK3dEntity::AddScale3f(float X, float Y, float Z, CKBOOL KeepChildren, CKBOOL Local) {
    VxVector scale(X, Y, Z);
    AddScale(&scale, KeepChildren, Local);
}

void RCK3dEntity::AddScale(const VxVector *Scale, CKBOOL KeepChildren, CKBOOL Local) {
    if (!Scale)
        return;

    VxVector current;
    GetScale(&current, Local);
    current.x *= Scale->x;
    current.y *= Scale->y;
    current.z *= Scale->z;
    SetScale(&current, KeepChildren, Local);
}

void RCK3dEntity::SetPosition3f(float X, float Y, float Z, CK3dEntity *Ref, CKBOOL KeepChildren) {
    VxVector pos(X, Y, Z);
    SetPosition(&pos, Ref, KeepChildren);
}

void RCK3dEntity::SetPosition(const VxVector *Pos, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Pos)
        return;

    if (Ref) {
        // Transform to world space then to local space
        VxVector worldPos;
        ((RCK3dEntity *) Ref)->Transform(&worldPos, Pos, nullptr);
        m_WorldMatrix[3][0] = worldPos.x;
        m_WorldMatrix[3][1] = worldPos.y;
        m_WorldMatrix[3][2] = worldPos.z;
    } else {
        m_WorldMatrix[3][0] = Pos->x;
        m_WorldMatrix[3][1] = Pos->y;
        m_WorldMatrix[3][2] = Pos->z;
    }

    // Update local matrix
    if (m_Parent) {
        Vx3DMultiplyMatrixInverse(m_LocalMatrix, m_Parent->m_WorldMatrix, m_WorldMatrix);
    } else {
        m_LocalMatrix = m_WorldMatrix;
    }

    // Update inverse
    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);

    // Update children if needed
    if (!KeepChildren) {
        for (int i = 0; i < m_Children.Size(); ++i) {
            RCK3dEntity *child = (RCK3dEntity *) m_Children[i];
            if (child) {
                Vx3DMultiplyMatrix(child->m_WorldMatrix, child->m_LocalMatrix, m_WorldMatrix);
                Vx3DInverseMatrix(child->m_InverseWorldMatrix, child->m_WorldMatrix);
            }
        }
    }
}

void RCK3dEntity::GetPosition(VxVector *Pos, CK3dEntity *Ref) const {
    if (!Pos)
        return;

    Pos->x = m_WorldMatrix[3][0];
    Pos->y = m_WorldMatrix[3][1];
    Pos->z = m_WorldMatrix[3][2];

    if (Ref) {
        ((RCK3dEntity *) Ref)->InverseTransform(Pos, Pos, nullptr);
    }
}

void RCK3dEntity::SetOrientation(const VxVector *Dir, const VxVector *Up, const VxVector *Right, CK3dEntity *Ref,
                                 CKBOOL KeepChildren) {
    VxVector d, u, r;

    if (Dir) d = *Dir;
    else d = VxVector(0, 0, 1);
    if (Up) u = *Up;
    else u = VxVector(0, 1, 0);

    d.Normalize();
    r = CrossProduct(u, d);
    r.Normalize();
    u = CrossProduct(d, r);

    // Set rotation part of world matrix
    m_WorldMatrix[0][0] = r.x;
    m_WorldMatrix[0][1] = r.y;
    m_WorldMatrix[0][2] = r.z;
    m_WorldMatrix[1][0] = u.x;
    m_WorldMatrix[1][1] = u.y;
    m_WorldMatrix[1][2] = u.z;
    m_WorldMatrix[2][0] = d.x;
    m_WorldMatrix[2][1] = d.y;
    m_WorldMatrix[2][2] = d.z;

    // Update local matrix
    if (m_Parent) {
        Vx3DMultiplyMatrixInverse(m_LocalMatrix, m_Parent->m_WorldMatrix, m_WorldMatrix);
    } else {
        m_LocalMatrix = m_WorldMatrix;
    }

    // Update inverse
    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);
}

void RCK3dEntity::GetOrientation(VxVector *Dir, VxVector *Up, VxVector *Right, CK3dEntity *Ref) {
    if (Right) {
        Right->x = m_WorldMatrix[0][0];
        Right->y = m_WorldMatrix[0][1];
        Right->z = m_WorldMatrix[0][2];
    }
    if (Up) {
        Up->x = m_WorldMatrix[1][0];
        Up->y = m_WorldMatrix[1][1];
        Up->z = m_WorldMatrix[1][2];
    }
    if (Dir) {
        Dir->x = m_WorldMatrix[2][0];
        Dir->y = m_WorldMatrix[2][1];
        Dir->z = m_WorldMatrix[2][2];
    }
}

// =====================================================
// Virtual Method Implementations
// =====================================================

void RCK3dEntity::PreDelete() {
    // Remove from parent
    if (m_Parent) {
        m_Parent->RemoveChild((CK3dEntity *) this);
    }

    // Remove all children
    while (m_Children.Size() > 0) {
        RCK3dEntity *child = (RCK3dEntity *) m_Children[0];
        if (child) {
            child->m_Parent = nullptr;
        }
        m_Children.Remove(m_Children.Begin());
    }

    CKRenderObject::PreDelete();
}

void RCK3dEntity::CheckPreDeletion() {
    CKRenderObject::CheckPreDeletion();
}

int RCK3dEntity::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    // Check meshes
    for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
        if (*it == o) return TRUE;
    }

    // Check animations
    for (CKObject **it = m_ObjectAnimations.Begin(); it != m_ObjectAnimations.End(); ++it) {
        if (*it == o) return TRUE;
    }

    return CKRenderObject::IsObjectUsed(o, cid);
}

CKERROR RCK3dEntity::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = CKRenderObject::PrepareDependencies(context);
    if (err != CK_OK) return err;

    // Dependencies are handled by the context's Fill method
    // Just call base class implementation

    return CK_OK;
}

CKERROR RCK3dEntity::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKRenderObject::RemapDependencies(context);
    if (err != CK_OK) return err;

    // Remap parent
    if (m_Parent) {
        m_Parent = (RCK3dEntity *) context.Remap((CKObject *) m_Parent);
    }

    // Remap meshes
    for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
        *it = context.Remap(*it);
    }
    RCKMesh *oldCurrent = m_CurrentMesh;
    m_CurrentMesh = (RCKMesh *) context.Remap((CKObject *) m_CurrentMesh);

    if (oldCurrent != m_CurrentMesh) {
        ENTITY_DEBUG_LOG_FMT("RemapDependencies: entity=%p name=%s current %p -> %p size=%d", this,
                             GetName() ? GetName() : "(null)", oldCurrent, m_CurrentMesh, m_Meshes.Size());
    }

    // Fallback: keep a valid current mesh if remapping dropped it
    if (!m_CurrentMesh && m_Meshes.Size() > 0) {
        m_CurrentMesh = (RCKMesh *) m_Meshes[0];
        ENTITY_DEBUG_LOG_FMT("RemapDependencies: restored current mesh from array -> %p", m_CurrentMesh);
    }

    return CK_OK;
}

CKERROR RCK3dEntity::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKRenderObject::Copy(o, context);
    if (err != CK_OK) return err;

    RCK3dEntity &src = (RCK3dEntity &) o;

    m_LocalMatrix = src.m_LocalMatrix;
    m_WorldMatrix = src.m_WorldMatrix;
    m_InverseWorldMatrix = src.m_InverseWorldMatrix;
    m_MoveableFlags = src.m_MoveableFlags;
    m_LocalBoundingBox = src.m_LocalBoundingBox;
    m_WorldBoundingBox = src.m_WorldBoundingBox;

    return CK_OK;
}

void RCK3dEntity::AddToScene(CKScene *scene, CKBOOL dependencies) {
    CKRenderObject::AddToScene(scene, dependencies);

    if (dependencies) {
        for (int i = 0; i < m_Children.Size(); ++i) {
            RCK3dEntity *child = (RCK3dEntity *) m_Children[i];
            if (child) {
                child->AddToScene(scene, dependencies);
            }
        }
    }
}

void RCK3dEntity::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    CKRenderObject::RemoveFromScene(scene, dependencies);

    if (dependencies) {
        for (int i = 0; i < m_Children.Size(); ++i) {
            RCK3dEntity *child = (RCK3dEntity *) m_Children[i];
            if (child) {
                child->RemoveFromScene(scene, dependencies);
            }
        }
    }
}

CKBOOL RCK3dEntity::IsToBeRendered() {
    if (IsHiddenByParent())
        return FALSE;
    return (CKObject::GetObjectFlags() & CK_OBJECT_VISIBLE) != 0;
}

void RCK3dEntity::SetZOrder(int Z) {
    field_124 = Z;
}

int RCK3dEntity::GetZOrder() {
    return field_124;
}

CKBOOL RCK3dEntity::IsToBeRenderedLast() {
    return (m_MoveableFlags & VX_MOVEABLE_RENDERLAST) != 0;
}

void RCK3dEntity::WorldMatrixChanged(int updateChildren, int keepScale) {
    // TODO: Implement properly based on IDA decompilation
    // For now, just mark that the matrix needs recalculation
    m_MoveableFlags |= VX_MOVEABLE_UPTODATE;
}

// =====================================================
// Stubs for remaining methods
// =====================================================

void RCK3dEntity::SetQuaternion(const VxQuaternion *Quat, CK3dEntity *Ref, CKBOOL KeepChildren, CKBOOL KeepScale) {
    if (!Quat)
        return;

    VxMatrix rot;
    Quat->ToMatrix(rot);

    // Preserve position and scale
    VxVector pos;
    GetPosition(&pos, Ref);

    // Apply rotation
    m_WorldMatrix[0][0] = rot[0][0];
    m_WorldMatrix[0][1] = rot[0][1];
    m_WorldMatrix[0][2] = rot[0][2];
    m_WorldMatrix[1][0] = rot[1][0];
    m_WorldMatrix[1][1] = rot[1][1];
    m_WorldMatrix[1][2] = rot[1][2];
    m_WorldMatrix[2][0] = rot[2][0];
    m_WorldMatrix[2][1] = rot[2][1];
    m_WorldMatrix[2][2] = rot[2][2];

    SetPosition(&pos, Ref, KeepChildren);
}

void RCK3dEntity::GetQuaternion(VxQuaternion *Quat, CK3dEntity *Ref) {
    if (!Quat)
        return;
    Quat->FromMatrix(m_WorldMatrix);
}

void RCK3dEntity::SetScale3f(float X, float Y, float Z, CKBOOL KeepChildren, CKBOOL Local) {
    VxVector scale(X, Y, Z);
    SetScale(&scale, KeepChildren, Local);
}

void RCK3dEntity::SetScale(const VxVector *Scale, CKBOOL KeepChildren, CKBOOL Local) {
    if (!Scale)
        return;

    // Get current scale
    VxVector currentScale;
    GetScale(&currentScale, Local);

    // Calculate scale factors
    VxVector factor;
    factor.x = (currentScale.x != 0) ? Scale->x / currentScale.x : Scale->x;
    factor.y = (currentScale.y != 0) ? Scale->y / currentScale.y : Scale->y;
    factor.z = (currentScale.z != 0) ? Scale->z / currentScale.z : Scale->z;

    // Apply scale
    VxMatrix &mat = Local ? m_LocalMatrix : m_WorldMatrix;
    mat[0][0] *= factor.x;
    mat[0][1] *= factor.x;
    mat[0][2] *= factor.x;
    mat[1][0] *= factor.y;
    mat[1][1] *= factor.y;
    mat[1][2] *= factor.y;
    mat[2][0] *= factor.z;
    mat[2][1] *= factor.z;
    mat[2][2] *= factor.z;

    // Update matrices
    if (Local && m_Parent) {
        Vx3DMultiplyMatrix(m_WorldMatrix, m_LocalMatrix, m_Parent->m_WorldMatrix);
    } else if (!Local) {
        if (m_Parent) {
            Vx3DMultiplyMatrixInverse(m_LocalMatrix, m_Parent->m_WorldMatrix, m_WorldMatrix);
        } else {
            m_LocalMatrix = m_WorldMatrix;
        }
    }
    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);
}

void RCK3dEntity::GetScale(VxVector *Scale, CKBOOL Local) {
    if (!Scale)
        return;

    const VxMatrix &mat = Local ? m_LocalMatrix : m_WorldMatrix;
    Scale->x = sqrtf(mat[0][0] * mat[0][0] + mat[0][1] * mat[0][1] + mat[0][2] * mat[0][2]);
    Scale->y = sqrtf(mat[1][0] * mat[1][0] + mat[1][1] * mat[1][1] + mat[1][2] * mat[1][2]);
    Scale->z = sqrtf(mat[2][0] * mat[2][0] + mat[2][1] * mat[2][1] + mat[2][2] * mat[2][2]);
}

CKBOOL RCK3dEntity::ConstructWorldMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) {
    m_WorldMatrix.SetIdentity();

    if (Quat) {
        Quat->ToMatrix(m_WorldMatrix);
    }

    if (Scale) {
        m_WorldMatrix[0][0] *= Scale->x;
        m_WorldMatrix[0][1] *= Scale->x;
        m_WorldMatrix[0][2] *= Scale->x;
        m_WorldMatrix[1][0] *= Scale->y;
        m_WorldMatrix[1][1] *= Scale->y;
        m_WorldMatrix[1][2] *= Scale->y;
        m_WorldMatrix[2][0] *= Scale->z;
        m_WorldMatrix[2][1] *= Scale->z;
        m_WorldMatrix[2][2] *= Scale->z;
    }

    if (Pos) {
        m_WorldMatrix[3][0] = Pos->x;
        m_WorldMatrix[3][1] = Pos->y;
        m_WorldMatrix[3][2] = Pos->z;
    }

    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);

    if (m_Parent) {
        Vx3DMultiplyMatrixInverse(m_LocalMatrix, m_Parent->m_WorldMatrix, m_WorldMatrix);
    } else {
        m_LocalMatrix = m_WorldMatrix;
    }

    return TRUE;
}

CKBOOL RCK3dEntity::ConstructWorldMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat,
                                           const VxQuaternion *Shear, float Sign) {
    return ConstructWorldMatrix(Pos, Scale, Quat);
}

CKBOOL RCK3dEntity::ConstructLocalMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) {
    m_LocalMatrix.SetIdentity();

    if (Quat) {
        Quat->ToMatrix(m_LocalMatrix);
    }

    if (Scale) {
        m_LocalMatrix[0][0] *= Scale->x;
        m_LocalMatrix[0][1] *= Scale->x;
        m_LocalMatrix[0][2] *= Scale->x;
        m_LocalMatrix[1][0] *= Scale->y;
        m_LocalMatrix[1][1] *= Scale->y;
        m_LocalMatrix[1][2] *= Scale->y;
        m_LocalMatrix[2][0] *= Scale->z;
        m_LocalMatrix[2][1] *= Scale->z;
        m_LocalMatrix[2][2] *= Scale->z;
    }

    if (Pos) {
        m_LocalMatrix[3][0] = Pos->x;
        m_LocalMatrix[3][1] = Pos->y;
        m_LocalMatrix[3][2] = Pos->z;
    }

    if (m_Parent) {
        Vx3DMultiplyMatrix(m_WorldMatrix, m_LocalMatrix, m_Parent->m_WorldMatrix);
    } else {
        m_WorldMatrix = m_LocalMatrix;
    }

    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);

    return TRUE;
}

CKBOOL RCK3dEntity::ConstructLocalMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat,
                                           const VxQuaternion *Shear, float Sign) {
    return ConstructLocalMatrix(Pos, Scale, Quat);
}

CKBOOL RCK3dEntity::Render(CKRenderContext *Dev, CKDWORD Flags) {
    RCKRenderContext *dev = (RCKRenderContext *) Dev;

    ENTITY_DEBUG_LOG_FMT("Render: entity=%p name=%s mesh=%p callbacks=%p flags=0x%X", 
                         this, GetName() ? GetName() : "(null)", m_CurrentMesh, m_Callbacks, Flags);

    // Must have mesh or callbacks
    if (!m_CurrentMesh && !m_Callbacks) {
        ENTITY_DEBUG_LOG("Render: SKIP - no mesh and no callbacks");
        return FALSE;
    }

    CKBOOL isPM = FALSE;

    // Check if extents are up to date (meaning we've already been verified visible)
    if ((m_MoveableFlags & VX_MOVEABLE_EXTENTSUPTODATE) != 0) {
        // Extents are valid - we know we're visible
        if ((Flags & CK_RENDER_CLEARVIEWPORT) == 0) {
            dev->SetWorldTransformationMatrix(m_WorldMatrix);
        }
    } else {
        // Need to check frustum visibility
        if (!IsInViewFrustrum(Dev, Flags)) {
            // Object is outside frustum - skip rendering
            return TRUE;
        }
    }

    // Handle indirect matrix (mirrored objects)
    CKDWORD savedInverseWinding = 0;
    if ((m_MoveableFlags & VX_MOVEABLE_INDIRECTMATRIX) != 0) {
        dev->m_RasterizerContext->GetRenderState(VXRENDERSTATE_INVERSEWINDING, &savedInverseWinding);
        dev->m_RasterizerContext->SetRenderState(VXRENDERSTATE_INVERSEWINDING, savedInverseWinding == 0 ? 1 : 0);
    }

    // Handle skin update for non-PM meshes
    if (m_Skin && m_CurrentMesh) {
        if (m_CurrentMesh->IsPM()) {
            isPM = TRUE;
        } else {
            // Update skin before callbacks
            VxTimeProfiler skinTimer;
            UpdateSkin();
            dev->m_Stats.SkinTime += skinTimer.Current();
        }
    }

    // Execute callbacks if present
    if (m_Callbacks) {
        // Execute pre-render callbacks
        if (m_Callbacks->m_PreCallBacks.Size() > 0) {
            VxTimeProfiler callbackTimer;
            dev->m_RasterizerContext->SetVertexShader(0);

            for (int i = 0; i < m_Callbacks->m_PreCallBacks.Size(); i++) {
                VxCallBack &cb = m_Callbacks->m_PreCallBacks[i];
                if (cb.callback) {
                    ((CK_RENDEROBJECT_CALLBACK) cb.callback)(Dev, (CK3dEntity *) this, cb.argument);
                }
            }

            dev->m_Stats.ObjectsCallbacksTime += callbackTimer.Current();
        }

        // Update skin for PM meshes after pre-callbacks
        if (isPM) {
            VxTimeProfiler skinTimer;
            UpdateSkin();
            dev->m_Stats.SkinTime += skinTimer.Current();
        }

        // Execute render callback (replaces default rendering) or default render
        if (m_Callbacks->m_CallBack) {
            ((CK_RENDEROBJECT_CALLBACK) m_Callbacks->m_CallBack->callback)(
                Dev, (CK3dEntity *) this, m_Callbacks->m_CallBack->argument);
        } else if (m_CurrentMesh && (m_CurrentMesh->GetFlags() & VXMESH_VISIBLE) != 0) {
            dev->m_Current3dEntity = this;
            m_CurrentMesh->Render(Dev, (CK3dEntity *) this);
            dev->m_Current3dEntity = nullptr;
        }

        // Execute post-render callbacks
        if (m_Callbacks->m_PostCallBacks.Size() > 0) {
            VxTimeProfiler callbackTimer;
            dev->m_RasterizerContext->SetVertexShader(0);

            for (int i = 0; i < m_Callbacks->m_PostCallBacks.Size(); i++) {
                VxCallBack &cb = m_Callbacks->m_PostCallBacks[i];
                if (cb.callback) {
                    ((CK_RENDEROBJECT_CALLBACK) cb.callback)(Dev, (CK3dEntity *) this, cb.argument);
                }
            }

            dev->m_Stats.ObjectsCallbacksTime += callbackTimer.Current();
        }
    } else {
        // No callbacks - just render the mesh
        if (m_CurrentMesh && (m_CurrentMesh->GetFlags() & VXMESH_VISIBLE) != 0) {
            ENTITY_DEBUG_LOG_FMT("Render: Calling mesh->Render(), mesh=%p flags=0x%X", 
                                 m_CurrentMesh, m_CurrentMesh->GetFlags());
            dev->m_Current3dEntity = this;
            m_CurrentMesh->Render(Dev, (CK3dEntity *) this);
            dev->m_Current3dEntity = nullptr;
        } else {
            ENTITY_DEBUG_LOG_FMT("Render: SKIP mesh - mesh=%p visible=%d", 
                                 m_CurrentMesh, 
                                 m_CurrentMesh ? (m_CurrentMesh->GetFlags() & VXMESH_VISIBLE) != 0 : -1);
        }
    }

    // Restore inverse winding if changed
    if ((m_MoveableFlags & VX_MOVEABLE_INDIRECTMATRIX) != 0) {
        CKDWORD currentWinding = 0;
        dev->m_RasterizerContext->GetRenderState(VXRENDERSTATE_INVERSEWINDING, &currentWinding);
        dev->m_RasterizerContext->SetRenderState(VXRENDERSTATE_INVERSEWINDING, currentWinding == 0 ? 1 : 0);
    }

    // Update render extents if requested
    if (Flags & CKRENDER_UPDATEEXTENTS) {
        dev->AddExtents2D(m_RenderExtents, (CKObject *) this);
    }

    return TRUE;
}

int RCK3dEntity::RayIntersection(const VxVector *Pos1, const VxVector *Pos2, VxIntersectionDesc *Desc, CK3dEntity *Ref,
                                 CK_RAYINTERSECTION iOptions) {
    // TODO: Implement ray intersection
    return 0;
}

void RCK3dEntity::GetRenderExtents(VxRect &rect) const {
    rect = m_RenderExtents;
}

const VxMatrix &RCK3dEntity::GetLastFrameMatrix() const {
    return m_WorldMatrix;
}

void RCK3dEntity::SetLocalMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    m_LocalMatrix = Mat;

    if (m_Parent) {
        Vx3DMultiplyMatrix(m_WorldMatrix, m_LocalMatrix, m_Parent->m_WorldMatrix);
    } else {
        m_WorldMatrix = m_LocalMatrix;
    }

    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);
}

const VxMatrix &RCK3dEntity::GetLocalMatrix() const {
    return m_LocalMatrix;
}

void RCK3dEntity::SetWorldMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    m_WorldMatrix = Mat;

    if (m_Parent) {
        Vx3DMultiplyMatrixInverse(m_LocalMatrix, m_Parent->m_WorldMatrix, m_WorldMatrix);
    } else {
        m_LocalMatrix = m_WorldMatrix;
    }

    Vx3DInverseMatrix(m_InverseWorldMatrix, m_WorldMatrix);

    // Update children
    if (!KeepChildren) {
        for (int i = 0; i < m_Children.Size(); ++i) {
            RCK3dEntity *child = (RCK3dEntity *) m_Children[i];
            if (child) {
                Vx3DMultiplyMatrix(child->m_WorldMatrix, child->m_LocalMatrix, m_WorldMatrix);
                Vx3DInverseMatrix(child->m_InverseWorldMatrix, child->m_WorldMatrix);
            }
        }
    }
}

const VxMatrix &RCK3dEntity::GetWorldMatrix() const {
    return m_WorldMatrix;
}

const VxMatrix &RCK3dEntity::GetInverseWorldMatrix() const {
    return m_InverseWorldMatrix;
}

void RCK3dEntity::Transform(VxVector *Dest, const VxVector *Src, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    Vx3DMultiplyMatrixVector(Dest, m_WorldMatrix, Src);
}

void RCK3dEntity::InverseTransform(VxVector *Dest, const VxVector *Src, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    Vx3DMultiplyMatrixVector(Dest, m_InverseWorldMatrix, Src);
}

void RCK3dEntity::TransformVector(VxVector *Dest, const VxVector *Src, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    Vx3DRotateVector(Dest, m_WorldMatrix, Src);
}

void RCK3dEntity::InverseTransformVector(VxVector *Dest, const VxVector *Src, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    Vx3DRotateVector(Dest, m_InverseWorldMatrix, Src);
}

void RCK3dEntity::TransformMany(VxVector *Dest, const VxVector *Src, int count, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    for (int i = 0; i < count; ++i) {
        Vx3DMultiplyMatrixVector(&Dest[i], m_WorldMatrix, &Src[i]);
    }
}

void RCK3dEntity::InverseTransformMany(VxVector *Dest, const VxVector *Src, int count, CK3dEntity *Ref) const {
    if (!Dest || !Src)
        return;
    for (int i = 0; i < count; ++i) {
        Vx3DMultiplyMatrixVector(&Dest[i], m_InverseWorldMatrix, &Src[i]);
    }
}

void RCK3dEntity::ChangeReferential(CK3dEntity *Ref) {
    // TODO: Implement referential change
}

CKPlace *RCK3dEntity::GetReferencePlace() const {
    return nullptr;
}

void RCK3dEntity::AddObjectAnimation(CKObjectAnimation *anim) {
    if (anim) {
        m_ObjectAnimations.PushBack(anim);
    }
}

void RCK3dEntity::RemoveObjectAnimation(CKObjectAnimation *anim) {
    for (CKObject **it = m_ObjectAnimations.Begin(); it != m_ObjectAnimations.End(); ++it) {
        if (*it == anim) {
            m_ObjectAnimations.Remove(it);
            return;
        }
    }
}

CKObjectAnimation *RCK3dEntity::GetObjectAnimation(int index) const {
    if (index < 0 || index >= m_ObjectAnimations.Size())
        return nullptr;
    return (CKObjectAnimation *) m_ObjectAnimations[index];
}

int RCK3dEntity::GetObjectAnimationCount() const {
    return m_ObjectAnimations.Size();
}

CKSkin *RCK3dEntity::CreateSkin() {
    // TODO: Implement skin creation
    return nullptr;
}

CKBOOL RCK3dEntity::DestroySkin() {
    if (m_Skin) {
        // TODO: Properly delete skin
        m_Skin = nullptr;
        return TRUE;
    }
    return FALSE;
}

CKBOOL RCK3dEntity::UpdateSkin() {
    if (!m_Skin)
        return FALSE;
    // TODO: Implement skin update
    return TRUE;
}

CKSkin *RCK3dEntity::GetSkin() const {
    return (CKSkin *) m_Skin;
}

void RCK3dEntity::UpdateBox(CKBOOL World) {
    if (m_CurrentMesh) {
        m_LocalBoundingBox = m_CurrentMesh->GetLocalBox();
    }

    if (World) {
        // Transform local box to world
        VxVector corners[8];
        corners[0] = VxVector(m_LocalBoundingBox.Min.x, m_LocalBoundingBox.Min.y, m_LocalBoundingBox.Min.z);
        corners[1] = VxVector(m_LocalBoundingBox.Max.x, m_LocalBoundingBox.Min.y, m_LocalBoundingBox.Min.z);
        corners[2] = VxVector(m_LocalBoundingBox.Min.x, m_LocalBoundingBox.Max.y, m_LocalBoundingBox.Min.z);
        corners[3] = VxVector(m_LocalBoundingBox.Max.x, m_LocalBoundingBox.Max.y, m_LocalBoundingBox.Min.z);
        corners[4] = VxVector(m_LocalBoundingBox.Min.x, m_LocalBoundingBox.Min.y, m_LocalBoundingBox.Max.z);
        corners[5] = VxVector(m_LocalBoundingBox.Max.x, m_LocalBoundingBox.Min.y, m_LocalBoundingBox.Max.z);
        corners[6] = VxVector(m_LocalBoundingBox.Min.x, m_LocalBoundingBox.Max.y, m_LocalBoundingBox.Max.z);
        corners[7] = VxVector(m_LocalBoundingBox.Max.x, m_LocalBoundingBox.Max.y, m_LocalBoundingBox.Max.z);

        m_WorldBoundingBox.Min = VxVector(FLT_MAX, FLT_MAX, FLT_MAX);
        m_WorldBoundingBox.Max = VxVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (int i = 0; i < 8; ++i) {
            VxVector worldCorner;
            Vx3DMultiplyMatrixVector(&worldCorner, m_WorldMatrix, &corners[i]);
            m_WorldBoundingBox.Min.x = (worldCorner.x < m_WorldBoundingBox.Min.x)
                                           ? worldCorner.x
                                           : m_WorldBoundingBox.Min.x;
            m_WorldBoundingBox.Min.y = (worldCorner.y < m_WorldBoundingBox.Min.y)
                                           ? worldCorner.y
                                           : m_WorldBoundingBox.Min.y;
            m_WorldBoundingBox.Min.z = (worldCorner.z < m_WorldBoundingBox.Min.z)
                                           ? worldCorner.z
                                           : m_WorldBoundingBox.Min.z;
            m_WorldBoundingBox.Max.x = (worldCorner.x > m_WorldBoundingBox.Max.x)
                                           ? worldCorner.x
                                           : m_WorldBoundingBox.Max.x;
            m_WorldBoundingBox.Max.y = (worldCorner.y > m_WorldBoundingBox.Max.y)
                                           ? worldCorner.y
                                           : m_WorldBoundingBox.Max.y;
            m_WorldBoundingBox.Max.z = (worldCorner.z > m_WorldBoundingBox.Max.z)
                                           ? worldCorner.z
                                           : m_WorldBoundingBox.Max.z;
        }
    }
}

const VxBbox &RCK3dEntity::GetBoundingBox(CKBOOL Local) {
    return Local ? m_LocalBoundingBox : m_WorldBoundingBox;
}

CKBOOL RCK3dEntity::SetBoundingBox(const VxBbox *BBox, CKBOOL Local) {
    if (!BBox)
        return FALSE;

    if (Local) {
        m_LocalBoundingBox = *BBox;
    } else {
        m_WorldBoundingBox = *BBox;
    }
    return TRUE;
}

const VxBbox &RCK3dEntity::GetHierarchicalBox(CKBOOL Local) {
    // TODO: Include children bounding boxes
    return GetBoundingBox(Local);
}

CKBOOL RCK3dEntity::GetBaryCenter(VxVector *Pos) {
    if (!Pos)
        return FALSE;

    Pos->x = (m_WorldBoundingBox.Min.x + m_WorldBoundingBox.Max.x) * 0.5f;
    Pos->y = (m_WorldBoundingBox.Min.y + m_WorldBoundingBox.Max.y) * 0.5f;
    Pos->z = (m_WorldBoundingBox.Min.z + m_WorldBoundingBox.Max.z) * 0.5f;

    return TRUE;
}

float RCK3dEntity::GetRadius() {
    VxVector size = m_WorldBoundingBox.Max - m_WorldBoundingBox.Min;
    return size.Magnitude() * 0.5f;
}

CKBOOL RCK3dEntity::IsHiddenByParent() {
    if (m_Parent) {
        if (!(m_Parent->GetObjectFlags() & CK_OBJECT_VISIBLE))
            return TRUE;
        return m_Parent->IsHiddenByParent();
    }
    return FALSE;
}

CKBOOL RCK3dEntity::IsVisible() {
    if (IsHiddenByParent())
        return FALSE;
    return (CKObject::GetObjectFlags() & CK_OBJECT_VISIBLE) != 0;
}

CKBOOL RCK3dEntity::IsInViewFrustrum(CKRenderContext *rc, CKDWORD flags) {
    if (!rc)
        return FALSE;

    RCKRenderContext *rrc = (RCKRenderContext *) rc;

    // Check if bounding box is in view frustum
    if (rrc->m_RasterizerContext) {
        return rrc->m_RasterizerContext->ComputeBoxVisibility(m_WorldBoundingBox, TRUE, nullptr) != 0;
    }

    return TRUE;
}

CKBOOL RCK3dEntity::IsInViewFrustrumHierarchic(CKRenderContext *rc) {
    // Check this entity and all parents
    if (!IsInViewFrustrum(rc, 0))
        return FALSE;

    // If parent is a place, check its clipping
    if (m_Parent && m_Parent->GetClassID() == CKCID_PLACE) {
        return m_Parent->IsInViewFrustrumHierarchic(rc);
    }

    return TRUE;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCK3dEntity::m_ClassID = CKCID_3DENTITY;

CKSTRING RCK3dEntity::GetClassName() {
    return (CKSTRING) "3D Entity";
}

int RCK3dEntity::GetDependenciesCount(int mode) {
    // Based on IDA decompilation at 0x1000b339
    switch (mode) {
    case 1: return 3; // Copy mode
    case 2: return 3; // Delete mode
    case 3: return 0; // Replace mode
    case 4: return 3; // Save mode
    default: return 0;
    }
}

CKSTRING RCK3dEntity::GetDependencies(int i, int mode) {
    // Based on IDA decompilation at 0x1000b38b
    switch (i) {
    case 0: return (CKSTRING) "Meshes";
    case 1: return (CKSTRING) "Children";
    case 2: return (CKSTRING) "Animation";
    default: return nullptr;
    }
}

void RCK3dEntity::Register() {
    // Based on IDA decompilation at 0x1000b3c4
    CKClassNeedNotificationFrom(RCK3dEntity::m_ClassID, CKCID_OBJECTANIMATION);
    CKClassNeedNotificationFrom(RCK3dEntity::m_ClassID, CKCID_MESH);
    CKClassNeedNotificationFrom(RCK3dEntity::m_ClassID, CKCID_3DENTITY);

    // Register associated parameter GUID: {5B8A05D5, 31EA28D4}
    CKClassRegisterAssociatedParameter(RCK3dEntity::m_ClassID, CKPGUID_3DENTITY);

    // Register default dependencies for different modes
    CKClassRegisterDefaultDependencies(RCK3dEntity::m_ClassID, 6, 1); // Copy mode
    CKClassRegisterDefaultDependencies(RCK3dEntity::m_ClassID, 4, 2); // Delete mode
    CKClassRegisterDefaultDependencies(RCK3dEntity::m_ClassID, 7, 4); // Save mode
}

CK3dEntity *RCK3dEntity::CreateInstance(CKContext *Context) {
    // Based on IDA decompilation at 0x1000b45f
    // Object size is 0x1A8 (424 bytes)
    RCK3dEntity *ent = new RCK3dEntity(Context, nullptr);
    return reinterpret_cast<CK3dEntity *>(ent);
}
