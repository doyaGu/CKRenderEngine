#include "RCKTargetCamera.h"
#include "RCKCamera.h"
#include "RCK3dEntity.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "VxMath.h"

// Static class ID definition
CK_CLASSID RCKTargetCamera::m_ClassID = CKCID_TARGETCAMERA;

/*************************************************
Summary: Constructor for RCKTargetCamera.
Purpose: Initializes target camera with no target.
Remarks:
- Calls base class RCKCamera constructor
- Sets m_Target to 0 (no target)

Implementation based on decompilation at 0x10043ae0.
*************************************************/
RCKTargetCamera::RCKTargetCamera(CKContext *Context, CKSTRING name)
    : RCKCamera(Context, name), m_Target(0) {
}

/*************************************************
Summary: Destructor for RCKTargetCamera.
Purpose: Cleans up target camera resources.
Remarks:
- Base class destructor handles camera cleanup
*************************************************/
RCKTargetCamera::~RCKTargetCamera() {
    // Base class destructor handles cleanup
}

//=============================================================================
// Target Methods
//=============================================================================

/*************************************************
Summary: Gets the target entity of this camera.
Return Value: Pointer to the target CK3dEntity, or NULL if no target.
Remarks:
- Uses m_Context to resolve the target ID to an object

Implementation based on decompilation at 0x10043b69.
*************************************************/
CK3dEntity *RCKTargetCamera::GetTarget() {
    return static_cast<CK3dEntity *>(m_Context->GetObject(m_Target));
}

/*************************************************
Summary: Sets the target entity of this camera.
Purpose: Sets an entity that the camera will look at.
Remarks:
- Clears flags on old target, sets flags on new target
- Uses object flags to manage target state:
  - Clears flag 0x200 and sets flag 0x2 on old target
  - Sets flag 0x200 and clears flag 0x2 on new target
- Stores target object ID

Implementation based on decompilation at 0x10043b89.
*************************************************/
void RCKTargetCamera::SetTarget(CK3dEntity *target) {
    // Don't allow setting self as target
    if (target && target->GetID() == GetID())
        return;

    // Get old target
    CK3dEntity *oldTarget = static_cast<CK3dEntity *>(m_Context->GetObject(m_Target));

    // Don't do anything if same target
    if (oldTarget == target)
        return;

    // Clear flags on old target
    if (oldTarget) {
        CKDWORD flags = oldTarget->GetFlags();
        flags &= ~0x200; // Clear "is target" flag
        flags |= 0x2;    // Set some other flag
        oldTarget->SetFlags(flags);
    }

    // Set flags on new target
    if (target) {
        CKDWORD flags = target->GetFlags();
        flags |= 0x200; // Set "is target" flag
        flags &= ~0x2;  // Clear some other flag
        target->SetFlags(flags);
        m_Target = target->GetID();
    } else {
        m_Target = 0;
    }
}

//=============================================================================
// CKObject Overrides
//=============================================================================

/*************************************************
Summary: Returns the class ID of this object.
Return Value: CKCID_TARGETCAMERA class identifier.

Implementation based on decompilation at 0x10043d39.
*************************************************/
CK_CLASSID RCKTargetCamera::GetClassID() {
    return m_ClassID;
}

/*************************************************
Summary: Returns the memory footprint of this object.
Return Value: Memory size in bytes.
Remarks:
- Adds 4 bytes for m_Target to base class size

Implementation based on decompilation at 0x10043d7f.
*************************************************/
int RCKTargetCamera::GetMemoryOccupation() {
    return RCKCamera::GetMemoryOccupation() + 4;
}

/*************************************************
Summary: Copies target camera data from another object.
Purpose: Deep copy including target reference.
Remarks:
- Calls base class Copy first
- Copies m_Target ID

Implementation based on decompilation at 0x10044107.
*************************************************/
CKERROR RCKTargetCamera::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCKCamera::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKTargetCamera *srcCamera = static_cast<RCKTargetCamera *>(&o);
    m_Target = srcCamera->m_Target;

    return CK_OK;
}

/*************************************************
Summary: PreSave method for RCKTargetCamera.
Purpose: Prepares target camera for saving by handling target object dependencies.
Remarks:
- Calls base class RCK3dEntity::PreSave() first to handle entity dependencies
- Saves the target object for later serialization
- Ensures target object is properly saved with the camera data

Implementation based on decompilation at 0x10043DD4:
- Simple implementation that only saves the target object
- Uses CKContext::GetObjectA to retrieve target object by ID
- Uses CKFile::SaveObject for dependency handling

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior and dependency handling
*************************************************/
void RCKTargetCamera::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first to handle entity dependencies
    RCK3dEntity::PreSave(file, flags);

    // Save target object
    CKObject *targetObject = m_Context->GetObject(m_Target);
    file->SaveObject(targetObject, flags);
}

/*************************************************
Summary: Save method for RCKTargetCamera.
Purpose: Saves target camera data to a state chunk including target object reference.
Remarks:
- Calls base class RCKCamera::Save() first to handle camera data
- Creates target camera-specific state chunk with identifier 0x10000000
- Saves target object reference for camera orientation
- Maintains backward compatibility with base camera serialization

Implementation based on decompilation at 0x10043E10:
- Uses chunk identifier 0x10000000 for target-specific data
- Retrieves target object using CKContext::GetObjectA
- Saves target object reference in the state chunk
- Handles both file and memory-based saving modes

Arguments:
- file: The file context for saving (may be NULL for memory operations)
- flags: Save flags controlling behavior and data inclusion
Return Value:
- CKStateChunk*: The created state chunk containing target camera data
*************************************************/
CKStateChunk *RCKTargetCamera::Save(CKFile *file, CKDWORD flags) {
    // Call base class save first to handle camera data
    CKStateChunk *baseChunk = RCKCamera::Save(file, flags);

    // Return early if no file context and not in specific save modes
    if (!file && (flags & 0x70000000) == 0) {
        return baseChunk;
    }

    // Create target camera-specific state chunk
    CKStateChunk *targetCameraChunk = CreateCKStateChunk(CKCID_TARGETCAMERA, file);
    if (!targetCameraChunk) {
        return baseChunk;
    }

    targetCameraChunk->StartWrite();
    targetCameraChunk->AddChunkAndDelete(baseChunk);

    // Write target-specific data with identifier 0x10000000
    targetCameraChunk->WriteIdentifier(0x10000000);

    // Save target object reference
    CKObject *targetObject = m_Context->GetObject(m_Target);
    targetCameraChunk->WriteObject(targetObject);

    // Close chunk appropriately based on class type
    if (GetClassID() == CKCID_TARGETCAMERA) {
        targetCameraChunk->CloseChunk();
    } else {
        targetCameraChunk->UpdateDataSize();
    }

    return targetCameraChunk;
}

/*************************************************
Summary: Load method for RCKTargetCamera.
Purpose: Loads target camera data from a state chunk including target object reference.
Remarks:
- Calls base class RCKCamera::Load() first to handle camera data
- Loads target object ID from state chunk
- Stores target object ID for later object resolution
- Maintains backward compatibility with base camera deserialization

Implementation based on decompilation at 0x10043EBC:
- Uses chunk identifier 0x10000000 for target-specific data
- Reads target object ID using CKStateChunk::ReadObjectID
- Stores target ID for later object resolution
- Simple implementation focused on target reference restoration

Arguments:
- chunk: The state chunk containing target camera data
- file: The file context for loading (may be NULL)
Return Value:
- CKERROR: 0 for success, -1 for invalid chunk
*************************************************/
CKERROR RCKTargetCamera::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) {
        return CKERR_INVALIDPARAMETER;
    }

    // Call base class load first to handle camera data
    RCKCamera::Load(chunk, file);

    // Load target object ID
    if (chunk->SeekIdentifier(0x10000000)) {
        m_Target = chunk->ReadObjectID();
    }

    return CK_OK;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CKSTRING RCKTargetCamera::GetClassName() {
    return (CKSTRING) "Target Camera";
}

int RCKTargetCamera::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKTargetCamera::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKTargetCamera::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(RCKTargetCamera::m_ClassID, CKCID_3DENTITY);

    // Register associated parameter GUID: CKPGUID_TARGETCAMERA
    CKClassRegisterAssociatedParameter(RCKTargetCamera::m_ClassID, CKPGUID_TARGETCAMERA);

    // Register default dependencies
    CKClassRegisterDefaultDependencies(RCKTargetCamera::m_ClassID, 1, CK_DEPENDENCIES_COPY);
}

CKTargetCamera *RCKTargetCamera::CreateInstance(CKContext *Context) {
    RCKTargetCamera *cam = new RCKTargetCamera(Context, nullptr);
    return reinterpret_cast<CKTargetCamera *>(cam);
}
