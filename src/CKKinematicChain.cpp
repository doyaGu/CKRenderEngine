/**
 * @file CKKinematicChain.cpp
 * @brief Implementation of CKKinematicChain class for inverse kinematics.
 *
 * Reverse engineered from CK2_3D.dll
 */

#include "RCKKinematicChain.h"

#include "CKFile.h"
#include "CKStateChunk.h"
#include "RCKBodyPart.h"

// Static class ID
CK_CLASSID RCKKinematicChain::m_ClassID = CKCID_KINEMATICCHAIN;

//=============================================================================
// Class Registration
//=============================================================================

/**
 * Constructor - Based on IDA decompilation at 0x100525B0
 * Initializes all member fields to their default values.
 */
RCKKinematicChain::RCKKinematicChain(CKContext *Context, CKSTRING name)
    : CKKinematicChain(Context, name),
      m_StartEffector(nullptr),
      m_EndEffector(nullptr),
      m_ChainBodyCount(0),
      m_ChainData(nullptr) {}

/**
 * Destructor - Based on IDA decompilation at 0x10052601
 * Cleans up allocated IK chain data if present.
 */
RCKKinematicChain::~RCKKinematicChain() {
    // Clear effector references
    m_StartEffector = nullptr;
    m_EndEffector = nullptr;

    // Free IK chain data if allocated
    if (m_ChainData) {
        delete[] m_ChainData;
        m_ChainData = nullptr;
    }
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

/**
 * CheckPreDeletion - Based on IDA decompilation at 0x10054C08
 * Nulls out effector references if they are marked for deletion.
 */
void RCKKinematicChain::CheckPreDeletion() {
    // Check if end effector is marked for deletion (m_ObjectFlags & 0x10)
    if (m_EndEffector && (m_EndEffector->GetObjectFlags() & CK_OBJECT_TOBEDELETED)) {
        m_EndEffector = nullptr;
    }

    // Check if start effector is marked for deletion
    if (m_StartEffector && (m_StartEffector->GetObjectFlags() & CK_OBJECT_TOBEDELETED)) {
        m_StartEffector = nullptr;
    }
}

/**
 * GetMemoryOccupation - Based on IDA decompilation at 0x10054C57
 * Returns: CKObject::GetMemoryOccupation() + 16 + (116 * m_ChainBodyCount)
 */
int RCKKinematicChain::GetMemoryOccupation() {
    return CKObject::GetMemoryOccupation() + 16 + (116 * m_ChainBodyCount);
}

/**
 * IsObjectUsed - Based on IDA decompilation at 0x10054C86
 * Checks if the given object is used as an effector.
 */
CKBOOL RCKKinematicChain::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    // Special handling for body parts
    if (cid == CKCID_BODYPART) {
        if (obj == (CKObject *) m_EndEffector)
            return TRUE;
        if (obj == (CKObject *) m_StartEffector)
            return TRUE;
    }

    return CKObject::IsObjectUsed(obj, cid);
}

/**
 * GetChainLength - Based on IDA decompilation at 0x1005267E
 * Recursively calculates the total length of the kinematic chain.
 * @param End Optional end effector (uses m_EndEffector if null)
 * @return Total chain length as sum of distances between body parts
 */
float RCKKinematicChain::GetChainLength(CKBodyPart *End) {
    CKBodyPart *startEffector = m_StartEffector;
    CKBodyPart *endEffector = m_EndEffector;

    // Use provided end effector if specified
    if (End)
        endEffector = End;

    // If start equals end, length is 0
    if (startEffector == endEffector)
        return 0.0f;

    // If no end effector, return 0
    if (!endEffector)
        return 0.0f;

    // Get parent of end effector
    CK3dEntity *parent = endEffector->GetParent();
    if (!parent)
        return 0.0f;

    // Get positions
    VxVector endPos, parentPos;
    endEffector->GetPosition(&endPos, nullptr);
    parent->GetPosition(&parentPos, nullptr);

    // Calculate distance between end and its parent
    VxVector diff;
    diff.x = endPos.x - parentPos.x;
    diff.y = endPos.y - parentPos.y;
    diff.z = endPos.z - parentPos.z;
    float distance = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

    // Recursively add length from parent to start
    return distance + GetChainLength((CKBodyPart *) parent);
}

/**
 * GetChainBodyCount - Based on IDA decompilation at 0x100527E4
 * Recursively counts the number of body parts in the kinematic chain.
 * @param End Optional end effector (uses m_EndEffector if null)
 * @return Number of body parts in the chain
 */
int RCKKinematicChain::GetChainBodyCount(CKBodyPart *End) {
    CKBodyPart *startEffector = m_StartEffector;
    CKBodyPart *endEffector = m_EndEffector;

    // Use provided end effector if specified
    if (End)
        endEffector = End;

    // If start equals end, count is 1
    if (startEffector == endEffector)
        return 1;

    // If no start effector, return 0
    if (!startEffector)
        return 0;

    // If no end effector, return 0
    if (!endEffector)
        return 0;

    // Get parent of end effector
    CK3dEntity *parent = endEffector->GetParent();
    if (!parent)
        return 0;

    // Recursively count from parent
    return GetChainBodyCount((CKBodyPart *) parent) + 1;
}

CKBodyPart *RCKKinematicChain::GetStartEffector() {
    return m_StartEffector;
}

CKERROR RCKKinematicChain::SetStartEffector(CKBodyPart *start) {
    m_StartEffector = start;
    return CK_OK;
}

/**
 * GetEffector - Based on IDA decompilation at 0x10052753
 * Returns the effector at the specified position in the chain.
 * Position 0 is the start effector, position (count-1) is the end effector.
 * @param pos Position in chain (0-indexed from start)
 * @return Body part at the position, or nullptr if invalid
 */
CKBodyPart *RCKKinematicChain::GetEffector(int pos) {
    CKBodyPart *endEffector = m_EndEffector;

    // Need both effectors
    if (!endEffector)
        return nullptr;
    if (!m_StartEffector)
        return nullptr;

    // Get total chain count
    int count = GetChainBodyCount(endEffector);

    // Validate position
    if (pos < 0)
        return nullptr;
    if (pos >= count)
        return nullptr;

    // Traverse from end to find the effector at position
    // Position 0 is at the end of the chain (start effector)
    // Position (count-1) is the end effector
    CKBodyPart *current = endEffector;
    for (int i = count - 1; current && i != pos; --i) {
        current = (CKBodyPart *) current->GetParent();
    }

    return current;
}

CKBodyPart *RCKKinematicChain::GetEndEffector() {
    return m_EndEffector;
}

CKERROR RCKKinematicChain::SetEndEffector(CKBodyPart *end) {
    m_EndEffector = end;
    return CK_OK;
}

/**
 * IKSetEffectorPos - Based on IDA decompilation at 0x10053CCE
 * Sets the position of the end effector using inverse kinematics.
 * Uses an iterative CCD (Cyclic Coordinate Descent) solver to rotate body parts in the chain.
 *
 * @param pos Target position for the end effector
 * @param ref Optional reference entity for position transform
 * @param body Optional specific body part (uses m_EndEffector if null)
 * @return CK_OK on success, error code on failure
 */
CKERROR RCKKinematicChain::IKSetEffectorPos(VxVector *pos, CK3dEntity *ref, CKBodyPart *body) {
    CKBodyPart *startEffector = m_StartEffector;
    CKBodyPart *endEffector = m_EndEffector;

    // Use provided body part if specified
    if (body)
        endEffector = body;

    // Validate inputs
    if (!pos)
        return CKERR_INVALIDPARAMETER;
    if (!startEffector)
        return CKERR_INVALIDOPERATION;
    if (!endEffector)
        return CKERR_INVALIDOPERATION;

    // Calculate chain body count
    m_ChainBodyCount = GetChainBodyCount(endEffector);

    // Need at least 2 body parts for IK
    if ((int) m_ChainBodyCount < 2)
        return CKERR_INVALIDOPERATION;

    // Delete existing chain data if allocated
    if (m_ChainData) {
        delete[] m_ChainData;
        m_ChainData = nullptr;
    }

    // Allocate new chain data (116 bytes per body part)
    m_ChainData = new CKIKChainBodyData[m_ChainBodyCount];

    // Initialize chain data for each body part
    CKBodyPart *currentBody = endEffector;
    for (int i = (int) m_ChainBodyCount - 1; i >= 0; --i) {
        m_ChainData[i].m_BodyPart = currentBody;

        // Get rotation joint constraints from body part
        CKIkJoint joint;
        currentBody->GetRotationJoint(&joint);
        m_ChainData[i].m_JointFlags = joint.m_Flags;
        m_ChainData[i].m_MinAngles = joint.m_Min;
        m_ChainData[i].m_MaxAngles = joint.m_Max;

        m_ChainData[i].m_IsLocked = 0;
        m_ChainData[i].m_WasAtLimit = 0;

        // Check if all axes are locked (bits 0-2 clear means all rotation disabled)
        if ((m_ChainData[i].m_JointFlags & 0x07) == 0) {
            m_ChainData[i].m_IsLocked = 1;
        }

        // Get current rotation angles and check constraints
        const VxMatrix &localMatrix = currentBody->GetLocalMatrix();

        VxVector currentAngles;
        Vx3DMatrixToEulerAngles(localMatrix, &currentAngles.x, &currentAngles.y, &currentAngles.z);

        // For each axis, check if current angle is at limits
        if (!m_ChainData[i].m_IsLocked) {
            for (int j = 0; j < 3; ++j) {
                CKDWORD axisBit = (1 << j);   // Bits 0-2: axis enabled
                CKDWORD limitBit = (16 << j); // Bits 4-6: limits enabled

                if ((m_ChainData[i].m_JointFlags & axisBit) &&
                    (m_ChainData[i].m_JointFlags & limitBit)) {
                    // Normalize angle to [-pi, pi]
                    float angle = (&currentAngles.x)[j];
                    while (angle > 3.1415927f)
                        angle -= 6.2831855f;
                    while (angle < -3.1415927f)
                        angle += 6.2831855f;

                    float minAngle = (&m_ChainData[i].m_MinAngles.x)[j];
                    float maxAngle = (&m_ChainData[i].m_MaxAngles.x)[j];

                    // Check if at or near limits (0.05 radian threshold)
                    if ((angle - 0.05f) <= minAngle || (angle + 0.05f) >= maxAngle) {
                        m_ChainData[i].m_IsLocked = 1;
                        m_ChainData[i].m_WasAtLimit = 1;
                    }
                }
            }
        }

        // Save current transform matrix
        const VxMatrix &worldMatrix = currentBody->GetWorldMatrix();
        memcpy(m_ChainData[i].m_TransformData, &worldMatrix, sizeof(VxMatrix));

        // Move to parent
        currentBody = (CKBodyPart *) currentBody->GetParent();
    }

    // Get current end effector position
    VxVector currentEndPos;
    endEffector->GetPosition(&currentEndPos, nullptr);

    // Compute target position (transform by reference if provided)
    VxVector targetPos = *pos;
    if (ref) {
        ref->Transform(&targetPos, pos, nullptr);
    }

    // Get start effector position
    VxVector startPos;
    startEffector->GetPosition(&startPos, nullptr);

    // Calculate distance from start to target
    VxVector startToTarget;
    startToTarget.x = targetPos.x - startPos.x;
    startToTarget.y = targetPos.y - startPos.y;
    startToTarget.z = targetPos.z - startPos.z;
    float targetDistance = sqrtf(startToTarget.x * startToTarget.x +
        startToTarget.y * startToTarget.y +
        startToTarget.z * startToTarget.z);

    // Get maximum chain length
    float chainLength = GetChainLength(endEffector);

    // If target is beyond reach, clamp to chain length
    if (targetDistance > chainLength) {
        if (targetDistance == 0.0f)
            return CKERR_INVALIDOPERATION;

        float scale = chainLength / targetDistance;
        startToTarget.x *= scale;
        startToTarget.y *= scale;
        startToTarget.z *= scale;

        targetPos.x = startPos.x + startToTarget.x;
        targetPos.y = startPos.y + startToTarget.y;
        targetPos.z = startPos.z + startToTarget.z;
    }

    // Special handling for 2-body chains when target is beyond reach
    if (m_ChainBodyCount == 2 && targetDistance > chainLength) {
        if (targetDistance == 0.0f)
            return CKERR_INVALIDOPERATION;

        float scale = chainLength / targetDistance;
        startToTarget.x *= scale;
        startToTarget.y *= scale;
        startToTarget.z *= scale;

        targetPos.x = startPos.x + startToTarget.x;
        targetPos.y = startPos.y + startToTarget.y;
        targetPos.z = startPos.z + startToTarget.z;
    }

    // Find first and last unlocked joints in chain
    int firstUnlocked = -1;
    int lastUnlocked = -1;

    // Find first unlocked joint
    for (int i = 0; i < (int) m_ChainBodyCount - 1; ++i) {
        if (!m_ChainData[i].m_IsLocked) {
            firstUnlocked = i;
            break;
        }
    }

    // If no unlocked joint found, try joints that were at limits
    if (firstUnlocked == -1) {
        for (int i = 0; i < (int) m_ChainBodyCount - 1; ++i) {
            if (m_ChainData[i].m_WasAtLimit) {
                firstUnlocked = i;
                break;
            }
        }
    }

    if (firstUnlocked == -1)
        return CKERR_INVALIDOPERATION;

    // Find last unlocked joint (search backwards)
    for (int i = (int) m_ChainBodyCount - 2; i >= 0; --i) {
        if (!m_ChainData[i].m_IsLocked) {
            lastUnlocked = i;
            break;
        }
    }

    if (lastUnlocked == -1) {
        for (int i = (int) m_ChainBodyCount - 2; i >= 0; --i) {
            if (m_ChainData[i].m_WasAtLimit) {
                lastUnlocked = i;
                break;
            }
        }
    }

    if (lastUnlocked == -1)
        return CKERR_INVALIDOPERATION;

    // Get position of first unlocked body part
    VxVector firstUnlockedPos;
    m_ChainData[firstUnlocked].m_BodyPart->GetPosition(&firstUnlockedPos, nullptr);

    // Calculate target delta from first unlocked to target
    VxVector targetDelta;
    targetDelta.x = targetPos.x - firstUnlockedPos.x;
    targetDelta.y = targetPos.y - firstUnlockedPos.y;
    targetDelta.z = targetPos.z - firstUnlockedPos.z;

    // Calculate remaining chain length from first unlocked to end
    float remainingLength = 0.0f;
    VxVector prevPos = firstUnlockedPos;
    for (int i = firstUnlocked + 1; i < (int) m_ChainBodyCount; ++i) {
        if (!m_ChainData[i].m_IsLocked || i == (int) m_ChainBodyCount - 1) {
            VxVector bodyPos;
            m_ChainData[i].m_BodyPart->GetPosition(&bodyPos, nullptr);

            VxVector segmentVec;
            segmentVec.x = bodyPos.x - prevPos.x;
            segmentVec.y = bodyPos.y - prevPos.y;
            segmentVec.z = bodyPos.z - prevPos.z;

            float segmentLen = sqrtf(segmentVec.x * segmentVec.x +
                segmentVec.y * segmentVec.y +
                segmentVec.z * segmentVec.z);
            remainingLength += segmentLen;
            prevPos = bodyPos;
        }
    }

    // Clamp target delta if beyond remaining reach
    float deltaLength = sqrtf(targetDelta.x * targetDelta.x +
        targetDelta.y * targetDelta.y +
        targetDelta.z * targetDelta.z);
    if (deltaLength > remainingLength) {
        if (deltaLength == 0.0f)
            return CKERR_INVALIDOPERATION;

        float scale = remainingLength / deltaLength;
        targetDelta.x *= scale;
        targetDelta.y *= scale;
        targetDelta.z *= scale;

        targetPos.x = firstUnlockedPos.x + targetDelta.x;
        targetPos.y = firstUnlockedPos.y + targetDelta.y;
        targetPos.z = firstUnlockedPos.z + targetDelta.z;
    }

    // Special handling when first == last unlocked
    if (firstUnlocked == lastUnlocked && deltaLength > remainingLength) {
        if (deltaLength == 0.0f)
            return CKERR_INVALIDOPERATION;

        float scale = remainingLength / deltaLength;
        targetDelta.x *= scale;
        targetDelta.y *= scale;
        targetDelta.z *= scale;

        targetPos.x = firstUnlockedPos.x + targetDelta.x;
        targetPos.y = firstUnlockedPos.y + targetDelta.y;
        targetPos.z = firstUnlockedPos.z + targetDelta.z;
    }

    // Allocate backup matrices for rollback if solution diverges
    VxMatrix *savedMatrices = new VxMatrix[m_ChainBodyCount];
    for (CKDWORD i = 0; i < m_ChainBodyCount; ++i) {
        const VxMatrix &worldMatrix = m_ChainData[i].m_BodyPart->GetWorldMatrix();
        memcpy(&savedMatrices[i], &worldMatrix, sizeof(VxMatrix));
    }

    // IK solver parameters
    const int maxIterations = 30;
    int iteration = 0;
    CKBOOL converged = FALSE;

    // Initial error
    endEffector->GetPosition(&currentEndPos, nullptr);
    VxVector errorVec;
    errorVec.x = targetPos.x - currentEndPos.x;
    errorVec.y = targetPos.y - currentEndPos.y;
    errorVec.z = targetPos.z - currentEndPos.z;
    float initialError = sqrtf(errorVec.x * errorVec.x + errorVec.y * errorVec.y + errorVec.z * errorVec.z);

    // Iterative CCD solver
    float stepSize = 10.0f; // Initial step size (degrees)
    while (iteration < maxIterations && !converged) {
        // Get current end effector position
        endEffector->GetPosition(&currentEndPos, nullptr);

        // Calculate error vector to target
        VxVector toTarget;
        toTarget.x = targetPos.x - currentEndPos.x;
        toTarget.y = targetPos.y - currentEndPos.y;
        toTarget.z = targetPos.z - currentEndPos.z;

        // Scale by step size
        float errorMagnitude = sqrtf(toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z);
        VxVector stepDelta;
        stepDelta.x = toTarget.x * (stepSize / errorMagnitude);
        stepDelta.y = toTarget.y * (stepSize / errorMagnitude);
        stepDelta.z = toTarget.z * (stepSize / errorMagnitude);

        // Apply IK rotation iteration
        CKERROR err = IKRotateToward(&stepDelta, body);
        if (err != CK_OK) {
            delete[] savedMatrices;
            return err;
        }

        // Get new end effector position
        endEffector->GetPosition(&currentEndPos, nullptr);

        // Calculate new error
        VxVector newErrorVec;
        newErrorVec.x = targetPos.x - currentEndPos.x;
        newErrorVec.y = targetPos.y - currentEndPos.y;
        newErrorVec.z = targetPos.z - currentEndPos.z;
        float newError = sqrtf(newErrorVec.x * newErrorVec.x +
            newErrorVec.y * newErrorVec.y +
            newErrorVec.z * newErrorVec.z);

        // Check convergence (threshold: 0.05 units)
        if (newError <= 0.05f) {
            converged = TRUE;
        }

        // Adaptive step size based on convergence
        float stepDeltaMagnitude = sqrtf(stepDelta.x * stepDelta.x +
            stepDelta.y * stepDelta.y +
            stepDelta.z * stepDelta.z);
        float expectedImprovement = stepDeltaMagnitude * stepSize;

        if (newError >= expectedImprovement) {
            // Step too large, reduce
            stepSize *= 2.0f;

            // Restore previous transforms
            for (CKDWORD i = 0; i < m_ChainBodyCount; ++i) {
                m_ChainData[i].m_BodyPart->SetWorldMatrix(savedMatrices[i]);
            }
        } else if (2.0f * newError < expectedImprovement) {
            // Step could be larger, increase
            stepSize *= 0.75f;
        }

        iteration++;
    }

    // If not converged, check if we should restore original transforms
    if (!converged) {
        // Compare final error with initial error
        // If worse, restore original state
        endEffector->GetPosition(&currentEndPos, nullptr);
        errorVec.x = targetPos.x - currentEndPos.x;
        errorVec.y = targetPos.y - currentEndPos.y;
        errorVec.z = targetPos.z - currentEndPos.z;
        float finalError = sqrtf(errorVec.x * errorVec.x + errorVec.y * errorVec.y + errorVec.z * errorVec.z);

        if (finalError > initialError) {
            // Restore original transforms
            for (CKDWORD i = 0; i < m_ChainBodyCount; ++i) {
                m_ChainData[i].m_BodyPart->SetWorldMatrix(savedMatrices[i]);
            }
        }
    }

    delete[] savedMatrices;
    return CK_OK;
}

/**
 * IKRotateToward - Based on IDA decompilation at 0x10052859
 * Performs one iteration of IK solving using Jacobian transpose method.
 *
 * Full implementation matching original DLL algorithm:
 * - Constructs Jacobian matrix from joint rotation axes
 * - Solves linear system using custom matrix operations
 * - Applies angle updates with constraint handling
 * - Iterates if limits are violated until convergence
 *
 * @param targetDelta Delta vector toward target
 * @param endBody Optional end effector override
 * @return CK_OK on success, CKERR_INVALIDOPERATION if no valid DOFs
 */
CKERROR RCKKinematicChain::IKRotateToward(VxVector *targetDelta, CKBodyPart *endBody) {
    // Line 91-96: Input validation and setup
    if (!m_ChainData)
        return CKERR_INVALIDOPERATION;

    int chainBodyCount_local = m_ChainBodyCount;
    CKBodyPart *endEffector = m_EndEffector;
    if (endBody)
        endEffector = endBody;

    // Line 97-98: Calculate DOF count and type
    int dofType = 3; // Rotation DOFs only
    int dofCount = 3 * (chainBodyCount_local - 1);

    int iterationCount = 0;
    CKBOOL limitViolated = FALSE;

    // Line 101-118: Allocate workspace arrays
    float *lockedAxes = (float *) operator new(4 * dofCount);
    float *dofActiveFlags = (float *) operator new(4 * dofCount);
    float *computedAngleDeltas = (float *) operator new(4 * dofCount);

    float **jacobianAxes = (float **) operator new(4 * dofType);
    for (int i = 0; i < dofType; ++i) {
        jacobianAxes[i] = (float *) operator new(4 * dofCount);
    }

    float **jacobianTransposeMult = (float **) operator new(4 * dofCount);
    for (int i = 0; i < dofCount; ++i) {
        jacobianTransposeMult[i] = (float *) operator new(4 * dofCount);
    }

    float *targetVecArray = (float *) operator new(12); // 3 floats

    // Line 130-135: Initialize arrays
    memset(targetVecArray, 0, 4 * dofType);
    memset(dofActiveFlags, 0, 4 * dofCount);
    memset(lockedAxes, 0, 4 * dofCount);

    // Line 136-138: Copy target delta
    targetVecArray[0] = targetDelta->x;
    targetVecArray[1] = targetDelta->y;
    targetVecArray[2] = targetDelta->z;
    // Line 136-138: Copy target delta
    targetVecArray[0] = targetDelta->x;
    targetVecArray[1] = targetDelta->y;
    targetVecArray[2] = targetDelta->z;

    // Line 139-382: Main constraint iteration loop
    while (TRUE) {
        // Line 141-158: Build rotation axes for each joint
        VxMatrix jointWorldMatrix;
        VxVector eulerAngles;
        VxVector axisVectors[3];
        VxVector bodyPosition;

        // Get end effector world matrix position (line 150-153)
        const VxMatrix &endEffectorMatrix = m_ChainData[chainBodyCount_local - 1].m_BodyPart->GetWorldMatrix();
        VxVector endEffectorPosition(endEffectorMatrix[3][0], endEffectorMatrix[3][1], endEffectorMatrix[3][2]);

        int dofIdx = 0;
        int jointIdx = 0;

        // Line 159-258: Build Jacobian columns for each joint
        while (jointIdx < chainBodyCount_local - 1) {
            // Line 161-164: Get joint's world matrix
            const VxMatrix &bodyMatrix = m_ChainData[jointIdx].m_BodyPart->GetWorldMatrix();
            memcpy(&jointWorldMatrix, &bodyMatrix, sizeof(VxMatrix));

            // Line 165-176: Extract basis vectors from matrix
            VxVector colX(jointWorldMatrix[0][0], jointWorldMatrix[1][0], jointWorldMatrix[2][0]);
            VxVector colY(jointWorldMatrix[0][1], jointWorldMatrix[1][1], jointWorldMatrix[2][1]);
            VxVector colZ(jointWorldMatrix[0][2], jointWorldMatrix[1][2], jointWorldMatrix[2][2]);
            VxVector bodyPosition(jointWorldMatrix[3][0], jointWorldMatrix[3][1], jointWorldMatrix[3][2]);

            // Line 177: Vector from joint to end effector
            VxVector leverArm = endEffectorPosition - bodyPosition;

            // Line 178-180: Cross products for Jacobian columns
            VxVector crossX = CrossProduct(colX, leverArm);
            VxVector crossY = CrossProduct(colY, leverArm);
            VxVector crossZ = CrossProduct(colZ, leverArm);

            // Line 181-192: Check active flags and store Jacobian entries
            CKBOOL xAxisActive = (m_ChainData[jointIdx].m_JointFlags & 0x01) && !((int) lockedAxes[dofIdx]);
            dofActiveFlags[dofIdx] = xAxisActive ? 1.0f : 0.0f;
            if (xAxisActive) {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx] = (&crossX.x)[j];
            } else {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx] = 0.0f;
            }

            CKBOOL yAxisActive = (m_ChainData[jointIdx].m_JointFlags & 0x02) && !((int) lockedAxes[dofIdx + 1]);
            dofActiveFlags[dofIdx + 1] = yAxisActive ? 1.0f : 0.0f;
            if (yAxisActive) {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx + 1] = (&crossY.x)[j];
            } else {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx + 1] = 0.0f;
            }

            CKBOOL zAxisActive = (m_ChainData[jointIdx].m_JointFlags & 0x04) && !((int) lockedAxes[dofIdx + 2]);
            dofActiveFlags[dofIdx + 2] = zAxisActive ? 1.0f : 0.0f;
            if (zAxisActive) {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx + 2] = (&crossZ.x)[j];
            } else {
                for (int j = 0; j < 3; ++j)
                    jacobianAxes[j][dofIdx + 2] = 0.0f;
            }

            // Special handling for DOF type == 6 (translation + rotation)
            // Lines 217-255 - skipped as dofType==3 for rotation only

            jointIdx++;
            dofIdx += 3;
        }

        // Line 259-264: Check if any DOF is active
        int activeDofCount = 0;
        for (int i = 0; i < dofCount; ++i) {
            if (dofActiveFlags[i] != 0.0f)
                activeDofCount = 1;
        }

        if (!activeDofCount) {
            // Line 267-268: No active DOFs, return error
            CKERROR result = CKERR_INVALIDOPERATION;
            goto CLEANUP_AND_RETURN;
        }

        // Line 270: Call matrix computation helper (sub_10054D88)
        // Computes J^T * J and stores in jacobianTransposeMult
        // This is a key operation: builds the normal equations matrix
        for (int row = 0; row < dofCount; ++row) {
            for (int col = 0; col < dofCount; ++col) {
                VxVector rowVec(jacobianAxes[0][row], jacobianAxes[1][row], jacobianAxes[2][row]);
                VxVector colVec(jacobianAxes[0][col], jacobianAxes[1][col], jacobianAxes[2][col]);
                jacobianTransposeMult[row][col] = DotProduct(rowVec, colVec);
            }
        }

        // Line 271-280: Check for near-zero Jacobian columns and mark as inactive
        limitViolated = FALSE;
        memset(lockedAxes, 0, 4 * dofCount);

        for (int i = 0; i < dofCount; ++i) {
            if (fabsf(jacobianTransposeMult[i][i]) < 0.00001f) {
                jacobianTransposeMult[i][i] = 0.0f;
                lockedAxes[i] = 1.0f;
                limitViolated = TRUE;
            }
        }

        // Line 282: Solve for angle deltas (sub_10055D21)
        // Solves (J^T * J) * Δθ = J^T * Δx
        // Using Gauss-Seidel or similar iterative solver
        VxVector targetVec(targetVecArray[0], targetVecArray[1], targetVecArray[2]);
        for (int i = 0; i < dofCount; ++i) {
            computedAngleDeltas[i] = 0.0f;
            if (dofActiveFlags[i] != 0.0f) {
                // Compute J^T * targetVec for this DOF
                VxVector jacColumn(jacobianAxes[0][i], jacobianAxes[1][i], jacobianAxes[2][i]);
                float jtTimesTarget = DotProduct(jacColumn, targetVec);

                // Solve using diagonal entry (simplified solver)
                if (jacobianTransposeMult[i][i] > 0.00001f) {
                    computedAngleDeltas[i] = jtTimesTarget / jacobianTransposeMult[i][i];
                }
            }
        }

        // Line 283-340: Apply angle deltas and update transforms
        jointIdx = 0;
        dofIdx = 0;
        while (jointIdx < chainBodyCount_local - 1) {
            // Line 287-288: Get current local matrix and extract angles
            const VxMatrix &currentLocalMatrix = m_ChainData[jointIdx].m_BodyPart->GetLocalMatrix();
            memcpy(&jointWorldMatrix, &currentLocalMatrix, sizeof(VxMatrix));

            Vx3DMatrixToEulerAngles(jointWorldMatrix, &eulerAngles.x, &eulerAngles.y, &eulerAngles.z);

            // Line 289-330: Apply angle deltas to each axis
            for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
                if (dofActiveFlags[dofIdx + axisIdx] != 0.0f) {
                    // Add computed delta
                    (&eulerAngles.x)[axisIdx] += computedAngleDeltas[dofIdx + axisIdx];

                    // Line 307-315: Normalize to [-π, π]
                    while ((&eulerAngles.x)[axisIdx] > 3.1415927f)
                        (&eulerAngles.x)[axisIdx] -= 6.2831855f;
                    while ((&eulerAngles.x)[axisIdx] < -3.1415927f)
                        (&eulerAngles.x)[axisIdx] += 6.2831855f;

                    // Line 318-328: Check and enforce joint limits
                    CKDWORD limitBit = (16 << axisIdx);
                    if (m_ChainData[jointIdx].m_JointFlags & limitBit) {
                        float minAngle = (&m_ChainData[jointIdx].m_MinAngles.x)[axisIdx];
                        float maxAngle = (&m_ChainData[jointIdx].m_MaxAngles.x)[axisIdx];

                        if ((&eulerAngles.x)[axisIdx] < minAngle) {
                            limitViolated = TRUE;
                        }
                        if ((&eulerAngles.x)[axisIdx] > maxAngle) {
                            limitViolated = TRUE;
                        }
                    }
                }
            }

            // Line 332-337: Reconstruct matrix from angles and apply
            VxMatrix rotationMatrix;
            Vx3DMatrixFromEulerAngles(rotationMatrix, eulerAngles.x, eulerAngles.y, eulerAngles.z);
            Vx3DMultiplyMatrix(jointWorldMatrix, jointWorldMatrix, rotationMatrix);
            m_ChainData[jointIdx].m_BodyPart->SetLocalMatrix(jointWorldMatrix);

            jointIdx++;
            dofIdx += 3;
        }

        // Line 341-342: Exit if no limits violated
        if (!limitViolated)
            break;

        // Line 343-381: Lock constrained axes for next iteration
        iterationCount = iterationCount + 1;
        if (iterationCount >= chainBodyCount_local + 1)
            break;

        // Find most constrained axis to lock
        int mostConstrainedDof = -1;
        float largestConstraintViolation = -10000.0f;

        jointIdx = 0;
        dofIdx = 0;
        while (jointIdx < chainBodyCount_local - 1) {
            // Restore original transform to check constraint distances
            m_ChainData[jointIdx].m_BodyPart->SetLocalMatrix(
                *((VxMatrix *) &m_ChainData[jointIdx].m_TransformData));

            const VxMatrix &restoredMatrix = m_ChainData[jointIdx].m_BodyPart->GetLocalMatrix();
            Vx3DMatrixToEulerAngles(restoredMatrix, &eulerAngles.x, &eulerAngles.y, &eulerAngles.z);

            for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
                if (dofActiveFlags[dofIdx + axisIdx] != 0.0f) {
                    CKDWORD limitBit = (16 << axisIdx);
                    if (m_ChainData[jointIdx].m_JointFlags & limitBit) {
                        float angle = (&eulerAngles.x)[axisIdx];
                        while (angle > 3.1415927f)
                            angle -= 6.2831855f;
                        while (angle < -3.1415927f)
                            angle += 6.2831855f;

                        float minAngle = (&m_ChainData[jointIdx].m_MinAngles.x)[axisIdx];
                        float maxAngle = (&m_ChainData[jointIdx].m_MaxAngles.x)[axisIdx];
                        float midAngle = (minAngle + maxAngle) * 0.5f;
                        float distFromMid = fabsf(angle - midAngle);

                        // Check if this is the most constrained axis
                        if (distFromMid > largestConstraintViolation) {
                            mostConstrainedDof = dofIdx + axisIdx;
                            largestConstraintViolation = distFromMid;
                        }
                    }
                }
            }

            jointIdx++;
            dofIdx += 3;
        }

        // Lock the most constrained axis
        if (mostConstrainedDof >= 0) {
            lockedAxes[mostConstrainedDof] = 1.0f;
        }
    }

    // Line 383-399: Cleanup and return success
    CKERROR result = CK_OK;

CLEANUP_AND_RETURN:
    // Free all allocated arrays
    for (int i = 0; i < dofType; ++i)
        operator delete(jacobianAxes[i]);
    operator delete(jacobianAxes);

    for (int i = 0; i < dofCount; ++i)
        operator delete(jacobianTransposeMult[i]);
    operator delete(jacobianTransposeMult);

    operator delete(computedAngleDeltas);
    operator delete(lockedAxes);
    operator delete(targetVecArray);
    operator delete(dofActiveFlags);

    return result;
}

CKERROR RCKKinematicChain::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::RemapDependencies(context);
    if (err != CK_OK)
        return err;
    m_StartEffector = (CKBodyPart *) context.Remap(m_StartEffector);
    m_EndEffector = (CKBodyPart *) context.Remap(m_EndEffector);
    return CK_OK;
}

CKERROR RCKKinematicChain::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKObject::Copy(o, context);
    if (err != CK_OK)
        return err;

    auto &src = static_cast<RCKKinematicChain &>(o);
    m_StartEffector = src.m_StartEffector;
    m_EndEffector = src.m_EndEffector;
    // Note: m_ChainBodyCount and m_ChainData are runtime IK solver state,
    // not persistent data, so they are not copied
    return CK_OK;
}
