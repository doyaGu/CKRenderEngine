#include "RCKCharacter.h"

#include "VxMath.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKScene.h"
#include "CKDependencies.h"
#include "RCK3dEntity.h"
#include "RCKBodyPart.h"
#include "RCKAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"

//=============================================================================
// Constructor/Destructor
// Based on decompilation at 0x1000F9B0 and 0x1000FB5B
//=============================================================================

RCKCharacter::RCKCharacter(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_BodyParts(),
      m_Animations(),
      m_SecondaryAnimations(nullptr),
      m_SecondaryAnimationsCount(0),
      m_SecondaryAnimationsAllocated(0),
      m_RootBodyPart(nullptr),
      m_ActiveAnimation(nullptr),
      m_AnimDest(nullptr),
      m_Warper(nullptr),
      m_FrameDest(0.0f),
      field_1D4(0),
      m_FloorRef(nullptr),
      m_AnimationLevelOfDetail(1.0f),
      m_FrameSrc(0.0f),
      m_AnimSrc(nullptr),
      m_TransitionMode(0) {
    // Create the warper animation (internal transition animation)
    // Based on DLL: Creates a CKKeyedAnimation for transitions
    CKBOOL isDynamic = m_Context->IsInDynamicCreationMode();
    m_Warper = (RCKKeyedAnimation *) m_Context->CreateObject(
        CKCID_KEYEDANIMATION,
        nullptr,
        CK_OBJECTCREATION_SameDynamic,
        nullptr);

    if (m_Warper) {
        // Set the character reference on the warper using RCKAnimation interface
        ((RCKAnimation *) m_Warper)->m_Character = (RCKCharacter *) this;

        // Modify object flags: set 0x23 (hidden, internal, no save)
        ((CKObject *) m_Warper)->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
    }
}

RCKCharacter::~RCKCharacter() {
    // Clear body parts array
    m_BodyParts.Clear();

    // Clear animations array
    m_Animations.Clear();

    // Delete secondary animations array
    if (m_SecondaryAnimations) {
        delete[] m_SecondaryAnimations;
        m_SecondaryAnimations = nullptr;
    }
    m_SecondaryAnimationsCount = 0;
    m_SecondaryAnimationsAllocated = 0;
}

//=============================================================================
// Serialization Methods
//=============================================================================

void RCKCharacter::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first
    RCK3dEntity::PreSave(file, flags);

    // Save root body part
    if (m_RootBodyPart) {
        file->SaveObject((CKObject *) m_RootBodyPart);
    }

    // Save all body parts
    int bpCount = m_BodyParts.Size();
    for (int i = 0; i < bpCount; ++i) {
        CKObject *bp = m_BodyParts.GetObject(i);
        if (bp) {
            file->SaveObject(bp);
        }
    }

    // Save all animations
    int animCount = m_Animations.Size();
    for (int i = 0; i < animCount; ++i) {
        CKObject *anim = m_Animations.GetObject(i);
        if (anim) {
            file->SaveObject(anim);
        }
    }
}

CKStateChunk *RCKCharacter::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    if (!file && !(flags & CK_STATESAVE_CHARACTERONLY)) {
        return baseChunk;
    }

    CKStateChunk *chunk = CreateCKStateChunk(CKCID_CHARACTER, file);
    if (!chunk) {
        return baseChunk;
    }

    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Save body parts array with identifier 0x400000
    chunk->WriteIdentifier(CK_STATESAVE_CHARACTERBODYPARTS);
    m_BodyParts.Save(chunk);

    // Optionally save body parts as sub-chunks
    if (!file && (flags & CK_STATESAVE_CHARACTERSAVEPARTS) != 0) {
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERSAVEPARTS);
        int count = m_BodyParts.Size();
        chunk->StartSubChunkSequence(count);

        for (int i = 0; i < count; ++i) {
            CKObject *bp = m_BodyParts.GetObject(i);
            CKStateChunk *subChunk = nullptr;
            if (bp) {
                subChunk = bp->Save(nullptr, CK_STATESAVE_BODYPARTALL);
            }
            chunk->WriteSubChunkSequence(subChunk);
            if (subChunk) {
                DeleteCKStateChunk(subChunk);
            }
        }
    }

    // Save animations array with identifier 0xFFC00000
    chunk->WriteIdentifier(CK_STATESAVE_CHARACTERONLY);
    if (file) {
        m_Animations.Save(chunk);
    }

    // Save character object references in sequence
    chunk->StartObjectIDSequence(4);
    chunk->WriteObjectSequence((CKObject *) m_ActiveAnimation);
    chunk->WriteObjectSequence((CKObject *) m_AnimDest);
    chunk->WriteObjectSequence((CKObject *) m_RootBodyPart);
    chunk->WriteObjectSequence((CKObject *) m_FloorRef);

    if (GetClassID() == CKCID_CHARACTER) {
        chunk->CloseChunk();
    } else {
        chunk->UpdateDataSize();
    }

    return chunk;
}

CKERROR RCKCharacter::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) {
        return CKERR_INVALIDPARAMETER;
    }

    RCK3dEntity::Load(chunk, file);

    // Handle legacy format (data version < 5)
    if (chunk->GetDataVersion() < 5) {
        if (file) {
            if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERBODYPARTS)) {
                m_BodyParts.Clear();
                m_BodyParts.Load(m_Context, chunk);
            }

            if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERANIMATIONS)) {
                m_Animations.Clear();
                m_Animations.Load(m_Context, chunk);

                // Original reads these immediately after loading animations
                m_ActiveAnimation = (RCKKeyedAnimation *) chunk->ReadObject(m_Context);
                m_AnimDest = (RCKAnimation *) chunk->ReadObject(m_Context);
            }
        } else {
            // Save current and next active animations
            if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERSAVEANIMS)) {
                (void) chunk->ReadDword();
                m_ActiveAnimation = (RCKKeyedAnimation *) chunk->ReadObject(m_Context);
                m_AnimDest = (RCKAnimation *) chunk->ReadObject(m_Context);
            }

            // Optional body parts sub-chunk sequence
            if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERSAVEPARTS)) {
                const int count = (int) chunk->ReadDword();
                for (int i = 0; i < count; ++i) {
                    const CK_ID objID = chunk->ReadObjectID();
                    CKObject *bp = m_Context->GetObject(objID);
                    CKStateChunk *subChunk = chunk->ReadSubChunk();
                    if (bp && subChunk) {
                        bp->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                }
            }
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERROOT)) {
            m_RootBodyPart = (RCKBodyPart *) chunk->ReadObject(m_Context);
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERFLOORREF)) {
            m_FloorRef = (RCK3dEntity *) chunk->ReadObject(m_Context);
        }
    }
    // Current format (data version >= 5)
    else {
        if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERBODYPARTS)) {
            m_BodyParts.Clear();
            m_BodyParts.Load(m_Context, chunk);
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERSAVEPARTS)) {
            const int sequenceCount = chunk->StartReadSequence();
            if (sequenceCount == m_BodyParts.Size()) {
                for (int i = 0; i < sequenceCount; ++i) {
                    CKStateChunk *subChunk = chunk->ReadSubChunk();
                    CKObject *bp = m_BodyParts.GetObject(i);
                    if (bp && subChunk) {
                        bp->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                }
            }
        }

        if (chunk->SeekIdentifier(CK_STATESAVE_CHARACTERONLY)) {
            if (file) {
                m_Animations.Clear();
                m_Animations.Load(m_Context, chunk);
            }

            chunk->StartReadSequence();
            m_ActiveAnimation = (RCKKeyedAnimation *) chunk->ReadObject(m_Context);
            m_AnimDest = (RCKAnimation *) chunk->ReadObject(m_Context);
            m_RootBodyPart = (RCKBodyPart *) chunk->ReadObject(m_Context);
            m_FloorRef = (RCK3dEntity *) chunk->ReadObject(m_Context);

            if (!m_RootBodyPart && GetChildrenCount()) {
                m_RootBodyPart = (RCKBodyPart *) GetChild(0);
            }
        }
    }

    return CK_OK;
}

//=============================================================================
// Static Class Methods
//=============================================================================

CK_CLASSID RCKCharacter::m_ClassID = CKCID_CHARACTER;

CKSTRING RCKCharacter::GetClassName() {
    return (CKSTRING) "Character";
}

int RCKCharacter::GetDependenciesCount(int mode) {
    if (mode == CK_DEPENDENCIES_COPY) {
        return 1;
    }
    return 0;
}

CKSTRING RCKCharacter::GetDependencies(int i, int mode) {
    if (i == 0 && mode == 1) {
        return (CKSTRING) "Share Animations";
    }
    return nullptr;
}

void RCKCharacter::Register() {
    CKCLASSNOTIFYFROMCID(RCKCharacter, CKCID_ANIMATION);
    CKCLASSNOTIFYFROMCID(RCKCharacter, CKCID_BODYPART);
    CKPARAMETERFROMCLASS(RCKCharacter, CKPGUID_CHARACTER);
    CKCLASSDEFAULTCOPYDEPENDENCIES(RCKCharacter, CK_DEPENDENCIES_COPY);
}

CKCharacter *RCKCharacter::CreateInstance(CKContext *Context) {
    return (CKCharacter *) new RCKCharacter(Context, nullptr);
}

//=============================================================================
// Virtual Method Overrides
//=============================================================================

CK_CLASSID RCKCharacter::GetClassID() {
    return m_ClassID;
}

void RCKCharacter::CheckPreDeletion() {
    // Based on decompilation at 0x1001242F
    RCK3dEntity::CheckPreDeletion();

    // Check animations array (removes deleted objects)
    m_Animations.Check();

    // Clear active animation if marked for deletion
    if (m_ActiveAnimation && ((CKObject *) m_ActiveAnimation)->IsToBeDeleted()) {
        m_ActiveAnimation = nullptr;
    }

    // Clear destination animation if marked for deletion
    if (m_AnimDest && ((CKObject *) m_AnimDest)->IsToBeDeleted()) {
        m_AnimDest = nullptr;
    }

    // Check body parts array (removes deleted objects)
    m_BodyParts.Check();

    // Clear root body part if marked for deletion
    if (m_RootBodyPart && ((CKObject *) m_RootBodyPart)->IsToBeDeleted()) {
        m_RootBodyPart = nullptr;
    }

    // Clear floor reference if marked for deletion
    if (m_FloorRef && ((CKObject *) m_FloorRef)->IsToBeDeleted()) {
        m_FloorRef = nullptr;
    }
}

int RCKCharacter::GetMemoryOccupation() {
    return RCK3dEntity::GetMemoryOccupation() + (sizeof(RCKCharacter) - sizeof(RCK3dEntity)) + m_BodyParts.GetMemoryOccupation(FALSE) + m_Animations.GetMemoryOccupation(FALSE);
}

int RCKCharacter::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    // Based on decompilation at 0x10012558
    if (cid == CKCID_ANIMATION) {
        // Check if animation is in our list
        if (m_Animations.FindObject(o)) {
            return 1;
        }
    } else if (cid == CKCID_BODYPART) {
        // Check if body part is in our list
        if (m_BodyParts.FindObject(o)) {
            return 1;
        }
    }

    return RCK3dEntity::IsObjectUsed(o, cid);
}

void RCKCharacter::Show(CK_OBJECT_SHOWOPTION show) {
    // Based on decompilation at 0x1000FCBE
    RCK3dEntity::Show(show);

    // Show all body parts with same option
    int count = m_BodyParts.Size();
    for (int i = 0; i < count; ++i) {
        CKObject *bp = m_BodyParts.GetObject(i);
        if (bp) {
            ((CKRenderObject *) bp)->Show(show);
        }
    }
}

float RCKCharacter::GetRadius() {
    // Based on decompilation at 0x1000FC23
    const VxBbox &box = GetHierarchicalBox(FALSE);

    float dx = box.Max.x - box.Min.x;
    float dy = box.Max.y - box.Min.y;
    float dz = box.Max.z - box.Min.z;

    float maxDim = (dy >= dx) ? dy : dx;
    if (dz >= maxDim) {
        maxDim = dz;
    }

    return maxDim * 0.5f;
}

const VxBbox &RCKCharacter::GetBoundingBox(CKBOOL Local) {
    // Based on decompilation at 0x10011CF9
    return GetHierarchicalBox(Local);
}

CKBOOL RCKCharacter::GetBaryCenter(VxVector *Pos) {
    // Based on decompilation at 0x10011D18
    if (Pos) {
        const VxBbox &box = GetHierarchicalBox(FALSE);
        VxVector sum = box.Min + box.Max;
        *Pos = sum * 0.5f;
    }
    return TRUE;
}

void RCKCharacter::AddToScene(CKScene *scene, CKBOOL dependencies) {
    // Based on decompilation at 0x1001227F
    if (!scene) return;

    RCK3dEntity::AddToScene(scene, dependencies);

    if (dependencies) {
        // Add all body parts to scene
        int bpCount = m_BodyParts.Size();
        for (int i = 0; i < bpCount; ++i) {
            CKObject *bp = m_BodyParts.GetObject(i);
            if (bp) {
                ((CKBeObject *) bp)->AddToScene(scene, dependencies);
            }
        }

        // Add all animations to scene
        int animCount = m_Animations.Size();
        for (int i = 0; i < animCount; ++i) {
            CKObject *anim = m_Animations.GetObject(i);
            if (anim) {
                ((CKBeObject *) anim)->AddToScene(scene, dependencies);
            }
        }
    }
}

void RCKCharacter::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    // Based on decompilation at 0x10012357
    if (!scene) return;

    RCK3dEntity::RemoveFromScene(scene, dependencies);

    if (dependencies) {
        // Remove all body parts from scene
        int bpCount = m_BodyParts.Size();
        for (int i = 0; i < bpCount; ++i) {
            CKObject *bp = m_BodyParts.GetObject(i);
            if (bp) {
                ((CKBeObject *) bp)->RemoveFromScene(scene, dependencies);
            }
        }

        // Remove all animations from scene
        int animCount = m_Animations.Size();
        for (int i = 0; i < animCount; ++i) {
            CKObject *anim = m_Animations.GetObject(i);
            if (anim) {
                ((CKBeObject *) anim)->RemoveFromScene(scene, dependencies);
            }
        }
    }
}

//=============================================================================
// Dependency Methods
//=============================================================================

CKERROR RCKCharacter::PrepareDependencies(CKDependenciesContext &context) {
    // Based on decompilation at 0x10012E79
    CKERROR err = RCK3dEntity::PrepareDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    CKDWORD charDeps = context.GetClassDependencies(CKCID_CHARACTER);
    CKDWORD animDeps = context.GetClassDependencies(CKCID_ANIMATION);

    // Prepare body parts
    m_BodyParts.Prepare(context);

    // Prepare floor reference
    if (m_FloorRef) {
        ((CKObject *) m_FloorRef)->PrepareDependencies(context);
    }

    // Prepare animations if needed
    if (!context.IsInMode(CK_DEPENDENCIES_COPY) || (animDeps & 4) != 0 || (charDeps & 1) != 0) {
        m_Animations.Prepare(context);
    }

    // Prepare secondary animations if deleting
    if (context.IsInMode(CK_DEPENDENCIES_DELETE)) {
        for (int i = 0; i < m_SecondaryAnimationsAllocated; ++i) {
            if (m_SecondaryAnimations && m_SecondaryAnimations[i].Transition) {
                ((CKObject *) m_SecondaryAnimations[i].Transition)->PrepareDependencies(context);
            }
        }

        if (m_Warper) {
            ((CKObject *) m_Warper)->PrepareDependencies(context);
        }
    }

    return context.FinishPrepareDependencies((CKObject *) this, m_ClassID);
}

CKERROR RCKCharacter::RemapDependencies(CKDependenciesContext &context) {
    // Based on decompilation at 0x10012FE6
    CKERROR err = RCK3dEntity::RemapDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    // Remap body parts
    m_BodyParts.Remap(context);

    // Remap floor reference
    if (m_FloorRef) {
        m_FloorRef = (RCK3dEntity *) context.Remap((CKObject *) m_FloorRef);
    }

    // Remap animations
    m_Animations.Remap(context);

    // Remap active animation
    if (m_ActiveAnimation) {
        m_ActiveAnimation = (RCKKeyedAnimation *) context.Remap((CKObject *) m_ActiveAnimation);
    }

    // Remap destination animation
    if (m_AnimDest) {
        m_AnimDest = (RCKAnimation *) context.Remap((CKObject *) m_AnimDest);
    }

    // Remap root body part
    if (m_RootBodyPart) {
        m_RootBodyPart = (RCKBodyPart *) context.Remap((CKObject *) m_RootBodyPart);
    }

    return CK_OK;
}

CKERROR RCKCharacter::Copy(CKObject &o, CKDependenciesContext &context) {
    // Based on decompilation at 0x100130AE
    CKERROR err = RCK3dEntity::Copy(o, context);
    if (err != CK_OK) {
        return err;
    }

    RCKCharacter *src = (RCKCharacter *) &o;

    // Copy animations array
    m_Animations = src->m_Animations;

    // Copy body parts array
    m_BodyParts = src->m_BodyParts;

    // Copy active animation reference
    m_ActiveAnimation = src->m_ActiveAnimation;

    // Copy destination animation reference
    m_AnimDest = src->m_AnimDest;

    // Copy root body part reference
    m_RootBodyPart = src->m_RootBodyPart;

    // Copy frame destination
    m_FrameDest = src->m_FrameDest;

    // Copy field_1D4
    field_1D4 = src->field_1D4;

    // Copy floor reference
    m_FloorRef = src->m_FloorRef;

    return CK_OK;
}

//=============================================================================
// Body Part Methods
//=============================================================================

CKERROR RCKCharacter::AddBodyPart(CKBodyPart *part) {
    // Based on decompilation at 0x1000FE1E
    if (!part) {
        return CKERR_INVALIDPARAMETER;
    }

    // Check if already in list
    if (m_BodyParts.FindObject((CKObject *) part)) {
        return CKERR_ALREADYPRESENT;
    }

    // Add to our list
    m_BodyParts.PushBack((CKObject *) part);

    // Get previous character and remove from it
    RCKBodyPart *rckPart = (RCKBodyPart *) part;
    RCKCharacter *prevChar = rckPart->m_Character;
    if (prevChar && prevChar != this) {
        // Call RemoveBodyPart on the previous character
        prevChar->RemoveBodyPart(part);
    }

    // Set character reference on body part
    rckPart->m_Character = this;

    // If no parent, add as child and set as root
    if (!((CK3dEntity *) part)->GetParent()) {
        AddChild((CK3dEntity *) part, TRUE);
        m_RootBodyPart = (RCKBodyPart *) part;
    }

    // Find floor reference if not set
    if (!m_FloorRef) {
        FindFloorReference();
    }

    return CK_OK;
}

CKERROR RCKCharacter::RemoveBodyPart(CKBodyPart *part) {
    // Based on decompilation at 0x1000FF30
    if (!part) {
        return CKERR_INVALIDPARAMETER;
    }

    // Find and remove from list
    int removed = m_BodyParts.RemoveObject((CKObject *) part);
    if (removed < 0) {
        return CKERR_INVALIDPARAMETER;
    }

    // Clear character reference
    ((RCKBodyPart *) part)->m_Character = nullptr;

    // Update floor reference if it was this body part
    if ((CKBodyPart *) m_FloorRef == part) {
        m_FloorRef = nullptr;
        FindFloorReference();
    }

    return CK_OK;
}

CKBodyPart *RCKCharacter::GetRootBodyPart() {
    return (CKBodyPart *) m_RootBodyPart;
}

CKERROR RCKCharacter::SetRootBodyPart(CKBodyPart *part) {
    // Based on decompilation at 0x1000FF9A
    if ((RCKBodyPart *) part != m_RootBodyPart) {
        // Clear previous root's character reference
        if (m_RootBodyPart) {
            m_RootBodyPart->m_Character = nullptr;
        }

        m_RootBodyPart = (RCKBodyPart *) part;

        // Set new root's character reference
        if (m_RootBodyPart) {
            m_RootBodyPart->m_Character = this;
        }
    }
    return CK_OK;
}

CKBodyPart *RCKCharacter::GetBodyPart(int index) {
    if (index < 0 || index >= m_BodyParts.Size()) {
        return nullptr;
    }
    return (CKBodyPart *) m_BodyParts.GetObject(index);
}

int RCKCharacter::GetBodyPartCount() {
    return m_BodyParts.Size();
}

//=============================================================================
// Animation Methods
//=============================================================================

CKERROR RCKCharacter::AddAnimation(CKAnimation *anim) {
    // Based on decompilation at 0x10010776
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // Check if already in list
    if (m_Animations.FindObject((CKObject *) anim)) {
        return CK_OK; // Already present
    }

    // Add to our list
    m_Animations.PushBack((CKObject *) anim);

    // Get previous character and remove from it via RCKAnimation
    RCKAnimation *rckAnim = (RCKAnimation *) anim;
    RCKCharacter *prevChar = rckAnim->m_Character;
    if (prevChar && prevChar != this) {
        // Call RemoveAnimation on the previous character
        prevChar->RemoveAnimation(anim);
    }

    // Set character reference on animation
    rckAnim->m_Character = this;

    // Handle keyed animations
    if (CKIsChildClassOf((CKObject *) anim, CKCID_KEYEDANIMATION)) {
        if (m_RootBodyPart) {
            // If animation has no root entity, use our root body part
            if (!rckAnim->m_RootEntity) {
                rckAnim->m_RootEntity = (RCK3dEntity *) m_RootBodyPart;
            }
        } else {
            // If we have no root body part, use animation's root entity
            m_RootBodyPart = (RCKBodyPart *) rckAnim->m_RootEntity;
        }

        // Center animation at current frame
        float frame = ((CKAnimation *) anim)->GetFrame();
        ((CKKeyedAnimation *) anim)->CenterAnimation(frame);
    }

    return CK_OK;
}

CKERROR RCKCharacter::RemoveAnimation(CKAnimation *anim) {
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // Find and remove from list
    int removed = m_Animations.RemoveObject((CKObject *) anim);
    if (removed < 0) {
        return CKERR_INVALIDPARAMETER;
    }

    // Clear character reference
    ((RCKAnimation *) anim)->m_Character = nullptr;

    return CK_OK;
}

CKAnimation *RCKCharacter::GetAnimation(int index) {
    if (index < 0 || index >= m_Animations.Size()) {
        return nullptr;
    }
    return (CKAnimation *) m_Animations.GetObject(index);
}

int RCKCharacter::GetAnimationCount() {
    return m_Animations.Size();
}

CKAnimation *RCKCharacter::GetWarper() {
    return (CKAnimation *) m_Warper;
}

//=============================================================================
// Active Animation Methods
//=============================================================================

CKAnimation *RCKCharacter::GetActiveAnimation() {
    return (CKAnimation *) m_ActiveAnimation;
}

CKAnimation *RCKCharacter::GetNextActiveAnimation() {
    return (CKAnimation *) m_AnimDest;
}

CKERROR RCKCharacter::SetActiveAnimation(CKAnimation *anim) {
    m_ActiveAnimation = (RCKKeyedAnimation *) anim;
    return CK_OK;
}

CKERROR RCKCharacter::SetNextActiveAnimation(CKAnimation *anim, CKDWORD transitionmode, float warplength) {
    // Based on decompilation at 0x10010982
    // This is a complex function handling animation transitions

    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // Store transition parameters
    m_TransitionMode = transitionmode;
    m_AnimSrc = (RCKAnimation *) m_ActiveAnimation;

    // Get source frame
    if (m_ActiveAnimation) {
        m_FrameSrc = ((CKAnimation *) m_ActiveAnimation)->GetFrame();
    } else {
        m_FrameSrc = 0.0f;
    }

    // Set destination animation and frame
    m_AnimDest = (RCKAnimation *) anim;
    m_FrameDest = warplength;

    // Handle different transition modes
    // CK_TRANSITION_FROMNOW (0x01) - immediate transition
    // CK_TRANSITION_FROMWARPFROMCURRENT (0x02) - warp from current
    // CK_TRANSITION_TOSTART (0x08) - start from beginning
    // CK_TRANSITION_WARPSTART (0x12) - combined warp start
    // CK_TRANSITION_WARPMASK (0x132) - mask for warp modes

    if (transitionmode == CK_TRANSITION_FROMNOW) {
        // Immediate transition - just set active animation
        m_ActiveAnimation = (RCKKeyedAnimation *) anim;
        m_AnimDest = nullptr;
    } else if (transitionmode & CK_TRANSITION_WARPMASK) {
        // Warp transition modes
        if (m_Warper && CKIsChildClassOf((CKObject *) anim, CKCID_KEYEDANIMATION)) {
            CKKeyedAnimation *destAnim = (CKKeyedAnimation *) anim;

            // Create transition from current to destination
            ((CKKeyedAnimation *) m_Warper)->CreateTransition(
                (CKKeyedAnimation *) m_AnimSrc,
                destAnim,
                transitionmode,
                warplength,
                0.0f);

            ((CKAnimation *) m_Warper)->SetFrame(0.0f);

            // Set warper as active animation during transition
            m_ActiveAnimation = m_Warper;
        }
    }
    // Otherwise just leave m_AnimDest set and wait for end

    return CK_OK;
}

//=============================================================================
// Animation Processing Methods
//=============================================================================

void RCKCharacter::ProcessAnimation(float deltat) {
    // Based on decompilation at 0x10011038

    VxTimeProfiler profiler;

    RCKAnimation *destAnim = m_AnimDest;
    RCKKeyedAnimation *destKeyed = nullptr;
    if (destAnim && CKIsChildClassOf((CKObject *) destAnim, CKCID_KEYEDANIMATION)) {
        destKeyed = (RCKKeyedAnimation *) destAnim;
    }

    CKAnimation *activeAnim = (CKAnimation *) m_ActiveAnimation;
    RCKKeyedAnimation *activeKeyed = nullptr;
    if (m_ActiveAnimation && CKIsChildClassOf((CKObject *) m_ActiveAnimation, CKCID_KEYEDANIMATION)) {
        activeKeyed = m_ActiveAnimation;
    }

    bool doActivePostUpdate = true;
    float nextFrameForActive = 0.0f;

    if (activeAnim) {
        float curFrame = activeAnim->GetFrame();
        if (activeAnim->GetLength() != curFrame || activeAnim->CanBeInterrupt()) {
            const float activeLen = activeAnim->GetLength();
            float nextFrame = activeAnim->GetNextFrame(deltat);

            if (nextFrame < activeLen) {
                if (nextFrame < 0.0f) {
                    AlignCharacterWithRootPosition();
                    m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
                    m_AnimDest = nullptr;

                    if (destAnim) {
                        const float destLen = destAnim->GetLength();
                        if (destLen != 0.0f) {
                            while (nextFrame < 0.0f) {
                                nextFrame += destLen;
                            }
                        }

                        const float destFrame = nextFrame + m_FrameDest;
                        if (destKeyed) {
                            destKeyed->CenterAnimation(destFrame);
                        }
                        destAnim->SetFrame(destFrame);
                    }

                    doActivePostUpdate = false;
                }
            } else {
                if (activeAnim->CanBeInterrupt() || m_AnimDest) {
                    AlignCharacterWithRootPosition();

                    if (activeKeyed && m_RootBodyPart) {
                        CKObjectAnimation *rootObjAnim = activeKeyed->GetAnimation((CK3dEntity *) m_RootBodyPart);
                        if (rootObjAnim) {
                            VxVector posEnd;
                            VxVector posCentered;
                            const float centeredFrame = activeAnim->GetFrame();
                            rootObjAnim->EvaluatePosition(activeLen, posEnd);
                            rootObjAnim->EvaluatePosition(centeredFrame, posCentered);
                            VxVector delta = posEnd - posCentered;
                            m_RootBodyPart->Translate(&delta, (CK3dEntity *) this, FALSE);

                            VxVector pos;
                            ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                            SetPosition(&pos, nullptr, TRUE);
                        }
                    }

                    m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
                    m_AnimDest = nullptr;

                    if (destAnim) {
                        if (destKeyed) {
                            destKeyed->CenterAnimation(m_FrameDest);
                        }

                        if (activeLen != 0.0f) {
                            while (nextFrame >= activeLen) {
                                nextFrame -= activeLen;
                            }
                        }

                        const float destFrame = nextFrame + m_FrameDest;
                        destAnim->SetFrame(destFrame);

                        if (m_RootBodyPart) {
                            VxVector pos;
                            ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                            SetPosition(&pos, nullptr, TRUE);
                        }

                        if (destKeyed) {
                            destKeyed->CenterAnimation(destFrame);
                        }
                    }

                    doActivePostUpdate = false;
                } else {
                    nextFrame = activeLen;
                }
            }

            nextFrameForActive = nextFrame;
        } else {
            m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
            m_AnimDest = nullptr;

            if (destAnim) {
                if (destKeyed) {
                    destKeyed->CenterAnimation(m_FrameDest);
                }

                float destLen = ((CKAnimation *) m_ActiveAnimation)->GetLength();
                if (destLen != 0.0f) {
                    while (curFrame >= destLen) {
                        curFrame -= destLen;
                    }
                }
                destAnim->SetFrame(curFrame + m_FrameDest);
            }

            doActivePostUpdate = false;
        }
    } else {
        m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
        AlignCharacterWithRootPosition();
        m_AnimDest = nullptr;
        doActivePostUpdate = false;
    }

    if (doActivePostUpdate) {
        activeAnim = (CKAnimation *) m_ActiveAnimation;
        activeKeyed = nullptr;
        if (m_ActiveAnimation && CKIsChildClassOf((CKObject *) m_ActiveAnimation, CKCID_KEYEDANIMATION)) {
            activeKeyed = m_ActiveAnimation;
        }

        if (activeAnim && activeKeyed) {
            activeAnim->SetFrame(nextFrameForActive);
            if (m_RootBodyPart) {
                VxVector pos;
                ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                SetPosition(&pos, nullptr, TRUE);
            }
            activeKeyed->CenterAnimation(nextFrameForActive);
        }
    }

    for (int i = 0; i < m_SecondaryAnimationsCount; ++i) {
        CKSecondaryAnimation &secAnim = m_SecondaryAnimations[i];
        const CK_SECONDARYANIMATION_RUNTIME_MODE mode = secAnim.RuntimeMode;

        if (mode == CKSECONDARYANIMATIONRUNTIME_STARTINGWARP) {
            RCKAnimation *anim = secAnim.Transition;
            if (!anim) {
                continue;
            }

            const float next = anim->GetNextFrame(deltat);
            const float remaining = next - anim->GetLength();
            if (remaining < 0.0f) {
                anim->SetFrame(next);
            } else {
                secAnim.RuntimeMode = CKSECONDARYANIMATIONRUNTIME_PLAYING;
                auto *source = (CKAnimation *) m_Context->GetObject(secAnim.SourceAnimId);
                PreDeleteBodyPartsForAnimation(source);

                const float startFrame = secAnim.GetStartingFrame();
                if (source) {
                    source->SetFrame(remaining + startFrame);
                }
            }
            continue;
        }

        if (mode == CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP) {
            RCKAnimation *anim = secAnim.Transition;
            if (!anim) {
                continue;
            }

            const float next = anim->GetNextFrame(deltat);
            const float remaining = next - anim->GetLength();
            if (remaining < 0.0f) {
                anim->SetFrame(next);
            } else {
                const int removeIndex = i;
                --i;
                RemoveSecondaryAnimationAt(removeIndex);
            }
            continue;
        }

        if (mode != CKSECONDARYANIMATIONRUNTIME_PLAYING) {
            continue;
        }

        auto *anim = (CKAnimation *) m_Context->GetObject(secAnim.SourceAnimId);
        if (!anim) {
            continue;
        }

        const float next = anim->GetNextFrame(deltat);
        const float remaining = next - anim->GetLength();
        if (remaining < 0.0f) {
            anim->SetFrame(next);
            continue;
        }

        int removeIt = 0;
        if ((secAnim.Flags & CKSECONDARYANIMATION_LOOP) != 0) {
            anim->SetFrame(remaining);
        } else if ((secAnim.Flags & CKSECONDARYANIMATION_LOOPNTIMES) != 0) {
            if ((int) --secAnim.LoopCountRemaining > 0) {
                anim->SetFrame(remaining);
            } else if ((secAnim.Flags & CKSECONDARYANIMATION_LASTFRAME) != 0) {
                anim->SetFrame(anim->GetLength());
                secAnim.Flags &= ~CKSECONDARYANIMATION_LOOPNTIMES;
            } else {
                removeIt = 1;
            }
        } else if ((secAnim.Flags & CKSECONDARYANIMATION_LASTFRAME) != 0) {
            anim->SetFrame(anim->GetLength());
        } else {
            removeIt = 1;
        }

        if (removeIt) {
            if ((secAnim.Flags & CKSECONDARYANIMATION_DOWARP) != 0 && CKIsChildClassOf((CKObject *) anim, CKCID_KEYEDANIMATION)) {
                RCKKeyedAnimation *active = m_ActiveAnimation;
                if (active && CKIsChildClassOf((CKObject *) active, CKCID_KEYEDANIMATION)) {
                    RCKKeyedAnimation *transition = secAnim.Transition;
                    if (!transition) {
                        transition = (RCKKeyedAnimation *) m_Context->CreateObject(
                            CKCID_KEYEDANIMATION,
                            nullptr,
                            CK_OBJECTCREATION_SameDynamic,
                            nullptr);
                        secAnim.Transition = transition;
                        if (transition) {
                            ((RCKAnimation *) transition)->m_Flags |= CKANIMATION_SECONDARYWARPER;
                        }
                    }

                    if (transition) {
                        float targetFrame = ((CKAnimation *) active)->GetFrame() + secAnim.WarpLength;
                        const float activeLen = ((CKAnimation *) active)->GetLength();
                        if (activeLen != 0.0f) {
                            while (targetFrame >= activeLen) {
                                targetFrame -= activeLen;
                            }
                        }

                        transition->CreateTransition(anim, active, 0, secAnim.WarpLength, targetFrame);
                        secAnim.Flags |= CKSECONDARYANIMATION_DOWARP;
                        ((CKAnimation *) transition)->SetFrame(0.0f);
                        PreDeleteBodyPartsForAnimation(transition);
                        secAnim.RuntimeMode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
                        continue;
                    }
                }
            }

            const int removeIndex = i;
            --i;
            RemoveSecondaryAnimationAt(removeIndex);
        }
    }

    m_MoveableFlags &= ~VX_MOVEABLE_CHARACTERRENDERED;
    m_Context->AddProfileTime(CK_PROFILE_ANIMATIONTIME, profiler.Current());
}

void RCKCharacter::SetAutomaticProcess(CKBOOL process) {
    // Based on decompilation at 0x10010FE9
    if (process) {
        m_3dEntityFlags |= CK_3DENTITY_CHARACTERDOPROCESS;
    } else {
        m_3dEntityFlags &= ~CK_3DENTITY_CHARACTERDOPROCESS;
    }
}

CKBOOL RCKCharacter::IsAutomaticProcess() {
    // Based on decompilation at 0x10011022
    return (m_3dEntityFlags & CK_3DENTITY_CHARACTERDOPROCESS) != 0;
}

void RCKCharacter::GetEstimatedVelocity(float deltat, VxVector *velocity) {
    // Based on decompilation at 0x1000FD26
    if (!velocity) return;

    // Default to zero
    *velocity = VxVector::axis0();

    CKAnimation *active = GetActiveAnimation();
    if (!active || !CKIsChildClassOf((CKObject *) active, CKCID_KEYEDANIMATION)) {
        return;
    }

    CKKeyedAnimation *keyedAnim = (CKKeyedAnimation *) active;
    CKObjectAnimation *rootAnim = keyedAnim->GetAnimation((CK3dEntity *) m_RootBodyPart);

    if (rootAnim) {
        float currentFrame = keyedAnim->GetFrame();
        float nextFrame = keyedAnim->GetNextFrame(deltat);

        VxVector pos1, pos2;
        rootAnim->EvaluatePosition(currentFrame, pos1);
        rootAnim->EvaluatePosition(nextFrame, pos2);

        *velocity = pos2 - pos1;
    }
}

//=============================================================================
// Secondary Animation Methods
//=============================================================================

CKERROR RCKCharacter::PlaySecondaryAnimation(CKAnimation *anim, float StartingFrame, CK_SECONDARYANIMATION_FLAGS PlayFlags, float warplength, int LoopCount) {
    // Based on decompilation at 0x10010011
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // Check animation belongs to this character
    if (anim->GetCharacter() != (CKCharacter *) this) {
        return CKERR_INVALIDPARAMETER;
    }

    CK_ID animID = anim->GetID();

    // Check if already playing
    for (int i = 0; i < m_SecondaryAnimationsCount; ++i) {
        if (m_SecondaryAnimations[i].SourceAnimId == animID) {
            return CK_OK; // Already playing
        }
    }

    // Ensure we have space
    if (m_SecondaryAnimationsCount >= m_SecondaryAnimationsAllocated) {
        int newSize = m_SecondaryAnimationsAllocated + 2;
        CKSecondaryAnimation *newArray = new CKSecondaryAnimation[newSize];

        if (m_SecondaryAnimations) {
            memcpy(newArray, m_SecondaryAnimations, sizeof(CKSecondaryAnimation) * m_SecondaryAnimationsAllocated);
            delete[] m_SecondaryAnimations;
        }

        m_SecondaryAnimations = newArray;
        m_SecondaryAnimationsAllocated = (CKWORD) newSize;

        // Initialize new slots
        memset(&m_SecondaryAnimations[m_SecondaryAnimationsCount], 0, sizeof(CKSecondaryAnimation) * 2);
    }

    // Add the secondary animation
    CKSecondaryAnimation &secAnim = m_SecondaryAnimations[m_SecondaryAnimationsCount];
    secAnim.SourceAnimId = animID;
    secAnim.Flags = PlayFlags;
    secAnim.WarpLength = warplength;
    secAnim.SetStartingFrame(StartingFrame);
    secAnim.RuntimeMode = CKSECONDARYANIMATIONRUNTIME_PLAYING;
    secAnim.LoopCountRemaining = LoopCount;
    secAnim.Transition = nullptr;

    // Set frame
    anim->SetFrame(StartingFrame);

    m_SecondaryAnimationsCount++;
    return CK_OK;
}

CKERROR RCKCharacter::StopSecondaryAnimation(CKAnimation *anim, CKBOOL warp, float warplength) {
    // Based on decompilation at 0x1001044B
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    if (anim->GetCharacter() != (CKCharacter *) this) {
        return CKERR_INVALIDPARAMETER;
    }

    CK_ID animID = anim->GetID();

    // Find the secondary animation
    int idx = -1;
    for (int i = 0; i < m_SecondaryAnimationsCount; ++i) {
        if (m_SecondaryAnimations[i].SourceAnimId == animID) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        return CK_OK; // Not playing
    }

    int warped = 0;
    if (warp) {
        if (m_SecondaryAnimations[idx].RuntimeMode == CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP) {
            return CK_OK;
        }

        RCKKeyedAnimation *active = m_ActiveAnimation;
        if (active && CKIsChildClassOf((CKObject *) active, CKCID_KEYEDANIMATION)) {
            RCKKeyedAnimation *transition = m_SecondaryAnimations[idx].Transition;
            if (!transition) {
                transition = (RCKKeyedAnimation *) m_Context->CreateObject(
                    CKCID_KEYEDANIMATION,
                    nullptr,
                    CK_OBJECTCREATION_SameDynamic,
                    nullptr);
                m_SecondaryAnimations[idx].Transition = transition;
                if (transition) {
                    ((RCKAnimation *) transition)->m_Flags |= CKANIMATION_SECONDARYWARPER;
                }
            }

            CKAnimation *fromAnim = nullptr;
            if (m_SecondaryAnimations[idx].RuntimeMode == CKSECONDARYANIMATIONRUNTIME_PLAYING) {
                fromAnim = (CKAnimation *) m_Context->GetObject(m_SecondaryAnimations[idx].SourceAnimId);
            } else {
                fromAnim = (CKAnimation *) m_SecondaryAnimations[idx].Transition;
            }

            float targetFrame = ((CKAnimation *) active)->GetFrame() + warplength;
            const float activeLen = ((CKAnimation *) active)->GetLength();
            if (activeLen != 0.0f) {
                while (targetFrame >= activeLen) {
                    targetFrame -= activeLen;
                }
            }

            if (transition && fromAnim && CKIsChildClassOf((CKObject *) fromAnim, CKCID_KEYEDANIMATION)) {
                transition->CreateTransition(fromAnim, active, 0, warplength, targetFrame);
                m_SecondaryAnimations[idx].Flags |= CKSECONDARYANIMATION_DOWARP;
                m_SecondaryAnimations[idx].WarpLength = warplength;
                ((CKAnimation *) transition)->SetFrame(0.0f);

                PreDeleteBodyPartsForAnimation(transition);

                m_SecondaryAnimations[idx].RuntimeMode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
                warped = 1;
            }
        }
    }

    if (!warped) {
        RemoveSecondaryAnimationAt(idx);
    }
    return CK_OK;
}

CKERROR RCKCharacter::StopSecondaryAnimation(CKAnimation *anim, float warplength) {
    return StopSecondaryAnimation(anim, FALSE, warplength);
}

int RCKCharacter::GetSecondaryAnimationsCount() {
    return m_SecondaryAnimationsCount;
}

CKAnimation *RCKCharacter::GetSecondaryAnimation(int index) {
    if (index < 0 || index >= m_SecondaryAnimationsCount || !m_SecondaryAnimations) {
        return nullptr;
    }
    return (CKAnimation *) m_Context->GetObject(m_SecondaryAnimations[index].SourceAnimId);
}

void RCKCharacter::FlushSecondaryAnimations() {
    // Based on decompilation at 0x10012264
    // Clear body parts and animations arrays
    m_BodyParts.Clear();
    m_Animations.Clear();

    // Delete secondary animations array
    if (m_SecondaryAnimations) {
        delete[] m_SecondaryAnimations;
        m_SecondaryAnimations = nullptr;
    }
    m_SecondaryAnimationsCount = 0;
    m_SecondaryAnimationsAllocated = 0;
}

//=============================================================================
// Utility Methods
//=============================================================================

void RCKCharacter::AlignCharacterWithRootPosition() {
    // Based on decompilation at 0x10011DE8
    CKAnimation *active = GetActiveAnimation();

    if (!active) {
        // No active animation - just set position from root body part
        if (m_RootBodyPart) {
            VxVector pos;
            ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
            SetPosition(&pos, nullptr, TRUE);
        }
        return;
    }

    if (!CKIsChildClassOf((CKObject *) active, CKCID_KEYEDANIMATION)) {
        return;
    }

    CKKeyedAnimation *keyedAnim = (CKKeyedAnimation *) active;

    if (m_RootBodyPart) {
        // Get position from root body part and set character position
        VxVector pos;
        ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
        SetPosition(&pos, nullptr, TRUE);
    }

    // Center animation
    float frame = keyedAnim->GetFrame();
    keyedAnim->CenterAnimation(frame);
}

CK3dEntity *RCKCharacter::GetFloorReferenceObject() {
    return (CK3dEntity *) m_FloorRef;
}

void RCKCharacter::SetFloorReferenceObject(CK3dEntity *FloorRef) {
    m_FloorRef = (RCK3dEntity *) FloorRef;
}

void RCKCharacter::SetAnimationLevelOfDetail(float LOD) {
    m_AnimationLevelOfDetail = LOD;
}

float RCKCharacter::GetAnimationLevelOfDetail() {
    return m_AnimationLevelOfDetail;
}

void RCKCharacter::GetWarperParameters(CKDWORD *TransitionMode, CKAnimation **AnimSrc, float *FrameSrc,
                                       CKAnimation **AnimDest, float *FrameDest) {
    // Based on decompilation at 0x10010911
    if (TransitionMode) *TransitionMode = m_TransitionMode;
    if (AnimSrc) *AnimSrc = (CKAnimation *) m_AnimSrc;
    if (AnimDest) *AnimDest = (CKAnimation *) m_AnimDest;
    if (FrameSrc) *FrameSrc = m_FrameSrc;
    if (FrameDest) *FrameDest = m_FrameDest;
}

//=============================================================================
// Private Helper Methods
//=============================================================================

void RCKCharacter::FindFloorReference() {
    // Based on decompilation at 0x10012018
    if (!m_RootBodyPart) {
        return;
    }

    static constexpr const char *kFloorRef = "FloorRef";
    static constexpr const char *kFootsteps = "Footsteps";
    static constexpr const char *kFoot = "Foot";
    static constexpr const char *kPas = "Pas";

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kFloorRef)) {
            m_FloorRef = (RCK3dEntity *) child;
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kFootsteps)) {
            m_FloorRef = (RCK3dEntity *) child;
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kFoot)) {
            m_FloorRef = (RCK3dEntity *) child;
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kPas)) {
            m_FloorRef = (RCK3dEntity *) child;
            return;
        }
    }
}

void RCKCharacter::RemoveSecondaryAnimationAt(int index) {
    // Based on decompilation at 0x10011AB1
    if (index < 0 || index >= m_SecondaryAnimationsCount) {
        return;
    }

    CKAnimation *anim = (CKAnimation *) m_Context->GetObject(m_SecondaryAnimations[index].SourceAnimId);
    if (anim) {
        PreDeleteBodyPartsForAnimation(anim);
    }

    const int newCount = (int) m_SecondaryAnimationsCount - 1;
    m_SecondaryAnimationsCount = (CKWORD) newCount;

    if (index == newCount) {
        memset(&m_SecondaryAnimations[index], 0, sizeof(CKSecondaryAnimation));
        return;
    }

    RCKKeyedAnimation *preserved = m_SecondaryAnimations[index].Transition;
    memmove(
        &m_SecondaryAnimations[index],
        &m_SecondaryAnimations[index + 1],
        sizeof(CKSecondaryAnimation) * (newCount - index));

    memset(&m_SecondaryAnimations[newCount], 0, sizeof(CKSecondaryAnimation));
    m_SecondaryAnimations[newCount].Transition = preserved;
}

void RCKCharacter::PreDeleteBodyPartsForAnimation(CKAnimation *anim) {
    // Based on decompilation helper at 0x10048148
    // Iterates ObjectAnimations in a keyed animation and calls PreDelete on body parts.
    if (!anim || !CKIsChildClassOf((CKObject *) anim, CKCID_KEYEDANIMATION)) {
        return;
    }

    auto *keyed = static_cast<CKKeyedAnimation *>(anim);
    const int count = keyed->GetAnimationCount();
    for (int idx = 0; idx < count; ++idx) {
        CKObjectAnimation *objAnim = keyed->GetAnimation(idx);
        if (!objAnim) {
            continue;
        }

        CK3dEntity *ent = objAnim->Get3dEntity();
        if (ent && CKIsChildClassOf((CKObject *) ent, CKCID_BODYPART)) {
            ent->PreDelete();
        }
    }
}
