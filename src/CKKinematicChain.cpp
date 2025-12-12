/**
 * @file CKKinematicChain.cpp
 * @brief Implementation of CKKinematicChain class for inverse kinematics.
 *
 * Reverse engineered from CK2_3D.dll
 */

#include "RCKKinematicChain.h"
#include "CKStateChunk.h"
#include "CKFile.h"

// Static class ID
CK_CLASSID RCKKinematicChain::m_ClassID = CKCID_KINEMATICCHAIN;

//=============================================================================
// Class Registration
//=============================================================================

RCKKinematicChain::RCKKinematicChain(CKContext *Context, CKSTRING name)
    : CKKinematicChain(Context, name), m_StartEffector(nullptr), m_EndEffector(nullptr) {
}

CKSTRING RCKKinematicChain::GetClassName() {
    return (CKSTRING) "Kinematic Chain";
}

int RCKKinematicChain::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKKinematicChain::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKKinematicChain::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_BODYPART);
    CKClassNeedNotificationFrom(m_ClassID, CKCID_CHARACTER);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_KINEMATICCHAIN);
    CKClassRegisterDefaultOptions(m_ClassID, CK_GENERALOPTIONS_NODUPLICATENAMECHECK);
}

CKKinematicChain *RCKKinematicChain::CreateInstance(CKContext *Context) {
    // CKKinematicChain is abstract, but we need an instance for serialization
    return reinterpret_cast<CKKinematicChain *>(new RCKKinematicChain(Context));
}

//=============================================================================
// Save/Load Implementation
//=============================================================================

/*************************************************
Summary: Save method for CKKinematicChain.
Purpose: Saves kinematic chain data to a state chunk.
Remarks:
- Creates CKStateChunk with CKCID_KINEMATICCHAIN (13)
- Saves base object data via CKObject::Save
- Saves effector objects under identifier 0xFF:
  - First writes NULL placeholder (for compatibility)
  - Start effector (m_StartEffector at offset +20)
  - End effector (m_EndEffector at offset +24)

Implementation based on IDA decompilation at 0x10055E51:
- Creates chunk with CreateCKStateChunk(13, file)
- Calls CKObject::Save() for base data
- StartWrite(), AddChunkAndDelete() to merge base chunk
- If file or flags: writes identifier 0xFF with 3 objects
- CloseChunk() if classID==13, else UpdateDataSize()

Arguments:
- file: The file context for saving
- flags: Save flags controlling behavior
Return Value:
- CKStateChunk*: The created state chunk containing chain data
*************************************************/
CKStateChunk *RCKKinematicChain::Save(CKFile *file, CKDWORD flags) {
    // Create state chunk for kinematic chain (CKCID_KINEMATICCHAIN = 13)
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_KINEMATICCHAIN, file);
    if (!chunk)
        return nullptr;

    // Get base class chunk
    CKStateChunk *baseChunk = CKObject::Save(file, flags);

    // Start writing and merge base chunk
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Save effector data if saving to file or flags are set
    // Based on decompilation: if (file || (BYTE)flags)
    if (file || (flags & 0xFF)) {
        chunk->WriteIdentifier(0xFF);

        // Write NULL placeholder (for compatibility/reserved)
        chunk->WriteObject(nullptr);

        // Write start effector (offset +20 = m_StartEffector)
        chunk->WriteObject(m_StartEffector);

        // Write end effector (offset +24 = m_EndEffector)
        chunk->WriteObject(m_EndEffector);
    }

    // Close chunk appropriately based on class type
    if (GetClassID() == CKCID_KINEMATICCHAIN)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/*************************************************
Summary: Load method for CKKinematicChain.
Purpose: Loads kinematic chain data from a state chunk.
Remarks:
- Loads base object data via CKObject::Load
- Reads effector objects under identifier 0xFF:
  - Reads and discards NULL placeholder
  - Start effector into m_StartEffector (offset +20)
  - End effector into m_EndEffector (offset +24)

Implementation based on IDA decompilation at 0x10055F05:
- Returns -1 if chunk is NULL
- Calls CKObject::Load() first
- If SeekIdentifier(0xFF):
  - ReadObject() and discard (placeholder)
  - ReadObject() into offset +20 (m_StartEffector)
  - ReadObject() into offset +24 (m_EndEffector)
- Returns 0 on success

Arguments:
- chunk: The state chunk containing chain data
- file: The file context for loading (may be NULL)
Return Value:
- CKERROR: CK_OK (0) for success, -1 for invalid chunk
*************************************************/
CKERROR RCKKinematicChain::Load(CKStateChunk *chunk, CKFile *file) {
    // Validate chunk
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Load base object data
    CKERROR err = CKObject::Load(chunk, file);
    if (err != CK_OK)
        return err;

    // Load effector data if present
    if (chunk->SeekIdentifier(0xFF)) {
        // Read and discard NULL placeholder (for compatibility)
        chunk->ReadObject(m_Context);

        // Read start effector (offset +20)
        m_StartEffector = (CKBodyPart *) chunk->ReadObject(m_Context);

        // Read end effector (offset +24)
        m_EndEffector = (CKBodyPart *) chunk->ReadObject(m_Context);
    }

    return CK_OK;
}

//=============================================================================
// Base class overrides (stubs)
//=============================================================================

CK_CLASSID RCKKinematicChain::GetClassID() {
    return m_ClassID;
}

float RCKKinematicChain::GetChainLength(CKBodyPart *End) {
    (void) End;
    return 0.0f;
}

int RCKKinematicChain::GetChainBodyCount(CKBodyPart *End) {
    (void) End;
    return 0;
}

CKBodyPart *RCKKinematicChain::GetStartEffector() {
    return m_StartEffector;
}

CKERROR RCKKinematicChain::SetStartEffector(CKBodyPart *start) {
    m_StartEffector = start;
    return CK_OK;
}

CKBodyPart *RCKKinematicChain::GetEffector(int pos) {
    // Only start/end known for now
    if (pos == 0) {
        return m_StartEffector;
    }
    return (pos == 1) ? m_EndEffector : nullptr;
}

CKBodyPart *RCKKinematicChain::GetEndEffector() {
    return m_EndEffector;
}

CKERROR RCKKinematicChain::SetEndEffector(CKBodyPart *end) {
    m_EndEffector = end;
    return CK_OK;
}

CKERROR RCKKinematicChain::IKSetEffectorPos(VxVector *pos, CK3dEntity *ref, CKBodyPart *body) {
    (void) pos;
    (void) ref;
    (void) body;
    return CKERR_NOTIMPLEMENTED;
}

int RCKKinematicChain::GetMemoryOccupation() {
    return sizeof(RCKKinematicChain);
}

CKERROR RCKKinematicChain::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::RemapDependencies(context);
    if (err != CK_OK)
        return err;
    m_StartEffector = (CKBodyPart *)context.Remap(m_StartEffector);
    m_EndEffector = (CKBodyPart *)context.Remap(m_EndEffector);
    return CK_OK;
}

CKERROR RCKKinematicChain::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKObject::Copy(o, context);
    if (err != CK_OK)
        return err;

    auto &target = static_cast<RCKKinematicChain &>(o);
    target.m_StartEffector = m_StartEffector;
    target.m_EndEffector = m_EndEffector;
    return CK_OK;
}
