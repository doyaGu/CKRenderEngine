#include "RCKCurve.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCKCurvePoint.h"
#include "RCKMesh.h"

/**
 * @brief RCKCurve constructor
 * @param Context The CKContext instance
 * @param name Optional name for the curve
 */
RCKCurve::RCKCurve(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_Opened(FALSE),
      m_Length(0),
      m_StepCount(0),
      m_FittingCoeff(0.0f),
      m_Color(0),
      m_LoadingFlag(0) {
    // Initialize control points array
}

/**
 * @brief RCKCurve destructor
 */
RCKCurve::~RCKCurve() {
    // Cleanup resources if needed
}

/**
 * @brief Get the class ID for RCKCurve
 * @return The class ID (43)
 */
CK_CLASSID RCKCurve::GetClassID() {
    return m_ClassID;
}

/**
 * @brief Check if curve is hidden by parent
 * @return TRUE if hidden by parent, FALSE otherwise
 */
CKBOOL RCKCurve::IsHiddenByParent() {
    // Implementation based on parent visibility
    return FALSE;
}

/**
 * @brief Check if curve is visible
 * @return TRUE if visible, FALSE otherwise
 */
CKBOOL RCKCurve::IsVisible() {
    // Implementation based on curve visibility flags
    return TRUE;
}

/**
 * @brief Prepare curve for saving by handling dependencies
 * @param file The CKFile to save to
 * @param flags Save flags
 */
void RCKCurve::PreSave(CKFile *file, CKDWORD flags) {
    // Call base class PreSave first
    RCK3dEntity::PreSave(file, flags);

    CKDWORD pointCount = m_ControlPoints.Size();
    CKObject **points = pointCount ? reinterpret_cast<CKObject **>(&m_ControlPoints[0]) : nullptr;
    if (pointCount && points) {
        file->SaveObjects(points, pointCount, 0xFFFFFFFF);
    }
}

/**
 * @brief Save the curve data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKCurve::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // If no file and no special flags, return base chunk only
    if (!file && (flags & 0xFFC00000) == 0)
        return baseChunk;

    // Create a new state chunk for curve data
    CKStateChunk *chunk = CreateCKStateChunk(43, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write curve specific data
    chunk->WriteIdentifier(0xFFC00000); // Curve identifier

    // Save control points array
    m_ControlPoints.Save(chunk);

    // Write curve properties
    chunk->WriteFloat(m_FittingCoeff);
    chunk->WriteDword(m_StepCount);
    chunk->WriteDword(m_Opened);

    // If not saving to file, save control point sub-chunks
    if (!file) {
        chunk->WriteIdentifier(0xFF000000);
        CKDWORD pointCount = m_ControlPoints.Size();
        chunk->WriteDword(pointCount);

        for (CKDWORD i = 0; i < pointCount; ++i) {
            CKObject *point = reinterpret_cast<CKObject *>(m_ControlPoints[i]);
            CKStateChunk *pointChunk = point ? point->Save(nullptr, flags) : nullptr;
            chunk->WriteObject(point);
            chunk->WriteSubChunk(pointChunk);
            if (pointChunk) {
                DeleteCKStateChunk(pointChunk);
            }
        }
    }

    // Close or update the chunk based on class ID
    if (GetClassID() == CKCID_CURVE)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Load curve data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    m_LoadingFlag = 1;

    // Handle different data versions
    if (chunk->GetDataVersion() < 5) {
        // Legacy format handling for data version < 5
        if (chunk->SeekIdentifier(0x800000u)) {
            m_ControlPoints.Clear();
            m_ControlPoints.Load(m_Context, chunk);
        }

        // Load curve properties from legacy identifiers
        if (chunk->SeekIdentifier(0x400000u))
            m_FittingCoeff = chunk->ReadFloat();

        if (chunk->SeekIdentifier(0x1000000u))
            m_StepCount = chunk->ReadDword();

        if (chunk->SeekIdentifier(0x2000000u))
            m_Opened = chunk->ReadDword();

        // Load control point sub-chunks in legacy format
        if (chunk->SeekIdentifier(0xFF000000)) {
            int pointCount = chunk->ReadDword();
            for (int i = 0; i < pointCount; ++i) {
                CK_ID objectID = chunk->ReadObjectID();
                CKObject *object = m_Context->GetObject(objectID);
                CKStateChunk *subChunk = chunk->ReadSubChunk();

                if (object) {
                    object->Load(subChunk, nullptr);
                }

                if (subChunk) {
                    DeleteCKStateChunk(subChunk);
                }
            }
        }
    } else {
        // Modern format for data version >= 5
        if (chunk->SeekIdentifier(0xFFC00000)) {
            m_ControlPoints.Clear();
            m_ControlPoints.Load(m_Context, chunk);

            // Load curve properties
            m_FittingCoeff = chunk->ReadFloat();
            m_StepCount = chunk->ReadDword();
            m_Opened = chunk->ReadDword();
        }

        // Load control point sub-chunks if not in file context
        if (!file && chunk->SeekIdentifier(0xFF000000)) {
            int pointCount = chunk->ReadDword();
            for (int i = 0; i < pointCount; ++i) {
                CK_ID objectID = chunk->ReadObjectID();
                CKObject *object = m_Context->GetObject(objectID);
                CKStateChunk *subChunk = chunk->ReadSubChunk();
                if (object) {
                    object->Load(subChunk, nullptr);
                }
                if (subChunk) {
                    DeleteCKStateChunk(subChunk);
                }
            }
        }
    }

    // Clear loading flag
    m_LoadingFlag = 0;

    // Modify object flags
    ModifyObjectFlags(0, 0x400u);

    return CK_OK;
}

/**
 * @brief Check pre-deletion conditions
 */
void RCKCurve::CheckPreDeletion() {
    // Implementation for pre-deletion checks
}

/**
 * @brief Get memory occupation for the curve
 * @return Memory size in bytes
 */
int RCKCurve::GetMemoryOccupation() {
    return sizeof(RCKCurve) + m_ControlPoints.Size() * sizeof(CKObject *);
}

/**
 * @brief Check if object is used by this curve
 * @param o The object to check
 * @param cid The class ID to check
 * @return TRUE if object is used, FALSE otherwise
 */
int RCKCurve::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    CKObject **begin = reinterpret_cast<CKObject **>(m_ControlPoints.Begin());
    CKObject **end = reinterpret_cast<CKObject **>(m_ControlPoints.End());
    for (CKObject **it = begin; it && it != end; ++it) {
        if (*it == o) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * @brief Prepare dependencies for the curve
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = RCK3dEntity::PrepareDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    m_ControlPoints.Prepare(context);

    if (m_CurrentMesh) {
        m_CurrentMesh->PrepareDependencies(context);
    }

    return context.FinishPrepareDependencies(this, m_ClassID);
}

/**
 * @brief Remap dependencies for the curve
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = RCK3dEntity::RemapDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    m_ControlPoints.Remap(context);
    ModifyObjectFlags(0, 0x400u);

    return CK_OK;
}

/**
 * @brief Copy curve data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = RCK3dEntity::Copy(o, context);
    if (err != CK_OK) {
        return err;
    }

    RCKCurve &src = static_cast<RCKCurve &>(o);

    CreateLineMesh();

    for (int i = 0; i < src.m_ControlPoints.Size(); ++i) {
        m_ControlPoints.PushBack(src.m_ControlPoints[i]);
    }

    m_Opened = src.m_Opened;
    m_Length = src.m_Length;
    m_StepCount = src.m_StepCount;
    m_FittingCoeff = src.m_FittingCoeff;
    m_Color = src.m_Color;

    return CK_OK;
}

/**
 * @brief Render the curve
 * @param Dev The render context
 * @param Flags Render flags
 * @return TRUE if rendered successfully, FALSE otherwise
 */
CKBOOL RCKCurve::Render(CKRenderContext *Dev, CKDWORD Flags) {
    // Curve rendering implementation
    return TRUE;
}

// =====================================================
// CKCurve-specific virtual methods
// =====================================================

float RCKCurve::GetLength() {
    return m_Length;
}

void RCKCurve::Open() {
    m_Opened = TRUE;
}

void RCKCurve::Close() {
    m_Opened = FALSE;
}

CKBOOL RCKCurve::IsOpen() {
    return m_Opened;
}

CKERROR RCKCurve::GetPos(float step, VxVector *Pos, VxVector *Dir) {
    if (!Pos) return CKERR_INVALIDPARAMETER;

    // Simple linear interpolation along curve
    // In a real implementation, this would use spline math
    int pointCount = m_ControlPoints.Size();
    if (pointCount < 2) {
        if (pointCount == 1 && m_ControlPoints[0]) {
            reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[0])->GetPosition(Pos);
        }
        return CK_OK;
    }

    // Clamp step to [0, 1]
    if (step < 0.0f) step = 0.0f;
    if (step > 1.0f) step = 1.0f;

    // Find which segment we're in
    float t = step * (pointCount - 1);
    int segment = (int) t;
    float localT = t - segment;

    if (segment >= pointCount - 1) {
        segment = pointCount - 2;
        localT = 1.0f;
    }

    // Get positions of the two endpoints
    VxVector p0, p1;
    reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[segment])->GetPosition(&p0);
    reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[segment + 1])->GetPosition(&p1);

    // Linear interpolation
    Pos->x = p0.x + localT * (p1.x - p0.x);
    Pos->y = p0.y + localT * (p1.y - p0.y);
    Pos->z = p0.z + localT * (p1.z - p0.z);

    if (Dir) {
        Dir->x = p1.x - p0.x;
        Dir->y = p1.y - p0.y;
        Dir->z = p1.z - p0.z;
        // Normalize direction
        float len = sqrtf(Dir->x * Dir->x + Dir->y * Dir->y + Dir->z * Dir->z);
        if (len > 0.0001f) {
            Dir->x /= len;
            Dir->y /= len;
            Dir->z /= len;
        }
    }

    return CK_OK;
}

CKERROR RCKCurve::GetLocalPos(float step, VxVector *Pos, VxVector *Dir) {
    // Get world pos then transform to local
    VxVector worldPos;
    CKERROR result = GetPos(step, &worldPos, Dir);
    if (result != CK_OK) return result;

    // Transform to local coordinates
    if (Pos) {
        Vx3DMultiplyMatrixVector(Pos, m_InverseWorldMatrix, &worldPos);
    }

    return CK_OK;
}

CKERROR RCKCurve::GetTangents(int index, VxVector *InTangent, VxVector *OutTangent) {
    if (index < 0 || index >= m_ControlPoints.Size())
        return CKERR_INVALIDPARAMETER;
    auto *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[index]);
    if (pt) {
        pt->GetTangents(InTangent, OutTangent);
    }
    return CK_OK;
}

CKERROR RCKCurve::GetTangents(CKCurvePoint *pt, VxVector *InTangent, VxVector *OutTangent) {
    if (!pt) return CKERR_INVALIDPARAMETER;
    RCKCurvePoint *rpt = reinterpret_cast<RCKCurvePoint *>(pt);
    rpt->GetTangents(InTangent, OutTangent);
    return CK_OK;
}

CKERROR RCKCurve::SetTangents(int index, VxVector *InTangent, VxVector *OutTangent) {
    if (index < 0 || index >= m_ControlPoints.Size())
        return CKERR_INVALIDPARAMETER;
    auto *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[index]);
    if (pt) {
        pt->SetTangents(InTangent, OutTangent);
    }
    return CK_OK;
}

CKERROR RCKCurve::SetTangents(CKCurvePoint *pt, VxVector *InTangent, VxVector *OutTangent) {
    if (!pt) return CKERR_INVALIDPARAMETER;
    RCKCurvePoint *rpt = reinterpret_cast<RCKCurvePoint *>(pt);
    rpt->SetTangents(InTangent, OutTangent);
    return CK_OK;
}

void RCKCurve::SetFittingCoeff(float coeff) {
    m_FittingCoeff = coeff;
}

float RCKCurve::GetFittingCoeff() {
    return m_FittingCoeff;
}

CKERROR RCKCurve::RemoveControlPoint(CKCurvePoint *pt, CKBOOL DeletePoint) {
    if (!pt) return CKERR_INVALIDPARAMETER;

    for (int i = 0; i < m_ControlPoints.Size(); i++) {
        if (m_ControlPoints[i] == reinterpret_cast<CKObject *>(pt)) {
            m_ControlPoints.RemoveAt(i);
            if (DeletePoint) {
                m_Context->DestroyObject(reinterpret_cast<CKObject *>(pt));
            }
            return CK_OK;
        }
    }
    return CKERR_NOTFOUND;
}

CKERROR RCKCurve::InsertControlPoint(CKCurvePoint *pt, CKCurvePoint *after) {
    if (!pt) return CKERR_INVALIDPARAMETER;

    int insertPos = 0;
    if (after) {
        for (int i = 0; i < m_ControlPoints.Size(); i++) {
            if (m_ControlPoints[i] == reinterpret_cast<CKObject *>(after)) {
                insertPos = i + 1;
                break;
            }
        }
    }

    m_ControlPoints.Insert(insertPos, reinterpret_cast<CKObject *>(pt));
    return CK_OK;
}

CKERROR RCKCurve::AddControlPoint(CKCurvePoint *pt) {
    if (!pt) return CKERR_INVALIDPARAMETER;
    m_ControlPoints.PushBack(reinterpret_cast<CKObject *>(pt));
    return CK_OK;
}

// Additional curve methods
int RCKCurve::GetControlPointCount() {
    return m_ControlPoints.Size();
}

CKCurvePoint *RCKCurve::GetControlPoint(int index) {
    if (index < 0 || index >= m_ControlPoints.Size())
        return nullptr;
    return reinterpret_cast<CKCurvePoint *>(m_ControlPoints[index]);
}

CKERROR RCKCurve::RemoveAllControlPoints() {
    m_ControlPoints.Clear();
    return CK_OK;
}

CKERROR RCKCurve::SetStepCount(int count) {
    m_StepCount = count;
    return CK_OK;
}

int RCKCurve::GetStepCount() {
    return m_StepCount;
}

CKERROR RCKCurve::CreateLineMesh() {
    // Stub - creates line mesh for rendering curve
    return CK_OK;
}

CKERROR RCKCurve::UpdateMesh() {
    // Stub - updates the line mesh after control point changes
    return CK_OK;
}

VxColor RCKCurve::GetColor() {
    VxColor color;
    color.r = ((m_Color >> 16) & 0xFF) / 255.0f;
    color.g = ((m_Color >> 8) & 0xFF) / 255.0f;
    color.b = (m_Color & 0xFF) / 255.0f;
    color.a = ((m_Color >> 24) & 0xFF) / 255.0f;
    return color;
}

void RCKCurve::SetColor(const VxColor &color) {
    m_Color = ((CKDWORD) (color.a * 255) << 24) |
        ((CKDWORD) (color.r * 255) << 16) |
        ((CKDWORD) (color.g * 255) << 8) |
        (CKDWORD) (color.b * 255);
}

void RCKCurve::Update() {
    // Recalculate curve length and update mesh
    m_Length = 0;
    // Implementation would calculate actual curve length
}

// Static class registration methods
CKSTRING RCKCurve::GetClassName() {
    return (CKSTRING) "Curve";
}

int RCKCurve::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKCurve::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKCurve::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_CURVEPOINT);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_CURVE);
}

CKCurve *RCKCurve::CreateInstance(CKContext *Context) {
    return reinterpret_cast<CKCurve *>(new RCKCurve(Context));
}

// Static class ID
CK_CLASSID RCKCurve::m_ClassID = CKCID_CURVE;
