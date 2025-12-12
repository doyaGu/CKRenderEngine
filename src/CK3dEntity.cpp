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

extern int (*g_RayIntersection)(RCKMesh *, VxVector &, VxVector &, VxIntersectionDesc *, CK_RAYINTERSECTION, VxMatrix const &);

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
    if (m_ObjectAnimations && m_ObjectAnimations->Size() > 0) {
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
    
    // Log the object flags IMMEDIATELY after CKBeObject::Load to see what SDK restored
    ENTITY_DEBUG_LOG_FMT("Load: After CKBeObject::Load - m_ObjectFlags=0x%X CK_OBJECT_VISIBLE=%d", 
                         m_ObjectFlags, (m_ObjectFlags & CK_OBJECT_VISIBLE) ? 1 : 0);

    // Preserve moveable flags that should not be modified during load
    // Based on IDA: v16 = GetMoveableFlags() & 0x40000
    CKDWORD preservedFlags = GetMoveableFlags() & 0x40000;

    // Initialize identity matrix for transformation
    VxMatrix worldMatrix;
    worldMatrix.SetIdentity();

    // Load object animations (chunk 0x2000)
    // Deduplicate using last-pointer tracking as in IDA
    if (chunk->SeekIdentifier(0x2000)) {
        if (!m_ObjectAnimations) {
            m_ObjectAnimations = new XObjectPointerArray();
        }

        XObjectPointerArray tempAnims;
        tempAnims.Load(m_Context, chunk);

        CKObject *lastAnim = nullptr;
        for (CKObject **it = tempAnims.Begin(); it != tempAnims.End(); ++it) {
            CKObject *anim = *it;
            if (anim && anim != lastAnim) {
                m_ObjectAnimations->AddIfNotHere(anim);
                lastAnim = anim;
            }
        }
    }

    // Load meshes (chunk 0x4000)
    // Based on IDA and docs: First reads current mesh, then mesh array
    if (chunk->SeekIdentifier(0x4000)) {
        // Read current mesh object first
        CKMesh *currentMesh = (CKMesh *)chunk->ReadObject(m_Context);
        ENTITY_DEBUG_LOG_FMT("Load: Read current mesh = %p", currentMesh);

        // Load mesh array into a temporary array to preserve original ordering/deduplication
        XObjectPointerArray tempMeshes;
        tempMeshes.Load(m_Context, chunk);

        CKObject *lastMesh = nullptr;
        for (CKObject **it = tempMeshes.Begin(); it != tempMeshes.End(); ++it) {
            CKMesh *mesh = (CKMesh *)*it;
            if (mesh && mesh != lastMesh) {
                m_Meshes.AddIfNotHere(mesh);
                lastMesh = mesh;
                ENTITY_DEBUG_LOG_FMT("Load: Added mesh = %p (%s)", mesh, mesh->GetName());
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
    // Matches IDA implementation @0x1000a7b9 EXACTLY
    if (chunk->SeekIdentifier(0x100000)) {
        // IDA line 117: Dword = CKStateChunk::ReadDword(a2);
        CKDWORD entityFlags = chunk->ReadDword();
        
        // IDA line 118: this->SetFlags(this, Dword & 0xFFFFEFDF);
        // SetFlags stores (entityFlags & mask) to m_3dEntityFlags
        // The mask 0xFFFFEFDF clears bits 5 and 12, but preserves presence indicators
        SetFlags(entityFlags & 0xFFFFEFDF);
        
        // IDA line 119: v53 = CKStateChunk::ReadDword(a2) & 0xBB3DBFEB;
        CKDWORD moveableFlags = chunk->ReadDword() & 0xBB3DBFEB;

        ENTITY_DEBUG_LOG_FMT("Load: entityFlags=0x%X moveableFlags=0x%X m_ObjectFlags=0x%X", 
                             entityFlags, moveableFlags, m_ObjectFlags);

        // IDA line 120-123: if ( v16 ) v53 |= 0x40000u;
        if (preservedFlags) {
            moveableFlags |= 0x40000;
        }

        // IDA line 124-127: if ( (v53 & 0x100000) != 0 ) SetPriority(10000)
        if ((moveableFlags & 0x100000) != 0) {
            if (m_SceneGraphNode) {
                m_SceneGraphNode->SetPriority(10000, FALSE);
            }
        }

        // IDA line 128-143: Read matrix row vectors
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

        // IDA line 144-146: Calculate determinant for handedness
        VxVector cross = CrossProduct(row0, row1);
        float dot = DotProduct(cross, row2);
        if (dot < 0.0f) {
            moveableFlags |= 0x1000000;  // VX_MOVEABLE_INDIRECTMATRIX
        } else {
            moveableFlags &= ~0x1000000;
        }

        // IDA line 147-150: Sync VX_MOVEABLE_VISIBLE from CKObject::IsVisible()
        // if ( CKObject::IsVisible(this) ) v53 |= 2u; else v53 &= ~2u;
        if (IsVisible()) {
            moveableFlags |= VX_MOVEABLE_VISIBLE;
        } else {
            moveableFlags &= ~VX_MOVEABLE_VISIBLE;
        }

        // IDA line 151-154: Sync VX_MOVEABLE_HIERARCHICALHIDE from CK_OBJECT_HIERACHICALHIDE
        // if ( sub_1000CE70(this) ) v53 |= 0x10000000u; else v53 &= ~0x10000000u;
        if (m_ObjectFlags & CK_OBJECT_HIERACHICALHIDE) {
            moveableFlags |= VX_MOVEABLE_HIERARCHICALHIDE;
        } else {
            moveableFlags &= ~VX_MOVEABLE_HIERARCHICALHIDE;
        }

        // IDA line 155: SetMoveableFlags(this, v53);
        SetMoveableFlags(moveableFlags);
        
        ENTITY_DEBUG_LOG_FMT("Load: After sync - m_MoveableFlags=0x%X VISIBLE=%d IsVisible=%d", 
                             m_MoveableFlags, (m_MoveableFlags & VX_MOVEABLE_VISIBLE) ? 1 : 0, IsVisible() ? 1 : 0);

        // IDA line 156-157: Read optional Place (if entityFlags & 0x10000)
        if (m_3dEntityFlags & 0x10000) {
            chunk->ReadObjectID();  // Place reference (deprecated)
        }

        // IDA line 158-166: Read optional parent (if entityFlags & 0x20000)
        if (m_3dEntityFlags & 0x20000) {
            CK3dEntity *parent = (CK3dEntity *)chunk->ReadObject(m_Context);
            SetParent(parent, TRUE);
        } else {
            SetParent(nullptr, TRUE);
        }

        // IDA line 167-171: Read optional priority (if entityFlags & 0x100000)
        if (m_3dEntityFlags & 0x100000) {
            int priority = chunk->ReadInt();
            if (m_SceneGraphNode) {
                m_SceneGraphNode->SetPriority((CKWORD)priority, FALSE);
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
        chunk->Skip(1);  // IDA shows a padding byte consumed before the matrix
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

    // Set world matrix respecting file/context rules from original implementation
    if (file) {
        SetWorldMatrix(worldMatrix, TRUE);
    } else {
        CKBOOL shouldSetMatrix = TRUE;
        CKMesh *meshForInit = GetCurrentMesh();
        CKScene *currentScene = m_Context ? m_Context->GetCurrentScene() : nullptr;
        if (meshForInit && currentScene && currentScene->GetObjectInitialValue(meshForInit)) {
            shouldSetMatrix = FALSE;
        }
        if (shouldSetMatrix) {
            SetWorldMatrix(worldMatrix, TRUE);
        }
    }

    // Load skin data (chunk 0x200000) - complex vertex weighting system
    // Based on IDA: checks for chunk, calls CreateSkin(), reads bone/vertex data
    if (chunk->SeekIdentifier(0x200000)) {
        // Create skin if needed
        if (!m_Skin) {
            CreateSkin();
        }

        if (m_Skin) {
            const int dataVersion = chunk->GetDataVersion();

            // Older files (<6) have an extra byte to skip
            if (dataVersion < 6) {
                chunk->Skip(1);
            }

            // Read object initialization matrix
            VxMatrix objectInitMatrix;
            chunk->ReadMatrix(objectInitMatrix);
            m_Skin->SetObjectInitMatrix(objectInitMatrix);
            Vx3DInverseMatrix(m_Skin->m_InverseWorldMatrix, objectInitMatrix);

            // Bones are stored as an object-ID sequence followed by flags/matrices
            int boneCount = chunk->StartReadSequence();
            m_Skin->SetBoneCount(boneCount);

            for (int i = 0; i < boneCount; i++) {
                CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
                if (boneData) {
                    CK3dEntity *boneEntity = (CK3dEntity *)chunk->ReadObject(m_Context);
                    boneData->SetBone(boneEntity);
                } else {
                    // Consume the object even if we cannot store it
                    chunk->ReadObject(m_Context);
                }
            }

            for (int i = 0; i < boneCount; i++) {
                // Flags are currently unused; consume them to match layout
                chunk->ReadDword();
                VxMatrix boneMatrix;
                chunk->ReadMatrix(boneMatrix);

                CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
                if (boneData) {
                    boneData->SetBoneInitialInverseMatrix(boneMatrix);
                }
            }

            // Read vertex data
            int vertexCount = chunk->ReadInt();
            m_Skin->SetVertexCount(vertexCount);

            for (int i = 0; i < vertexCount; i++) {
                RCKSkinVertexData *vertexData = static_cast<RCKSkinVertexData *>(m_Skin->GetVertexData(i));
                if (!vertexData) {
                    continue;
                }

                int vertexBoneCount = chunk->ReadInt();
                vertexData->SetBoneCount(vertexBoneCount);

                if (dataVersion < 6) {
                    chunk->Skip(1);
                }

                VxVector initPos;
                chunk->ReadVector(&initPos);
                vertexData->SetInitialPos(initPos);

                if (dataVersion < 6) {
                    chunk->Skip(1);
                }

                // Read bone indices then weights (buffered in original implementation)
                if (vertexBoneCount > 0) {
                    chunk->ReadAndFillBuffer_LEndian(4 * vertexBoneCount, (void *)vertexData->GetBonesArray());

                    if (dataVersion < 6) {
                        chunk->Skip(1);
                    }

                    chunk->ReadAndFillBuffer_LEndian(4 * vertexBoneCount, (void *)vertexData->GetWeightsArray());
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
    // Based on IDA: triggered when CK_3DENTITY_BBOXVALID (0x80000) and CK_OBJECT_VISIBLE are set
    if ((m_3dEntityFlags & 0x80000) && (m_ObjectFlags & CK_OBJECT_VISIBLE)) {
        VxBbox defaultBbox;
        defaultBbox.Min = VxVector(-1.0f, -1.0f, -1.0f);
        defaultBbox.Max = VxVector(1.0f, 1.0f, 1.0f);
        SetBoundingBox(&defaultBbox, TRUE);
    }

    // FINAL VISIBILITY CHECK
    // Log the final visibility state for debugging
    ENTITY_DEBUG_LOG_FMT("Load: Final state - m_ObjectFlags=0x%X m_MoveableFlags=0x%X VISIBLE=%d/%d",
                         m_ObjectFlags, m_MoveableFlags,
                         (m_ObjectFlags & CK_OBJECT_VISIBLE) ? 1 : 0,
                         (m_MoveableFlags & VX_MOVEABLE_VISIBLE) ? 1 : 0);

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
    if (file && GetClassID() != CKCID_CURVE && m_Meshes.Size() > 0) {
        file->SaveObjects((CKObject **)m_Meshes.Begin(), m_Meshes.Size(), flags);
    }

    // Save object animations
    // Based on decompilation: checks m_ObjectAnimations pointer
    if (file && m_ObjectAnimations && m_ObjectAnimations->Size() > 0) {
        file->SaveObjects((CKObject **)m_ObjectAnimations->Begin(), m_ObjectAnimations->Size(), flags);
    }

    // Handle skin-related mesh flags for proper rendering
    // Based on decompilation: checks m_Skin, calls GetCurrentMesh()
    if (m_Skin && m_CurrentMesh) {
        m_CurrentMesh->SetFlags(m_CurrentMesh->GetFlags() | 0x200000);
    }

    // Save children entities if requested by save flags
    // Based on decompilation: checks flags & 0x40000
    if (file && (flags & 0x40000) && m_Children.Size() > 0) {
        file->SaveObjects((CKObject **)m_Children.Begin(), m_Children.Size(), 0xFFFFFFFF);
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
    // Based on IDA: checks GetClassID() != CKCID_CURVE and writes current mesh first
    if (GetClassID() != CKCID_CURVE && (m_CurrentMesh || m_Meshes.Size() > 0)) {
        chunk->WriteIdentifier(0x4000);
        chunk->WriteObject(m_CurrentMesh);
        m_Meshes.Save(chunk);
    }

    // Save object animations (chunk 0x2000)
    // Based on IDA: checks m_ObjectAnimations pointer and size
    if (m_ObjectAnimations && m_ObjectAnimations->Size() > 0) {
        chunk->WriteIdentifier(0x2000);
        m_ObjectAnimations->Save(chunk);
    }

    // Save main entity data (chunk 0x100000)
    // Based on IDA Save function @0x1000a2b2
    {
        chunk->WriteIdentifier(0x100000);

        // Get Place object for presence check
        CKObject *placeObject = m_Context->GetObjectA(m_Place);
        
        // Build entity flags (stored in m_3dEntityFlags)
        // Based on IDA: conditionally sets/clears presence indicator bits
        // CK_3DENTITY_PARENTVALID (0x20000) - has parent
        if (m_Parent) {
            m_3dEntityFlags |= 0x20000;  // CK_3DENTITY_PARENTVALID
        } else {
            m_3dEntityFlags &= ~0x20000;
        }
        
        // CK_3DENTITY_PLACEVALID (0x10000) - has place
        if (placeObject) {
            m_3dEntityFlags |= 0x10000;  // CK_3DENTITY_PLACEVALID
        } else {
            m_3dEntityFlags &= ~0x10000;
        }
        
        // CK_3DENTITY_ZORDERVALID (0x100000) - has priority
        int priority = m_SceneGraphNode ? m_SceneGraphNode->m_Priority : 0;
        if (priority != 0) {
            m_3dEntityFlags |= 0x100000;  // CK_3DENTITY_ZORDERVALID
        } else {
            m_3dEntityFlags &= ~0x100000;
        }

        // Write entity flags and moveable flags
        chunk->WriteDword(m_3dEntityFlags);
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

        // Write optional Place reference (if CK_3DENTITY_PLACEVALID)
        if (placeObject) {
            chunk->WriteObject(placeObject);
        }
        
        // Write optional parent (if CK_3DENTITY_PARENTVALID)
        if (m_Parent) {
            chunk->WriteObject(m_Parent);
        }
        
        // Write optional priority (if CK_3DENTITY_ZORDERVALID)
        if (priority != 0) {
            chunk->WriteInt(priority);
        }
    }

    // Save skin data (chunk 0x200000)
    // Based on IDA: checks m_Skin, writes bone/vertex data
    if (m_Skin) {
        chunk->WriteIdentifier(0x200000);

        // Write object initialization matrix
        chunk->WriteMatrix(m_Skin->GetObjectInitMatrix());

        // Write bone data: first an object-ID sequence, then flags/matrices
        int boneCount = m_Skin->GetBoneCount();
        chunk->StartObjectIDSequence(boneCount);

        for (int i = 0; i < boneCount; i++) {
            CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
            CK3dEntity *bone = boneData ? boneData->GetBone() : nullptr;
            chunk->WriteObjectSequence(bone);
        }

        for (int i = 0; i < boneCount; i++) {
            CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
            // Flags are unknown in current implementation; write zero to preserve layout
            chunk->WriteDword(0);

            VxMatrix boneMatrix;
            if (boneData) {
                boneMatrix = static_cast<RCKSkinBoneData *>(boneData)->GetInitialInverseMatrix();
            } else {
                boneMatrix.SetIdentity();
            }
            chunk->WriteMatrix(boneMatrix);
        }

        // Write vertex data
        int vertexCount = m_Skin->GetVertexCount();
        chunk->WriteInt(vertexCount);

        for (int i = 0; i < vertexCount; i++) {
            RCKSkinVertexData *vertexData = static_cast<RCKSkinVertexData *>(m_Skin->GetVertexData(i));
            if (!vertexData) {
                chunk->WriteInt(0);
                VxVector zero(0.0f, 0.0f, 0.0f);
                chunk->WriteVector(&zero);
                continue;
            }

            int vertexBoneCount = vertexData->GetBoneCount();
            chunk->WriteInt(vertexBoneCount);

            VxVector &initPos = vertexData->GetInitialPos();
            chunk->WriteVector(&initPos);

            if (vertexBoneCount > 0) {
                chunk->WriteBufferNoSize_LEndian(4 * vertexBoneCount, vertexData->GetBonesArray());
                chunk->WriteBufferNoSize_LEndian(4 * vertexBoneCount, vertexData->GetWeightsArray());
            }
        }

        // Write normal data if present (chunk 0x1000)
        int normalCount = m_Skin->GetNormalCount();
        if (normalCount > 0 && normalCount == m_Skin->GetVertexCount()) {
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
      m_Meshes(),
      m_CurrentMesh(nullptr),
      m_Place(0),
      m_ObjectAnimations(nullptr),  // Pointer to dynamically allocated array
      m_3dEntityFlags(0),           // CK_3DENTITY_FLAGS
      m_LastFrameMatrix(nullptr),  // Pointer, not DWORD - based on IDA destructor using operator delete
      m_Skin(nullptr),
      m_MoveableFlags(0x4000B),  // Default from IDA: VX_MOVEABLE_VISIBLE | VX_MOVEABLE_PICKABLE | 0x40008
      m_SceneGraphNode(nullptr) {
    // Initialize matrices to identity - based on IDA constructor calling sub_1000C180 (SetIdentity)
    m_LocalMatrix.SetIdentity();
    m_WorldMatrix.SetIdentity();
    m_InverseWorldMatrix.SetIdentity();
    

    // Default render extents (VxRect ctor sets zeros in IDA)
    m_RenderExtents.left = m_RenderExtents.top = 0.0f;
    m_RenderExtents.right = m_RenderExtents.bottom = 0.0f;
    
    // Create scene graph node - critical for rendering!
    // Based on IDA analysis at 0x10005086: RCK3dEntity constructor calls RCKRenderManager::CreateNode
    RCKRenderManager *renderManager = (RCKRenderManager *)m_Context->GetRenderManager();
    if (renderManager) {
        m_SceneGraphNode = renderManager->CreateNode(this);
        ENTITY_DEBUG_LOG_FMT("Constructor: Created scene graph node=%p for entity=%p", m_SceneGraphNode, this);
    } else {
        ENTITY_DEBUG_LOG("Constructor: WARNING - No RenderManager, cannot create scene graph node!");
    }
}

/*************************************************
Summary: Destructor for RCK3dEntity.
Purpose: Cleans up all allocated resources.

Implementation based on decompilation at 0x100050b5:
- Sets vftable pointer
- Destroys skin if present
- Frees object animations array
- Deletes m_LastFrameMatrix (operator delete)
- Destroys scene graph node
- Clears children array
- Clears meshes array
- Calls base class destructor
*************************************************/
RCK3dEntity::~RCK3dEntity() {
    // Destroy skin if present
    // IDA: if ( this->m_Skin ) RCK3dEntity::DestroySkin(this);
    if (m_Skin) {
        DestroySkin();
    }
    
    // Delete object animations array if allocated
    // IDA: if ( this->m_ObjectAnimations.m_Begin ) sub_1000BCB0(...)
    if (m_ObjectAnimations) {
        delete m_ObjectAnimations;
        m_ObjectAnimations = nullptr;
    }
    
    // Delete last frame matrix if allocated
    // IDA: operator delete(this->m_LastFrameMatrix);
    if (m_LastFrameMatrix) {
        delete m_LastFrameMatrix;
        m_LastFrameMatrix = nullptr;
    }
    
    // Clean up scene graph node
    // IDA: if ( this->m_SceneGraphNode ) sub_1000BCE0(...)
    if (m_SceneGraphNode) {
        delete m_SceneGraphNode;
        m_SceneGraphNode = nullptr;
    }
    
    // Children array is cleared by XSObjectPointerArray destructor
    // IDA: sub_1004A200(&this->m_Children);
    // Also detach children from this parent
    for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
        RCK3dEntity *child = (RCK3dEntity *)*it;
        if (child) {
            child->m_Parent = nullptr;
        }
    }
    
    // Meshes array is cleared by XObjectPointerArray destructor
    // IDA: sub_1001A080(&this->m_Meshs.m_Begin);
    
    // Base class destructor called automatically
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
    size += m_ObjectAnimations ? sizeof(void *) * m_ObjectAnimations->Size() : 0;

    return size;
}

// =====================================================
// Parent/Child Hierarchy Methods
// =====================================================

int RCK3dEntity::GetChildrenCount() const {
    return m_Children.Size();
}

CK3dEntity *RCK3dEntity::GetChild(int pos) const {
    // IDA: return *std::vector<void*>::operator[](&this->m_Children.m_Begin, pos);
    // Original has NO bounds checking - matches binary behavior
    return (CK3dEntity *) m_Children[pos];
}

CKBOOL RCK3dEntity::SetParent(CK3dEntity *Parent, CKBOOL KeepWorldPos) {
    // IDA: RCK3dEntity::SetParent at 0x1000932b
    RCK3dEntity *parent = (RCK3dEntity *) Parent;
    
    // Cannot parent to self
    if (parent == this)
        return FALSE;
    
    // Already has this parent
    if (m_Parent == parent)
        return TRUE;
    
    // Cannot parent a Place to another Place
    if (CKIsChildClassOf(this, CKCID_PLACE) && CKIsChildClassOf(parent, CKCID_PLACE))
        return FALSE;
    
    // Check for cycles - cannot parent to a descendant
    if (parent) {
        for (RCK3dEntity *ancestor = parent->m_Parent; ancestor; ancestor = ancestor->m_Parent) {
            if (ancestor == this)
                return FALSE;
        }
    }
    
    // Remove from old parent's children array
    if (m_Parent) {
        m_Parent->m_Children.Remove(this);
    }
    
    // Remove this node from its current scene graph parent (if it has one)
    if (m_SceneGraphNode && m_SceneGraphNode->m_Parent) {
        m_SceneGraphNode->m_Parent->RemoveNode(m_SceneGraphNode);
    }
    
    // Set new parent
    m_Parent = parent;
    
    // Add to new parent
    if (parent) {
        // Add to parent's children array
        parent->m_Children.PushBack(this);
        // Add to parent's scene graph node
        if (parent->m_SceneGraphNode && m_SceneGraphNode) {
            parent->m_SceneGraphNode->AddNode(m_SceneGraphNode);
        }
    } else {
        // No parent - check if object should be added to root scene graph
        // Only add to root if not marked for deletion (CK_OBJECT_TOBEDELETED = 0x10)
        if (!(m_ObjectFlags & CK_OBJECT_TOBEDELETED) && m_SceneGraphNode) {
            RCKRenderManager *rm = (RCKRenderManager *) m_Context->GetRenderManager();
            if (rm && rm->GetRootNode()) {
                rm->GetRootNode()->AddNode(m_SceneGraphNode);
            }
        }
    }
    
    // Handle matrix update
    if (KeepWorldPos) {
        if (m_Parent) {
            // Recompute local matrix: LocalMatrix = InverseParentWorld * WorldMatrix
            const VxMatrix &invParent = m_Parent->GetInverseWorldMatrix();
            Vx3DMultiplyMatrix(m_LocalMatrix, invParent, m_WorldMatrix);
        } else {
            // No parent - local = world
            m_LocalMatrix = m_WorldMatrix;
        }
    } else {
        // Local matrix stays, world matrix needs recalculation
        LocalMatrixChanged(0, 1);
    }
    
    // Update Place reference
    CK_ID newPlace = 0;
    if (CKIsChildClassOf(parent, CKCID_PLACE)) {
        // Parent is a Place - use it as reference
        newPlace = parent ? parent->GetID() : 0;
    } else if (parent) {
        // Inherit parent's Place
        newPlace = parent->m_Place;
    }
    
    if (newPlace != m_Place) {
        UpdatePlace(newPlace);
    }
    
    return TRUE;
}

CK3dEntity *RCK3dEntity::GetParent() const {
    return (CK3dEntity *) m_Parent;
}

CKBOOL RCK3dEntity::AddChild(CK3dEntity *Child, CKBOOL KeepWorldPos) {
    // IDA: if (Child) return Child->SetParent(this, KeepWorldPos); else return 0;
    if (Child)
        return ((RCK3dEntity *)Child)->SetParent((CK3dEntity *)this, KeepWorldPos);
    return FALSE;
}

CKBOOL RCK3dEntity::AddChildren(const XObjectPointerArray &Children, CKBOOL KeepWorldPos) {
    // IDA: AddChildren at 0x10009582
    // First pass: Mark all objects with CK_OBJECT_TEMPMARKER (0x800)
    for (CKObject **it = Children.Begin(); it != Children.End(); ++it) {
        if (*it) {
            (*it)->ModifyObjectFlags(CK_OBJECT_TEMPMARKER, 0);
        }
    }
    
    // Second pass: Add children that are 3D entities
    int result = 0;
    for (CKObject **it = Children.Begin(); it != Children.End(); ++it) {
        if (CKIsChildClassOf(*it, CKCID_3DENTITY)) {
            RCK3dEntity *entity = (RCK3dEntity *) *it;
            // Only add if no parent OR parent doesn't have marker flag
            if (!entity->GetParent() || !(entity->GetParent()->GetObjectFlags() & CK_OBJECT_TEMPMARKER)) {
                result |= entity->SetParent((CK3dEntity *)this, KeepWorldPos);
            }
        }
    }
    
    // Third pass: Clear the marker flag
    for (CKObject **it = Children.Begin(); it != Children.End(); ++it) {
        if (*it) {
            (*it)->ModifyObjectFlags(0, CK_OBJECT_TEMPMARKER);
        }
    }
    
    return result;
}

CKBOOL RCK3dEntity::RemoveChild(CK3dEntity *Mov) {
    // IDA: if (Mov) return Mov->SetParent(NULL, TRUE); else return 0;
    if (Mov)
        return Mov->SetParent(nullptr, TRUE);
    return FALSE;
}

CKBOOL RCK3dEntity::CheckIfSameKindOfHierarchy(CK3dEntity *Mov, CKBOOL SameRecur) const {
    // IDA: CheckIfSameKindOfHierarchy at 0x1000980D
    if (!Mov)
        return FALSE;
    
    int myChildCount = GetChildrenCount();
    int otherChildCount = Mov->GetChildrenCount();
    
    // Child counts must match
    if (myChildCount != otherChildCount)
        return FALSE;
    
    if (SameRecur) {
        // Same order recursion - check children in order
        for (int i = 0; i < myChildCount; i++) {
            RCK3dEntity *myChild = (RCK3dEntity *)const_cast<RCK3dEntity*>(this)->GetChild(i);
            CK3dEntity *otherChild = Mov->GetChild(i);
            if (!myChild->CheckIfSameKindOfHierarchy(otherChild, TRUE))
                return FALSE;
        }
        return TRUE;
    } else {
        // Non-ordered matching - find any matching child
        for (int i = 0; i < myChildCount; i++) {
            RCK3dEntity *myChild = (RCK3dEntity *)const_cast<RCK3dEntity*>(this)->GetChild(i);
            int k;
            for (k = 0; k < otherChildCount; k++) {
                CK3dEntity *otherChild = Mov->GetChild(k);
                if (myChild->CheckIfSameKindOfHierarchy(otherChild, FALSE))
                    break;
            }
            if (k >= otherChildCount)
                return FALSE;
        }
        return TRUE;
    }
}

CK3dEntity *RCK3dEntity::HierarchyParser(CK3dEntity *current) const {
    // IDA: HierarchyParser at 0x10009715
    RCK3dEntity *entity = (RCK3dEntity *) current;
    
    if (current) {
        // If current has children, return first child
        if (entity->m_Children.Size() > 0) {
            return entity->GetChild(0);
        }
        
        // Walk up the hierarchy looking for next sibling
        while (true) {
            RCK3dEntity *parent = entity->m_Parent;
            if (!parent)
                return nullptr;
            
            // Find current's index in parent's children
            int childCount = parent->m_Children.Size();
            int idx = 0;
            do {
                if (parent->m_Children[idx] == entity)
                    break;
                idx++;
            } while (idx < childCount);
            idx++; // Move to next sibling
            
            // If there's a next sibling, return it
            if (idx != childCount) {
                return parent->GetChild(idx);
            }
            
            // Stop if we've reached the root (this object)
            if (parent == this)
                return nullptr;
            
            // Move up to parent and continue
            entity = parent;
        }
    } else {
        // current is NULL - start from this object's first child
        if (m_Children.Size() > 0) {
            return const_cast<RCK3dEntity *>(this)->GetChild(0);
        }
        return nullptr;
    }
}

// =====================================================
// Flags Methods
// =====================================================

CKDWORD RCK3dEntity::GetFlags() const {
    return m_3dEntityFlags;
}

/*************************************************
Summary: SetFlags - Sets the 3D entity flags.
Purpose: Stores flags to m_3dEntityFlags and handles CK_3DENTITY_UPDATELASTFRAME changes.

Implementation based on IDA decompilation at 0x100055dd:
- Saves old CK_3DENTITY_UPDATELASTFRAME (0x1000) state
- Stores new flags to m_3dEntityFlags (offset 0x74)
- If UPDATELASTFRAME changed:
  - If cleared: Unregister from render manager, delete m_LastFrameMatrix
  - If set: Register with render manager
*************************************************/
void RCK3dEntity::SetFlags(CKDWORD Flags) {
    const CKDWORD oldUpdateLastFrame = m_3dEntityFlags & 0x1000;  // CK_3DENTITY_UPDATELASTFRAME
    m_3dEntityFlags = Flags;

    const CKBOOL updateLastFrame = (m_3dEntityFlags & 0x1000) != 0;
    RCKRenderManager *rm = (RCKRenderManager *)m_Context->GetRenderManager();

    // Register/unregister entity for last-frame tracking based on flag transition
    if (oldUpdateLastFrame && !updateLastFrame) {
        if (rm) {
            rm->UnregisterLastFrameEntity(this);
        }
        if (m_LastFrameMatrix) {
            delete m_LastFrameMatrix;
            m_LastFrameMatrix = nullptr;
        }
    } else if (!oldUpdateLastFrame && updateLastFrame) {
        if (rm) {
            rm->RegisterLastFrameEntity(this);
        }
    }
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

/*************************************************
Summary: SetMoveableFlags - Sets the moveable flags to a specific value.
Purpose: Computes which flags to add/remove and delegates to ModifyMoveableFlags.

Implementation based on decompilation at 0x10005751:
- Computes Add = (~(~Flags & m_MoveableFlags)) & (Flags ^ m_MoveableFlags)
- Computes Remove = ~Flags & m_MoveableFlags
- Calls ModifyMoveableFlags(Add, Remove)
This ensures proper side effects (visibility sync, scene graph notification)
*************************************************/
void RCK3dEntity::SetMoveableFlags(CKDWORD Flags) {
    // IDA: return this->ModifyMoveableFlags(
    //         this,
    //         ~(~a2 & this->m_MoveableFlags) & (a2 ^ this->m_MoveableFlags),
    //         ~a2 & this->m_MoveableFlags);
    CKDWORD toRemove = ~Flags & m_MoveableFlags;
    CKDWORD toAdd = ~(~Flags & m_MoveableFlags) & (Flags ^ m_MoveableFlags);
    ModifyMoveableFlags(toAdd, toRemove);
}

/*************************************************
Summary: ModifyMoveableFlags - Modifies moveable flags with proper side effects.
Purpose: Adds and removes flags, syncs with object flags, notifies scene graph.

Implementation based on decompilation at 0x1000579f:
- Clears 'Remove' bits from m_MoveableFlags
- Sets 'Add' bits to m_MoveableFlags
- If VX_MOVEABLE_VISIBLE or VX_MOVEABLE_HIERARCHICALHIDE changed:
  - Sync with m_ObjectFlags (CK_OBJECT_VISIBLE = 0x1000, CK_OBJECT_HIERACHICALHIDE = 0x200)
  - Notify scene graph node via EntityFlagsChanged
- If VX_MOVEABLE_RENDERLAST or VX_MOVEABLE_RENDERFIRST changed:
  - Update scene graph node priority (10000 for RENDERFIRST, 0 otherwise)
*************************************************/
CKDWORD RCK3dEntity::ModifyMoveableFlags(CKDWORD Add, CKDWORD Remove) {
    // Apply flag changes
    m_MoveableFlags &= ~Remove;
    m_MoveableFlags |= Add;
    
    // Check if visibility-related flags changed
    if (((Remove | Add) & (VX_MOVEABLE_VISIBLE | VX_MOVEABLE_HIERARCHICALHIDE)) != 0) {
        // Sync VX_MOVEABLE_VISIBLE with CK_OBJECT_VISIBLE
        if ((Remove & VX_MOVEABLE_VISIBLE) != 0) {
            // Clear CK_OBJECT_VISIBLE (0x1000) - IDA mask: 0xFFFFEFFF
            m_ObjectFlags &= ~CK_OBJECT_VISIBLE;
        }
        if ((Add & VX_MOVEABLE_VISIBLE) != 0) {
            // Set CK_OBJECT_VISIBLE
            m_ObjectFlags |= CK_OBJECT_VISIBLE;
        }
        
        // Sync VX_MOVEABLE_HIERARCHICALHIDE with CK_OBJECT_HIERACHICALHIDE
        if ((Remove & VX_MOVEABLE_HIERARCHICALHIDE) != 0) {
            // Clear CK_OBJECT_HIERACHICALHIDE (0x200)
            m_ObjectFlags &= ~CK_OBJECT_HIERACHICALHIDE;
        }
        if ((Add & VX_MOVEABLE_HIERARCHICALHIDE) != 0) {
            // Set CK_OBJECT_HIERACHICALHIDE
            m_ObjectFlags |= CK_OBJECT_HIERACHICALHIDE;
        }
        
        // Notify scene graph node
        if (m_SceneGraphNode) {
            m_SceneGraphNode->EntityFlagsChanged(1);
        }
    }
    
    // Check if render order flags changed
    if (((Remove | Add) & (VX_MOVEABLE_RENDERLAST | VX_MOVEABLE_RENDERFIRST)) != 0) {
        if (m_SceneGraphNode) {
            if ((Remove & VX_MOVEABLE_RENDERFIRST) != 0) {
                m_SceneGraphNode->SetPriority(0, 0);
            }
            if ((Add & VX_MOVEABLE_RENDERFIRST) != 0) {
                m_SceneGraphNode->SetPriority(10000, 0);
            }
        }
    }
    
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

/*************************************************
Summary: Rotate - Rotates the entity around an axis by an angle.
Purpose: Applies a rotation to the entity's orientation.

Implementation based on decompilation at 0x100077f5:
- Check if angle is significant (> ~0.00000012)
- If Ref, transform axis from Ref space to world space using TransformVector
- Build rotation matrix from (transformed) axis and angle
- Multiply rotation by current world matrix
- Preserve original position (row 3)
- Call SetWorldMatrix to apply
*************************************************/
void RCK3dEntity::Rotate(const VxVector *Axis, float Angle, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Axis)
        return;
    
    // IDA: if ( sub_100518A0(Angle) >= 0.00000011920929 ) - check angle threshold
    if (fabsf(Angle) < 0.00000011920929f)
        return;
    
    // Save original position
    VxVector originalPos(m_WorldMatrix[3][0], m_WorldMatrix[3][1], m_WorldMatrix[3][2]);
    
    // Transform axis if Ref is provided
    VxVector worldAxis;
    if (Ref) {
        // IDA: Vx3DRotateVector(&v10, &Ref->m_WorldMatrix, Axis);
        Vx3DRotateVector(&worldAxis, ((RCK3dEntity*)Ref)->m_WorldMatrix, Axis);
    } else {
        worldAxis = *Axis;
    }
    
    // Build rotation matrix from axis-angle
    VxMatrix rot;
    Vx3DMatrixFromRotation(rot, worldAxis, Angle);

    // IDA: Vx3DMultiplyMatrix(&v11, &v8, &this->m_WorldMatrix);
    VxMatrix newWorld;
    Vx3DMultiplyMatrix(newWorld, rot, m_WorldMatrix);

    // IDA: Preserve original position in row 3
    newWorld[3][0] = originalPos.x;
    newWorld[3][1] = originalPos.y;
    newWorld[3][2] = originalPos.z;

    // IDA: this->SetWorldMatrix(this, &v11, KeepChildren);
    SetWorldMatrix(newWorld, KeepChildren);
}

void RCK3dEntity::Translate3f(float X, float Y, float Z, CK3dEntity *Ref, CKBOOL KeepChildren) {
    VxVector trans(X, Y, Z);
    Translate(&trans, Ref, KeepChildren);
}

/*************************************************
Summary: Translate - Translates the entity by a vector.
Purpose: Adds a displacement to the current world position.

Implementation based on decompilation at 0x10006bf9:
- If Ref, transform the vector from Ref space to world space (direction only)
- Add the transformed vector to current world position
- Call WorldPositionChanged to propagate
*************************************************/
void RCK3dEntity::Translate(const VxVector *Vect, CK3dEntity *Ref, CKBOOL KeepChildren) {
    if (!Vect)
        return;

    // IDA: v12 = *Vect; if (Ref) Ref->TransformVector(Ref, &v12, Vect, 0);
    VxVector trans = *Vect;
    if (Ref) {
        // Transform direction vector from Ref's local space to world space
        ((RCK3dEntity *)Ref)->TransformVector(&trans, Vect, nullptr);
    }
    
    // IDA: Add to world matrix row 3 (translation)
    m_WorldMatrix[3][0] += trans.x;
    m_WorldMatrix[3][1] += trans.y;
    m_WorldMatrix[3][2] += trans.z;
    
    // Propagate changes
    WorldPositionChanged(KeepChildren, 1);
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

/*************************************************
Summary: SetPosition - Sets the position of the entity.
Purpose: Modifies the translation component of the world matrix.

Implementation based on decompilation at 0x10006ad8:
- Gets pointer to row 3 of world matrix (translation component)
- If Ref, transforms position from Ref space to world space
- Otherwise, directly copies position
- Calls WorldPositionChanged to update matrices and propagate to children
*************************************************/
void RCK3dEntity::SetPosition(const VxVector *Pos, CK3dEntity *Ref, CKBOOL KeepChildren) {
    // IDA: v4 = sub_1000C160(&this->m_WorldMatrix, 3); // Get row 3 (translation)
    //      v5 = sub_1000CC40(v4, 0); // Get column 0
    //      if ( Ref ) Ref->Transform(Ref, (VxVector *)v5, Pos, 0);
    //      else memcpy(v5, Pos, 0xCu);
    //      RCK3dEntity::WorldPositionChanged(this, KeepChildren, 1);
    
    if (!Pos)
        return;
    
    // Get pointer to translation component (row 3) of world matrix
    float *worldPos = &m_WorldMatrix[3][0];
    
    if (Ref) {
        // Transform position from Ref space to world space
        VxVector result;
        ((RCK3dEntity *)Ref)->Transform(&result, Pos, nullptr);
        worldPos[0] = result.x;
        worldPos[1] = result.y;
        worldPos[2] = result.z;
    } else {
        // Direct copy
        worldPos[0] = Pos->x;
        worldPos[1] = Pos->y;
        worldPos[2] = Pos->z;
    }
    
    // Propagate changes - keepScale=1 preserves scale in bounding box calculations
    WorldPositionChanged(KeepChildren, 1);
}

/*************************************************
Summary: GetPosition - Gets the position of the entity.
Purpose: Returns the translation component of the world or local matrix.

Implementation based on decompilation at 0x10006b51:
- If Ref is our parent: return local matrix translation (position relative to parent)
- If Ref is another entity: return world position transformed to Ref's space
- If Ref is null: return world position directly
*************************************************/
void RCK3dEntity::GetPosition(VxVector *Pos, CK3dEntity *Ref) const {
    if (!Pos)
        return;

    if (Ref) {
        if (Ref == (CK3dEntity *)m_Parent) {
            // IDA: Return local matrix row 3 (position relative to parent)
            Pos->x = m_LocalMatrix[3][0];
            Pos->y = m_LocalMatrix[3][1];
            Pos->z = m_LocalMatrix[3][2];
        } else {
            // Transform world position to Ref's local space
            VxVector worldPos(m_WorldMatrix[3][0], m_WorldMatrix[3][1], m_WorldMatrix[3][2]);
            ((RCK3dEntity *)Ref)->InverseTransform(Pos, &worldPos, nullptr);
        }
    } else {
        // Return world matrix row 3 (absolute world position)
        Pos->x = m_WorldMatrix[3][0];
        Pos->y = m_WorldMatrix[3][1];
        Pos->z = m_WorldMatrix[3][2];
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
    // IDA @ 0x1000b8bf
    // Call base class first
    CKBeObject::PreDelete();

    // Iterate through children - detach those not being deleted
    int index = 0;
    int childCount = GetChildrenCount();
    while (childCount-- > 0) {
        RCK3dEntity *child = (RCK3dEntity *)GetChild(index);
        if (!child || (child->GetObjectFlags() & CK_OBJECT_TOBEDELETED)) {
            // Child is null or being deleted, skip
            ++index;
        } else {
            // Child is not being deleted, detach from this parent
            child->SetParent(nullptr, TRUE);
        }
    }

    // Handle parent relationship
    RCK3dEntity *parent = (RCK3dEntity *)GetParent();
    if (parent) {
        // If parent is not being deleted, detach from parent
        if (!(parent->GetObjectFlags() & CK_OBJECT_TOBEDELETED)) {
            SetParent(nullptr, TRUE);
        }
    } else {
        // No parent - remove scene graph node from its parent
        if (m_SceneGraphNode) {
            CKSceneGraphNode *graphParent = m_SceneGraphNode->m_Parent;
            if (graphParent) {
                graphParent->RemoveNode(m_SceneGraphNode);
            }
        }
    }
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
    if (m_ObjectAnimations) {
        for (CKObject **it = m_ObjectAnimations->Begin(); it != m_ObjectAnimations->End(); ++it) {
            if (*it == o) return TRUE;
        }
    }

    return CKRenderObject::IsObjectUsed(o, cid);
}

CKERROR RCK3dEntity::PrepareDependencies(CKDependenciesContext &context) {
    // IDA: PrepareDependencies at 0x1000b4d5
    CKERROR err = CKBeObject::PrepareDependencies(context);
    if (err != CK_OK)
        return err;
    
    // Get class-specific dependency flags for CKCID_3DENTITY (33)
    CKDWORD classDeps = context.GetClassDependencies(CKCID_3DENTITY);
    
    // Bit 0 (1): Process meshes
    if (classDeps & 1) {
        for (int i = 0; i < GetMeshCount(); ++i) {
            CKMesh *mesh = GetMesh(i);
            if (mesh)
                mesh->PrepareDependencies(context);
        }
    }
    
    // Bit 1 (2): Process children
    if (classDeps & 2) {
        for (int i = 0; i < GetChildrenCount(); ++i) {
            CK3dEntity *child = GetChild(i);
            if (child)
                child->PrepareDependencies(context);
        }
    }
    
    // Bit 2 (4): Process object animations
    if (classDeps & 4) {
        for (int i = 0; i < GetObjectAnimationCount(); ++i) {
            CKObjectAnimation *anim = GetObjectAnimation(i);
            if (anim)
                anim->PrepareDependencies(context);
        }
    }
    
    // Process skin bones if skin exists and (not mode REPLACE or meshes flag is set)
    // IDA: if ( this->m_Skin && (!sub_1000CE10(a2, 2) || (ClassDependencies & 1) != 0) )
    // sub_1000CE10 checks if mode == 2 (CK_DEPENDENCIES_REPLACE)
    if (m_Skin && (!context.IsInMode(CK_DEPENDENCIES_REPLACE) || (classDeps & 1))) {
        CKMesh *currentMesh = GetCurrentMesh();
        if (currentMesh)
            currentMesh->PrepareDependencies(context);
        
        for (int i = 0; i < m_Skin->GetBoneCount(); ++i) {
            CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
            if (boneData) {
                CK3dEntity *boneEntity = boneData->GetBone();
                if (boneEntity)
                    boneEntity->PrepareDependencies(context);
            }
        }
    }
    
    return context.FinishPrepareDependencies(this, m_ClassID);
}

/*************************************************
Summary: RemapDependencies - Remaps object pointers after a copy/paste operation.
Purpose: Updates internal object references to point to newly created copies.

Implementation based on decompilation at 0x1000b6dd:
- Calls CKBeObject::RemapDependencies
- Gets ClassDependencies for mode 33
- If ClassID == 42 (CKCID_CHARACTER), adds flag 2 to ClassDependencies
- Remaps m_Place
- If flag 1: Remaps m_Meshs array and m_CurrentMesh
- If flag 2: Remaps parent via GetParent/SetParent
- If flag 4: Remaps m_ObjectAnimations
- Remaps skin bones if m_Skin exists
*************************************************/
CKERROR RCK3dEntity::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKBeObject::RemapDependencies(context);
    if (err != CK_OK)
        return err;
    
    // Get class dependencies and check for CKCID_CHARACTER special case
    CKDWORD classDeps = context.GetClassDependencies(CKCID_3DENTITY);
    if (GetClassID() == CKCID_CHARACTER)
        classDeps |= 2;
    
    // Remap m_Place (sub_1000CDA0 remaps an ID/pointer)
    m_Place = (CK_ID)context.Remap((CKObject *)(CKDWORD)m_Place);
    
    // Remap meshes if flag 1 set
    if (classDeps & 1) {
        m_Meshes.Remap(context);
        RCKMesh *remappedMesh = (RCKMesh *)context.Remap((CKObject *)m_CurrentMesh);
        SetCurrentMesh(remappedMesh, FALSE);
    }
    
    // Remap parent if flag 2 set
    if (classDeps & 2) {
        CK3dEntity *parent = GetParent();
        CK3dEntity *remappedParent = (CK3dEntity *)context.Remap((CKObject *)parent);
        if (remappedParent)
            SetParent(remappedParent, TRUE);
    }
    
    // Remap object animations if flag 4 set
    if ((classDeps & 4) && m_ObjectAnimations) {
        m_ObjectAnimations->Remap(context);
    }
    
    // Remap skin bones
    if (m_Skin) {
        int boneCount = m_Skin->GetBoneCount();
        for (int i = 0; i < boneCount; ++i) {
            CKSkinBoneData *boneData = m_Skin->GetBoneData(i);
            if (boneData) {
                // Remap bone entity
                CK3dEntity *bone = boneData->GetBone();
                CK3dEntity *remappedBone = (CK3dEntity *)context.Remap((CKObject *)bone);
                boneData->SetBone(remappedBone);
                
                // Remap bone's parent if exists
                if (remappedBone) {
                    CK3dEntity *boneParent = remappedBone->GetParent();
                    CK3dEntity *remappedBoneParent = (CK3dEntity *)context.Remap((CKObject *)boneParent);
                    if (remappedBoneParent)
                        remappedBone->SetParent(remappedBoneParent, TRUE);
                }
            }
        }
    }
    
    return CK_OK;
}

/*************************************************
Summary: Copy - Copies entity data from source object.
Purpose: Deep copies all relevant entity data respecting dependency context.

Implementation based on decompilation at 0x1000ba8b:
- SetFlags from source
- Call CKBeObject::Copy
- SetFlags from source again
- Copy MoveableFlags masked with 0xFF3FBFEB
- Copy Parent via GetParent/SetParent
- Copy ZOrder via GetZOrder/SetZOrder
- Copy WorldMatrix
- Copy m_Meshs array
- Copy CurrentMesh via GetCurrentMesh/SetCurrentMesh
- If ClassDependencies flag 4: Copy ObjectAnimations
- If source has Skin: Create new Skin and copy
*************************************************/
CKERROR RCK3dEntity::Copy(CKObject &o, CKDependenciesContext &context) {
    RCK3dEntity &src = (RCK3dEntity &)o;
    
    // Set flags from source before CKBeObject::Copy
    SetFlags(src.GetFlags());
    
    CKERROR err = CKBeObject::Copy(o, context);
    if (err != CK_OK)
        return err;
    
    // Get class dependencies
    CKDWORD classDeps = context.GetClassDependencies(CKCID_3DENTITY);
    
    // Set flags from source again
    SetFlags(src.GetFlags());
    
    // Copy moveable flags with mask 0xFF3FBFEB
    CKDWORD moveableFlags = src.GetMoveableFlags() & 0xFF3FBFEB;
    SetMoveableFlags(moveableFlags);
    
    // Copy parent
    CK3dEntity *srcParent = src.GetParent();
    SetParent(srcParent, TRUE);
    
    // Copy ZOrder
    int zorder = src.GetZOrder();
    SetZOrder(zorder);
    
    // Copy world matrix
    SetWorldMatrix(src.m_WorldMatrix, TRUE);
    
    // Copy meshes array
    m_Meshes = src.m_Meshes;
    
    // Copy current mesh
    CKMesh *srcMesh = src.GetCurrentMesh();
    SetCurrentMesh((RCKMesh *)srcMesh, FALSE);
    
    // Copy object animations if flag 4 set
    if (classDeps & 4) {
        int animCount = src.GetObjectAnimationCount();
        for (int i = 0; i < animCount; ++i) {
            CKObjectAnimation *anim = src.GetObjectAnimation(i);
            AddObjectAnimation(anim);
        }
    }
    
    // Copy skin if source has one
    if (src.m_Skin) {
        m_Skin = new RCKSkin(*src.m_Skin);
    }
    
    return CK_OK;
}

/*************************************************
Summary: AddToScene - Adds entity and optionally dependencies to a scene.
Purpose: Registers entity with scene, optionally adding meshes, animations, and children.

Implementation based on decompilation at 0x10009e01:
- Returns if scene is NULL
- Calls CKBeObject::AddToScene
- If dependencies flag set:
  - Adds all meshes to scene
  - Adds all object animations to scene
  - Adds children that are not already in scene
*************************************************/
void RCK3dEntity::AddToScene(CKScene *scene, CKBOOL dependencies) {
    if (!scene)
        return;
    
    CKBeObject::AddToScene(scene, dependencies);
    
    if (dependencies) {
        // Add meshes to scene
        for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
            CKBeObject *mesh = (CKBeObject *)*it;
            if (mesh)
                mesh->AddToScene(scene, dependencies);
        }
        
        // Add object animations to scene
        if (m_ObjectAnimations) {
            for (CKObject **it = m_ObjectAnimations->Begin(); it != m_ObjectAnimations->End(); ++it) {
                CKBeObject *anim = (CKBeObject *)*it;
                if (anim)
                    anim->AddToScene(scene, dependencies);
            }
        }
        
        // Add children that are not already in scene
        int childCount = GetChildrenCount();
        for (int i = 0; i < childCount; ++i) {
            RCK3dEntity *child = (RCK3dEntity *)GetChild(i);
            if (child && !child->IsInScene(scene))
                child->AddToScene(scene, dependencies);
        }
    }
}

/*************************************************
Summary: RemoveFromScene - Removes entity and optionally dependencies from a scene.
Purpose: Unregisters entity from scene, optionally removing meshes and animations.

Implementation based on decompilation at 0x10009f41:
- Returns if scene is NULL
- Calls CKBeObject::RemoveFromScene
- If dependencies flag set:
  - Removes all meshes from scene
  - Removes all object animations from scene
Note: Does NOT recursively remove children (different from AddToScene)
*************************************************/
void RCK3dEntity::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    if (!scene)
        return;
    
    CKBeObject::RemoveFromScene(scene, dependencies);
    
    if (dependencies) {
        // Remove meshes from scene
        for (CKObject **it = m_Meshes.Begin(); it != m_Meshes.End(); ++it) {
            CKBeObject *mesh = (CKBeObject *)*it;
            if (mesh)
                mesh->RemoveFromScene(scene, dependencies);
        }
        
        // Remove object animations from scene
        if (m_ObjectAnimations) {
            for (CKObject **it = m_ObjectAnimations->Begin(); it != m_ObjectAnimations->End(); ++it) {
                CKBeObject *anim = (CKBeObject *)*it;
                if (anim)
                    anim->RemoveFromScene(scene, dependencies);
            }
        }
    }
}

/*************************************************
Summary: SetZOrder - Sets the rendering Z-order (priority).
Purpose: Uses scene graph node priority for render ordering.
*************************************************/
void RCK3dEntity::SetZOrder(int Z) {
    if (m_SceneGraphNode) {
        m_SceneGraphNode->SetPriority((CKWORD)Z, 0);
    }
}

/*************************************************
Summary: GetZOrder - Gets the rendering Z-order (priority).
Purpose: Returns scene graph node priority.
*************************************************/
int RCK3dEntity::GetZOrder() {
    if (m_SceneGraphNode) {
        return m_SceneGraphNode->m_Priority;
    }
    return 0;
}

/*************************************************
Summary: IsToBeRenderedLast - Check if entity should render after opaque objects.
Purpose: Used for transparency sorting in render pipeline.

Implementation based on decompilation at 0x10005910:
- If VX_MOVEABLE_ZBUFONLY (0x100000) is set, return FALSE (render early)
- If VX_MOVEABLE_RENDERLAST (0x10000) is set, return TRUE
- Else if mesh exists, return mesh->IsTransparent()
- Else return FALSE
*************************************************/
CKBOOL RCK3dEntity::IsToBeRenderedLast() {
    // IDA: if ( (this->m_MoveableFlags & 0x100000) != 0 ) return 0;
    if ((m_MoveableFlags & VX_MOVEABLE_ZBUFONLY) != 0)
        return FALSE;
    
    // IDA: if ( (this->m_MoveableFlags & 0x10000) != 0 ) return 1;
    if ((m_MoveableFlags & VX_MOVEABLE_RENDERLAST) != 0)
        return TRUE;
    
    // IDA: if ( this->m_CurrentMesh ) return this->m_CurrentMesh->IsTransparent(this->m_CurrentMesh);
    if (m_CurrentMesh) {
        return m_CurrentMesh->IsTransparent();
    }
    
    return FALSE;
}

/*************************************************
Summary: IsToBeRendered - Check if entity should be rendered.
Purpose: Combines visibility check with mesh/callback availability.

Implementation based on decompilation at 0x100058be:
- Must have VX_MOVEABLE_VISIBLE (0x2) set
- AND must have either:
  - A current mesh with VXMESH_VISIBLE (0x2) flag, OR
  - Callbacks registered
*************************************************/
CKBOOL RCK3dEntity::IsToBeRendered() {
    // IDA: return (this->m_MoveableFlags & 2) != 0 
    //     && (this->m_CurrentMesh && (this->m_CurrentMesh->m_Flags & 2) != 0 || this->m_Callbacks);
    if ((m_MoveableFlags & VX_MOVEABLE_VISIBLE) == 0)
        return FALSE;
    
    if (m_CurrentMesh && (m_CurrentMesh->GetFlags() & VXMESH_VISIBLE) != 0)
        return TRUE;
    
    if (m_Callbacks)
        return TRUE;
    
    return FALSE;
}

/*************************************************
Summary: WorldMatrixChanged - Called when the world matrix has been directly modified.
Purpose: Updates internal state, local matrix, and propagates changes to children.

Implementation based on decompilation at 0x100062d6:
- Invalidates scene graph node's bounding box
- Clears ALLINSIDE (0x1000) and ALLOUTSIDE (0x2000) flags, invalidates inverse matrix (0x400000)
- If HASMOVED (0x20000) is set, clears 0x40000000; else sets HASMOVED and notifies RenderManager
- Updates local matrix: if parent exists, LocalMatrix = ParentInverse * WorldMatrix; else LocalMatrix = WorldMatrix
- If updateChildren: recalculate inverse and update children's local matrices to keep world position
- Else: propagate LocalMatrixChanged to children (so they move with parent)
*************************************************/
void RCK3dEntity::WorldMatrixChanged(int updateChildren, int keepScale) {
    // Invalidate scene graph node's bounding box
    if (m_SceneGraphNode) {
        m_SceneGraphNode->InvalidateBox(keepScale);
    }
    
    // Clear ALLINSIDE, ALLOUTSIDE, and INVERSEWORLDMATVALID flags
    // IDA: this->m_MoveableFlags &= 0xFFBBFFFB (clears bits 0x440004 - but 0xFFBBFFFB means clear ~0x440004)
    m_MoveableFlags &= 0xFFBBFFFB;
    
    // Check VX_MOVEABLE_HASMOVED (0x20000) flag
    if ((m_MoveableFlags & VX_MOVEABLE_HASMOVED) != 0) {
        // Already moved this frame - clear the "needs update" bit
        m_MoveableFlags &= ~0x40000000u;
    } else {
        // First move this frame - set HASMOVED and notify render manager
        m_MoveableFlags |= VX_MOVEABLE_HASMOVED;
        RCKRenderManager *renderManager = (RCKRenderManager *)m_Context->GetRenderManager();
        if (renderManager) {
            renderManager->AddMovedEntity(this);
        }
    }
    
    // Update local matrix based on parent relationship
    if (m_Parent) {
        // LocalMatrix = ParentInverseWorld * WorldMatrix
        const VxMatrix &parentInverse = m_Parent->GetInverseWorldMatrix();
        Vx3DMultiplyMatrix(m_LocalMatrix, parentInverse, m_WorldMatrix);
    } else {
        // No parent - local matrix equals world matrix
        memcpy(&m_LocalMatrix, &m_WorldMatrix, sizeof(VxMatrix));
    }
    
    // Handle children
    if (updateChildren) {
        // Keep children's world positions - recalculate their local matrices
        GetInverseWorldMatrix();  // Force recalculation of inverse matrix
        
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                // Child's local = OurInverse * Child's World
                Vx3DMultiplyMatrix(child->m_LocalMatrix, m_InverseWorldMatrix, child->m_WorldMatrix);
            }
        }
    } else {
        // Children move with parent - propagate local matrix changes
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                child->LocalMatrixChanged(0, 0);
            }
        }
    }
}

/*************************************************
Summary: LocalMatrixChanged - Called when the local matrix has been modified.
Purpose: Recomputes world matrix from local * parent's world, propagates to children.

Implementation based on decompilation at 0x100064a4:
- Invalidates scene graph node's bounding box
- Clears ALLINSIDE, ALLOUTSIDE, INVERSEWORLDMATVALID flags
- Handles HASMOVED flag and notifies render manager
- Updates world matrix: if parent exists, WorldMatrix = ParentWorld * LocalMatrix; else WorldMatrix = LocalMatrix
- Propagates changes to children similar to WorldMatrixChanged
*************************************************/
void RCK3dEntity::LocalMatrixChanged(int updateChildren, int keepScale) {
    // Invalidate scene graph node's bounding box
    if (m_SceneGraphNode) {
        m_SceneGraphNode->InvalidateBox(keepScale);
    }
    
    // Clear ALLINSIDE, ALLOUTSIDE, and INVERSEWORLDMATVALID flags
    m_MoveableFlags &= 0xFFBBFFFB;
    
    // Check VX_MOVEABLE_HASMOVED (0x20000) flag
    if ((m_MoveableFlags & VX_MOVEABLE_HASMOVED) != 0) {
        m_MoveableFlags &= ~0x40000000u;
    } else {
        m_MoveableFlags |= VX_MOVEABLE_HASMOVED;
        RCKRenderManager *renderManager = (RCKRenderManager *)m_Context->GetRenderManager();
        if (renderManager) {
            renderManager->AddMovedEntity(this);
        }
    }
    
    // Update world matrix based on parent relationship
    if (m_Parent) {
        // WorldMatrix = ParentWorld * LocalMatrix
        Vx3DMultiplyMatrix(m_WorldMatrix, m_Parent->m_WorldMatrix, m_LocalMatrix);
    } else {
        // No parent - world matrix equals local matrix
        memcpy(&m_WorldMatrix, &m_LocalMatrix, sizeof(VxMatrix));
    }
    
    // Handle children
    if (updateChildren) {
        // Keep children's world positions
        GetInverseWorldMatrix();
        
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                Vx3DMultiplyMatrix(child->m_LocalMatrix, m_InverseWorldMatrix, child->m_WorldMatrix);
            }
        }
    } else {
        // Children move with parent
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                child->LocalMatrixChanged(0, 0);
            }
        }
    }
}

/*************************************************
Summary: WorldPositionChanged - Called when only the position (translation) of world matrix changed.
Purpose: Optimized update when only position changes - updates inverse matrix incrementally.

Implementation based on decompilation at 0x1000666b:
- Invalidates scene graph bounding box
- Clears BOXVALID (0x4) and INVERSEWORLDMATVALID (0x400000) flags (mask 0xFFFBFFFB)
- Handles HASMOVED flag and notifies render manager
- Updates local matrix from parent's inverse * world matrix
- If inverse matrix was valid (0x400000), updates only the translation part incrementally
- Propagates changes to children similar to WorldMatrixChanged
*************************************************/
void RCK3dEntity::WorldPositionChanged(int updateChildren, int keepScale) {
    // Invalidate scene graph node's bounding box
    if (m_SceneGraphNode) {
        m_SceneGraphNode->InvalidateBox(keepScale);
    }
    
    // IDA: this->m_MoveableFlags &= 0xFFFBFFFB; (clears 0x4 BOXVALID and 0x400004)
    // This is different from WorldMatrixChanged which clears 0xFFBBFFFB
    m_MoveableFlags &= 0xFFFBFFFB;
    
    // Check VX_MOVEABLE_HASMOVED (0x20000) flag
    if ((m_MoveableFlags & VX_MOVEABLE_HASMOVED) != 0) {
        // Already moved this frame - clear the "needs update" bit (0x40000000)
        m_MoveableFlags &= ~0x40000000u;
    } else {
        // First move this frame - set HASMOVED and notify render manager
        m_MoveableFlags |= VX_MOVEABLE_HASMOVED;
        RCKRenderManager *renderManager = (RCKRenderManager *)m_Context->GetRenderManager();
        if (renderManager) {
            renderManager->AddMovedEntity(this);
        }
    }
    
    // Update local matrix based on parent relationship
    if (m_Parent) {
        // LocalMatrix = ParentInverseWorld * WorldMatrix
        const VxMatrix &parentInverse = m_Parent->GetInverseWorldMatrix();
        Vx3DMultiplyMatrix(m_LocalMatrix, parentInverse, m_WorldMatrix);
    } else {
        // No parent - local matrix equals world matrix
        memcpy(&m_LocalMatrix, &m_WorldMatrix, sizeof(VxMatrix));
    }
    
    // IDA: If inverse matrix was valid, update only the translation part incrementally
    // This is an optimization - we can update just the translation of the inverse
    // instead of recomputing the entire inverse matrix
    if ((m_MoveableFlags & VX_MOVEABLE_INVERSEWORLDMATVALID) != 0) {
        // IDA: Compute new inverse translation: inv_T = -(R^T * T)
        // Where R is the rotation part and T is the translation
        // inv[3][0] = -(inv[0][0]*world[3][0] + inv[1][0]*world[3][1] + inv[2][0]*world[3][2])
        // inv[3][1] = -(inv[0][1]*world[3][0] + inv[1][1]*world[3][1] + inv[2][1]*world[3][2])
        // inv[3][2] = -(inv[0][2]*world[3][0] + inv[1][2]*world[3][1] + inv[2][2]*world[3][2])
        float tx = m_WorldMatrix[3][0];
        float ty = m_WorldMatrix[3][1];
        float tz = m_WorldMatrix[3][2];
        
        m_InverseWorldMatrix[3][0] = -(m_InverseWorldMatrix[0][0] * tx + 
                                       m_InverseWorldMatrix[1][0] * ty + 
                                       m_InverseWorldMatrix[2][0] * tz);
        m_InverseWorldMatrix[3][1] = -(m_InverseWorldMatrix[0][1] * tx + 
                                       m_InverseWorldMatrix[1][1] * ty + 
                                       m_InverseWorldMatrix[2][1] * tz);
        m_InverseWorldMatrix[3][2] = -(m_InverseWorldMatrix[0][2] * tx + 
                                       m_InverseWorldMatrix[1][2] * ty + 
                                       m_InverseWorldMatrix[2][2] * tz);
    }
    
    // Handle children
    if (updateChildren) {
        // Keep children's world positions - recalculate their local matrices
        GetInverseWorldMatrix();  // Force recalculation of inverse matrix if needed
        
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                // Child's local = OurInverse * Child's World
                Vx3DMultiplyMatrix(child->m_LocalMatrix, m_InverseWorldMatrix, child->m_WorldMatrix);
            }
        }
    } else {
        // Children move with parent - propagate local matrix changes
        for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK3dEntity *child = (RCK3dEntity *)*it;
            if (child && (child->m_MoveableFlags & VX_MOVEABLE_DONTUPDATEFROMPARENT) == 0) {
                child->LocalMatrixChanged(0, 0);
            }
        }
    }
}

// =====================================================
// Place Hierarchy Management
// =====================================================

/*************************************************
Summary: SaveLastFrameMatrix - Saves the current world matrix as the last frame matrix.
Purpose: Used for motion blur / temporal interpolation between frames.

Implementation based on decompilation at 0x100087BD:
- Allocates m_LastFrameMatrix if not already allocated
- Copies current world matrix to last frame matrix
*************************************************/
void RCK3dEntity::SaveLastFrameMatrix() {
    if (!m_LastFrameMatrix) {
        m_LastFrameMatrix = new VxMatrix();
    }
    memcpy(m_LastFrameMatrix, &m_WorldMatrix, sizeof(VxMatrix));
}

void RCK3dEntity::UpdatePlace(CK_ID placeId) {
    // IDA: UpdatePlace at 0x100092C3
    // Set this entity's place and recursively update all children
    m_Place = placeId;
    
    for (CKObject **it = m_Children.Begin(); it != m_Children.End(); ++it) {
        RCK3dEntity *child = (RCK3dEntity *)*it;
        if (child) {
            child->UpdatePlace(placeId);
        }
    }
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
        if (m_Callbacks->m_Callback) {
            ((CK_RENDEROBJECT_CALLBACK) m_Callbacks->m_Callback->callback)(Dev, (CK3dEntity *) this, m_Callbacks->m_Callback->argument);
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
        dev->AddExtents2D(m_RenderExtents, this);
    }

    return TRUE;
}

int RCK3dEntity::RayIntersection(const VxVector *Pos1, const VxVector *Pos2, VxIntersectionDesc *Desc, CK3dEntity *Ref, CK_RAYINTERSECTION iOptions) {
    if (!m_CurrentMesh || !Pos1 || !Pos2 || !Desc)
        return 0;

    RCKMesh *mesh = m_CurrentMesh;
    if (!mesh)
        return 0;

    // Transform the ray into this entity's local space
    VxVector originLocal, endLocal;
    if (Ref) {
        VxMatrix invRef;
        Vx3DInverseMatrix(invRef, Ref->GetWorldMatrix());
        Vx3DMultiplyMatrixVector(&originLocal, invRef, Pos1);
        Vx3DMultiplyMatrixVector(&endLocal, invRef, Pos2);
    } else {
        const VxMatrix &invWorld = GetInverseWorldMatrix();
        Vx3DMultiplyMatrixVector(&originLocal, invWorld, Pos1);
        Vx3DMultiplyMatrixVector(&endLocal, invWorld, Pos2);
    }

    VxVector direction = endLocal - originLocal;
    return g_RayIntersection ? g_RayIntersection(mesh, originLocal, direction, Desc, iOptions, m_WorldMatrix) : 0;
}

void RCK3dEntity::GetRenderExtents(VxRect &rect) const {
    rect = m_RenderExtents;
}

const VxMatrix &RCK3dEntity::GetLastFrameMatrix() const {
    // IDA at 0x10008850: returns address of m_WorldMatrix (same as GetWorldMatrix)
    // The m_LastFrameMatrix is only allocated/used when CK_OBJECT_FREEMOTION flag is set
    // For now, return world matrix as the "last frame" matrix
    return m_WorldMatrix;
}

/*************************************************
Summary: SetLocalMatrix - Sets the local transformation matrix.
Purpose: Updates local matrix and propagates change via LocalMatrixChanged.

Implementation based on decompilation at 0x10008874:
- Copies the matrix to m_LocalMatrix
- Calls LocalMatrixChanged(KeepChildren, 1)
*************************************************/
void RCK3dEntity::SetLocalMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    // IDA: memcpy(v3, v4, 0x40u); // Copy 64-byte matrix
    memcpy(&m_LocalMatrix, &Mat, sizeof(VxMatrix));
    // IDA: RCK3dEntity::LocalMatrixChanged(this, a2, 1);
    LocalMatrixChanged(KeepChildren, 1);
}

const VxMatrix &RCK3dEntity::GetLocalMatrix() const {
    return m_LocalMatrix;
}

/*************************************************
Summary: SetWorldMatrix - Sets the world transformation matrix.
Purpose: Updates world matrix and propagates change via WorldMatrixChanged.

Implementation based on decompilation at 0x100088c4:
- Copies the matrix to m_WorldMatrix via qmemcpy
- Calls WorldMatrixChanged(KeepChildren, 1)
*************************************************/
void RCK3dEntity::SetWorldMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    // IDA: qmemcpy(&this->m_WorldMatrix, arg0, sizeof(this->m_WorldMatrix));
    memcpy(&m_WorldMatrix, &Mat, sizeof(VxMatrix));
    // IDA: RCK3dEntity::WorldMatrixChanged(this, a2, 1);
    WorldMatrixChanged(KeepChildren, 1);
}

const VxMatrix &RCK3dEntity::GetWorldMatrix() const {
    return m_WorldMatrix;
}

/*************************************************
Summary: GetInverseWorldMatrix - Returns the inverse of the world matrix.
Purpose: Uses lazy evaluation with caching via INVERSEWORLDMATVALID flag.

Implementation based on decompilation at 0x10008909:
- If VX_MOVEABLE_INVERSEWORLDMATVALID (0x400000) is NOT set:
  - Set the flag
  - Compute inverse: m_InverseWorldMatrix = inverse(m_WorldMatrix)
- Return reference to cached inverse matrix
*************************************************/
const VxMatrix &RCK3dEntity::GetInverseWorldMatrix() const {
    // Lazy evaluation - compute inverse only when needed
    // IDA: if ( (this->m_MoveableFlags & 0x400000) == 0 )
    if ((m_MoveableFlags & VX_MOVEABLE_INVERSEWORLDMATVALID) == 0) {
        // Mark as valid and compute
        // Note: const_cast required as this is logically const (caching)
        RCK3dEntity *mutableThis = const_cast<RCK3dEntity *>(this);
        mutableThis->m_MoveableFlags |= VX_MOVEABLE_INVERSEWORLDMATVALID;
        Vx3DInverseMatrix(mutableThis->m_InverseWorldMatrix, m_WorldMatrix);
    }
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
    // Preserve current world transform, then reparent and recompute local matrix
    VxMatrix world = GetWorldMatrix();

    if (Ref) {
        VxMatrix invRef;
        Vx3DInverseMatrix(invRef, Ref->GetWorldMatrix());

        VxMatrix newLocal;
        Vx3DMultiplyMatrix(newLocal, invRef, world);
        SetParent(Ref, TRUE);
        SetLocalMatrix(newLocal, TRUE);
    } else {
        // Removing referential: keep world as local and clear parent
        SetParent(nullptr, TRUE);
        SetLocalMatrix(world, TRUE);
    }
}

CKPlace *RCK3dEntity::GetReferencePlace() const {
    return (CKPlace *)m_Context->GetObject(m_Place);
}

void RCK3dEntity::AddObjectAnimation(CKObjectAnimation *anim) {
    if (anim) {
        // Allocate animation array if needed (IDA: operator new(0xCu))
        if (!m_ObjectAnimations) {
            m_ObjectAnimations = new XObjectPointerArray();
        }
        m_ObjectAnimations->PushBack(anim);
    }
}

void RCK3dEntity::RemoveObjectAnimation(CKObjectAnimation *anim) {
    if (!m_ObjectAnimations) return;
    for (CKObject **it = m_ObjectAnimations->Begin(); it != m_ObjectAnimations->End(); ++it) {
        if (*it == anim) {
            m_ObjectAnimations->Remove(it);
            return;
        }
    }
}

CKObjectAnimation *RCK3dEntity::GetObjectAnimation(int index) const {
    if (!m_ObjectAnimations || index < 0 || index >= m_ObjectAnimations->Size())
        return nullptr;
    return (CKObjectAnimation *) (*m_ObjectAnimations)[index];
}

int RCK3dEntity::GetObjectAnimationCount() const {
    return m_ObjectAnimations ? m_ObjectAnimations->Size() : 0;
}

CKSkin *RCK3dEntity::CreateSkin() {
    if (m_Skin)
        return m_Skin;

    m_Skin = new RCKSkin();
    m_Skin->SetObjectInitMatrix(GetWorldMatrix());
    return m_Skin;
}

CKBOOL RCK3dEntity::DestroySkin() {
    if (m_Skin) {
        if (m_CurrentMesh) {
            m_CurrentMesh->SetFlags(m_CurrentMesh->GetFlags() & ~0x200000);
        }

        delete m_Skin;
        m_Skin = nullptr;
        return TRUE;
    }
    return FALSE;
}

CKBOOL RCK3dEntity::UpdateSkin() {
    if (!m_Skin || !m_CurrentMesh)
        return FALSE;

    CKDWORD vStride = 0;
    CKBYTE *vertexPtr = m_CurrentMesh->GetModifierVertices(&vStride);
    int vertexCount = m_CurrentMesh->GetModifierVertexCount();

    CKDWORD nStride = 0;
    CKBYTE *normalPtr = nullptr;
    if (m_Skin->GetNormalCount() > 0) {
        normalPtr = (CKBYTE *)m_CurrentMesh->GetNormalsPtr(&nStride);
    }

    if (!vertexPtr || vertexCount == 0)
        return FALSE;

    CKBOOL result = m_Skin->CalcPoints(vertexCount, vertexPtr, vStride, normalPtr, nStride);

    if (result) {
        m_CurrentMesh->UpdateBoundingVolumes(TRUE);
        m_MoveableFlags |= VX_MOVEABLE_UPTODATE | VX_MOVEABLE_BOXVALID;
    }

    return result;
}

CKSkin *RCK3dEntity::GetSkin() const {
    return (CKSkin *) m_Skin;
}

/*************************************************
Summary: UpdateBox - Updates the entity's bounding boxes.
Purpose: Computes local and world bounding boxes from mesh or user-defined box.

Implementation based on decompilation at 0x10006113:
- If VX_MOVEABLE_USERBOX (0x10) is set: use user-defined box, transform to world
- If mesh exists and not up-to-date: get box from mesh, merge with skin if present, transform to world
- If no mesh: use world position as point box
- Sets VX_MOVEABLE_BOXVALID (0x4000) when box is up-to-date
*************************************************/
void RCK3dEntity::UpdateBox(CKBOOL World) {
    // IDA: if ( (this->m_MoveableFlags & 0x10) != 0 )  - VX_MOVEABLE_USERBOX
    if ((m_MoveableFlags & VX_MOVEABLE_USERBOX) != 0) {
        // User-defined bounding box - just transform local to world
        // IDA: BYTE1(m_MoveableFlags) |= 0x40u; -> sets 0x4000 (BOXVALID)
        m_MoveableFlags |= VX_MOVEABLE_BOXVALID;
        // Transform local box to world using VxBbox::TransformFrom
        m_WorldBoundingBox.TransformFrom(m_LocalBoundingBox, m_WorldMatrix);
    }
    else if (m_CurrentMesh) {
        // IDA: if ( (this->m_MoveableFlags & 4) == 0 || (this->m_CurrentMesh->m_Flags & 1) == 0 )
        // Only update if box is not valid OR mesh changed (flag 1 = mesh dirty)
        if ((m_MoveableFlags & VX_MOVEABLE_UPTODATE) == 0 || 
            (m_CurrentMesh->GetFlags() & 0x1) == 0) {
            
            // Set BOXVALID flag
            m_MoveableFlags |= VX_MOVEABLE_BOXVALID;
            
            // Get local box from mesh
            m_LocalBoundingBox = m_CurrentMesh->GetLocalBox();
            
            // IDA: if ( this->m_Skin ) - merge skin bounding box
            if (m_Skin) {
                // Skinning updates vertex positions directly; refresh deformation first
                UpdateSkin();
                m_CurrentMesh->UpdateBoundingVolumes(TRUE);
                m_LocalBoundingBox = m_CurrentMesh->GetLocalBox();
            }
            
            // Transform local to world
            m_WorldBoundingBox.TransformFrom(m_LocalBoundingBox, m_WorldMatrix);
            
            // Set UPTODATE flag to indicate box is valid
            m_MoveableFlags |= VX_MOVEABLE_UPTODATE;
        }
    }
    else {
        // No mesh - use world position as point box (min=max=position)
        // IDA: Get row 3 (translation) and set both min and max to it
        VxVector pos(m_WorldMatrix[3][0], m_WorldMatrix[3][1], m_WorldMatrix[3][2]);
        m_WorldBoundingBox.Max = pos;
        m_WorldBoundingBox.Min = pos;
        
        // Clear local box
        memset(&m_LocalBoundingBox, 0, sizeof(VxBbox));
        
        // Clear BOXVALID flag - IDA: BYTE1(v7) &= ~0x40u;
        m_MoveableFlags &= ~VX_MOVEABLE_BOXVALID;
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

/*************************************************
Summary: Show - Shows or hides the entity.
Purpose: Controls entity visibility and hierarchical hiding.

Implementation based on decompilation at 0x10005494:
- Calls CKObject::Show() first
- Clears hierarchy-affecting flags
- Sets VX_MOVEABLE_VISIBLE based on show parameter
- Sets VX_MOVEABLE_HIERARCHICALHIDE if hiding children
- Notifies scene graph of flag changes
*************************************************/
void RCK3dEntity::Show(CK_OBJECT_SHOWOPTION show) {
    // IDA: 0x10005494
    ENTITY_DEBUG_LOG_FMT("Show called: entity=%p show=0x%x", this, show);
    
    // Call base class Show
    CKObject::Show(show);
    
    // Clear hierarchy-affecting flags: preserve critical flags but clear transient ones
    // IDA: this->m_MoveableFlags &= 0x8800BFC8 | preserved_flags
    // The mask ~0x77FF4037 preserves flags that shouldn't change on Show/Hide
    // This clears: ALLINSIDE(0x1000), ALLOUTSIDE(0x2000), HIERARCHICALHIDE(0x10000000), etc.
    const CKDWORD preserve_mask = 0x8800BFC8 | (VX_MOVEABLE_PICKABLE | VX_MOVEABLE_VISIBLE |
                                                 VX_MOVEABLE_UPTODATE | VX_MOVEABLE_USERBOX |
                                                 VX_MOVEABLE_EXTENTSUPTODATE | VX_MOVEABLE_BOXVALID |
                                                 VX_MOVEABLE_RENDERLAST | VX_MOVEABLE_HASMOVED |
                                                 VX_MOVEABLE_WORLDALIGNED | VX_MOVEABLE_NOZBUFFERWRITE |
                                                 VX_MOVEABLE_RENDERFIRST | VX_MOVEABLE_NOZBUFFERTEST |
                                                 VX_MOVEABLE_INVERSEWORLDMATVALID | VX_MOVEABLE_DONTUPDATEFROMPARENT |
                                                 VX_MOVEABLE_INDIRECTMATRIX | VX_MOVEABLE_ZBUFONLY |
                                                 VX_MOVEABLE_STENCILONLY | VX_MOVEABLE_CHARACTERRENDERED |
                                                 VX_MOVEABLE_RESERVED2);
    m_MoveableFlags &= preserve_mask;
    
    // Set or clear VX_MOVEABLE_VISIBLE (0x2) based on show parameter
    // show & 1 means "show" (CKSHOW = 1)
    // show & 2 means "hide children too" (CKHIDE_HIERARCHY = 2)
    if ((show & 1) != 0) {
        m_MoveableFlags |= VX_MOVEABLE_VISIBLE;
        ENTITY_DEBUG_LOG_FMT("Show: entity=%p made VISIBLE", this);
    } else {
        m_MoveableFlags &= ~VX_MOVEABLE_VISIBLE;
        ENTITY_DEBUG_LOG_FMT("Show: entity=%p made INVISIBLE", this);
        
        // If hiding children too, set hierarchical hide flag
        if ((show & 2) != 0) {
            m_MoveableFlags |= VX_MOVEABLE_HIERARCHICALHIDE;
            ENTITY_DEBUG_LOG_FMT("Show: entity=%p set HIERARCHICALHIDE", this);
        }
    }
    
    // Notify scene graph node about flag changes
    if (m_SceneGraphNode) {
        m_SceneGraphNode->EntityFlagsChanged(TRUE);
    }
}

/*************************************************
Summary: IsHiddenByParent - Checks if any parent has hierarchical hide flag.
Purpose: Used for visibility determination in scene graph.

Implementation based on decompilation at 0x10005556:
- Walks up the parent chain
- Returns TRUE if any parent has VX_MOVEABLE_HIERARCHICALHIDE (0x10000000) set
- Returns FALSE if no parent has the flag
Note: Original uses m_MoveableFlags, NOT CK_OBJECT_VISIBLE
*************************************************/
CKBOOL RCK3dEntity::IsHiddenByParent() {
    for (RCK3dEntity *parent = m_Parent; parent != nullptr; parent = parent->m_Parent) {
        if ((parent->m_MoveableFlags & VX_MOVEABLE_HIERARCHICALHIDE) != 0) {
            return TRUE;
        }
    }
    return FALSE;
}

/*************************************************
Summary: IsVisible - Returns TRUE if the entity should be rendered.
Purpose: Combines object visibility with parent hierarchy check.

Implementation based on decompilation at 0x1000552b:
- Returns CKObject::IsVisible() && !IsHiddenByParent()
*************************************************/
CKBOOL RCK3dEntity::IsVisible() {
    return CKObject::IsVisible() && !IsHiddenByParent();
}

CKBOOL RCK3dEntity::IsInViewFrustrum(CKRenderContext *rc, CKDWORD flags) {
    if (!rc)
        return FALSE;

    RCKRenderContext *dev = (RCKRenderContext *) rc;

    // Check if bounding box is in view frustum
    if (dev->m_RasterizerContext) {
        return dev->m_RasterizerContext->ComputeBoxVisibility(m_WorldBoundingBox, TRUE, nullptr) != 0;
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
    CKClassNeedNotificationFrom(m_ClassID, CKCID_OBJECTANIMATION);
    CKClassNeedNotificationFrom(m_ClassID, CKCID_MESH);
    CKClassNeedNotificationFrom(m_ClassID, CKCID_3DENTITY);

    // Register associated parameter GUID: {5B8A05D5, 31EA28D4}
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_3DENTITY);

    // Register default dependencies for different modes
    CKClassRegisterDefaultDependencies(m_ClassID, 6, 1); // Copy mode
    CKClassRegisterDefaultDependencies(m_ClassID, 4, 2); // Delete mode
    CKClassRegisterDefaultDependencies(m_ClassID, 7, 4); // Save mode
}

CK3dEntity *RCK3dEntity::CreateInstance(CKContext *Context) {
    // Based on IDA decompilation at 0x1000b45f
    // Object size is 0x1A8 (424 bytes)
    RCK3dEntity *ent = new RCK3dEntity(Context, nullptr);
    return reinterpret_cast<CK3dEntity *>(ent);
}
