#include "RCKCurvePoint.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCKCurve.h"

/**
 * @brief RCKCurvePoint constructor
 * @param Context The CKContext instance
 * @param name Optional name for the curve point
 */
RCKCurvePoint::RCKCurvePoint(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_Curve(nullptr),
      m_Tension(0.0f),
      m_Continuity(0.0f),
      m_Bias(0.0f),
      m_Length(0.0f),
      m_UseTCB(FALSE),
      m_Linear(FALSE) {
    // Initialize vectors
    m_ReservedVector = VxVector(0.0f, 0.0f, 0.0f);
    m_TangentIn = VxVector(0.0f, 0.0f, 0.0f);
    m_m_TangentOut = VxVector(0.0f, 0.0f, 0.0f);
    m_NotUsedVector = VxVector(0.0f, 0.0f, 0.0f);
}

/**
 * @brief RCKCurvePoint destructor
 */
RCKCurvePoint::~RCKCurvePoint() {
    // Cleanup resources if needed
}

/**
 * @brief Get the class ID for RCKCurvePoint
 * @return The class ID (36)
 */
CK_CLASSID RCKCurvePoint::GetClassID() {
    return 36;
}

/**
 * @brief Check if curve point is hidden by parent
 * @return TRUE if hidden by parent, FALSE otherwise
 */
CKBOOL RCKCurvePoint::IsHiddenByParent() {
    // Implementation based on parent visibility
    return FALSE;
}

/**
 * @brief Check if curve point is visible
 * @return TRUE if visible, FALSE otherwise
 */
CKBOOL RCKCurvePoint::IsVisible() {
    // Implementation based on curve point visibility flags
    return TRUE;
}

/**
 * @brief Save the curve point data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKCurvePoint::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // If no file and no special flags, return base chunk only
    if (!file && (flags & 0xFFC00000) == 0)
        return baseChunk;

    // Create a new state chunk for curve point data
    CKStateChunk *chunk = CreateCKStateChunk(36, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write curve point specific data
    chunk->WriteIdentifier(0x10000000u); // Curve point identifier

    // Write curve point properties
    chunk->WriteObject(reinterpret_cast<CKObject *>(m_Curve));
    chunk->WriteInt(m_UseTCB);
    chunk->WriteInt(m_Linear);
    chunk->WriteFloat(m_Tension);
    chunk->WriteFloat(m_Continuity);
    chunk->WriteFloat(m_Bias);
    chunk->WriteVector(&m_TangentIn);
    chunk->WriteVector(&m_m_TangentOut);

    // Close or update the chunk based on class ID
    if (GetClassID() == 36)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Load curve point data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurvePoint::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    // Handle different data versions
    if (chunk->GetDataVersion() < 5) {
        // Legacy format handling for data version < 5
        if (chunk->SeekIdentifier(0x10000000u)) {
            // Load basic curve point properties
            m_Curve = reinterpret_cast<CKCurve *>(chunk->ReadObject(m_Context));
            m_UseTCB = chunk->ReadInt();
            m_Linear = chunk->ReadInt();

            // Load position data (legacy format)
            VxVector position;
            position.x = chunk->ReadFloat();
            position.y = chunk->ReadFloat();
            position.z = chunk->ReadFloat();
            SetPosition(&position, nullptr, FALSE);
        }

        // Load TCB parameters (legacy format)
        if (chunk->SeekIdentifier(0x20000000u)) {
            m_Tension = chunk->ReadFloat();
            m_Continuity = chunk->ReadFloat();
            m_Bias = chunk->ReadFloat();
        }

        // Load reserved vector (legacy format)
        if (chunk->SeekIdentifier(0x80000000)) {
            m_ReservedVector.x = chunk->ReadFloat();
            m_ReservedVector.y = chunk->ReadFloat();
            m_ReservedVector.z = chunk->ReadFloat();
        }

        // Load tangent vectors (legacy format)
        if (chunk->SeekIdentifier(0x40000000u)) {
            m_TangentIn.x = chunk->ReadFloat();
            m_TangentIn.y = chunk->ReadFloat();
            m_TangentIn.z = chunk->ReadFloat();
            m_m_TangentOut.x = chunk->ReadFloat();
            m_m_TangentOut.y = chunk->ReadFloat();
            m_m_TangentOut.z = chunk->ReadFloat();
        }
    } else {
        // Modern format for data version >= 5
        if (chunk->SeekIdentifier(0x10000000u)) {
            // Load all curve point properties in modern format
            m_Curve = reinterpret_cast<CKCurve *>(chunk->ReadObject(m_Context));
            m_UseTCB = chunk->ReadInt();
            m_Linear = chunk->ReadInt();
            m_Tension = chunk->ReadFloat();
            m_Continuity = chunk->ReadFloat();
            m_Bias = chunk->ReadFloat();
            chunk->ReadVector(&m_TangentIn);
            chunk->ReadVector(&m_m_TangentOut);
        }
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the curve point
 * @return Memory size in bytes
 */
int RCKCurvePoint::GetMemoryOccupation() {
    return sizeof(RCKCurvePoint);
}

/**
 * @brief Copy curve point data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurvePoint::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    RCK3dEntity::Copy(o, context);

    // Copy curve point specific data
    RCKCurvePoint &target = (RCKCurvePoint &) o;
    target.m_Curve = m_Curve;
    target.m_Tension = m_Tension;
    target.m_Continuity = m_Continuity;
    target.m_Bias = m_Bias;
    target.m_Length = m_Length;
    target.m_ReservedVector = m_ReservedVector;
    target.m_TangentIn = m_TangentIn;
    target.m_m_TangentOut = m_m_TangentOut;
    target.m_NotUsedVector = m_NotUsedVector;
    target.m_UseTCB = m_UseTCB;
    target.m_Linear = m_Linear;

    return CK_OK;
}

// Transformation methods implementation
void RCKCurvePoint::Rotate(const VxVector *Axis, float Angle, CK3dEntity *Ref, CKBOOL KeepChildren) {
    RCK3dEntity::Rotate(Axis, Angle, Ref, KeepChildren);
}

void RCKCurvePoint::Translate(const VxVector *Vect, CK3dEntity *Ref, CKBOOL KeepChildren) {
    RCK3dEntity::Translate(Vect, Ref, KeepChildren);
}

void RCKCurvePoint::AddScale(const VxVector *Scale, CKBOOL KeepChildren, CKBOOL Local) {
    RCK3dEntity::AddScale(Scale, KeepChildren, Local);
}

void RCKCurvePoint::SetPosition(const VxVector *Pos, CK3dEntity *Ref, CKBOOL KeepChildren) {
    RCK3dEntity::SetPosition(Pos, Ref, KeepChildren);
}

void RCKCurvePoint::SetOrientation(const VxVector *Dir, const VxVector *Up, const VxVector *Right, CK3dEntity *Ref,
                                   CKBOOL KeepChildren) {
    RCK3dEntity::SetOrientation(Dir, Up, Right, Ref, KeepChildren);
}

void RCKCurvePoint::SetQuaternion(const VxQuaternion *Quat, CK3dEntity *Ref, CKBOOL KeepChildren, CKBOOL KeepScale) {
    RCK3dEntity::SetQuaternion(Quat, Ref, KeepChildren, KeepScale);
}

void RCKCurvePoint::SetScale(const VxVector *Scale, CKBOOL KeepChildren, CKBOOL Local) {
    RCK3dEntity::SetScale(Scale, KeepChildren, Local);
}

CKBOOL RCKCurvePoint::ConstructWorldMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) {
    return RCK3dEntity::ConstructWorldMatrix(Pos, Scale, Quat);
}

CKBOOL RCKCurvePoint::ConstructWorldMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat,
                                             const VxQuaternion *Shear, float Sign) {
    return RCK3dEntity::ConstructWorldMatrixEx(Pos, Scale, Quat, Shear, Sign);
}

CKBOOL RCKCurvePoint::ConstructLocalMatrix(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat) {
    return RCK3dEntity::ConstructLocalMatrix(Pos, Scale, Quat);
}

CKBOOL RCKCurvePoint::ConstructLocalMatrixEx(const VxVector *Pos, const VxVector *Scale, const VxQuaternion *Quat,
                                             const VxQuaternion *Shear, float Sign) {
    return RCK3dEntity::ConstructLocalMatrixEx(Pos, Scale, Quat, Shear, Sign);
}

void RCKCurvePoint::SetLocalMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    RCK3dEntity::SetLocalMatrix(Mat, KeepChildren);
}

void RCKCurvePoint::SetWorldMatrix(const VxMatrix &Mat, CKBOOL KeepChildren) {
    RCK3dEntity::SetWorldMatrix(Mat, KeepChildren);
}

// =====================================================
// CKCurvePoint-specific virtual methods
// =====================================================

CKCurve *RCKCurvePoint::GetCurve() {
    return m_Curve;
}

float RCKCurvePoint::GetBias() {
    return m_Bias;
}

void RCKCurvePoint::SetBias(float bias) {
    m_Bias = bias;
    NotifyUpdate();
}

float RCKCurvePoint::GetTension() {
    return m_Tension;
}

void RCKCurvePoint::SetTension(float tension) {
    m_Tension = tension;
    NotifyUpdate();
}

float RCKCurvePoint::GetContinuity() {
    return m_Continuity;
}

void RCKCurvePoint::SetContinuity(float continuity) {
    m_Continuity = continuity;
    NotifyUpdate();
}

CKBOOL RCKCurvePoint::IsLinear() {
    return m_Linear;
}

void RCKCurvePoint::SetLinear(CKBOOL linear) {
    m_Linear = linear;
    NotifyUpdate();
}

void RCKCurvePoint::UseTCB(CKBOOL use) {
    m_UseTCB = use;
    NotifyUpdate();
}

CKBOOL RCKCurvePoint::IsTCB() {
    return m_UseTCB;
}

float RCKCurvePoint::GetLength() {
    return m_Length;
}

void RCKCurvePoint::GetTangents(VxVector *InTangent, VxVector *OutTangent) {
    if (InTangent) *InTangent = m_TangentIn;
    if (OutTangent) *OutTangent = m_m_TangentOut;
}

void RCKCurvePoint::SetTangents(VxVector *InTangent, VxVector *OutTangent) {
    if (InTangent) m_TangentIn = *InTangent;
    if (OutTangent) m_m_TangentOut = *OutTangent;
    NotifyUpdate();
}

void RCKCurvePoint::NotifyUpdate() {
    // Notify parent curve that this point has changed
    if (m_Curve) {
        // The curve needs to recalculate based on updated control point
        // Specific implementation depends on curve's update mechanism
    }
}

// Static class registration methods
CKSTRING RCKCurvePoint::GetClassName() {
    return "CurvePoint";
}

int RCKCurvePoint::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKCurvePoint::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKCurvePoint::Register() {
    // Based on IDA decompilation
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_CURVEPOINT);
}

CKCurvePoint *RCKCurvePoint::CreateInstance(CKContext *Context) {
    return reinterpret_cast<CKCurvePoint *>(new RCKCurvePoint(Context));
}

// Static class ID
CK_CLASSID RCKCurvePoint::m_ClassID = CKCID_CURVEPOINT;
