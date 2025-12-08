#include "RCKCharacter.h"
#include "RCK3dEntity.h"
#include "RCKBodyPart.h"
#include "RCKAnimation.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKScene.h"
#include "VxMath.h"

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKCharacter::RCKCharacter(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name) {
}

RCKCharacter::~RCKCharacter() {
}

//=============================================================================
// Serialization Methods
//=============================================================================

/*************************************************
Summary: PreSave method for RCKCharacter.
Purpose: Prepares character for saving by handling body parts and animations dependencies.
Remarks:
- Calls base class RCK3dEntity::PreSave() first to handle entity dependencies
- Saves the current body part object for later serialization
- Saves all body parts in the character's body parts array
- Saves all animations in the character's animations array

Implementation based on decompilation at 0x100125C7:
- Saves current body part object with file context
- Iterates through body parts array and saves all objects
- Iterates through animations array and saves all objects
- Uses CKFile::SaveObject and CKFile::SaveObjects for dependency handling

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior and dependency handling
*************************************************/
void RCKCharacter::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first to handle entity dependencies
    RCK3dEntity::PreSave(file, flags);

    // Save current body part object (offset 448)
    CKObject *currentBodyPart = *(CKObject **) ((char *) this + 448);
    file->SaveObject(currentBodyPart, 0xFFFFFFFF);

    // Save all body parts in the character (offset 424)
    XSObjectPointerArray *bodyParts = (XSObjectPointerArray *) ((char *) this + 424);
    if (bodyParts->Size() > 0) {
        CKObject **bodyPartsBegin = (CKObject **) bodyParts->Begin();
        int bodyPartsCount = bodyParts->Size();
        file->SaveObjects(bodyPartsBegin, bodyPartsCount, 0xFFFFFFFF);
    }

    // Save all animations in the character (offset 432)
    XSObjectPointerArray *animations = (XSObjectPointerArray *) ((char *) this + 432);
    if (animations->Size() > 0) {
        CKObject **animationsBegin = (CKObject **) animations->Begin();
        int animationsCount = animations->Size();
        file->SaveObjects(animationsBegin, animationsCount, 0xFFFFFFFF);
    }
}

/*************************************************
Summary: Save method for RCKCharacter.
Purpose: Saves character data to a state chunk including body parts, animations, and character-specific properties.
Remarks:
- Calls base class RCK3dEntity::Save() first to handle entity data
- Creates character-specific state chunk with multiple identifiers
- Saves body parts array with identifier 0x400000
- Optionally saves body parts as sub-chunks for detailed serialization
- Saves animations array with identifier 0xFFC00000 (file context only)
- Saves character object references in sequence (target, up vector, current body part, root)

Implementation based on decompilation at 0x10012648:
- Uses chunk identifier 0x400000 for body parts array
- Uses chunk identifier 0x10000000 for optional sub-chunk sequence
- Uses chunk identifier 0xFFC00000 for animations array
- Saves object sequence: target, up vector, current body part, root
- Handles both file and memory-based saving modes

Arguments:
- file: The file context for saving (may be NULL for memory operations)
- flags: Save flags controlling behavior and data inclusion
Return Value:
- CKStateChunk*: The created state chunk containing character data
*************************************************/
CKStateChunk *RCKCharacter::Save(CKFile *file, CKDWORD flags) {
    // Call base class save first to handle entity data
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // Return early if no file context and not in specific save modes
    if (!file && (flags & 0xFFC00000) == 0) {
        return baseChunk;
    }

    // Create character-specific state chunk
    CKStateChunk *characterChunk = CreateCKStateChunk(CKCID_CHARACTER, file);
    if (!characterChunk) {
        return baseChunk;
    }

    characterChunk->StartWrite();
    characterChunk->AddChunkAndDelete(baseChunk);

    // Save body parts array with identifier 0x400000 (offset 424)
    XSObjectPointerArray *bodyParts = (XSObjectPointerArray *) ((char *) this + 424);
    characterChunk->WriteIdentifier(0x400000);
    bodyParts->Save(characterChunk);

    // Optionally save body parts as sub-chunks for detailed serialization
    if (!file && (flags & 0x10000000) != 0) {
        characterChunk->WriteIdentifier(0x10000000);
        int bodyPartsCount = bodyParts->Size();
        characterChunk->StartSubChunkSequence(bodyPartsCount);

        // Save each body part as a sub-chunk
        for (CKObject **bodyPart = (CKObject **) bodyParts->Begin();
             bodyPart != bodyParts->End(); ++bodyPart) {
            CKStateChunk *subChunk = nullptr;
            if (*bodyPart) {
                subChunk = (*bodyPart)->Save(nullptr, 0x7FFFFFFF);
            }
            characterChunk->WriteSubChunkSequence(subChunk);
            if (subChunk) {
                DeleteCKStateChunk(subChunk);
            }
        }
    }

    // Save animations array with identifier 0xFFC00000 (file context only) (offset 432)
    XSObjectPointerArray *animations = (XSObjectPointerArray *) ((char *) this + 432);
    characterChunk->WriteIdentifier(0xFFC00000);
    if (file) {
        animations->Save(characterChunk);
    }

    // Save character object references in sequence
    characterChunk->StartObjectIDSequence(4);
    characterChunk->WriteObjectSequence(*(CKObject **) ((char *) this + 452)); // target
    characterChunk->WriteObjectSequence(*(CKObject **) ((char *) this + 456)); // up vector
    characterChunk->WriteObjectSequence(*(CKObject **) ((char *) this + 448)); // current body part
    characterChunk->WriteObjectSequence(*(CKObject **) ((char *) this + 472)); // root

    // Close chunk appropriately based on class type
    if (GetClassID() == CKCID_CHARACTER) {
        characterChunk->CloseChunk();
    } else {
        characterChunk->UpdateDataSize();
    }

    return characterChunk;
}

/*************************************************
Summary: Load method for RCKCharacter.
Purpose: Loads character data from a state chunk including body parts, animations, and character properties.
Remarks:
- Calls base class RCK3dEntity::Load() first to handle entity data
- Supports both legacy format (data version < 5) and current format
- Legacy format handles file context and memory context separately
- Current format uses identifiers 0x400000, 0x10000000, and 0xFFC00000
- Loads body parts array and optionally processes sub-chunks
- Loads animations array and character object references
- Handles fallback for current body part if not loaded

Implementation based on decompilation at 0x10012824:
- Legacy format: separate handling for file and memory contexts
- Current format: unified loading with sequence processing
- Loads body parts with identifier 0x400000
- Processes sub-chunk sequence with identifier 0x10000000
- Loads animations with identifier 0xFFC00000
- Restores object references: target, up vector, current body part, root

Arguments:
- chunk: The state chunk containing character data
- file: The file context for loading (may be NULL)
Return Value:
- CKERROR: 0 for success, -1 for invalid chunk
*************************************************/
CKERROR RCKCharacter::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) {
        return CKERR_INVALIDPARAMETER;
    }

    // Call base class load first to handle entity data
    RCK3dEntity::Load(chunk, file);

    // Get member arrays using offsets
    XSObjectPointerArray *bodyParts = (XSObjectPointerArray *) ((char *) this + 424);
    XSObjectPointerArray *animations = (XSObjectPointerArray *) ((char *) this + 432);

    // Handle legacy format (data version < 5)
    if (chunk->GetDataVersion() < 5) {
        if (file) {
            // File context loading for legacy format
            if (chunk->SeekIdentifier(0x400000)) {
                bodyParts->Clear();
                bodyParts->Load(m_Context, chunk);
            }

            if (chunk->SeekIdentifier(0x1000000)) {
                animations->Clear();
                animations->Load(m_Context, chunk);
                *(CKObject **) ((char *) this + 452) = chunk->ReadObject(m_Context); // target
                *(CKObject **) ((char *) this + 456) = chunk->ReadObject(m_Context); // up vector
            }
        } else {
            // Memory context loading for legacy format
            if (chunk->SeekIdentifier(0x4000000)) {
                chunk->ReadDword();                                                  // Skip unknown value
                *(CKObject **) ((char *) this + 452) = chunk->ReadObject(m_Context); // target
                *(CKObject **) ((char *) this + 456) = chunk->ReadObject(m_Context); // up vector
            }

            if (chunk->SeekIdentifier(0x10000000)) {
                int bodyPartsCount = chunk->ReadDword();
                for (int i = 0; i < bodyPartsCount; ++i) {
                    CKDWORD objectID = chunk->ReadObjectID();
                    CKObject *bodyPart = m_Context->GetObject(objectID);
                    CKStateChunk *subChunk = chunk->ReadSubChunk();
                    if (bodyPart && subChunk) {
                        bodyPart->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                }
            }
        }

        // Load current body part and root
        if (chunk->SeekIdentifier(0x2000000)) {
            *(CKObject **) ((char *) this + 448) = chunk->ReadObject(m_Context); // current body part
        }

        if (chunk->SeekIdentifier(0x20000000)) {
            *(CKObject **) ((char *) this + 472) = chunk->ReadObject(m_Context); // root
        }
    }
    // Current format (data version >= 5)
    else {
        // Load body parts array
        if (chunk->SeekIdentifier(0x400000)) {
            bodyParts->Clear();
            bodyParts->Load(m_Context, chunk);
        }

        // Process sub-chunk sequence for body parts
        if (chunk->SeekIdentifier(0x10000000)) {
            int sequenceCount = chunk->StartReadSequence();
            if (sequenceCount == bodyParts->Size()) {
                for (CKObject **bodyPart = (CKObject **) bodyParts->Begin();
                     bodyPart != bodyParts->End(); ++bodyPart) {
                    CKStateChunk *subChunk = chunk->ReadSubChunk();
                    if (*bodyPart && subChunk) {
                        (*bodyPart)->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                }
            }
        }

        // Load animations array and character object references
        if (chunk->SeekIdentifier(0xFFC00000)) {
            if (file) {
                animations->Clear();
                animations->Load(m_Context, chunk);
            }

            chunk->StartReadSequence();
            *(CKObject **) ((char *) this + 452) = chunk->ReadObject(m_Context); // target
            *(CKObject **) ((char *) this + 456) = chunk->ReadObject(m_Context); // up vector
            *(CKObject **) ((char *) this + 448) = chunk->ReadObject(m_Context); // current body part
            *(CKObject **) ((char *) this + 472) = chunk->ReadObject(m_Context); // root

            // Set fallback current body part if not loaded
            // Note: GetCurrentBodyPart() method not available in current implementation
        }
    }

    return CK_OK;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCKCharacter::m_ClassID = CKCID_CHARACTER;

CKSTRING RCKCharacter::GetClassName() {
    return (CKSTRING) "Character";
}

int RCKCharacter::GetDependenciesCount(int mode) {
    // Based on IDA analysis
    switch (mode) {
    case 1: return 1; // Copy mode
    case 2: return 0; // Delete mode
    case 3: return 0; // Replace mode
    case 4: return 0; // Save mode
    default: return 0;
    }
}

CKSTRING RCKCharacter::GetDependencies(int i, int mode) {
    // Based on IDA analysis
    if (i == 0) {
        return (CKSTRING) "ShareAnimation";
    }
    return nullptr;
}

void RCKCharacter::Register() {
    // Based on IDA analysis at address
    CKClassNeedNotificationFrom(RCKCharacter::m_ClassID, CKCID_ANIMATION);
    CKClassNeedNotificationFrom(RCKCharacter::m_ClassID, CKCID_BODYPART);

    // Register associated parameter GUID: CKPGUID_CHARACTER
    CKClassRegisterAssociatedParameter(RCKCharacter::m_ClassID, CKPGUID_CHARACTER);

    // Register default dependencies
    CKClassRegisterDefaultDependencies(RCKCharacter::m_ClassID, 1, CK_DEPENDENCIES_COPY);
}

CKCharacter *RCKCharacter::CreateInstance(CKContext *Context) {
    // Object size is 0x1EC (492 bytes)
    RCKCharacter *character = new RCKCharacter(Context, nullptr);
    return reinterpret_cast<CKCharacter *>(character);
}

//=============================================================================
// CKCharacter Virtual Methods Implementation
//=============================================================================

CK_CLASSID RCKCharacter::GetClassID() {
    return m_ClassID;
}

void RCKCharacter::CheckPreDeletion() {
    RCK3dEntity::CheckPreDeletion();
}

int RCKCharacter::GetMemoryOccupation() {
    return sizeof(RCKCharacter);
}

int RCKCharacter::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    return RCK3dEntity::IsObjectUsed(o, cid);
}

CKERROR RCKCharacter::PrepareDependencies(CKDependenciesContext &context) {
    return RCK3dEntity::PrepareDependencies(context);
}

CKERROR RCKCharacter::RemapDependencies(CKDependenciesContext &context) {
    return RCK3dEntity::RemapDependencies(context);
}

CKERROR RCKCharacter::Copy(CKObject &o, CKDependenciesContext &context) {
    return RCK3dEntity::Copy(o, context);
}

void RCKCharacter::AddToScene(CKScene *scene, CKBOOL dependencies) {
    RCK3dEntity::AddToScene(scene, dependencies);
}

void RCKCharacter::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    RCK3dEntity::RemoveFromScene(scene, dependencies);
}

void RCKCharacter::ApplyPatchForOlderVersion(int NbObject, CKFileObject *FileObjects) {
    RCK3dEntity::ApplyPatchForOlderVersion(NbObject, FileObjects);
}

const VxBbox &RCKCharacter::GetBoundingBox(CKBOOL Local) {
    return RCK3dEntity::GetBoundingBox(Local);
}

CKBOOL RCKCharacter::GetBaryCenter(VxVector *Pos) {
    return RCK3dEntity::GetBaryCenter(Pos);
}

float RCKCharacter::GetRadius() {
    return RCK3dEntity::GetRadius();
}

// Body Parts
CKERROR RCKCharacter::AddBodyPart(CKBodyPart *part) {
    return CK_OK;
}

CKERROR RCKCharacter::RemoveBodyPart(CKBodyPart *part) {
    return CK_OK;
}

CKBodyPart *RCKCharacter::GetRootBodyPart() {
    return nullptr;
}

CKERROR RCKCharacter::SetRootBodyPart(CKBodyPart *part) {
    return CK_OK;
}

CKBodyPart *RCKCharacter::GetBodyPart(int index) {
    return nullptr;
}

int RCKCharacter::GetBodyPartCount() {
    return 0;
}

// Animations
CKERROR RCKCharacter::AddAnimation(CKAnimation *anim) {
    return CK_OK;
}

CKERROR RCKCharacter::RemoveAnimation(CKAnimation *anim) {
    return CK_OK;
}

CKAnimation *RCKCharacter::GetAnimation(int index) {
    return nullptr;
}

int RCKCharacter::GetAnimationCount() {
    return 0;
}

CKAnimation *RCKCharacter::GetWarper() {
    return nullptr;
}

// Playing Animations
CKAnimation *RCKCharacter::GetActiveAnimation() {
    return nullptr;
}

CKAnimation *RCKCharacter::GetNextActiveAnimation() {
    return nullptr;
}

CKERROR RCKCharacter::SetActiveAnimation(CKAnimation *anim) {
    return CK_OK;
}

CKERROR RCKCharacter::SetNextActiveAnimation(CKAnimation *anim, CKDWORD transitionmode, float warplength) {
    return CK_OK;
}

// Animation processing
void RCKCharacter::ProcessAnimation(float deltat) {
}

void RCKCharacter::SetAutomaticProcess(CKBOOL process) {
}

CKBOOL RCKCharacter::IsAutomaticProcess() {
    return FALSE;
}

void RCKCharacter::GetEstimatedVelocity(float deltat, VxVector *velocity) {
    if (velocity) {
        velocity->x = 0.0f;
        velocity->y = 0.0f;
        velocity->z = 0.0f;
    }
}

// Secondary Animations
CKERROR RCKCharacter::PlaySecondaryAnimation(CKAnimation *anim, float StartingFrame,
                                             CK_SECONDARYANIMATION_FLAGS PlayFlags, float warplength, int LoopCount) {
    return CK_OK;
}

CKERROR RCKCharacter::StopSecondaryAnimation(CKAnimation *anim, CKBOOL warp, float warplength) {
    return CK_OK;
}

CKERROR RCKCharacter::StopSecondaryAnimation(CKAnimation *anim, float warplength) {
    return CK_OK;
}

int RCKCharacter::GetSecondaryAnimationsCount() {
    return 0;
}

CKAnimation *RCKCharacter::GetSecondaryAnimation(int index) {
    return nullptr;
}

void RCKCharacter::FlushSecondaryAnimations() {
}

void RCKCharacter::AlignCharacterWithRootPosition() {
}

CK3dEntity *RCKCharacter::GetFloorReferenceObject() {
    return nullptr;
}

void RCKCharacter::SetFloorReferenceObject(CK3dEntity *FloorRef) {
}

void RCKCharacter::SetAnimationLevelOfDetail(float LOD) {
}

float RCKCharacter::GetAnimationLevelOfDetail() {
    return 1.0f;
}

void RCKCharacter::GetWarperParameters(CKDWORD *TransitionMode, CKAnimation **AnimSrc, float *FrameSrc,
                                       CKAnimation **AnimDest, float *FrameDest) {
    if (TransitionMode) *TransitionMode = 0;
    if (AnimSrc) *AnimSrc = nullptr;
    if (FrameSrc) *FrameSrc = 0.0f;
    if (AnimDest) *AnimDest = nullptr;
    if (FrameDest) *FrameDest = 0.0f;
}
