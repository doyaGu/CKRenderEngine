#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKDependencies.h"
#include "CKGlobals.h"

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKKeyedAnimation::RCKKeyedAnimation(CKContext *Context, CKSTRING name)
    : RCKAnimation(Context, name),
      m_Merged(FALSE),
      m_MergeFactor(0.5f),
      m_RootAnimation(nullptr),
      m_Vector(0.0f, 0.0f, 0.0f) {}

RCKKeyedAnimation::~RCKKeyedAnimation() {
    RCKKeyedAnimation::Clear();
}

CK_CLASSID RCKKeyedAnimation::GetClassID() {
    return CKCID_KEYEDANIMATION;
}

//=============================================================================
// Serialization
//=============================================================================

CKStateChunk *RCKKeyedAnimation::Save(CKFile *file, CKDWORD flags) {
    // Create state chunk with class ID 18 (CKCID_KEYEDANIMATION)
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_KEYEDANIMATION, file);
    CKStateChunk *baseChunk = RCKAnimation::Save(file, flags);

    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Save animations list (identifier 0x1000)
    if (file || (flags & 0x1000) != 0) {
        chunk->WriteIdentifier(0x1000);
        m_Animations.Save(chunk);
    }

    // Save merged state and factor (identifier 0x100000)
    if (file || (flags & 0x100000) != 0) {
        chunk->WriteIdentifier(0x100000);
        chunk->WriteInt(m_Merged);
        chunk->WriteFloat(m_MergeFactor);
    }

    // Save animation sub-chunks (identifier 0x200000) - only when not saving to file
    if (!file && (flags & 0x200000) != 0) {
        chunk->WriteIdentifier(0x200000);
        chunk->WriteDword(m_Animations.Size());
        for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
            CKObject *obj = *it;
            CKStateChunk *subChunk = obj ? obj->Save(nullptr, flags) : nullptr;
            chunk->WriteObject(obj);
            chunk->WriteSubChunk(subChunk);
            if (subChunk) {
                DeleteCKStateChunk(subChunk);
            }
        }
    }

    // Close the chunk
    if (GetClassID() == CKCID_KEYEDANIMATION) {
        chunk->CloseChunk();
    } else {
        chunk->UpdateDataSize();
    }

    return chunk;
}

CKERROR RCKKeyedAnimation::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Load base class
    RCKAnimation::Load(chunk, file);

    // Load animations list (identifier 0x1000)
    if (chunk->SeekIdentifier(0x1000)) {
        m_Animations.Clear();
        m_Animations.Load(m_Context, chunk);
        m_Animations.Check();
    }

    // Load merged state and factor (identifier 0x100000)
    if (chunk->SeekIdentifier(0x100000)) {
        m_Merged = chunk->ReadInt();
        m_MergeFactor = chunk->ReadFloat();
    }

    // Load animation sub-chunks (identifier 0x200000) - only when not loading from file
    if (!file) {
        if (chunk->SeekIdentifier(0x200000)) {
            int count = chunk->ReadDword();
            for (int i = 0; i < count; ++i) {
                CKObject *obj = chunk->ReadObject(m_Context);
                CKStateChunk *subChunk = chunk->ReadSubChunk();
                if (obj) {
                    obj->Load(subChunk, nullptr);
                }
                if (subChunk) {
                    DeleteCKStateChunk(subChunk);
                }
            }
        }
        ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED);
        // Note: sub_100499C1(this) - internal function to update animation state
    }

    // Clear flag 0x40
    m_Flags &= ~0x40;

    // Process each animation and set up parent references
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *anim = (RCKObjectAnimation *) *it;
        if (anim) {
            // Set this as parent (sub_1004A2D0)
            // anim->SetParentAnimation(this);

            // Check for app data containing vector
            VxVector *appData = (VxVector *) anim->GetAppData();
            if (appData) {
                m_Vector = *appData;
                delete appData;
                anim->SetAppData(nullptr);
            }
        }
    }

    return CK_OK;
}

int RCKKeyedAnimation::GetMemoryOccupation() {
    return sizeof(RCKKeyedAnimation);
}

CKERROR RCKKeyedAnimation::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCKAnimation::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKKeyedAnimation *src = (RCKKeyedAnimation *) &o;
    m_RootAnimation = nullptr; // Clear root animation on copy
    // Copy animations array
    m_Animations.Clear();
    for (CKObject **it = src->m_Animations.Begin(); it != src->m_Animations.End(); ++it) {
        m_Animations.PushBack(*it);
    }
    m_Merged = src->m_Merged;
    m_MergeFactor = src->m_MergeFactor;
    m_Vector = src->m_Vector;

    return CK_OK;
}

//=============================================================================
// CKKeyedAnimation Virtual Methods
//=============================================================================

CKERROR RCKKeyedAnimation::AddAnimation(CKObjectAnimation *anim) {
    if (!anim)
        return CKERR_INVALIDPARAMETER;

    // Add to animations array
    m_Animations.PushBack(anim);

    // Set the parent keyed animation on the object animation
    RCKObjectAnimation *objAnim = (RCKObjectAnimation *) anim;
    objAnim->m_ParentKeyedAnimation = this;

    // Modify flags
    ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED); // Clear NOTTOBESAVED (0x400)
    m_Flags &= ~0x40;                             // Clear flag 0x40

    return CK_OK;
}

CKERROR RCKKeyedAnimation::RemoveAnimation(CKObjectAnimation *anim) {
    if (!anim)
        return CKERR_INVALIDPARAMETER;

    // Remove from animations array
    m_Animations.Remove(anim);

    // Modify flags
    ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED); // Clear NOTTOBESAVED (0x400)
    m_Flags &= ~0x40;                             // Clear flag 0x40

    return CK_OK;
}

int RCKKeyedAnimation::GetAnimationCount() {
    return m_Animations.Size();
}

CKObjectAnimation *RCKKeyedAnimation::GetAnimation(CK3dEntity *ent) {
    if (!ent)
        return nullptr;

    // Check if looking for root entity's animation
    if (ent == (CK3dEntity *) m_RootEntity) {
        if (m_RootAnimation)
            return (CKObjectAnimation *) m_RootAnimation;

        // Search for and cache root animation
        for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
            RCKObjectAnimation *anim = (RCKObjectAnimation *) *it;
            if (anim && anim->Get3dEntity() == ent) {
                m_RootAnimation = anim;
                return (CKObjectAnimation *) anim;
            }
        }
        return nullptr;
    }

    // Search for animation by entity
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        CKObjectAnimation *anim = (CKObjectAnimation *) *it;
        if (anim && anim->Get3dEntity() == ent) {
            return anim;
        }
    }
    return nullptr;
}

CKObjectAnimation *RCKKeyedAnimation::GetAnimation(int index) {
    if (index < 0 || index >= m_Animations.Size())
        return nullptr;
    return (CKObjectAnimation *) m_Animations[index];
}

void RCKKeyedAnimation::Clear() {
    m_Animations.Clear();
    m_RootAnimation = nullptr;
}

//=============================================================================
// Animation Manipulation Methods
//=============================================================================

void RCKKeyedAnimation::CenterAnimation(float frame) {
    // If there's already a root animation cached, use it
    // Otherwise, get the root animation from the root entity
    if (!m_RootAnimation) {
        m_RootAnimation = (RCKObjectAnimation *) GetAnimation((CK3dEntity *) m_RootEntity);
    }

    if (m_RootAnimation) {
        VxVector position;
        // Evaluate the position of the root animation at the given frame
        m_RootAnimation->EvaluatePosition(frame, position);

        // Negate the position to create the centering offset
        // sub_1000C0E0 negates a vector: (-x, -y, -z)
        m_Vector.x = -position.x;
        m_Vector.y = -position.y;
        m_Vector.z = -position.z;

        // Set the parent keyed animation on root animation
        // sub_1004A2D0 sets m_ParentKeyedAnimation
        m_RootAnimation->m_ParentKeyedAnimation = this;
    }
}

CKAnimation *RCKKeyedAnimation::CreateMergedAnimation(CKAnimation *anim2, CKBOOL dynamic) {
    if (!anim2)
        return nullptr;

    // Get the character associated with this animation
    CKCharacter *character = GetCharacter();
    if (!character)
        return nullptr;

    RCKKeyedAnimation *otherAnim = (RCKKeyedAnimation *) anim2;

    // Check if this animation is merged (has flag 0x80)
    CK_OBJECTCREATION_OPTIONS creationFlags = (m_Flags & 0x80)
                                                  ? CK_OBJECTCREATION_DYNAMIC
                                                  : CK_OBJECTCREATION_NONAMECHECK;

    // Create name for the merged animation
    char buffer[260];
    CKSTRING thisName = GetName();
    CKSTRING otherName = otherAnim->GetName();

    if (!thisName)
        thisName = "";
    if (!otherName)
        otherName = "";

    sprintf(buffer, "%s+%s", thisName, otherName);

    // Create a new keyed animation object
    CKObject *mergedObj = m_Context->CreateObject(CKCID_KEYEDANIMATION, buffer, creationFlags, nullptr);
    RCKKeyedAnimation *merged = (RCKKeyedAnimation *) mergedObj;

    // Set the character for the merged animation
    merged->m_Character = (RCKCharacter *) character;

    // Add merged animation to character's scenes
    ((CKBeObject *) character)->AddToSelfScenes((CKSceneObject *) mergedObj);

    // Set length to the max of both animations
    float maxLength = (otherAnim->GetLength() >= m_Length) ? otherAnim->GetLength() : m_Length;

    // Set merged animation properties
    merged->m_Merged = TRUE;
    merged->m_RootEntity = m_RootEntity;

    // Process all animations from this keyed animation
    int thisAnimCount = m_Animations.Size();
    int otherAnimCount = otherAnim->m_Animations.Size();

    for (int i = 0; i < thisAnimCount; ++i) {
        CKObjectAnimation *thisObjAnim = (CKObjectAnimation *) m_Animations[i];
        if (!thisObjAnim)
            continue;

        CK3dEntity *entity = thisObjAnim->Get3dEntity();
        if (!entity)
            continue;

        // Find matching entity in other animation
        CKObjectAnimation *otherObjAnim = otherAnim->GetAnimation(entity);

        if (otherObjAnim) {
            // Both animations have this entity, create merged object animation
            if (creationFlags == CK_OBJECTCREATION_DYNAMIC) {
                thisObjAnim->ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0);
            } else {
                thisObjAnim->ModifyObjectFlags(0, CK_OBJECT_DYNAMIC);
            }

            CKObjectAnimation *mergedObjAnim = thisObjAnim->CreateMergedAnimation(otherObjAnim, dynamic);
            if (mergedObjAnim) {
                merged->AddAnimation(mergedObjAnim);
            }
        } else if (!dynamic) {
            // Only this animation has this entity - copy it
            // For now, simplified: just use standard copy without detailed dependency control
            CKDependencies dep;
            dep.m_Flags = CK_DEPENDENCIES_CUSTOM;

            CKObjectAnimation *copiedAnim = (CKObjectAnimation *) m_Context->CopyObject(
                thisObjAnim, &dep, nullptr, creationFlags);

            if (copiedAnim) {
                merged->AddAnimation(copiedAnim);
            }
        }
    }

    // Process animations from other animation that are not in this animation
    if (!dynamic) {
        for (int j = 0; j < otherAnimCount; ++j) {
            CKObjectAnimation *otherObjAnim = (CKObjectAnimation *) otherAnim->GetAnimation(j);
            if (!otherObjAnim)
                continue;

            CK3dEntity *otherEntity = otherObjAnim->Get3dEntity();
            if (!otherEntity)
                continue;

            // Check if this entity already exists in merged animation
            CKObjectAnimation *existingAnim = merged->GetAnimation(otherEntity);

            if (!existingAnim) {
                // Copy this animation
                CKDependencies dep;
                dep.m_Flags = CK_DEPENDENCIES_CUSTOM;

                CKObjectAnimation *copiedAnim = (CKObjectAnimation *) m_Context->CopyObject(
                    otherObjAnim, &dep, nullptr, creationFlags);

                if (copiedAnim) {
                    merged->AddAnimation(copiedAnim);
                }
            }
        }
    }

    // Set the length of merged animation
    merged->SetLength(maxLength);

    // Clear the NOTTOBESAVED flag
    merged->ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED);

    // Update animation state
    merged->m_RootAnimation = nullptr;
    merged->m_RootEntity = nullptr;
    merged->ModifyObjectFlags(0, CK_OBJECT_NOTTOBESAVED);

    if (merged->m_Animations.Size() > 0) {
        CKObjectAnimation *firstAnim = merged->GetAnimation(0);
        if (firstAnim) {
            merged->m_RootEntity = (RCK3dEntity *) firstAnim->Get3dEntity();
        }
    }

    return (CKAnimation *) merged;
}

float RCKKeyedAnimation::CreateTransition(CKAnimation *in, CKAnimation *out,
                                          CKDWORD OutTransitionMode, float length, float FrameTo) {
    if (!in)
        return 0.0f;

    RCKKeyedAnimation *animIn = (RCKKeyedAnimation *) in;
    RCKKeyedAnimation *animOut = (RCKKeyedAnimation *) out;

    // Ensure length is at least 1 frame
    if (length < 1.0f)
        length = 1.0f;

    if (!animOut)
        return 0.0f;

    // Set flag 0x40 on this animation
    m_Flags |= 0x40;

    // Set flag 0x40 on both in and out animations
    if ((animOut->m_Flags & 0x40) == 0) {
        animOut->m_Flags |= 0x40;
    }

    if ((animIn->m_Flags & 0x40) == 0) {
        animIn->m_Flags |= 0x40;
    }

    // Handle OutTransitionMode flag 0x100 - compute FrameTo from animation length
    if ((OutTransitionMode & 0x100) == 0x100) {
        FrameTo = animOut->GetLength() * animIn->m_MergeFactor;
    }

    // If both animations are the same, return 0
    if (animOut == animIn)
        return 0.0f;

    // Get the length of the out animation
    float outLength = animOut->GetLength();

    // Compute step values
    float stepTo = FrameTo / animIn->m_MergeFactor;

    // Set up the root entity and character from animIn
    m_RootEntity = animIn->m_RootEntity;
    m_RootAnimation = nullptr;
    m_Character = animIn->m_Character;

    // Check that both animations have the same character
    if (animOut->m_Character != m_Character)
        return -1.0f;

    // Get the number of steps in animIn
    float inStepCount = fabs(animIn->GetLength());

    // Copy the flag 0x8 from animIn if present
    if (animIn->m_Flags & 0x8) {
        m_Flags |= 0x8;
    } else {
        m_Flags &= ~0x8;
    }

    // Get counts of object animations
    int outAnimCount = animOut->GetAnimationCount();
    int inAnimCount = animIn->GetAnimationCount();

    // Ensure we have enough object animations in this animation
    int currentCount = m_Animations.Size();
    if (currentCount < inAnimCount) {
        int neededCount = inAnimCount - currentCount;
        for (int i = 0; i < neededCount; ++i) {
            // Check if we should create dynamic objects
            CKBOOL isDynamic = (m_ObjectFlags & (CK_OBJECT_DYNAMIC | CK_OBJECT_HIERACHICALHIDE)) ==
                (CK_OBJECT_DYNAMIC | CK_OBJECT_HIERACHICALHIDE);

            CK_OBJECTCREATION_OPTIONS opts = isDynamic ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK;

            RCKObjectAnimation *newAnim = (RCKObjectAnimation *) m_Context->CreateObject(
                CKCID_OBJECTANIMATION, nullptr, opts, nullptr);

            if (newAnim) {
                m_Animations.PushBack(newAnim);
                newAnim->m_ParentKeyedAnimation = this;
            }
        }
    }

    // Mark all object animations with flag 0xC0000000 initially
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *objAnim = (RCKObjectAnimation *) *it;
        if (objAnim) {
            objAnim->m_Flags |= 0xC0000000;
        }
    }

    // Create transition for each object animation
    for (int i = 0; i < m_Animations.Size(); ++i) {
        RCKObjectAnimation *objAnim = (RCKObjectAnimation *) m_Animations[i];
        if (!objAnim)
            continue;

        objAnim->m_Flags &= ~0x80000000;

        CKObjectAnimation *inObjAnim = nullptr;
        CKObjectAnimation *outObjAnim = nullptr;

        if (i < inAnimCount) {
            inObjAnim = animIn->GetAnimation(i);
        }

        if (inObjAnim && i < outAnimCount) {
            CK3dEntity *entity = inObjAnim->Get3dEntity();
            if (entity) {
                outObjAnim = animOut->GetAnimation(entity);
            }
        }

        if (inObjAnim && outObjAnim) {
            CKBOOL veloc = FALSE;
            CKBOOL dontTurn = FALSE;

            CK3dEntity *entity = inObjAnim->Get3dEntity();
            if (entity && entity == (CK3dEntity *) m_RootEntity) {
                veloc = (OutTransitionMode & 0x40) != 0;
                dontTurn = (animOut->m_Flags & 0x10) != 0;
            }

            objAnim->CreateTransition(length, inObjAnim, outLength, outObjAnim, stepTo, veloc, dontTurn, nullptr);
        } else {
            objAnim->ClearAll();
        }
    }

    // Clear any animations that weren't used (still have flag 0x80000000)
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *objAnim = (RCKObjectAnimation *) *it;
        if (objAnim) {
            if (objAnim->m_Flags & 0x80000000) {
                objAnim->ClearAll();
            }
            objAnim->m_Flags &= ~0x80000000;
        }
    }

    // Set the length
    SetLength(length);

    // Return the computed step value
    return stepTo * animIn->m_MergeFactor;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCKKeyedAnimation::m_ClassID = CKCID_KEYEDANIMATION;

CKSTRING RCKKeyedAnimation::GetClassName() {
    return (CKSTRING) "Keyed Animation";
}

int RCKKeyedAnimation::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKKeyedAnimation::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKKeyedAnimation::Register() {
    // Based on IDA analysis
    CKClassNeedNotificationFrom(RCKKeyedAnimation::m_ClassID, RCKObjectAnimation::m_ClassID);

    // Register associated parameter GUID: {0x5BF0D34D, 0x19E69236} (same as Animation)
    CKClassRegisterAssociatedParameter(RCKKeyedAnimation::m_ClassID, CKPGUID_ANIMATION);
}

CKKeyedAnimation *RCKKeyedAnimation::CreateInstance(CKContext *Context) {
    // Object size is 0x54 (84 bytes)
    RCKKeyedAnimation *anim = new RCKKeyedAnimation(Context, nullptr);
    return reinterpret_cast<CKKeyedAnimation *>(anim);
}
