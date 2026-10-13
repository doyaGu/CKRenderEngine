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
#include <math.h>

//=============================================================================
// Helper function corresponding to sub_10048148 in IDA
// Iterates animation entities and sets exclusive animation on body parts
// When exclusiveAnim is non-null, sets the body part's exclusive animation to it
// When exclusiveAnim is null, releases this animation's exclusive gate
//=============================================================================

static void NotifyBodyPartsInAnimation(CKAnimation *anim, CKAnimation *exclusiveAnim) {
    // This only works for CKKeyedAnimation which has GetAnimation/GetAnimationCount methods
    if (!anim || !CKIsChildClassOf(anim, CKCID_KEYEDANIMATION)) {
        return;
    }

    CKKeyedAnimation *keyedAnim = (CKKeyedAnimation *)anim;
    int count = keyedAnim->GetAnimationCount();

    for (int i = 0; i < count; ++i) {
        CKObjectAnimation *objAnim = keyedAnim->GetAnimation(i);
        if (!objAnim)
            continue;

        // Get the entity from the object animation (vtable+0xF4)
        CK3dEntity *entity = objAnim->Get3dEntity();
        if (!entity)
            continue;

        // Check if it's a body part (CKCID_BODYPART = 0x2A = 42)
        if (CKIsChildClassOf(entity, CKCID_BODYPART)) {
            // Call SetExclusiveAnimation (vtable+0x1D8) on the body part
            CKBodyPart *bp = (CKBodyPart *)entity;
            // A removed secondary must not release a later secondary's gate.
            if (exclusiveAnim || bp->GetExclusiveAnimation() == anim)
                bp->SetExclusiveAnimation(exclusiveAnim);
        }
    }
}

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
    // Based on IDA decompilation at 0x1000F9B0
    // Create the warper animation (internal transition animation)
    CKBOOL isDynamic = m_Context->IsInDynamicCreationMode();
    m_Warper = (RCKKeyedAnimation *)m_Context->CreateObject(
        CKCID_KEYEDANIMATION,
        nullptr,
        CK_OBJECTCREATION_Dynamic(isDynamic),
        nullptr);

    if (m_Warper) {
        // Set the character reference on the warper
        ((RCKAnimation *)m_Warper)->m_Character = this;

        // Set flag 0x08 (CKANIMATION_INTERNAL) on the warper
        CKDWORD flags = ((RCKAnimation *)m_Warper)->m_Flags;
        flags |= CKANIMATION_ALLOWTURN;
        ((RCKAnimation *)m_Warper)->m_Flags = flags;

        // Modify object flags: set 0x23 (CK_OBJECT_NOTTOBELISTEDANDSAVED)
        ((CKObject *)m_Warper)->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
    }
}

RCKCharacter::~RCKCharacter() {
    // Context-wide cleanup can already have destroyed a list member or the
    // warper. Visit only live registry objects, never dereference stale slots.
    for (int id = 1; id <= m_Context->GetObjectCount(); ++id) {
        CKObject *object = m_Context->GetObject(id);
        if (!object)
            continue;
        if (CKIsChildClassOf(object, CKCID_BODYPART)) {
            RCKBodyPart *part = static_cast<RCKBodyPart *>(object);
            if (part->m_Character == this)
                part->m_Character = nullptr;
        } else if (CKIsChildClassOf(object, CKCID_ANIMATION)) {
            RCKAnimation *animation = static_cast<RCKAnimation *>(object);
            if (animation->m_Character == this)
                animation->m_Character = nullptr;
        }
    }

    // Based on IDA decompilation at 0x1000FB5B
    // Delete secondary animations array
    if (m_SecondaryAnimations) {
        delete[] m_SecondaryAnimations;
        m_SecondaryAnimations = nullptr;
    }

    // Clear animations array
    m_Animations.Clear();

    // Clear body parts array
    m_BodyParts.Clear();
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
                // A missing slot is a valid empty snapshot; failure of a
                // present body part cannot produce a successful parent save.
                if (!subChunk) {
                    DeleteCKStateChunk(chunk);
                    return nullptr;
                }
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
                    CKERROR loadError = CK_OK;
                    if (bp && subChunk) {
                        loadError = bp->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                    if (loadError != CK_OK)
                        return loadError;
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
                    CKERROR loadError = CK_OK;
                    if (bp && subChunk) {
                        loadError = bp->Load(subChunk, nullptr);
                    }
                    if (subChunk) {
                        DeleteCKStateChunk(subChunk);
                    }
                    if (loadError != CK_OK)
                        return loadError;
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
    return "Character";
}

int RCKCharacter::GetDependenciesCount(int mode) {
    if (mode == CK_DEPENDENCIES_COPY) {
        return 1;
    }
    return 0;
}

CKSTRING RCKCharacter::GetDependencies(int i, int mode) {
    if (i == 0 && mode == 1) {
        return "Share Animations";
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
    if (m_Warper && m_Warper->IsToBeDeleted()) {
        m_Warper = nullptr;
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
    return RCK3dEntity::GetMemoryOccupation() + (sizeof(RCKCharacter) - sizeof(RCK3dEntity)) + m_BodyParts.GetMemoryOccupation(FALSE) + m_Animations.GetMemoryOccupation(FALSE)
        + m_SecondaryAnimationsAllocated * sizeof(CKSecondaryAnimation);
}

CKBOOL RCKCharacter::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    // Based on decompilation at 0x10012558
    if (cid == CKCID_ANIMATION) {
        // Check if animation is in our list
        if (m_Animations.FindObject(o)) {
            return TRUE;
        }
    } else if (cid == CKCID_BODYPART) {
        // Check if body part is in our list
        if (m_BodyParts.FindObject(o)) {
            return TRUE;
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
    // Based on IDA decompilation at 0x1001227F
    if (!scene) return;

    RCK3dEntity::AddToScene(scene, dependencies);

    if (dependencies) {
        // Add all body parts to scene
        for (CKObject **it = m_BodyParts.Begin(); it != m_BodyParts.End(); ++it) {
            CKObject *bp = *it;
            if (bp) {
                ((CKBeObject *)bp)->AddToScene(scene, dependencies);
            }
        }

        // Add all animations to scene
        for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
            CKObject *anim = *it;
            if (anim) {
                ((CKBeObject *)anim)->AddToScene(scene, dependencies);
            }
        }
    }
}

void RCKCharacter::RemoveFromScene(CKScene *scene, CKBOOL dependencies) {
    // Based on IDA decompilation at 0x10012357
    // Correct the original animation branch so removal cannot add an animation.
    if (!scene) return;

    RCK3dEntity::RemoveFromScene(scene, dependencies);

    if (dependencies) {
        // Remove all body parts from scene
        for (CKObject **it = m_BodyParts.Begin(); it != m_BodyParts.End(); ++it) {
            CKObject *bp = *it;
            if (bp) {
                ((CKBeObject *)bp)->RemoveFromScene(scene, dependencies);
            }
        }

        for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
            CKObject *anim = *it;
            if (anim) {
                ((CKBeObject *)anim)->RemoveFromScene(scene, dependencies);
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
    CKDWORD entityDeps = context.GetClassDependencies(CKCID_3DENTITY);

    // Prepare body parts
    m_BodyParts.Prepare(context);

    // Prepare floor reference
    if (m_FloorRef) {
        ((CKObject *) m_FloorRef)->PrepareDependencies(context);
    }

    // The entity animation dependency or character animation dependency enables copying.
    if (!context.IsInMode(CK_DEPENDENCIES_COPY) || (entityDeps & 4) != 0 || (charDeps & 1) != 0) {
        m_Animations.Prepare(context);
    }

    // Prepare secondary animations if deleting
    if (context.IsInMode(CK_DEPENDENCIES_DELETE)) {
        for (CKWORD i = 0; i < m_SecondaryAnimationsAllocated; ++i) {
            if (m_SecondaryAnimations && m_SecondaryAnimations[i].Animation) {
                ((CKObject *) m_SecondaryAnimations[i].Animation)->PrepareDependencies(context);
            }
        }

        if (m_Warper) {
            ((CKObject *) m_Warper)->PrepareDependencies(context);
        }
    }

    return context.FinishPrepareDependencies((CKObject *) this, m_ClassID);
}

CKERROR RCKCharacter::RemapDependencies(CKDependenciesContext &context) {
    // Based on decompilation at 0x10012FE3
    CKERROR err = RCK3dEntity::RemapDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    // Get class dependencies (result ignored but call matches IDA)
    context.GetClassDependencies(CKCID_CHARACTER);

    // Remap body parts
    m_BodyParts.Remap(context);

    // Remap animations
    m_Animations.Remap(context);

    // Remap active animation (always, no conditional)
    m_ActiveAnimation = (RCKKeyedAnimation *) context.Remap((CKObject *) m_ActiveAnimation);

    // Remap floor reference
    m_FloorRef = (RCK3dEntity *) context.Remap((CKObject *) m_FloorRef);

    // Remap destination animation
    m_AnimDest = (RCKAnimation *) context.Remap((CKObject *) m_AnimDest);

    // Remap root body part
    m_RootBodyPart = (RCKBodyPart *) context.Remap((CKObject *) m_RootBodyPart);

    return CK_OK;
}

CKERROR RCKCharacter::Copy(CKObject &o, CKDependenciesContext &context) {
    // Based on decompilation at 0x100130AE
    CKERROR err = RCK3dEntity::Copy(o, context);
    if (err != CK_OK) {
        return err;
    }

    RCKCharacter *src = (RCKCharacter *) &o;

    // Check class dependencies for animation copy
    CKDWORD charDeps = context.GetClassDependencies(CKCID_CHARACTER);
    CKDWORD entityDeps = context.GetClassDependencies(CKCID_3DENTITY);

    // Native class 33 is CKCID_3DENTITY, whose bit 2 selects animation dependencies.
    if ((entityDeps & 4) != 0 || (charDeps & 1) != 0) {
        // Copy animations array
        m_Animations = src->m_Animations;

        // Copy active animation reference
        m_ActiveAnimation = src->m_ActiveAnimation;

        // Copy destination animation reference
        m_AnimDest = src->m_AnimDest;

        // If dest anim was the source's warper, use our warper
        if (m_AnimDest == (RCKAnimation *)src->m_Warper) {
            m_AnimDest = (RCKAnimation *)m_Warper;
        }
    }

    // Copy body parts array
    m_BodyParts = src->m_BodyParts;

    // Copy root body part reference
    m_RootBodyPart = src->m_RootBodyPart;

    // Copy active animation reference (yes, again - matches IDA)
    m_ActiveAnimation = src->m_ActiveAnimation;

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

    RCKBodyPart *rckPart = (RCKBodyPart *) part;
    RCKCharacter *prevChar = rckPart->m_Character;
    RCK3dEntity *parent = rckPart->m_Parent;
    const CKBOOL rootPart = !parent ||
        (prevChar && prevChar != this && parent == static_cast<RCK3dEntity *>(prevChar));
    // Moving a character's direct root keeps its world pose. A joint that
    // already has another parent keeps that hierarchy.
    if (rootPart && !AddChild(reinterpret_cast<CK3dEntity *>(part), TRUE))
        return CKERR_INVALIDPARAMETER;

    m_BodyParts.PushBack(reinterpret_cast<CKObject *>(part));

    // Get previous character and remove all snapshot aliases from it.
    if (prevChar && prevChar != this) {
        while (prevChar->m_BodyParts.FindObject(reinterpret_cast<CKObject *>(part)))
            prevChar->RemoveBodyPart(part);
        if (prevChar->m_RootBodyPart == rckPart)
            prevChar->SetRootBodyPart(nullptr);
    }

    // Set character reference on body part
    rckPart->m_Character = this;

    if (rootPart)
        m_RootBodyPart = rckPart;

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
    if (!removed) {
        return CKERR_INVALIDPARAMETER;
    }

    // Snapshot lists can contain duplicates; the remaining slot still owns
    // the same association. A borrowed slot must not clear another owner.
    if (m_BodyParts.FindObject(reinterpret_cast<CKObject *>(part)))
        return CK_OK;
    RCKBodyPart *rckPart = reinterpret_cast<RCKBodyPart *>(part);
    if (rckPart->m_Character == this)
        rckPart->m_Character = nullptr;
    if (m_RootBodyPart == rckPart)
        m_RootBodyPart = nullptr;

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
        // Selecting a different root does not remove an existing member.
        if (m_RootBodyPart && CKIsChildClassOf(m_RootBodyPart, CKCID_BODYPART) &&
            m_RootBodyPart->m_Character == this &&
            !m_BodyParts.FindObject(m_RootBodyPart)) {
            m_RootBodyPart->m_Character = nullptr;
        }

        m_RootBodyPart = (RCKBodyPart *) part;

        // Set new root's character reference
        if (m_RootBodyPart && CKIsChildClassOf(m_RootBodyPart, CKCID_BODYPART)) {
            RCKCharacter *previous = m_RootBodyPart->m_Character;
            if (previous && previous != this) {
                while (previous->m_BodyParts.FindObject(m_RootBodyPart))
                    previous->RemoveBodyPart(part);
                if (previous->m_RootBodyPart == m_RootBodyPart)
                    previous->SetRootBodyPart(nullptr);
            }
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

    RCKAnimation *rckAnim = (RCKAnimation *) anim;
    // The original refreshes exact KeyedAnimation instances even on a
    // repeated Add. Derived implementations retain their own root policy.
    if (anim->GetClassID() == CKCID_KEYEDANIMATION)
        static_cast<RCKKeyedAnimation *>(rckAnim)->UpdateRootEntity();

    if (!m_Animations.FindObject(reinterpret_cast<CKObject *>(anim))) {
        m_Animations.PushBack(reinterpret_cast<CKObject *>(anim));
        RCKCharacter *prevChar = rckAnim->m_Character;
        if (prevChar && prevChar != this) {
            while (prevChar->m_Animations.FindObject(reinterpret_cast<CKObject *>(anim)))
                prevChar->RemoveAnimation(anim);
        }
        rckAnim->m_Character = this;
    }

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
    if (!removed) {
        return CKERR_INVALIDPARAMETER;
    }

    RCKAnimation *animation = static_cast<RCKAnimation *>(anim);
    if (!m_Animations.FindObject(reinterpret_cast<CKObject *>(anim)) &&
        animation->m_Character == this)
        animation->m_Character = nullptr;

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
    // Based on IDA decompilation at 0x10010982
    VxTimeProfiler profiler;

    RCKKeyedAnimation *destAnim = (RCKKeyedAnimation *)anim;

    if (!destAnim) {
        // Null animation passed - clear destination
        m_AnimDest = nullptr;
        if (m_ActiveAnimation) {
            // If active animation can be interrupted, clear it
            if ((m_ActiveAnimation->GetFlags() & CKANIMATION_CANBEBREAK) != 0) {
                m_ActiveAnimation = nullptr;
            }
        }
        return CKERR_INVALIDPARAMETER;
    }

    // Handle CK_TRANSITION_FROMANIMATION flag (0x200)
    if ((transitionmode & CK_TRANSITION_FROMANIMATION) != 0) {
        CKDWORD animTransMode = anim->GetTransitionMode();
        if (animTransMode) {
            transitionmode = animTransMode;
        }
    }

    // Check animation belongs to this character
    if (((RCKAnimation *)destAnim)->m_Character != this) {
        return CKERR_INVALIDPARAMETER;
    }

    if (!m_ActiveAnimation) {
        // No active animation - set destination and initialize
        m_AnimDest = (RCKAnimation *)destAnim;
        anim->SetStep(0.0f);
        if (CKIsChildClassOf(anim, CKCID_KEYEDANIMATION)) {
            ((CKKeyedAnimation *)destAnim)->CenterAnimation(0.0f);
        }
        anim->SetStep(0.0f);
        m_FrameDest = 0.0f;
        return CK_OK;
    }

    // CK_TRANSITION_LOOPIFEQUAL (0x80) - If same animation, loop
    if ((transitionmode & CK_TRANSITION_LOOPIFEQUAL) != 0 && destAnim == m_ActiveAnimation) {
        // Check if step > 0.3 (animation is playing)
        if (((RCKAnimation *)destAnim)->m_Step > 0.3f) {
            m_AnimDest = (RCKAnimation *)destAnim;
            m_FrameDest = 0.0f;
        }
        return CK_OK;
    }

    RCKKeyedAnimation *activeAnim = m_ActiveAnimation;
    CKDWORD activeFlags = 0;
    if (activeAnim) {
        activeFlags = activeAnim->GetFlags();
    }

    // Check if we can break or animation step is 0
    if (((transitionmode & CK_TRANSITION_FROMNOW) == CK_TRANSITION_FROMNOW && (activeFlags & CKANIMATION_CANBEBREAK) != 0) ||
        ((RCKAnimation *)m_ActiveAnimation)->m_Step == 0.0f) {
        
        if (activeAnim == destAnim) {
            // Same animation - just set destination
            m_AnimDest = (RCKAnimation *)destAnim;
            m_FrameDest = 0.0f;
        } else {
            // Different animation - align and switch
            AlignCharacterWithRootPosition();
            m_ActiveAnimation = destAnim;
            if (CKIsChildClassOf(anim, CKCID_KEYEDANIMATION)) {
                ((CKKeyedAnimation *)destAnim)->CenterAnimation(0.0f);
            }
            anim->SetStep(0.0f);
            m_AnimDest = nullptr;
            m_FrameDest = 0.0f;
        }
    }
    // CK_TRANSITION_TOSTART (0x08) - Wait until current ends
    else if ((transitionmode & CK_TRANSITION_TOSTART) == CK_TRANSITION_TOSTART) {
        m_AnimDest = (RCKAnimation *)destAnim;
        m_FrameDest = 0.0f;
    }
    // CK_TRANSITION_WARPMASK (0x132) - Warp transitions
    else if ((transitionmode & CK_TRANSITION_WARPMASK) != 0) {
        if (m_AnimDest != (RCKAnimation *)destAnim && destAnim) {
            if (CKIsChildClassOf(anim, CKCID_KEYEDANIMATION)) {
                // The builder does not generate a transition for identical
                // inputs. Reject before consuming pose or reusing old curves.
                if (m_ActiveAnimation == destAnim)
                    return CKERR_INVALIDPARAMETER;
                float frameDest = 0.0f;

                // Check if we're currently warping and dest can't be interrupted
                if (m_ActiveAnimation == m_Warper && m_AnimDest && !((CKAnimation *)m_AnimDest)->CanBeInterrupt()) {
                    return CK_OK;
                }

                if (m_ActiveAnimation) {
                    if (CKIsChildClassOf((CKObject *)m_ActiveAnimation, CKCID_KEYEDANIMATION)) {
                        if (((CKAnimation *)m_ActiveAnimation)->CanBeInterrupt()) {
                            // Animation can be interrupted - create warp now
                            if (!m_Warper || ((RCKAnimation *)m_ActiveAnimation)->m_Character != this)
                                return CKERR_INVALIDPARAMETER;
                            int targetCount = destAnim->GetAnimationCount();
                            float frameSrc = m_ActiveAnimation->GetFrame();
                            AlignCharacterWithRootPosition();
                            frameDest = m_Warper->CreateTransition(
                                (CKAnimation *)m_ActiveAnimation,
                                anim,
                                transitionmode,
                                warplength,
                                0.0f);
                            // FrameTo is signed, including valid negative SAMEPOS
                            // frames. Failed channel growth leaves fewer outputs.
                            if (m_Warper->GetAnimationCount() < targetCount)
                                return CKERR_INVALIDPARAMETER;
                            m_FrameSrc = frameSrc;
                            ((CKKeyedAnimation *)m_Warper)->CenterAnimation(0.0f);
                            ((CKAnimation *)m_Warper)->SetStep(0.0f);
                            m_AnimSrc = (RCKAnimation *)m_ActiveAnimation;
                            m_ActiveAnimation = m_Warper;
                            m_AnimDest = (RCKAnimation *)destAnim;
                            m_FrameDest = frameDest;
                            m_TransitionMode = transitionmode;
                        } else {
                            // Can't interrupt - check if at end
                            float curFrame = ((CKAnimation *)m_ActiveAnimation)->GetFrame();
                            float curLen = ((CKAnimation *)m_ActiveAnimation)->GetLength();
                            CKBOOL atEnd = (curLen == curFrame);

                            if (((CKAnimation *)m_ActiveAnimation)->IsLinkedToFrameRate()) {
                                // Length is the starting pose when playing
                                // backwards. An unbreakable clip must reach zero.
                                if (((CKAnimation *)m_ActiveAnimation)->GetLinkedFrameRate() < 0.0f) {
                                    atEnd = (curFrame == 0.0f);
                                } else {
                                    float checkFrame = ((CKAnimation *)m_ActiveAnimation)->GetFrame();
                                    float checkLen = ((CKAnimation *)m_ActiveAnimation)->GetLength();
                                    atEnd = (checkLen - 0.3f <= checkFrame);
                                }
                            }

                            if (atEnd) {
                                if (!m_Warper || ((RCKAnimation *)m_ActiveAnimation)->m_Character != this)
                                    return CKERR_INVALIDPARAMETER;
                                int targetCount = destAnim->GetAnimationCount();
                                float frameSrc = m_ActiveAnimation->GetFrame();
                                AlignCharacterWithRootPosition();
                                if (CKIsChildClassOf((CKObject *)m_ActiveAnimation, CKCID_KEYEDANIMATION)) {
                                    frameDest = m_Warper->CreateTransition(
                                        (CKAnimation *)m_ActiveAnimation,
                                        anim,
                                        transitionmode,
                                        warplength,
                                        0.0f);
                                }
                                if (m_Warper->GetAnimationCount() < targetCount)
                                    return CKERR_INVALIDPARAMETER;
                                m_FrameSrc = frameSrc;
                                m_AnimSrc = (RCKAnimation *)m_ActiveAnimation;
                                m_TransitionMode = transitionmode;
                                ((CKKeyedAnimation *)m_Warper)->CenterAnimation(0.0f);
                                ((CKAnimation *)m_Warper)->SetStep(0.0f);
                                m_ActiveAnimation = m_Warper;
                                m_AnimDest = (RCKAnimation *)destAnim;
                                m_FrameDest = frameDest;
                            }
                        }
                    } else {
                        // A plain animation has no channels for the keyed
                        // builder. Switch at the same interruption boundary.
                        const float frame = activeAnim->GetFrame();
                        const float length = activeAnim->GetLength();
                        CKBOOL atEnd = length == frame;
                        if (activeAnim->IsLinkedToFrameRate()) {
                            atEnd = activeAnim->GetLinkedFrameRate() < 0.0f
                                ? frame == 0.0f : length - 0.3f <= frame;
                        }
                        if (activeAnim->CanBeInterrupt() || atEnd) {
                            m_ActiveAnimation = destAnim;
                            anim->CenterAnimation(0.0f);
                            anim->SetStep(0.0f);
                            m_AnimDest = nullptr;
                            m_FrameDest = 0.0f;
                        }
                    }
                } else {
                    // No active animation
                    m_ActiveAnimation = destAnim;
                    m_AnimDest = nullptr;
                    m_FrameDest = 0.0f;
                }
            } else {
                // A plain target must respect the same source and pending
                // transition priority even though it has no keyed channels.
                if (m_ActiveAnimation == m_Warper && m_AnimDest && !((CKAnimation *)m_AnimDest)->CanBeInterrupt())
                    return CK_OK;

                CKBOOL canSwitch = ((CKAnimation *)activeAnim)->CanBeInterrupt();
                if (!canSwitch) {
                    const float frame = ((CKAnimation *)activeAnim)->GetFrame();
                    const float length = ((CKAnimation *)activeAnim)->GetLength();
                    if (((CKAnimation *)activeAnim)->IsLinkedToFrameRate()) {
                        canSwitch = ((CKAnimation *)activeAnim)->GetLinkedFrameRate() < 0.0f
                            ? frame == 0.0f : length - 0.3f <= frame;
                    } else {
                        canSwitch = length == frame;
                    }
                }
                if (canSwitch) {
                    // Preserve the ready plain target's cursor and pose.
                    m_ActiveAnimation = destAnim;
                    m_AnimDest = nullptr;
                    m_FrameDest = 0.0f;
                }
            }
        }
    } else {
        // Default - just set destination
        m_AnimDest = (RCKAnimation *)destAnim;
        m_FrameDest = 0.0f;
    }

    m_Context->AddProfileTime(CK_PROFILE_ANIMATIONTIME, profiler.Current());
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
        const CKBOOL reversePlayback = activeAnim->IsLinkedToFrameRate() && activeAnim->GetLinkedFrameRate() < 0.0f;
        if (activeAnim->GetLength() != curFrame || activeAnim->CanBeInterrupt() || reversePlayback) {
            const float activeLen = activeAnim->GetLength();
            float nextFrame = activeAnim->GetNextFrame(deltat);
            const float frameAdvance = nextFrame - curFrame;

            if (nextFrame < activeLen || reversePlayback) {
                if (nextFrame < 0.0f) {
                    const float sourceStep = activeKeyed ? activeKeyed->m_Step : 0.0f;
                    if (activeKeyed) {
                        if (m_RootBodyPart) {
                            RCKObjectAnimation *rootObjAnim = activeKeyed->m_RootAnimation;
                            if (rootObjAnim) {
                                // Complete the root pose in the source world
                                // basis before transferring its terminal yaw.
                                VxVector pos;
                                ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                                VxVector currentPosition(0.0f, 0.0f, 0.0f), endpointPosition(0.0f, 0.0f, 0.0f);
                                rootObjAnim->EvaluatePosition(curFrame, currentPosition);
                                rootObjAnim->EvaluatePosition(0.0f, endpointPosition);
                                const VxVector delta = endpointPosition - currentPosition;
                                rootObjAnim->SetStep(0.0f, reinterpret_cast<CKKeyedAnimation *>(activeKeyed));
                                m_RootBodyPart->LocalMatrixChanged(FALSE, FALSE);
                                // Keep the actual accumulated root position,
                                // including a SetFrame followed by Center.
                                m_RootBodyPart->SetPosition(&pos, nullptr, FALSE);
                                m_RootBodyPart->Translate(&delta, (CK3dEntity *) this, FALSE);

                                ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                                SetPosition(&pos, nullptr, TRUE);
                            }
                        }
                        activeKeyed->m_Step = 0.0f;
                    }
                    AlignCharacterWithRootPosition();
                    if (activeKeyed)
                        activeKeyed->m_Step = sourceStep;

                    m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
                    m_AnimDest = nullptr;

                    if (destAnim) {
                        if (activeAnim->IsLinkedToFrameRate() && destAnim->IsLinkedToFrameRate()) {
                            const float sourceRate = activeAnim->GetLinkedFrameRate();
                            if (sourceRate != 0.0f)
                                nextFrame *= destAnim->GetLinkedFrameRate() / sourceRate;
                        } else if (activeAnim->IsLinkedToFrameRate() != destAnim->IsLinkedToFrameRate() && nextFrame != 0.0f) {
                            // Mixed modes share the unused portion of this
                            // process, rather than a source-frame duration.
                            nextFrame /= frameAdvance;
                            if (destAnim->IsLinkedToFrameRate())
                                nextFrame *= deltat * destAnim->GetLinkedFrameRate() * 0.001f;
                        }

                        float entryFrame = m_FrameDest;
                        // A zero-offset restart of this clip enters at its
                        // end when continuing backwards. Other entries remain
                        // literal, including signed transition frame data.
                        if (destAnim == activeAnim && m_FrameDest == 0.0f && nextFrame < 0.0f)
                            entryFrame = destAnim->GetLength();

                        if (destKeyed) {
                            destKeyed->CenterAnimation(entryFrame);
                        }
                        destAnim->SetFrame(entryFrame + nextFrame);

                        if (destKeyed && m_RootBodyPart) {
                            VxVector pos;
                            ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                            SetPosition(&pos, nullptr, TRUE);
                        }

                        if (destKeyed) {
                            destKeyed->CenterAnimation(destAnim->GetFrame());
                        }
                    }

                    doActivePostUpdate = false;
                }
            } else {
                if (activeAnim->CanBeInterrupt() || m_AnimDest) {
                    const float sourceStep = activeKeyed ? activeKeyed->m_Step : 0.0f;
                    const float endpointStep = activeLen > 0.0f ? 1.0f : 0.0f;
                    if (activeKeyed) {
                        if (m_RootBodyPart) {
                            RCKObjectAnimation *rootObjAnim = activeKeyed->m_RootAnimation;
                            if (rootObjAnim) {
                                VxVector pos;
                                ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                                VxVector currentPosition(0.0f, 0.0f, 0.0f), endpointPosition(0.0f, 0.0f, 0.0f);
                                rootObjAnim->EvaluatePosition(curFrame, currentPosition);
                                rootObjAnim->EvaluatePosition(activeLen, endpointPosition);
                                const VxVector delta = endpointPosition - currentPosition;
                                rootObjAnim->SetStep(endpointStep, reinterpret_cast<CKKeyedAnimation *>(activeKeyed));
                                m_RootBodyPart->LocalMatrixChanged(FALSE, FALSE);
                                m_RootBodyPart->SetPosition(&pos, nullptr, FALSE);
                                m_RootBodyPart->Translate(&delta, (CK3dEntity *) this, FALSE);

                                ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                                SetPosition(&pos, nullptr, TRUE);
                            }
                        }
                        // The virtual callback sees the completed pose, while
                        // the inactive source retains its logical cursor.
                        activeKeyed->m_Step = endpointStep;
                    }
                    AlignCharacterWithRootPosition();
                    if (activeKeyed)
                        activeKeyed->m_Step = sourceStep;

                    m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
                    m_AnimDest = nullptr;

                    if (destAnim) {
                        if (destKeyed) {
                            destKeyed->CenterAnimation(m_FrameDest);
                        }

                        if (activeLen > 0.0f) {
                            // One queued clip is consumed once, including a
                            // restart of the same animation. Carry its full
                            // remaining time instead of looping the source.
                            nextFrame -= activeLen;
                            if (activeAnim->IsLinkedToFrameRate() && destAnim->IsLinkedToFrameRate()) {
                                const float sourceRate = activeAnim->GetLinkedFrameRate();
                                if (sourceRate != 0.0f)
                                    nextFrame *= destAnim->GetLinkedFrameRate() / sourceRate;
                            } else if (activeAnim->IsLinkedToFrameRate() != destAnim->IsLinkedToFrameRate() && nextFrame != 0.0f) {
                                nextFrame /= frameAdvance;
                                if (destAnim->IsLinkedToFrameRate())
                                    nextFrame *= deltat * destAnim->GetLinkedFrameRate() * 0.001f;
                            }
                        }

                        const float destFrame = nextFrame + m_FrameDest;
                        destAnim->SetFrame(destFrame);

                        if (destKeyed && m_RootBodyPart) {
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
            // A noninterruptible clip displayed its end on the preceding
            // tick. Consume that terminal orientation before promoting next.
            AlignCharacterWithRootPosition();
            m_ActiveAnimation = (RCKKeyedAnimation *) m_AnimDest;
            m_AnimDest = nullptr;

            if (destAnim) {
                if (destKeyed) {
                    destKeyed->CenterAnimation(m_FrameDest);
                }

                const float sourceLen = activeAnim->GetLength();
                if (sourceLen != 0.0f) {
                    while (curFrame >= sourceLen) {
                        curFrame -= sourceLen;
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

        if (activeAnim) {
            activeAnim->SetFrame(nextFrameForActive);
            if (activeKeyed) {
                if (m_RootBodyPart) {
                    VxVector pos;
                    ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
                    SetPosition(&pos, nullptr, TRUE);
                }
                activeKeyed->CenterAnimation(nextFrameForActive);
            }
        }
    }

    for (int i = 0; i < m_SecondaryAnimationsCount; ++i) {
        CKSecondaryAnimation &secAnim = m_SecondaryAnimations[i];
        const CK_SECONDARYANIMATION_RUNTIME_MODE mode = secAnim.Mode;

        if (mode == CKSECONDARYANIMATIONRUNTIME_STARTINGWARP) {
            RCKKeyedAnimation *anim = secAnim.Animation;
            if (!anim) {
                continue;
            }

            const float current = ((CKAnimation *)anim)->GetFrame();
            const float next = ((CKAnimation *)anim)->GetNextFrame(deltat);
            const float remaining = next - ((CKAnimation *)anim)->GetLength();
            if (remaining <= 0.0f) {
                ((CKAnimation *)anim)->SetFrame(next);
            } else {
                secAnim.Mode = CKSECONDARYANIMATIONRUNTIME_PLAYING;
                auto *source = (CKAnimation *)m_Context->GetObject(secAnim.AnimID);
                PreDeleteBodyPartsForAnimation(source);

                const float startFrame = secAnim.GetStartingFrame();
                if (source) {
                    float sourceRemaining = remaining;
                    if (anim->IsLinkedToFrameRate() && source->IsLinkedToFrameRate()) {
                        sourceRemaining *= source->GetLinkedFrameRate() / anim->GetLinkedFrameRate();
                    } else if (anim->IsLinkedToFrameRate() != source->IsLinkedToFrameRate()) {
                        // Mixed clocks share only the unused fraction of this Process call.
                        sourceRemaining /= next - current;
                        if (source->IsLinkedToFrameRate()) {
                            sourceRemaining *= deltat * source->GetLinkedFrameRate() * 0.001f;
                        }
                    }
                    source->SetFrame(sourceRemaining + startFrame);
                }
            }
            continue;
        }

        if (mode == CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP) {
            RCKKeyedAnimation *anim = secAnim.Animation;
            if (!anim) {
                continue;
            }

            const float next = ((CKAnimation *)anim)->GetNextFrame(deltat);
            const float remaining = next - ((CKAnimation *)anim)->GetLength();
            CKBOOL livePoseApplied = FALSE;
            if (secAnim.Padding) {
                // Mixed clocks and a held main's entry-only handoff depend
                // on Process call boundaries rather than elapsed time alone.
                // Blend the retained secondary pose towards the main pose
                // actually reached by the preceding primary phase instead.
                CKAnimation *target = GetActiveAnimation();
                const CKBOOL mainStopped = target == nullptr;
                if (!target)
                    target = (CKAnimation *)m_Context->GetObject(secAnim.Padding);
                if (target && CKIsChildClassOf(target, CKCID_KEYEDANIMATION) &&
                    target->GetCharacter() == reinterpret_cast<CKCharacter *>(this)) {
                    secAnim.Padding = target->GetID();
                    float targetFrame = target->GetFrame();
                    if (mainStopped) {
                        // Completion retains the old logical cursor. Sample
                        // its displayed endpoint without advancing it again.
                        targetFrame = target->IsLinkedToFrameRate() && target->GetLinkedFrameRate() < 0.0f
                            ? 0.0f : target->GetLength();
                    }
                    const float duration = ((CKAnimation *)anim)->GetLength();
                    const CKBOOL linked = ((CKAnimation *)anim)->IsLinkedToFrameRate();
                    const float rate = ((CKAnimation *)anim)->GetLinkedFrameRate();
                    // Frame-zero keys retain the captured return pose, even
                    // if the original secondary later advances.
                    anim->SetCurrentStep(0.0f);
                    anim->CreateTransition((CKAnimation *)anim, target, 0, duration, targetFrame);
                    anim->LinkToFrameRate(linked, rate);
                    PreDeleteBodyPartsForAnimation((CKAnimation *)anim);
                    // Even a held endpoint or the strict release tick must
                    // reflect this call's main pose before releasing its gate.
                    ((CKAnimation *)anim)->SetFrame(next > duration ? duration : next);
                    livePoseApplied = TRUE;
                }
            }
            if (remaining <= 0.0f) {
                if (!livePoseApplied)
                    ((CKAnimation *)anim)->SetFrame(next);
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

        auto *anim = (CKAnimation *)m_Context->GetObject(secAnim.AnimID);
        if (!anim) {
            continue;
        }

        const float next = anim->GetNextFrame(deltat);
        const float length = anim->GetLength();
        const float remaining = next - length;
        const bool reverseEnd = next < 0.0f;
        if (!reverseEnd && remaining <= 0.0f) {
            anim->SetFrame(next);
            continue;
        }

        const float terminalFrame = reverseEnd ? 0.0f : length;
        float loopFrame = reverseEnd ? next : remaining;
        double completedLoops = 1.0;
        if (length > 0.0f &&
            (secAnim.Flags & (CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_LOOPNTIMES))) {
            loopFrame = fmodf(next, length);
            if (reverseEnd) {
                completedLoops = ceil(-static_cast<double>(next) / length);
                if (loopFrame < 0.0f)
                    loopFrame += length;
                // Exact negative periods retain frame zero until the next
                // update crosses it, just as forward playback retains length.
            } else {
                completedLoops = floor(static_cast<double>(next) / length);
                // At an exact multiple retain the preceding period's last frame;
                // its final loop is consumed only when the next update exceeds it.
                if (loopFrame == 0.0f) {
                    loopFrame = length;
                    completedLoops -= 1.0;
                }
            }
        }

        int removeIt = 0;
        if ((secAnim.Flags & CKSECONDARYANIMATION_LOOP) != 0) {
            anim->SetFrame(loopFrame);
        } else if ((secAnim.Flags & CKSECONDARYANIMATION_LOOPNTIMES) != 0) {
            if (completedLoops < secAnim.LoopCountRemaining) {
                secAnim.LoopCountRemaining -= static_cast<CKDWORD>(completedLoops);
                anim->SetFrame(loopFrame);
            } else {
                secAnim.LoopCountRemaining = 0;
                if ((secAnim.Flags & CKSECONDARYANIMATION_LASTFRAME) != 0) {
                    anim->SetFrame(terminalFrame);
                    secAnim.Flags &= ~CKSECONDARYANIMATION_LOOPNTIMES;
                } else {
                    removeIt = 1;
                }
            }
        } else if ((secAnim.Flags & CKSECONDARYANIMATION_LASTFRAME) != 0) {
            anim->SetFrame(terminalFrame);
        } else {
            removeIt = 1;
        }

        if (removeIt) {
            // A reverse animation finishes at its first pose. Sample that
            // endpoint before the return builder reads the source's step.
            if (reverseEnd)
                anim->SetFrame(0.0f);
            if ((secAnim.Flags & CKSECONDARYANIMATION_DOWARP) != 0 && CKIsChildClassOf((CKObject *)anim, CKCID_KEYEDANIMATION)) {
                RCKKeyedAnimation *active = m_ActiveAnimation;
                if (active && CKIsChildClassOf((CKObject *)active, CKCID_KEYEDANIMATION) &&
                    active->m_Character == this &&
                    anim->GetCharacter() == reinterpret_cast<CKCharacter *>(this) &&
                    anim != static_cast<CKAnimation *>(active)) {
                    RCKKeyedAnimation *transition = secAnim.Animation;
                    if (!transition) {
                        transition = (RCKKeyedAnimation *)m_Context->CreateObject(
                            CKCID_KEYEDANIMATION,
                            nullptr,
                            IsDynamic() ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK,
                            nullptr);
                        secAnim.Animation = transition;
                        if (transition) {
                            ((RCKAnimation *)transition)->m_Flags |= CKANIMATION_SECONDARYWARPER;
                        }
                    }

                    if (transition) {
                        const float activeFrame = ((CKAnimation *)active)->GetFrame();
                        float targetFrame = activeFrame + secAnim.WarpLength;
                        const float activeLen = ((CKAnimation *)active)->GetLength();
                        const CKBOOL returnLinked = ((CKAnimation *)active)->IsLinkedToFrameRate();
                        const float returnRate = ((CKAnimation *)active)->GetLinkedFrameRate();
                        const float returnLength = secAnim.WarpLength < 1.0f ? 1.0f : secAnim.WarpLength;
                        RCKKeyedAnimation *returnTarget = active;
                        secAnim.Padding = 0;
                        if (activeLen > 0.0f) {
                            if (returnLinked && returnRate == 0.0f) {
                                targetFrame = activeFrame;
                            } else if (returnLinked && returnRate < 0.0f) {
                                // The return warp uses abs(primary FPS). Predict the
                                // primary backwards over the builder's actual duration.
                                targetFrame = activeFrame - returnLength;
                                if (targetFrame < 0.0f) {
                                    if (m_AnimDest == (RCKAnimation *)active)
                                        targetFrame += activeLen;
                                    if (targetFrame < 0.0f)
                                        targetFrame = 0.0f;
                                }
                            } else {
                                while (targetFrame > activeLen) {
                                    targetFrame -= activeLen;
                                }
                            }
                        } else {
                            targetFrame = 0.0f;
                        }

                        if (activeLen > 0.0f && returnLinked && returnRate != 0.0f &&
                            m_AnimDest && m_AnimDest != (RCKAnimation *)active &&
                            CKIsChildClassOf((CKObject *)m_AnimDest, CKCID_KEYEDANIMATION) &&
                            ((CKAnimation *)m_AnimDest)->GetCharacter() == reinterpret_cast<CKCharacter *>(this) &&
                            ((CKAnimation *)m_AnimDest)->IsLinkedToFrameRate()) {
                            const float futureFrame = activeFrame + (returnRate < 0.0f ? -returnLength : returnLength);
                            const CKBOOL crossesEnd = returnRate < 0.0f ? futureFrame < 0.0f :
                                futureFrame >= activeLen && (activeFrame != activeLen || ((CKAnimation *)active)->CanBeInterrupt());
                            if (crossesEnd) {
                                // Sample the queued clip after the primary has
                                // consumed its boundary and remaining time.
                                returnTarget = (RCKKeyedAnimation *)m_AnimDest;
                                const float remaining = returnRate < 0.0f ? futureFrame : futureFrame - activeLen;
                                targetFrame = m_FrameDest + remaining * (((CKAnimation *)returnTarget)->GetLinkedFrameRate() / returnRate);
                                const float targetLen = ((CKAnimation *)returnTarget)->GetLength();
                                // Match SetFrame, including literal zero entries
                                // for reverse clips, rather than adding a period.
                                if (targetFrame < 0.0f)
                                    targetFrame = 0.0f;
                                else if (targetFrame > targetLen)
                                    targetFrame = targetLen;
                            }
                        }

                        const CKBOOL heldEnd = activeFrame == activeLen &&
                            !((CKAnimation *)active)->CanBeInterrupt() && !(returnLinked && returnRate < 0.0f);
                        if (heldEnd || (m_AnimDest && m_AnimDest != (RCKAnimation *)active &&
                            CKIsChildClassOf((CKObject *)m_AnimDest, CKCID_KEYEDANIMATION) &&
                            ((CKAnimation *)m_AnimDest)->GetCharacter() == reinterpret_cast<CKCharacter *>(this) &&
                            returnLinked != ((CKAnimation *)m_AnimDest)->IsLinkedToFrameRate())) {
                            secAnim.Padding = ((CKAnimation *)active)->GetID();
                            targetFrame = activeFrame;
                        }

                        if (anim == (CKAnimation *)returnTarget) {
                            // Capture the secondary's current pose before it
                            // becomes the queued primary. The builder rejects
                            // equal inputs, so sample its future through the warp.
                            transition->CreateTransition(anim, (CKAnimation *)active, 0, secAnim.WarpLength, activeFrame);
                            transition->SetCurrentStep(0.0f);
                            transition->CreateTransition((CKAnimation *)transition, (CKAnimation *)returnTarget, 0, secAnim.WarpLength, targetFrame);
                        } else {
                            transition->CreateTransition(anim, (CKAnimation *)returnTarget, 0, secAnim.WarpLength, targetFrame);
                        }
                        if (returnTarget != active) {
                            // Keep the requested duration in the triggering
                            // primary's clock even when the target has another FPS.
                            transition->LinkToFrameRate(returnLinked, returnRate < 0.0f ? -returnRate : returnRate);
                        }
                        secAnim.Flags |= CKSECONDARYANIMATION_DOWARP;
                        ((CKAnimation *)transition)->SetFrame(0.0f);
                        PreDeleteBodyPartsForAnimation((CKAnimation *)transition);
                        secAnim.Mode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
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
    // Based on IDA decompilation at 0x10011022
    // Returns non-zero if flag is set (cast to CKBOOL)
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
    // Based on IDA decompilation at 0x10010011
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // Check animation belongs to this character
    if (((RCKAnimation *)anim)->m_Character != this) {
        return CKERR_INVALIDPARAMETER;
    }

    // Handle FROMANIMATION flag - get mode from animation itself
    if ((PlayFlags & CKSECONDARYANIMATION_FROMANIMATION) != 0) {
        CK_SECONDARYANIMATION_FLAGS animMode = anim->GetSecondaryAnimationMode();
        if (animMode) {
            PlayFlags = animMode;
        }
    }

    CK_ID animID = anim->GetID();

    // Check if already playing
    for (int i = 0; i < m_SecondaryAnimationsCount; ++i) {
        if (m_SecondaryAnimations[i].AnimID == animID) {
            return CK_OK; // Already playing
        }
    }

    CKSecondaryAnimation *oldArray = m_SecondaryAnimations;

    // Ensure we have space
    if (m_SecondaryAnimationsAllocated <= m_SecondaryAnimationsCount) {
        m_SecondaryAnimations = new CKSecondaryAnimation[m_SecondaryAnimationsAllocated + 2];
        if (oldArray) {
            memcpy(m_SecondaryAnimations, oldArray, sizeof(CKSecondaryAnimation) * m_SecondaryAnimationsAllocated);
            delete[] oldArray;
        }
        // Initialize new slots (2 new entries)
        memset(&m_SecondaryAnimations[m_SecondaryAnimationsCount], 0, sizeof(CKSecondaryAnimation) * 2);
        m_SecondaryAnimationsAllocated += 2;
    }

    CKBOOL warped = FALSE;
    // Handle DOWARP flag - create transition from active animation
    if ((PlayFlags & CKSECONDARYANIMATION_DOWARP) != 0) {
        if (CKIsChildClassOf((CKObject *)anim, CKCID_KEYEDANIMATION)) {
            RCKKeyedAnimation *activeKeyed = m_ActiveAnimation;
            if (activeKeyed && CKIsChildClassOf((CKObject *)activeKeyed, CKCID_KEYEDANIMATION) &&
                activeKeyed->m_Character == this && (CKAnimation *)activeKeyed != anim) {
                RCKKeyedAnimation *transition = m_SecondaryAnimations[m_SecondaryAnimationsCount].Animation;
                if (!transition) {
                    transition = (RCKKeyedAnimation *)m_Context->CreateObject(
                        CKCID_KEYEDANIMATION,
                        nullptr,
                        IsDynamic() ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK,
                        nullptr);

                    CKDWORD flags = ((RCKAnimation *)transition)->m_Flags;
                    flags |= CKANIMATION_SECONDARYWARPER;
                    ((RCKAnimation *)transition)->m_Flags = flags;
                    ((CKKeyedAnimation *)transition)->Clear();
                }

                m_SecondaryAnimations[m_SecondaryAnimationsCount].Animation = transition;
                transition->CreateTransition((CKAnimation *)activeKeyed, anim, 0, warplength, StartingFrame);
                m_SecondaryAnimations[m_SecondaryAnimationsCount].Mode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
                m_SecondaryAnimations[m_SecondaryAnimationsCount].WarpLength = warplength;
                ((CKAnimation *)transition)->SetFrame(0.0f);
                PreDeleteBodyPartsForAnimation((CKAnimation *)transition);
                m_SecondaryAnimations[m_SecondaryAnimationsCount].Mode = CKSECONDARYANIMATIONRUNTIME_STARTINGWARP;
                warped = TRUE;
            }
        }
    }
    if (!warped) {
        // An unavailable transition falls back to ordinary secondary playback.
        anim->SetFrame(StartingFrame);
        if (CKIsChildClassOf((CKObject *)anim, CKCID_KEYEDANIMATION)) {
            PreDeleteBodyPartsForAnimation(anim);
        }
        m_SecondaryAnimations[m_SecondaryAnimationsCount].Mode = CKSECONDARYANIMATIONRUNTIME_PLAYING;
    }

    // Set common fields
    m_SecondaryAnimations[m_SecondaryAnimationsCount].SetStartingFrame(StartingFrame);
    m_SecondaryAnimations[m_SecondaryAnimationsCount].AnimID = animID;
    m_SecondaryAnimations[m_SecondaryAnimationsCount].Flags = PlayFlags;
    m_SecondaryAnimations[m_SecondaryAnimationsCount].Padding = 0;
    m_SecondaryAnimations[m_SecondaryAnimationsCount].LoopCountRemaining = LoopCount;

    m_SecondaryAnimationsCount++;
    return CK_OK;
}

CKERROR RCKCharacter::StopSecondaryAnimation(CKAnimation *anim, CKBOOL warp, float warplength) {
    // Based on IDA decompilation at 0x1001044B
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    if (((RCKAnimation *)anim)->m_Character != this) {
        return CKERR_INVALIDPARAMETER;
    }

    CK_ID animID = anim->GetID();

    // Find the secondary animation
    int idx = 0;
    while (idx < m_SecondaryAnimationsCount && m_SecondaryAnimations[idx].AnimID != animID)
        ++idx;
    if (idx >= m_SecondaryAnimationsCount) {
        return CK_OK; // Not playing
    }

    int warped = 0;
    if (warp) {
        if (m_SecondaryAnimations[idx].Mode == CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP) {
            return CK_OK;
        }

        RCKKeyedAnimation *activeKeyed = m_ActiveAnimation;
        if (activeKeyed && CKIsChildClassOf((CKObject *)activeKeyed, CKCID_KEYEDANIMATION) &&
            activeKeyed->m_Character == this) {
            CKAnimation *fromAnim = nullptr;
            if (m_SecondaryAnimations[idx].Mode == CKSECONDARYANIMATIONRUNTIME_PLAYING) {
                fromAnim = (CKAnimation *)m_Context->GetObject(m_SecondaryAnimations[idx].AnimID);
            } else {
                fromAnim = (CKAnimation *)m_SecondaryAnimations[idx].Animation;
            }
            if (!fromAnim || fromAnim == (CKAnimation *)activeKeyed ||
                !CKIsChildClassOf((CKObject *)fromAnim, CKCID_KEYEDANIMATION)) {
                RemoveSecondaryAnimationAt(idx);
                return CK_OK;
            }
            RCKKeyedAnimation *transition = m_SecondaryAnimations[idx].Animation;
            if (!transition) {
                transition = (RCKKeyedAnimation *)m_Context->CreateObject(
                    CKCID_KEYEDANIMATION,
                    nullptr,
                    IsDynamic() ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK,
                    nullptr);
                m_SecondaryAnimations[idx].Animation = transition;
                if (transition) {
                    ((RCKAnimation *)transition)->m_Flags |= CKANIMATION_SECONDARYWARPER;
                }
            }

            const float activeFrame = ((CKAnimation *)activeKeyed)->GetFrame();
            float targetFrame = activeFrame + warplength;
            const float activeLen = ((CKAnimation *)activeKeyed)->GetLength();
            const CKBOOL returnLinked = ((CKAnimation *)activeKeyed)->IsLinkedToFrameRate();
            const float returnRate = ((CKAnimation *)activeKeyed)->GetLinkedFrameRate();
            const float returnLength = warplength < 1.0f ? 1.0f : warplength;
            RCKKeyedAnimation *returnTarget = activeKeyed;
            m_SecondaryAnimations[idx].Padding = 0;
            if (activeLen > 0.0f) {
                if (returnLinked && returnRate == 0.0f) {
                    targetFrame = activeFrame;
                } else if (returnLinked && returnRate < 0.0f) {
                    // Match the return warp's abs(primary FPS) clock and the
                    // main player's single queued-self restart, if any.
                    targetFrame = activeFrame - returnLength;
                    if (targetFrame < 0.0f) {
                        if (m_AnimDest == (RCKAnimation *)activeKeyed)
                            targetFrame += activeLen;
                        if (targetFrame < 0.0f)
                            targetFrame = 0.0f;
                    }
                } else {
                    while (targetFrame > activeLen)
                        targetFrame -= activeLen;
                }
            } else {
                targetFrame = 0.0f;
            }

            if (activeLen > 0.0f && returnLinked && returnRate != 0.0f &&
                m_AnimDest && m_AnimDest != (RCKAnimation *)activeKeyed &&
                CKIsChildClassOf((CKObject *)m_AnimDest, CKCID_KEYEDANIMATION) &&
                ((CKAnimation *)m_AnimDest)->GetCharacter() == reinterpret_cast<CKCharacter *>(this) &&
                ((CKAnimation *)m_AnimDest)->IsLinkedToFrameRate()) {
                const float futureFrame = activeFrame + (returnRate < 0.0f ? -returnLength : returnLength);
                const CKBOOL crossesEnd = returnRate < 0.0f ? futureFrame < 0.0f :
                    futureFrame >= activeLen && (activeFrame != activeLen || ((CKAnimation *)activeKeyed)->CanBeInterrupt());
                if (crossesEnd) {
                    // The main player consumes its queued clip later. Sample
                    // that clip without advancing it or consuming the queue.
                    returnTarget = (RCKKeyedAnimation *)m_AnimDest;
                    const float remaining = returnRate < 0.0f ? futureFrame : futureFrame - activeLen;
                    targetFrame = m_FrameDest + remaining * (((CKAnimation *)returnTarget)->GetLinkedFrameRate() / returnRate);
                    const float targetLen = ((CKAnimation *)returnTarget)->GetLength();
                    if (targetFrame < 0.0f)
                        targetFrame = 0.0f;
                    else if (targetFrame > targetLen)
                        targetFrame = targetLen;
                }
            }

            const CKBOOL heldEnd = activeFrame == activeLen &&
                !((CKAnimation *)activeKeyed)->CanBeInterrupt() && !(returnLinked && returnRate < 0.0f);
            if (heldEnd || (m_AnimDest && m_AnimDest != (RCKAnimation *)activeKeyed &&
                CKIsChildClassOf((CKObject *)m_AnimDest, CKCID_KEYEDANIMATION) &&
                ((CKAnimation *)m_AnimDest)->GetCharacter() == reinterpret_cast<CKCharacter *>(this) &&
                returnLinked != ((CKAnimation *)m_AnimDest)->IsLinkedToFrameRate())) {
                // A held main only enters the queued clip on its next call,
                // or stops without a queue. Track its actual pose as for mixed clocks.
                m_SecondaryAnimations[idx].Padding = ((CKAnimation *)activeKeyed)->GetID();
                targetFrame = activeFrame;
            }

            if (transition && fromAnim && CKIsChildClassOf((CKObject *)fromAnim, CKCID_KEYEDANIMATION)) {
                // During STARTINGWARP, fromAnim is transition itself. Capture
                // its current blend before resetting the new return cursor.
                if (fromAnim == (CKAnimation *)returnTarget) {
                    // The queued primary may be the original secondary. The
                    // builder rejects equal inputs, so capture its current
                    // pose in the private warp before sampling its future.
                    transition->CreateTransition(fromAnim, (CKAnimation *)activeKeyed, 0, warplength, activeFrame);
                    transition->SetCurrentStep(0.0f);
                    transition->CreateTransition((CKAnimation *)transition, (CKAnimation *)returnTarget, 0, warplength, targetFrame);
                } else {
                    transition->CreateTransition(fromAnim, (CKAnimation *)returnTarget, 0, warplength, targetFrame);
                }
                if (returnTarget != activeKeyed)
                    transition->LinkToFrameRate(returnLinked, returnRate < 0.0f ? -returnRate : returnRate);
                m_SecondaryAnimations[idx].Flags |= CKSECONDARYANIMATION_DOWARP;
                m_SecondaryAnimations[idx].WarpLength = warplength;
                ((CKAnimation *)transition)->SetFrame(0.0f);
                PreDeleteBodyPartsForAnimation((CKAnimation *)transition);
                m_SecondaryAnimations[idx].Mode = CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP;
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
    // Based on IDA decompilation at 0x100106FC
    if (!anim) {
        return CKERR_INVALIDPARAMETER;
    }

    // The native getter shifts flags back by 16; its caller's 0x800000 mask
    // is erroneous. Use the public DOWARP flag so this overload can warp.
    int mode = anim->GetSecondaryAnimationMode();
    CKBOOL warp = (mode & CKSECONDARYANIMATION_DOWARP) != 0;

    return StopSecondaryAnimation(anim, warp, warplength);
}

int RCKCharacter::GetSecondaryAnimationsCount() {
    return m_SecondaryAnimationsCount;
}

CKAnimation *RCKCharacter::GetSecondaryAnimation(int index) {
    if (index < 0 || index >= m_SecondaryAnimationsCount || !m_SecondaryAnimations) {
        return nullptr;
    }
    CKObject *obj = m_Context->GetObject(m_SecondaryAnimations[index].AnimID);
    if (!obj || !CKIsChildClassOf(obj, CKCID_ANIMATION)) {
        return nullptr;
    }
    return (CKAnimation *) obj;
}

void RCKCharacter::FlushSecondaryAnimations() {
    // Based on IDA decompilation at 0x100103CD
    // First find floor reference if not set
    if (!m_FloorRef) {
        FindFloorReference();
    }

    // Iterate through secondary animations and update body parts
    for (CKWORD i = 0; i < m_SecondaryAnimationsCount; ++i) {
        CKObject *obj = m_Context->GetObject(m_SecondaryAnimations[i].AnimID);
        RCKAnimation *anim = (obj && CKIsChildClassOf(obj, CKCID_ANIMATION)) ? (RCKAnimation *) obj : nullptr;
        if (anim) {
            NotifyBodyPartsInAnimation(anim, nullptr);
        }
        if (m_SecondaryAnimations[i].Animation)
            NotifyBodyPartsInAnimation((CKAnimation *)m_SecondaryAnimations[i].Animation, nullptr);
    }

    m_SecondaryAnimationsCount = 0;
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
        CKObjectAnimation *rootAnimation = keyedAnim->DoesCharacterTakeOrientation()
            ? keyedAnim->GetAnimation((CK3dEntity *)m_RootBodyPart) : nullptr;
        if (rootAnimation) {
            // Native 0x10011D81 extracts yaw relative to the first rotation.
            VxQuaternion first(0, 0, 0, 1), heading(0, 0, 0, 1);
            rootAnimation->EvaluateRotation(0.0f, first);
            rootAnimation->EvaluateRotation(keyedAnim->GetFrame(), heading);
            first.w = -first.w;
            heading.Multiply(first);
            heading.x = 0.0f;
            heading.z = 0.0f;
            heading.Normalize();
            VxMatrix turn, world;
            heading.ToMatrix(turn);
            Vx3DMultiplyMatrix(world, turn, m_WorldMatrix);
            VxVector position;
            ((CK3dEntity *)m_RootBodyPart)->GetPosition(&position, nullptr);
            world[3][0] = position.x;
            world[3][1] = position.y;
            world[3][2] = position.z;
            SetWorldMatrix(world, TRUE);
        } else {
            VxVector pos;
            ((CK3dEntity *) m_RootBodyPart)->GetPosition(&pos, nullptr);
            SetPosition(&pos, nullptr, TRUE);
        }
    }

    // Center animation
    float frame = keyedAnim->GetFrame();
    keyedAnim->CenterAnimation(frame);
}

CK3dEntity *RCKCharacter::GetFloorReferenceObject() {
    return (CK3dEntity *) m_FloorRef;
}

void RCKCharacter::SetFloorReferenceObject(CK3dEntity *FloorRef) {
    if (m_FloorRef == (RCK3dEntity *)FloorRef)
        return;
    m_FloorRef = (RCK3dEntity *) FloorRef;

    // Root and floor channels lead the low-LOD prefix. Rebuild that order when
    // the floor changes, including transitions retained for later reuse.
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        if (CKIsChildClassOf(*it, CKCID_KEYEDANIMATION))
            ((RCKKeyedAnimation *)*it)->m_Flags &= ~CKANIMATION_SUBANIMSSORTED;
    }
    RCKAnimation *playing[] = {m_ActiveAnimation, m_AnimDest, m_Warper};
    for (int i = 0; i < 3; ++i) {
        if (playing[i] && CKIsChildClassOf((CKObject *)playing[i], CKCID_KEYEDANIMATION))
            playing[i]->m_Flags &= ~CKANIMATION_SUBANIMSSORTED;
    }
    for (int i = 0; i < m_SecondaryAnimationsAllocated; ++i) {
        if (m_SecondaryAnimations[i].Animation)
            m_SecondaryAnimations[i].Animation->m_Flags &= ~CKANIMATION_SUBANIMSSORTED;
    }
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
            SetFloorReferenceObject((CK3dEntity *)child);
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kFootsteps)) {
            SetFloorReferenceObject((CK3dEntity *)child);
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kFoot)) {
            SetFloorReferenceObject((CK3dEntity *)child);
            return;
        }
    }

    for (CKObject **it = m_RootBodyPart->m_Children.Begin(); it != m_RootBodyPart->m_Children.End(); ++it) {
        CKObject *child = *it;
        if (!child) continue;
        CKSTRING name = child->GetName();
        if (name && strstr(name, kPas)) {
            SetFloorReferenceObject((CK3dEntity *)child);
            return;
        }
    }
}

void RCKCharacter::RemoveSecondaryAnimationAt(int index) {
    // Based on IDA decompilation at 0x10011AB1
    if (index < 0 || index >= m_SecondaryAnimationsCount) {
        return;
    }

    CKAnimation *anim = (CKAnimation *)m_Context->GetObject(m_SecondaryAnimations[index].AnimID);
    if (anim) {
        NotifyBodyPartsInAnimation(anim, nullptr);
    }
    if (m_SecondaryAnimations[index].Animation)
        NotifyBodyPartsInAnimation((CKAnimation *)m_SecondaryAnimations[index].Animation, nullptr);

    const int newCount = m_SecondaryAnimationsCount - 1;
    m_SecondaryAnimationsCount = (CKWORD)newCount;

    if (index == newCount) {
        // Last element - just clear it
        m_SecondaryAnimations[index].StartingFrameBits = 0;
        m_SecondaryAnimations[index].AnimID = 0;
        m_SecondaryAnimations[index].Flags = 0;
        m_SecondaryAnimations[index].WarpLength = 0;
        m_SecondaryAnimations[index].Padding = 0;
        m_SecondaryAnimations[index].LoopCountRemaining = 0;
        m_SecondaryAnimations[index].Mode = static_cast<CK_SECONDARYANIMATION_RUNTIME_MODE>(0);
        return;
    }

    // Preserve the Animation pointer from the element being removed
    RCKKeyedAnimation *preserved = m_SecondaryAnimations[index].Animation;

    // Move remaining elements down
    for (int i = index; i < newCount; ++i) {
        memcpy(&m_SecondaryAnimations[i], &m_SecondaryAnimations[i + 1], sizeof(CKSecondaryAnimation));
    }

    // Clear the last slot and restore preserved animation pointer
    m_SecondaryAnimations[newCount].StartingFrameBits = 0;
    m_SecondaryAnimations[newCount].AnimID = 0;
    m_SecondaryAnimations[newCount].Flags = 0;
    m_SecondaryAnimations[newCount].WarpLength = 0;
    m_SecondaryAnimations[newCount].Padding = 0;
    m_SecondaryAnimations[newCount].LoopCountRemaining = 0;
    m_SecondaryAnimations[newCount].Mode = static_cast<CK_SECONDARYANIMATION_RUNTIME_MODE>(0);
    m_SecondaryAnimations[newCount].Animation = preserved;
}

void RCKCharacter::PreDeleteBodyPartsForAnimation(CKAnimation *anim) {
    // Native 0x10048148 sets BodyPart::SetExclusiveAnimation, not PreDelete.
    // Playback callers pass the keyed animation itself as the exclusive gate.
    NotifyBodyPartsInAnimation(anim, anim);
}
