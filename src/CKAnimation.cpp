#include "RCKAnimation.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKCharacter.h"

CK_CLASSID RCKAnimation::m_ClassID = CKCID_ANIMATION;

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKAnimation::RCKAnimation(CKContext *Context, CKSTRING name)
    : CKAnimation(Context, name),
      m_Character(nullptr),
      m_Length(100.0f),
      m_Step(0.0f),
      m_RootEntity(nullptr),
      m_Flags(CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK),
      m_FrameRate(30.0f) {}

RCKAnimation::~RCKAnimation() {}

CK_CLASSID RCKAnimation::GetClassID() {
    return m_ClassID;
}

//=============================================================================
// Serialization
// Based on IDA decompilation at 0x10047979 (Save) and 0x10047ADC (Load)
//=============================================================================

CKStateChunk *RCKAnimation::Save(CKFile *file, CKDWORD flags) {
    // Create state chunk with class ID 16 (CKCID_ANIMATION)
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_ANIMATION, file);
    CKStateChunk *baseChunk = CKObject::Save(file, flags);

    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Save flags and frame rate (identifier 0x10)
    if (file || (flags & 0x10) != 0) {
        chunk->WriteIdentifier(0x10);
        chunk->WriteDword(m_Flags);
        chunk->WriteFloat(m_FrameRate);
    }

    // Save length (identifier 0x40)
    if (file || (flags & 0x40) != 0) {
        chunk->WriteIdentifier(0x40);
        chunk->WriteFloat(m_Length);
    }

    // Save root entity (identifier 0x80)
    if (file || (flags & 0x80) != 0) {
        chunk->WriteIdentifier(0x80);
        chunk->WriteObjectArray(nullptr, 0); // Empty object array (legacy)
        chunk->WriteObject((CKObject *) m_RootEntity);
    }

    // Save character (identifier 0x100)
    if (file || (flags & 0x100) != 0) {
        chunk->WriteIdentifier(0x100);
        chunk->WriteObject((CKObject *) m_Character);
    }

    // Save step (identifier 0x200)
    if (file || (flags & 0x200) != 0) {
        chunk->WriteIdentifier(0x200);
        chunk->WriteFloat(m_Step);
    }

    // Close the chunk
    if (GetClassID() == CKCID_ANIMATION) {
        chunk->CloseChunk();
    } else {
        chunk->UpdateDataSize();
    }

    return chunk;
}

CKERROR RCKAnimation::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Load base class
    CKObject::Load(chunk, file);

    // Load length (identifier 0x40)
    if (chunk->SeekIdentifier(0x40)) {
        m_Length = chunk->ReadFloat();
    }

    // Load flags and frame rate (identifier 0x10)
    int size10 = chunk->SeekIdentifierAndReturnSize(0x10);
    if (size10 > 0) {
        if (size10 == 12) {
            // Old format: canInterrupt, linkedToFrameRate, frameRate
            CKBOOL canInterrupt = chunk->ReadInt();
            int linkedToFrameRate = chunk->ReadInt();
            m_FrameRate = chunk->ReadFloat();
            LinkToFrameRate(linkedToFrameRate, m_FrameRate);
            SetCanBeInterrupt(canInterrupt);
        } else if (size10 == 8) {
            // New format: flags, frameRate
            m_Flags = chunk->ReadDword();
            m_FrameRate = chunk->ReadFloat();
        }
    }

    // Load root entity (identifier 0x80)
    if (chunk->SeekIdentifier(0x80)) {
        XObjectArray objArray;
        objArray.Load(chunk); // Load and discard (legacy)
        m_RootEntity = (RCK3dEntity *) chunk->ReadObject(m_Context);
    }

    // Load character (identifier 0x100)
    if (chunk->SeekIdentifier(0x100)) {
        m_Character = (RCKCharacter *) chunk->ReadObject(m_Context);
    }

    // Load step (identifier 0x200)
    if (chunk->SeekIdentifier(0x200)) {
        m_Step = chunk->ReadFloat();
    }

    return CK_OK;
}

int RCKAnimation::GetMemoryOccupation() {
    return CKSceneObject::GetMemoryOccupation() + (sizeof(RCKAnimation) - sizeof(CKSceneObject));
}

CKERROR RCKAnimation::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKObject::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKAnimation *src = (RCKAnimation *) &o;
    m_Character = src->m_Character;
    m_RootEntity = src->m_RootEntity;
    m_Length = src->m_Length;
    m_Step = src->m_Step;
    m_Flags = src->m_Flags;
    m_FrameRate = src->m_FrameRate;

    return CK_OK;
}

// Based on IDA decompilation at 0x10047D3A
CKERROR RCKAnimation::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::RemapDependencies(context);
    if (err != CK_OK)
        return err;

    m_RootEntity = (RCK3dEntity *) context.Remap((CKObject *) m_RootEntity);
    m_Character = (RCKCharacter *) context.Remap((CKObject *) m_Character);

    return CK_OK;
}

//=============================================================================
// CKAnimation Virtual Methods
// Based on IDA decompilation
//=============================================================================

// 0x10047E90
float RCKAnimation::GetLength() {
    return m_Length;
}

// 0x10047EB0 - Frame is calculated as m_Step * m_Length
float RCKAnimation::GetFrame() {
    return m_Step * m_Length;
}

// 0x10047745 - Complex calculation with frame rate
float RCKAnimation::GetNextFrame(float delta_t) {
    float currentFrame = m_Step * m_Length;

    if ((m_Flags & CKANIMATION_LINKTOFRAMERATE) != 0) {
        // Frame rate linked: advance by delta_t * frameRate * 0.001
        return currentFrame + delta_t * m_FrameRate * 0.001f;
    } else {
        // Not frame rate linked: advance by 1 frame
        return currentFrame + 1.0f;
    }
}

// 0x10047ED0
float RCKAnimation::GetStep() {
    return m_Step;
}

// 0x10047EF0 - Set m_Step as normalized position
void RCKAnimation::SetFrame(float frame) {
    if (m_Length != 0.0f) {
        m_Step = frame / m_Length;
    } else {
        m_Step = 0.0f;
    }
}

// SetStep and SetCurrentStep both set m_Step directly
void RCKAnimation::SetStep(float step) {
    m_Step = step;
}

// 0x10047F30
void RCKAnimation::SetLength(float nbframe) {
    m_Length = nbframe;
}

// 0x10047F50
CKCharacter *RCKAnimation::GetCharacter() {
    return (CKCharacter *) m_Character;
}

// 0x10047796 - Always sets m_FrameRate regardless of link status
void RCKAnimation::LinkToFrameRate(CKBOOL link, float fps) {
    if (link) {
        m_Flags |= CKANIMATION_LINKTOFRAMERATE;
    } else {
        m_Flags &= ~CKANIMATION_LINKTOFRAMERATE;
    }
    m_FrameRate = fps;
}

// 0x10047F70
float RCKAnimation::GetLinkedFrameRate() {
    return m_FrameRate;
}

// 0x10047F90
CKBOOL RCKAnimation::IsLinkedToFrameRate() {
    return (m_Flags & CKANIMATION_LINKTOFRAMERATE) != 0;
}

// 0x100477D2 - TransitionMode stored in bits 8-16
void RCKAnimation::SetTransitionMode(CK_ANIMATION_TRANSITION_MODE mode) {
    m_Flags &= ~CKANIMATION_TRANSITION_ALL;
    m_Flags |= ((CKDWORD) mode << CK_TRANSITION_MODE_SHIFT);
}

// 0x10047805
CK_ANIMATION_TRANSITION_MODE RCKAnimation::GetTransitionMode() {
    return (CK_ANIMATION_TRANSITION_MODE) ((m_Flags & CKANIMATION_TRANSITION_ALL) >> CK_TRANSITION_MODE_SHIFT);
}

// 0x1004781E - SecondaryAnimationMode stored in bits 18-23
void RCKAnimation::SetSecondaryAnimationMode(CK_SECONDARYANIMATION_FLAGS mode) {
    m_Flags &= ~CKANIMATION_SECONDARY_ALL;
    m_Flags |= ((CKDWORD) mode << CK_SECONDARY_FLAGS_SHIFT);
}

// 0x10047851
CK_SECONDARYANIMATION_FLAGS RCKAnimation::GetSecondaryAnimationMode() {
    return (CK_SECONDARYANIMATION_FLAGS) ((m_Flags & CKANIMATION_SECONDARY_ALL) >> CK_SECONDARY_FLAGS_SHIFT);
}

// 0x1004786A - CanBeInterrupt uses bit 2 (value 4)
void RCKAnimation::SetCanBeInterrupt(CKBOOL can) {
    if (can) {
        m_Flags |= CKANIMATION_CANBEBREAK;
    } else {
        m_Flags &= ~CKANIMATION_CANBEBREAK;
    }
}

// 0x10047FB0
CKBOOL RCKAnimation::CanBeInterrupt() {
    return (m_Flags & CKANIMATION_CANBEBREAK) != 0;
}

// 0x1004789D - CharacterOrientation uses bit 4 (value 0x10)
void RCKAnimation::SetCharacterOrientation(CKBOOL orient) {
    if (orient) {
        m_Flags |= CKANIMATION_ALIGNORIENTATION;
    } else {
        m_Flags &= ~CKANIMATION_ALIGNORIENTATION;
    }
}

// 0x10047FD0
CKBOOL RCKAnimation::DoesCharacterTakeOrientation() {
    return (m_Flags & CKANIMATION_ALIGNORIENTATION) != 0;
}

// 0x10061E40
void RCKAnimation::SetFlags(CKDWORD flags) {
    m_Flags = flags;
}

// 0x10061E20
CKDWORD RCKAnimation::GetFlags() {
    return m_Flags;
}

// 0x100478D0 - Has special handling for CKKeyedAnimation (class 18)
CK3dEntity *RCKAnimation::GetRootEntity() {
    // If no root entity and this is a CKKeyedAnimation, rebuild it
    if (!m_RootEntity && CKIsChildClassOf((CKObject *) this, CKCID_KEYEDANIMATION)) {
        // RCKKeyedAnimation::RebuildHierarchy(this);
        // This is handled by derived class
    }
    return (CK3dEntity *) m_RootEntity;
}

// 0x10047F10
void RCKAnimation::SetCurrentStep(float Step) {
    m_Step = Step;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

// 0x10047C74
CKSTRING RCKAnimation::GetClassName() {
    return (CKSTRING) "Animation";
}

// 0x10047C7E
int RCKAnimation::GetDependenciesCount(int mode) {
    return 0;
}

// 0x10047C85
CKSTRING RCKAnimation::GetDependencies(int i, int mode) {
    return nullptr;
}

// 0x10047C8C
void RCKAnimation::Register() {
    // Registers for notification from 3D entities (class 33 = CKCID_3DENTITY)
    CKCLASSNOTIFYFROMCID(RCKAnimation, CKCID_3DENTITY);

    // Register associated parameter GUID: {0x5BF0D34D, 0x19E69236}
    CKPARAMETERFROMCLASS(RCKAnimation, CKPGUID_ANIMATION);
}

// 0x10047CCD
CKAnimation *RCKAnimation::CreateInstance(CKContext *Context) {
    // Object size is 0x34 (52 bytes)
    return new RCKAnimation(Context, nullptr);
}
