#include "RCKKeyedAnimation.h"

#include <cmath>

#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObjectManager.h"
#include "CKDependencies.h"
#include "CKGlobals.h"
#include "CKMemoryPool.h"
#include "RCKObjectAnimation.h"
#include "RCK3dEntity.h"
#include "RCKCharacter.h"

CK_CLASSID RCKKeyedAnimation::m_ClassID = CKCID_KEYEDANIMATION;

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
    return m_ClassID;
}

//=============================================================================
// Virtual Methods from CKObject
//=============================================================================

void RCKKeyedAnimation::CheckPreDeletion() {
    RCKAnimation::CheckPreDeletion();

    // A dependency remap can retain empty slots. The array's deletion check
    // expects object pointers, so remove those slots before checking targets.
    CKBOOL removedEmpty = FALSE;
    while (m_Animations.RemoveObject(nullptr))
        removedEmpty = TRUE;
    if (m_Animations.Check() || removedEmpty) {
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
        UpdateRootEntity();
    }

    // Clear root animation if it's been deleted
    if (m_RootAnimation) {
        if (m_RootAnimation->IsToBeDeleted()) {
            m_RootAnimation = nullptr;
        }
    }
}

CKBOOL RCKKeyedAnimation::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    if (cid == CKCID_OBJECTANIMATION && m_Animations.IsHere(obj))
        return TRUE;

    return CKObject::IsObjectUsed(obj, cid);
}

void RCKKeyedAnimation::PreSave(CKFile *file, CKDWORD flags) {
    CKObject::PreSave(file, flags);
    file->SaveObjects((CKObject **)m_Animations.Begin(), m_Animations.Size(), flags);
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
    if (file || (flags & CK_STATESAVE_KEYEDANIMANIMLIST) != 0) {
        chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
        m_Animations.Save(chunk);
    }

    // Save merged state and factor (identifier 0x100000)
    if (file || (flags & CK_STATESAVE_KEYEDANIMMERGE) != 0) {
        chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMMERGE);
        chunk->WriteInt(m_Merged);
        chunk->WriteFloat(m_MergeFactor);
    }

    // Save animation sub-chunks (identifier 0x200000) - only when not saving to file
    if (!file && (flags & CK_STATESAVE_KEYEDANIMSUBANIMS) != 0) {
        chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS);
        chunk->WriteDword(m_Animations.Size());
        for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
            CKObject *obj = *it;
            CKStateChunk *subChunk = obj ? obj->Save(nullptr, flags) : nullptr;
            // A missing entry is a valid empty snapshot. A present animation
            // whose Save failed must not become a successful empty snapshot.
            if (obj && !subChunk) {
                DeleteCKStateChunk(chunk);
                return nullptr;
            }
            chunk->WriteObject(obj);
            chunk->WriteSubChunk(subChunk);
            DeleteCKStateChunk(subChunk);
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
    RCK3dEntity *previousRoot = m_RootEntity;
    CKERROR loadError = RCKAnimation::Load(chunk, file);
    if (loadError != CK_OK)
        return loadError;

    // File loads do not call UpdateRootEntity. Keep selective state loads from
    // discarding a valid centering cache, but invalidate a replaced root.
    if (m_RootEntity != previousRoot)
        m_RootAnimation = nullptr;

    // Load animations list
    if (chunk->SeekIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST)) {
        m_RootAnimation = nullptr;
        XSObjectPointerArray previousAnimations = m_Animations;
        m_Animations.Clear();
        m_Animations.Load(m_Context, chunk);
        m_Animations.Check();
        for (CKObject **it = previousAnimations.Begin(); it != previousAnimations.End(); ++it) {
            RCKObjectAnimation *anim = static_cast<RCKObjectAnimation *>(*it);
            if (anim && !m_Animations.IsHere(anim) && anim->m_ParentKeyedAnimation == this)
                SetParentKeyedAnimation(anim, nullptr);
        }
    }

    // Load merged state and factor
    if (chunk->SeekIdentifier(CK_STATESAVE_KEYEDANIMMERGE)) {
        m_Merged = chunk->ReadInt();
        m_MergeFactor = chunk->ReadFloat();
    }

    // Load animation sub-chunks - only when not loading from file
    if (!file) {
        if (chunk->SeekIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS)) {
            int count = chunk->ReadDword();
            for (int i = 0; i < count; ++i) {
                CKObject *obj = chunk->ReadObject(m_Context);
                CKStateChunk *subChunk = chunk->ReadSubChunk();
                if (obj) {
                    const CKERROR error = obj->Load(subChunk, nullptr);
                    if (loadError == CK_OK && error != CK_OK)
                        loadError = error;
                }
                DeleteCKStateChunk(subChunk);
            }
        }
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
        UpdateRootEntity();
    }

    m_Flags &= ~CKANIMATION_SUBANIMSSORTED;

    // Process each animation and set up parent references
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *anim = (RCKObjectAnimation *) *it;
        if (!anim)
            continue;
        SetParentKeyedAnimation(anim, this);

        // Check for app data containing vector
        VxVector *appData = (VxVector *) anim->GetAppData();
        if (appData) {
            m_Vector = *appData;
            delete appData;
            anim->SetAppData(nullptr);
        }
    }

    return loadError;
}

int RCKKeyedAnimation::GetMemoryOccupation() {
    int size = RCKAnimation::GetMemoryOccupation() + (sizeof(RCKKeyedAnimation) - sizeof(RCKAnimation));
    size += m_Animations.GetMemoryOccupation(FALSE);
    return size;
}

CKERROR RCKKeyedAnimation::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::PrepareDependencies(context);
    if (err != CK_OK)
        return err;

    m_Animations.Prepare(context);
    return context.FinishPrepareDependencies(this, RCKKeyedAnimation::m_ClassID);
}

CKERROR RCKKeyedAnimation::RemapDependencies(CKDependenciesContext &context) {
    RCK3dEntity *previousRoot = m_RootEntity;
    CKERROR err = RCKAnimation::RemapDependencies(context);
    if (err != CK_OK)
        return err;

    if (m_RootEntity != previousRoot)
        m_RootAnimation = nullptr;
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *previous = static_cast<RCKObjectAnimation *>(*it);
        if (!previous)
            continue;
        RCKObjectAnimation *remapped = static_cast<RCKObjectAnimation *>(context.Remap(previous));
        *it = remapped;
        if (remapped != previous) {
            m_RootAnimation = nullptr;
            if (!m_Animations.IsHere(previous) && previous->m_ParentKeyedAnimation == this)
                SetParentKeyedAnimation(previous, nullptr);
            SetParentKeyedAnimation(remapped, this);
        }
    }
    return CK_OK;
}

CKERROR RCKKeyedAnimation::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCKAnimation::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKKeyedAnimation *src = (RCKKeyedAnimation *) &o;

    m_RootAnimation = nullptr;
    XSObjectPointerArray previousAnimations = m_Animations;
    m_Animations = src->m_Animations;  // Array copy
    for (CKObject **it = previousAnimations.Begin(); it != previousAnimations.End(); ++it) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(*it);
        if (animation && !m_Animations.IsHere(animation) && animation->m_ParentKeyedAnimation == this)
            SetParentKeyedAnimation(animation, nullptr);
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

    m_Animations.PushBack(anim);
    SetParentKeyedAnimation((RCKObjectAnimation *) anim, this);

    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    m_Flags &= ~CKANIMATION_SUBANIMSSORTED;

    return CK_OK;
}

CKERROR RCKKeyedAnimation::RemoveAnimation(CKObjectAnimation *anim) {
    if (!anim)
        return CKERR_INVALIDPARAMETER;

    if (m_Animations.RemoveObject(anim) && !m_Animations.IsHere(anim)) {
        RCKObjectAnimation *objectAnimation = static_cast<RCKObjectAnimation *>(anim);
        if (objectAnimation->m_ParentKeyedAnimation == this)
            SetParentKeyedAnimation(objectAnimation, nullptr);
        if (m_RootAnimation == objectAnimation)
            m_RootAnimation = nullptr;
    }

    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    m_Flags &= ~CKANIMATION_SUBANIMSSORTED;

    return CK_OK;
}

int RCKKeyedAnimation::GetAnimationCount() {
    return m_Animations.Size();
}

CKObjectAnimation *RCKKeyedAnimation::GetAnimation(CK3dEntity *ent) {
    if (ent == (CK3dEntity *) m_RootEntity) {
        if (m_RootAnimation) {
            if (m_Animations.IsHere(m_RootAnimation) && m_RootAnimation->Get3dEntity() == ent)
                return (CKObjectAnimation *) m_RootAnimation;
            m_RootAnimation = nullptr;
        }

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
        RCKObjectAnimation *anim = (RCKObjectAnimation *) *it;
        if (anim && anim->Get3dEntity() == ent) {
            return (CKObjectAnimation *) anim;
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
    // Clear is also called by the SDL destructor. A child may already have
    // unregistered itself, so only detach pointers still owned by the context.
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *anim = static_cast<RCKObjectAnimation *>(*it);
        if (m_Context->m_ObjectManager->IsObjectSafe(anim) && anim->m_ParentKeyedAnimation == this)
            SetParentKeyedAnimation(anim, nullptr);
    }
    m_Animations.Clear();
    m_RootAnimation = nullptr;
    m_RootEntity = nullptr;
    m_Vector.Set(0.0f, 0.0f, 0.0f);
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    m_Flags &= ~CKANIMATION_SUBANIMSSORTED;
}

//=============================================================================
// Animation Manipulation Methods
//=============================================================================

// CK2_3D.dll 0x100481BB: propagate duration without rescaling key times.
void RCKKeyedAnimation::SetLength(float nbframe) {
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(*it);
        if (animation)
            animation->SetLength(nbframe);
    }
    RCKAnimation::SetLength(nbframe);
}

// CK2_3D.dll 0x1004A2F0 dispatches to the keyed SetStep override.
void RCKKeyedAnimation::SetFrame(float frame) {
    SetStep(m_Length != 0.0f ? frame / m_Length : 0.0f);
}

// CK2_3D.dll 0x10048366: apply the selected channels, then update hierarchy.
void RCKKeyedAnimation::SetStep(float step) {
    if (step > 1.0f) step = 1.0f;
    if (step < 0.0f) step = 0.0f;
    RCKObjectAnimation *root = static_cast<RCKObjectAnimation *>(GetAnimation(GetRootEntity()));
    if (root)
        SetParentKeyedAnimation(root, this);
    if (!(m_Flags & CKANIMATION_SUBANIMSSORTED))
        SortAnimations();

    int count = m_Animations.Size();
    if (m_Character && count) {
        int required = m_Character->GetFloorReferenceObject() ? 2 : 1;
        float lod = (m_Character->GetMoveableFlags() & VX_MOVEABLE_CHARACTERRENDERED)
            ? m_Character->GetAnimationLevelOfDetail() : 0.0f;
        if (count > required && lod < 1.0f)
            count = static_cast<int>(static_cast<double>(count - required) * lod) + required;
    }
    for (int i = 0; i < count; ++i) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(m_Animations[i]);
        if (animation)
            animation->SetStep(step, reinterpret_cast<CKKeyedAnimation *>(this));
    }
    if (m_Character && count)
        m_Character->LocalMatrixChanged(FALSE, TRUE);
    m_Step = step;
}

// CK2_3D.dll 0x10056362: cache the native contribution in the reserved slot.
float RCKKeyedAnimation::ComputeAnimationContribution(RCKObjectAnimation *animation) {
    if (!animation)
        return 0.0f;
    float contribution = 0.0f;
    if (animation->IsMerged()) {
        const float first = ComputeAnimationContribution(animation->m_Anim1);
        const float second = ComputeAnimationContribution(animation->m_Anim2);
        contribution = static_cast<float>((static_cast<double>(first) + second) * 0.5);
    } else if (animation->m_KeyframeData) {
        CKKeyframeData *data = animation->m_KeyframeData;
        CKAnimController *vectors[] = {data->m_PositionController, data->m_ScaleController};
        for (int channel = 0; channel < 2; ++channel) {
            CKAnimController *controller = vectors[channel];
            if (!controller || !controller->GetKeyCount())
                continue;
            const VxVector &first = static_cast<CKPositionKey *>(controller->GetKey(0))->Pos;
            float extent = 0.0f;
            for (int i = 1; i < controller->GetKeyCount(); ++i) {
                const VxVector &value = static_cast<CKPositionKey *>(controller->GetKey(i))->Pos;
                const double differenceSum = (static_cast<double>(first.x) - value.x) +
                    (static_cast<double>(first.y) - value.y) + (static_cast<double>(first.z) - value.z);
                float difference = static_cast<float>(std::fabs(differenceSum));
                if (difference > extent) extent = difference;
            }
            contribution += extent;
        }
        CKAnimController *rotations = data->m_RotationController;
        float rotationExtent = 0.0f;
        if (rotations) {
            for (int i = 0; i < rotations->GetKeyCount(); ++i) {
                const VxQuaternion &first = static_cast<CKRotationKey *>(rotations->GetKey(i))->Rot;
                for (int j = i + 1; j < rotations->GetKeyCount(); ++j) {
                    const VxQuaternion &second = static_cast<CKRotationKey *>(rotations->GetKey(j))->Rot;
                    const double dot = static_cast<double>(first.x) * second.x +
                        static_cast<double>(first.y) * second.y + static_cast<double>(first.z) * second.z +
                        static_cast<double>(first.w) * second.w;
                    float difference = static_cast<float>((1.0 - std::fabs(dot)) * 1000.0);
                    if (difference > rotationExtent) rotationExtent = difference;
                }
            }
        }
        contribution += rotationExtent;
        if (data->m_MorphController)
            contribution += 1000.0f;
    }
    memcpy(&animation->m_field_38, &contribution, sizeof(contribution));
    return contribution;
}

// CK2_3D.dll 0x10048282/0x1004821E: root/floor first, contribution descending.
void RCKKeyedAnimation::SortAnimations() {
    m_Flags |= CKANIMATION_SUBANIMSSORTED;
    if (!m_Character)
        return;
    CK3dEntity *floor = m_Character->GetFloorReferenceObject();
    for (int i = 0; i < m_Animations.Size(); ++i) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(m_Animations[i]);
        if (!animation)
            continue;
        CK3dEntity *entity = animation->Get3dEntity();
        float contribution = -1.0e10f;
        if (entity) {
            if (entity == reinterpret_cast<CK3dEntity *>(m_RootEntity) || entity == floor)
                contribution = 1.0e30f;
            else
                contribution = ComputeAnimationContribution(animation);
        }
        memcpy(&animation->m_field_38, &contribution, sizeof(contribution));
    }
    // Stable equal-score order avoids arbitrarily reordering duplicate channels.
    for (int i = 1; i < m_Animations.Size(); ++i) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(m_Animations[i]);
        float contribution = -1.0e30f;
        if (animation) memcpy(&contribution, &animation->m_field_38, sizeof(contribution));
        int j = i;
        while (j > 0) {
            RCKObjectAnimation *previous = static_cast<RCKObjectAnimation *>(m_Animations[j - 1]);
            float previousContribution = -1.0e30f;
            if (previous) memcpy(&previousContribution, &previous->m_field_38, sizeof(previousContribution));
            if (previousContribution >= contribution)
                break;
            m_Animations[j] = previous;
            --j;
        }
        m_Animations[j] = animation;
    }
}

void RCKKeyedAnimation::CenterAnimation(float frame) {
    // Resolve the hierarchy lazily and revalidate a channel that may have
    // been rebound since the last centering operation.
    m_RootAnimation = static_cast<RCKObjectAnimation *>(GetAnimation(GetRootEntity()));

    if (m_RootAnimation) {
        VxVector position(0.0f, 0.0f, 0.0f);
        m_RootAnimation->EvaluatePosition(frame, position);

        // Negate the position
        m_Vector.x = -position.x;
        m_Vector.y = -position.y;
        m_Vector.z = -position.z;

        SetParentKeyedAnimation(m_RootAnimation, this);
    }
}

// CK2_3D.dll 0x1004A320, 0x10048538 and 0x1004859B.
float RCKKeyedAnimation::GetMergeFactor() {
    return m_MergeFactor;
}

void RCKKeyedAnimation::SetMergeFactor(float factor) {
    m_MergeFactor = factor;
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(*it);
        if (animation)
            animation->SetMergeFactor(factor);
    }
}

CKBOOL RCKKeyedAnimation::IsMerged() {
    return m_Merged;
}

CKAnimation *RCKKeyedAnimation::CreateMergedAnimation(CKAnimation *anim2, CKBOOL dynamic) {
    if (!anim2 || !GetCharacter())
        return nullptr;

    RCKKeyedAnimation *other = static_cast<RCKKeyedAnimation *>(anim2);
    // The SDL object flags differ from the original DLL's legacy dynamic bit.
    // Animation transition/orientation flags must not determine object lifetime.
    CKBOOL dynamicObject = IsDynamic();
    CK_OBJECTCREATION_OPTIONS options = dynamicObject ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK;
    XString name;
    if (GetName()) name << GetName();
    name << "+";
    if (other->GetName()) name << other->GetName();

    RCKKeyedAnimation *merged = static_cast<RCKKeyedAnimation *>(
        m_Context->CreateObject(CKCID_KEYEDANIMATION, name.CStr(), options, nullptr));
    if (!merged)
        return nullptr;
    merged->m_Character = m_Character;
    merged->m_Merged = TRUE;
    merged->m_RootEntity = m_RootEntity;
    float length = other->GetLength() >= m_Length ? other->GetLength() : m_Length;
    CKBOOL complete = TRUE;

    // The original boolean selects intersection-only versus union membership.
    // First keep this list's order, then append unmatched entries from the other.
    for (int side = 0; side < (dynamic ? 1 : 2) && complete; ++side) {
        RCKKeyedAnimation *source = side ? other : this;
        RCKKeyedAnimation *opposite = side ? this : other;
        for (int i = 0; i < source->GetAnimationCount(); ++i) {
            RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(source->GetAnimation(i));
            if (!animation || !animation->Get3dEntity())
                continue;
            CKObjectAnimation *match = opposite->GetAnimation(animation->Get3dEntity());
            if (side && match)
                continue;

            RCKObjectAnimation *child = nullptr;
            if (match) {
                // The object-animation factory reads this internal creation bit.
                // Restore it afterwards so creating a blend does not edit its source.
                CKDWORD flags = animation->GetFlags();
                animation->SetFlags(dynamicObject ? flags | 0x20000000u : flags & ~0x20000000u);
                child = static_cast<RCKObjectAnimation *>(animation->CreateMergedAnimation(match));
                animation->SetFlags(flags);
            } else if (!dynamic) {
                CKDependencies dependencies;
                CKCopyDefaultClassDependencies(dependencies, CK_DEPENDENCIES_COPY);
                dependencies.m_Flags = CK_DEPENDENCIES_CUSTOM;
                dependencies.ModifyOptions(CKCID_CHARACTER, 0, 1);
                child = static_cast<RCKObjectAnimation *>(m_Context->CopyObject(animation, &dependencies, nullptr, options));
                if (child && child->m_KeyframeData && child->GetLength() > 0.0f) {
                    // Original 0x1004A64D scales key times, not the duration.
                    const float sourceLength = child->GetLength();
                    const float nativeFactor = length / sourceLength;
                    // Preserve normal native ratio rounding, but recover a
                    // finite time map when the ratio itself exceeds float.
                    const double factor = std::isfinite(nativeFactor) && nativeFactor != 0.0f
                        ? nativeFactor : static_cast<double>(length) / sourceLength;
                    CKKeyframeData *data = child->m_KeyframeData;
                    CKAnimController *controllers[] = {data->m_PositionController, data->m_ScaleController,
                        data->m_RotationController, data->m_ScaleAxisController, data->m_MorphController};
                    if (factor > 0.0f && factor != 1.0f) {
                        for (int channel = 0; channel < 5; ++channel) {
                            CKAnimController *controller = controllers[channel];
                            if (!controller)
                                continue;
                            for (int key = 0; key < controller->GetKeyCount(); ++key) {
                                CKKey *value = controller->GetKey(key);
                                value->SetTime(static_cast<float>(static_cast<double>(value->GetTime()) * factor));
                            }
                        }
                    }
                }
            } else {
                continue;
            }
            if (!child) {
                complete = FALSE;
                break;
            }
            merged->AddAnimation(child);
        }
    }

    if (!complete) {
        // Only objects created for this result are destroyed; source animations
        // and their dependencies remain live when a child factory fails.
        CKDependencies none;
        none.m_Flags = CK_DEPENDENCIES_NONE;
        while (merged->GetAnimationCount()) {
            CKObjectAnimation *child = merged->GetAnimation(0);
            merged->RemoveAnimation(child);
            m_Context->DestroyObject(child, 0, &none);
        }
        m_Context->DestroyObject(merged, 0, &none);
        return nullptr;
    }
    merged->SetLength(length);
    reinterpret_cast<CKBeObject *>(m_Character)->AddToSelfScenes(merged);
    merged->UpdateRootEntity();
    return merged;
}

float RCKKeyedAnimation::CreateTransition(CKAnimation *in, CKAnimation *out, CKDWORD OutTransitionMode, float length, float FrameTo) {
    // CK2_3D.dll 0x10048B57: first source is in, second is out.
    if (!in || !out || in == out)
        return 0.0f;
    RCKKeyedAnimation *animIn = static_cast<RCKKeyedAnimation *>(in);
    RCKKeyedAnimation *animOut = static_cast<RCKKeyedAnimation *>(out);
    if (animIn->m_Character != animOut->m_Character)
        return -1.0f;
    if (length < 1.0f) length = 1.0f;
    if (!(animIn->m_Flags & CKANIMATION_SUBANIMSSORTED)) animIn->SortAnimations();
    if (!(animOut->m_Flags & CKANIMATION_SUBANIMSSORTED)) animOut->SortAnimations();
    float inStep = animIn->GetStep();
    float outLength = animOut->GetLength();
    if (OutTransitionMode & CK_TRANSITION_WARPTOSAMEPOS)
        FrameTo = inStep * outLength;
    float stepTo = outLength != 0.0f ? FrameTo / outLength : 0.0f;
    RCK3dEntity *root = animOut->m_RootEntity;
    RCKCharacter *character = animOut->m_Character;
    CKDWORD inFlags = animIn->m_Flags;
    CKDWORD outFlags = animOut->m_Flags;
    float fps = fabsf(animOut->GetLinkedFrameRate());
    CKBOOL linked = animOut->IsLinkedToFrameRate();

    int outCount = animOut->GetAnimationCount();
    XSArray<RCKObjectAnimation *> outList, inList;
    outList.Resize(outCount); inList.Resize(outCount);
    for (int i = 0; i < outCount; ++i) {
        RCKObjectAnimation *destination = static_cast<RCKObjectAnimation *>(animOut->GetAnimation(i));
        outList[i] = destination; inList[i] = nullptr;
        if (!destination || !destination->Get3dEntity())
            continue;
        // The native loop deliberately keeps the last matching source channel.
        for (int j = 0; j < animIn->GetAnimationCount(); ++j) {
            RCKObjectAnimation *source = static_cast<RCKObjectAnimation *>(animIn->GetAnimation(j));
            if (source && source->Get3dEntity() == destination->Get3dEntity())
                inList[i] = source;
        }
    }

    XSArray<CKAnimKey> startingKeys;
    if (OutTransitionMode & CK_TRANSITION_WARPTOBEST) {
        startingKeys.Resize(outCount);
        for (int i = 0; i < outCount; ++i) {
            startingKeys[i].m_Pos.Set(0, 0, 0); startingKeys[i].m_Scl.Set(1, 1, 1);
            startingKeys[i].m_Rot = VxQuaternion(0, 0, 0, 1);
            startingKeys[i].m_SclRot = VxQuaternion(0, 0, 0, 1);
            if (inList[i] && outList[i])
                inList[i]->EvaluateKeys(inStep, &startingKeys[i].m_Rot, &startingKeys[i].m_Pos,
                    &startingKeys[i].m_Scl, &startingKeys[i].m_SclRot);
        }
        int samples = static_cast<int>(outLength * 0.5f);
        if (samples < 1) samples = 1;
        float bestError = 1.0e30f;
        float bestFrame = 0.0f;
        for (int frame = 0; frame < samples; ++frame) {
            float error = 0.0f;
            for (int i = 0; i < outCount; ++i) {
                if (!inList[i] || !outList[i]) continue;
                RCKObjectAnimation *destination = outList[i];
                const CKAnimKey &start = startingKeys[i];
                VxVector value;
                VxQuaternion rotation;
                if (destination->HasPositionInfo() && destination->EvaluatePosition(static_cast<float>(frame), value))
                    error += fabsf(value.x - start.m_Pos.x) + fabsf(value.y - start.m_Pos.y) + fabsf(value.z - start.m_Pos.z);
                if (destination->HasScaleInfo() && destination->EvaluateScale(static_cast<float>(frame), value))
                    error += fabsf(value.x - start.m_Scl.x) + fabsf(value.y - start.m_Scl.y) + fabsf(value.z - start.m_Scl.z);
                if (destination->HasRotationInfo() && destination->EvaluateRotation(static_cast<float>(frame), rotation)) {
                    const double dot = static_cast<double>(rotation.x) * start.m_Rot.x +
                        static_cast<double>(rotation.y) * start.m_Rot.y + static_cast<double>(rotation.z) * start.m_Rot.z +
                        static_cast<double>(rotation.w) * start.m_Rot.w;
                    error += static_cast<float>(10.0 * (1.0 - std::fabs(dot)));
                }
            }
            // Sum all channels; the original overwrites each category's score
            // with its last channel. Keep the earliest frame on equal scores.
            if (error < bestError) { bestError = error; bestFrame = static_cast<float>(frame); }
        }
        stepTo = outLength != 0.0f ? bestFrame / outLength : 0.0f;
    }

    // Dependency remapping may leave empty slots; the legacy deletion check
    // requires object pointers before preparing the transition result list.
    while (m_Animations.RemoveObject(nullptr)) {
    }
    m_Animations.Check();
    int previousCount = m_Animations.Size();
    while (m_Animations.Size() < outCount) {
        CK_OBJECTCREATION_OPTIONS options = IsDynamic() ? CK_OBJECTCREATION_DYNAMIC : CK_OBJECTCREATION_NONAMECHECK;
        RCKObjectAnimation *created = static_cast<RCKObjectAnimation *>(
            m_Context->CreateObject(CKCID_OBJECTANIMATION, nullptr, options, nullptr));
        if (!created) {
            CKDependencies none; none.m_Flags = CK_DEPENDENCIES_NONE;
            while (m_Animations.Size() > previousCount) {
                RCKObjectAnimation *last = static_cast<RCKObjectAnimation *>(m_Animations[m_Animations.Size() - 1]);
                RemoveAnimation(last); m_Context->DestroyObject(last, 0, &none);
            }
            return -1.0f;
        }
        AddAnimation(created);
    }
    m_RootEntity = root; m_RootAnimation = nullptr; m_Character = character;
    m_Flags |= CKANIMATION_SUBANIMSSORTED;
    LinkToFrameRate(linked, fps);
    m_Flags = (m_Flags & ~CKANIMATION_ALLOWTURN) | (outFlags & CKANIMATION_ALLOWTURN);
    for (int i = 0; i < m_Animations.Size(); ++i) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(m_Animations[i]);
        if (animation) animation->m_Flags |= CK_OBJECTANIMATION_WARPER | CK_OBJECTANIMATION_RESERVED;
    }
    // In-place input transitions modify their mapped channels, not array indices.
    int count = animIn == this ? outCount : m_Animations.Size();
    for (int i = 0; i < count; ++i) {
        RCKObjectAnimation *destination = i < outCount ? outList[i] : nullptr;
        RCKObjectAnimation *source = i < outCount ? inList[i] : nullptr;
        RCKObjectAnimation *animation = animIn == this ? source : static_cast<RCKObjectAnimation *>(m_Animations[i]);
        if (!animation) continue;
        if (source && destination) {
            animation->m_Flags &= ~CK_OBJECTANIMATION_WARPER;
            CKBOOL isRoot = root == reinterpret_cast<RCK3dEntity *>(destination->Get3dEntity());
            CKBOOL velocity = isRoot && (OutTransitionMode & CK_TRANSITION_USEVELOCITY);
            CKBOOL dontTurn = isRoot && (inFlags & CKANIMATION_ALIGNORIENTATION);
            // Repair the entity's registration before the lower-level builder
            // directly assigns its entity pointer, including reused channels.
            animation->Set3dEntity(destination->Get3dEntity());
            animation->CreateTransition(length, source, inStep, destination, stepTo, velocity, dontTurn,
                startingKeys.Size() ? &startingKeys[i] : nullptr);
            SetParentKeyedAnimation(animation, this);
        } else if (animIn != this) {
            animation->ClearAll();
        }
    }
    for (int i = 0; i < m_Animations.Size(); ++i) {
        RCKObjectAnimation *animation = static_cast<RCKObjectAnimation *>(m_Animations[i]);
        if (!animation) continue;
        if (animation->m_Flags & CK_OBJECTANIMATION_WARPER) animation->ClearAll();
        animation->m_Flags &= ~CK_OBJECTANIMATION_WARPER;
    }
    SetLength(length);
    return stepTo * outLength;
}


// sub_100499C1 - Update root entity by walking up hierarchy from first animation's entity
void RCKKeyedAnimation::UpdateRootEntity() {
    m_RootAnimation = nullptr;
    m_RootEntity = nullptr;
    ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);

    if (m_Animations.Size() == 0)
        return;

    // Empty snapshot slots and unbound animations do not identify a hierarchy.
    CK3dEntity *entity = nullptr;
    for (CKObject **it = m_Animations.Begin(); it != m_Animations.End(); ++it) {
        RCKObjectAnimation *anim = static_cast<RCKObjectAnimation *>(*it);
        if (anim && anim->Get3dEntity()) {
            entity = anim->Get3dEntity();
            break;
        }
    }
    while (entity) {
        CK3dEntity *parent = entity->GetParent();
        if (parent) {
            // Check if parent's class ID is 40 (CKCID_CHARACTER)
            if (parent->GetClassID() == CKCID_CHARACTER) {
                m_RootEntity = (RCK3dEntity *) entity;
                return;
            }
        }
        // Check if parent is our character's root (cast m_Character to CK3dEntity*)
        if (parent == (CK3dEntity *) m_Character) {
            m_RootEntity = (RCK3dEntity *) entity;
            return;
        }
        entity = parent;
    }
}

// sub_1004A2D0 - Set parent keyed animation on object animation
// Note: This must be called from within RCKKeyedAnimation methods due to friend access
void RCKKeyedAnimation::SetParentKeyedAnimation(RCKObjectAnimation *objAnim, RCKKeyedAnimation *parent) {
    if (objAnim) {
        objAnim->m_ParentKeyedAnimation = parent;
    }
}

//=============================================================================
// Static Class Methods (for class registration)
//=============================================================================

CKSTRING RCKKeyedAnimation::GetClassName() {
    return "Keyed Animation";
}

int RCKKeyedAnimation::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKKeyedAnimation::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKKeyedAnimation::Register() {
    // Based on IDA analysis
    CKCLASSNOTIFYFROM(RCKKeyedAnimation, RCKObjectAnimation);

    // Register associated parameter GUID: {0x5BF0D34D, 0x19E69236} (same as Animation)
    CKPARAMETERFROMCLASS(RCKKeyedAnimation, CKPGUID_ANIMATION);
}

CKKeyedAnimation *RCKKeyedAnimation::CreateInstance(CKContext *Context) {
    // Object size is 0x54 (84 bytes)
    RCKKeyedAnimation *anim = new RCKKeyedAnimation(Context, nullptr);
    return reinterpret_cast<CKKeyedAnimation *>(anim);
}
