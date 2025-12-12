#include "RCKAnimation.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKCharacter.h"

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKAnimation::RCKAnimation(CKContext *Context, CKSTRING name)
    : CKAnimation(Context, name),
      m_Character(nullptr),
      m_Length(0.0f),
      m_CurrentFrame(0.0f),
      m_Step(1.0f),
      m_RootEntity(nullptr),
      m_Flags(0),
      m_FrameRate(30.0f),
      m_TransitionMode((CK_ANIMATION_TRANSITION_MODE) 0),
      m_SecondaryAnimationMode((CK_SECONDARYANIMATION_FLAGS) 0) {
}

RCKAnimation::~RCKAnimation() {
}

CK_CLASSID RCKAnimation::GetClassID() {
    return CKCID_ANIMATION;
}

//=============================================================================
// Serialization
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
    return CKSceneObject::GetMemoryOccupation() + 24;
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

//=============================================================================
// CKAnimation Virtual Methods
//=============================================================================

float RCKAnimation::GetLength() {
    return m_Length;
}

float RCKAnimation::GetFrame() {
    return m_CurrentFrame;
}

float RCKAnimation::GetNextFrame(float delta_t) {
    return m_CurrentFrame + delta_t * m_Step;
}

float RCKAnimation::GetStep() {
    return m_Step;
}

void RCKAnimation::SetFrame(float frame) {
    m_CurrentFrame = frame;
}

void RCKAnimation::SetStep(float step) {
    m_Step = step;
}

void RCKAnimation::SetLength(float nbframe) {
    m_Length = nbframe;
}

CKCharacter *RCKAnimation::GetCharacter() {
    return (CKCharacter *) m_Character;
}

void RCKAnimation::LinkToFrameRate(CKBOOL link, float fps) {
    if (link) {
        m_Flags |= 1; // Flag for linked to frame rate
        m_FrameRate = fps;
    } else {
        m_Flags &= ~1;
    }
}

float RCKAnimation::GetLinkedFrameRate() {
    return m_FrameRate;
}

CKBOOL RCKAnimation::IsLinkedToFrameRate() {
    return (m_Flags & 1) != 0;
}

void RCKAnimation::SetTransitionMode(CK_ANIMATION_TRANSITION_MODE mode) {
    m_TransitionMode = mode;
}

CK_ANIMATION_TRANSITION_MODE RCKAnimation::GetTransitionMode() {
    return m_TransitionMode;
}

void RCKAnimation::SetSecondaryAnimationMode(CK_SECONDARYANIMATION_FLAGS mode) {
    m_SecondaryAnimationMode = mode;
}

CK_SECONDARYANIMATION_FLAGS RCKAnimation::GetSecondaryAnimationMode() {
    return m_SecondaryAnimationMode;
}

void RCKAnimation::SetCanBeInterrupt(CKBOOL can) {
    if (can) {
        m_Flags |= 2;
    } else {
        m_Flags &= ~2;
    }
}

CKBOOL RCKAnimation::CanBeInterrupt() {
    return (m_Flags & 2) != 0;
}

void RCKAnimation::SetCharacterOrientation(CKBOOL orient) {
    if (orient) {
        m_Flags |= 4;
    } else {
        m_Flags &= ~4;
    }
}

CKBOOL RCKAnimation::DoesCharacterTakeOrientation() {
    return (m_Flags & 4) != 0;
}

void RCKAnimation::SetFlags(CKDWORD flags) {
    m_Flags = flags;
}

CKDWORD RCKAnimation::GetFlags() {
    return m_Flags;
}

CK3dEntity *RCKAnimation::GetRootEntity() {
    return (CK3dEntity *) m_RootEntity;
}

void RCKAnimation::SetCurrentStep(float Step) {
    m_Step = Step;
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCKAnimation::m_ClassID = CKCID_ANIMATION;

CKSTRING RCKAnimation::GetClassName() {
    return (CKSTRING) "Animation";
}

int RCKAnimation::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKAnimation::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKAnimation::Register() {
    // Based on IDA analysis
    CKClassNeedNotificationFrom(RCKAnimation::m_ClassID, CKCID_3DENTITY);

    // Register associated parameter GUID: {0x5BF0D34D, 0x19E69236}
    CKClassRegisterAssociatedParameter(RCKAnimation::m_ClassID, CKPGUID_ANIMATION);
}

CKAnimation *RCKAnimation::CreateInstance(CKContext *Context) {
    // Object size is 0x34 (52 bytes)
    return new RCKAnimation(Context, nullptr);
}
