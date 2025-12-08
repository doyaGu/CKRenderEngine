#include "RCKCurve.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCKCurvePoint.h"

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
      field_1C8(0) {
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
    return 43;
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

    // Save control point objects
    int pointCount = m_ControlPoints.Size();
    if (pointCount > 0) {
        // Convert RCKCurvePoint array to CKObject array for saving
        XArray<CKObject *> objects;
        for (int i = 0; i < pointCount; ++i) {
            objects.PushBack(static_cast<CKObject *>(&m_ControlPoints[i]));
        }
        // Save objects using the file context
        for (int i = 0; i < pointCount; ++i) {
            file->SaveObject(objects[i], flags);
        }
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
    // Convert RCKCurvePoint array to XObjectPointerArray for saving
    XObjectPointerArray objectArray;
    for (int i = 0; i < m_ControlPoints.Size(); ++i) {
        objectArray.PushBack(static_cast<CKObject *>(&m_ControlPoints[i]));
    }
    objectArray.Save(chunk);

    // Write curve properties
    chunk->WriteFloat(m_FittingCoeff);
    chunk->WriteDword(m_StepCount);
    chunk->WriteDword(m_Opened);

    // If not saving to file, save control point sub-chunks
    if (!file) {
        chunk->WriteIdentifier(0xFF000000);
        int pointCount = m_ControlPoints.Size();
        chunk->WriteDword(pointCount);

        // Save each control point with its sub-chunk
        for (int i = 0; i < m_ControlPoints.Size(); ++i) {
            CKStateChunk *pointChunk = nullptr;
            CKObject *point = static_cast<CKObject *>(&m_ControlPoints[i]);
            if (point) {
                pointChunk = point->Save(nullptr, flags);
            }

            chunk->WriteObject(point);
            chunk->WriteSubChunk(pointChunk);

            if (pointChunk) {
                DeleteCKStateChunk(pointChunk);
            }
        }
    }

    // Close or update the chunk based on class ID
    if (GetClassID() == 43)
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

    // Set loading flag
    field_1C8 = 1;

    // Handle different data versions
    if (chunk->GetDataVersion() < 5) {
        // Legacy format handling for data version < 5
        if (chunk->SeekIdentifier(0x800000u)) {
            // Clear existing control points
            m_ControlPoints.Clear();
            XObjectPointerArray objectArray;
            objectArray.Load(m_Context, chunk);
            // Convert back to RCKCurvePoint array
            for (int i = 0; i < objectArray.Size(); ++i) {
                RCKCurvePoint *point = static_cast<RCKCurvePoint *>(objectArray[i]);
                if (point) {
                    m_ControlPoints.PushBack(*point);
                }
            }
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
            // Clear existing control points
            m_ControlPoints.Clear();
            XObjectPointerArray objectArray;
            objectArray.Load(m_Context, chunk);
            // Convert back to RCKCurvePoint array
            for (int i = 0; i < objectArray.Size(); ++i) {
                RCKCurvePoint *point = static_cast<RCKCurvePoint *>(objectArray[i]);
                if (point) {
                    m_ControlPoints.PushBack(*point);
                }
            }

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
    field_1C8 = 0;

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
    return sizeof(RCKCurve) + m_ControlPoints.Size() * sizeof(RCKCurvePoint *);
}

/**
 * @brief Check if object is used by this curve
 * @param o The object to check
 * @param cid The class ID to check
 * @return TRUE if object is used, FALSE otherwise
 */
int RCKCurve::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    // Check if object is used in control points
    for (int i = 0; i < m_ControlPoints.Size(); ++i) {
        if (static_cast<CKObject *>(&m_ControlPoints[i]) == o) {
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
    // Prepare dependencies for control points
    return CK_OK;
}

/**
 * @brief Remap dependencies for the curve
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::RemapDependencies(CKDependenciesContext &context) {
    // Remap dependencies for control points
    return CK_OK;
}

/**
 * @brief Copy curve data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKCurve::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    RCK3dEntity::Copy(o, context);

    // Copy curve specific data
    RCKCurve &target = (RCKCurve &) o;
    target.m_Opened = m_Opened;
    target.m_Length = m_Length;
    target.m_StepCount = m_StepCount;
    target.m_FittingCoeff = m_FittingCoeff;
    target.m_Color = m_Color;
    target.field_1C8 = field_1C8;

    // Copy control points (shallow copy)
    target.m_ControlPoints = m_ControlPoints;

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
        if (pointCount == 1) {
            m_ControlPoints[0].GetPosition(Pos);
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
    m_ControlPoints[segment].GetPosition(&p0);
    m_ControlPoints[segment + 1].GetPosition(&p1);

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

    m_ControlPoints[index].GetTangents(InTangent, OutTangent);
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

    m_ControlPoints[index].SetTangents(InTangent, OutTangent);
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

    // Cast to RCKCurvePoint* for comparison
    RCKCurvePoint *rpt = reinterpret_cast<RCKCurvePoint *>(pt);
    for (int i = 0; i < m_ControlPoints.Size(); i++) {
        if (&m_ControlPoints[i] == rpt) {
            m_ControlPoints.RemoveAt(i);
            return CK_OK;
        }
    }
    return CKERR_NOTFOUND;
}

CKERROR RCKCurve::InsertControlPoint(CKCurvePoint *pt, CKCurvePoint *after) {
    if (!pt) return CKERR_INVALIDPARAMETER;

    // Cast to RCKCurvePoint* for comparison
    RCKCurvePoint *rafter = reinterpret_cast<RCKCurvePoint *>(after);

    // Find position to insert
    int insertPos = 0;
    if (after) {
        for (int i = 0; i < m_ControlPoints.Size(); i++) {
            if (&m_ControlPoints[i] == rafter) {
                insertPos = i + 1;
                break;
            }
        }
    }

    // Insert the point - need to dereference the cast pointer
    RCKCurvePoint *rpt = reinterpret_cast<RCKCurvePoint *>(pt);
    m_ControlPoints.Insert(insertPos, *rpt);
    return CK_OK;
}

CKERROR RCKCurve::AddControlPoint(CKCurvePoint *pt) {
    if (!pt) return CKERR_INVALIDPARAMETER;
    RCKCurvePoint *rpt = reinterpret_cast<RCKCurvePoint *>(pt);
    m_ControlPoints.PushBack(*rpt);
    return CK_OK;
}

// Additional curve methods
int RCKCurve::GetControlPointCount() {
    return m_ControlPoints.Size();
}

CKCurvePoint *RCKCurve::GetControlPoint(int index) {
    if (index < 0 || index >= m_ControlPoints.Size())
        return nullptr;
    return reinterpret_cast<CKCurvePoint *>(&m_ControlPoints[index]);
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
