#include "RCKKeyedAnimation.h"
#include "RCKObjectAnimation.h"
#include "RCK3dEntity.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKKeyedAnimation::RCKKeyedAnimation(CKContext *Context, CKSTRING name)
    : RCKAnimation(Context, name),
      m_RuntimeFlags(0),
      m_Merged(FALSE),
      m_MergeFactor(0.0f),
      m_RootAnimation(nullptr),
      m_Vector(0.0f, 0.0f, 0.0f) {
}

RCKKeyedAnimation::~RCKKeyedAnimation() {
    Clear();
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
    m_Animations.PushBack(anim);
    return CK_OK;
}

CKERROR RCKKeyedAnimation::RemoveAnimation(CKObjectAnimation *anim) {
    if (!anim)
        return CKERR_INVALIDPARAMETER;
    m_Animations.Remove(anim);
    return CK_OK;
}

int RCKKeyedAnimation::GetAnimationCount() {
    return m_Animations.Size();
}

CKObjectAnimation *RCKKeyedAnimation::GetAnimation(CK3dEntity *ent) {
    if (!ent)
        return nullptr;

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
