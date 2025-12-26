#include "RCKCurve.h"

#include "VxMath.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCKCurvePoint.h"
#include "RCKMesh.h"

CK_CLASSID RCKCurve::m_ClassID = CKCID_CURVE;

/**
 * @brief RCKCurve constructor
 * @param Context The CKContext instance
 * @param name Optional name for the curve
 */
RCKCurve::RCKCurve(CKContext *Context, CKSTRING name) : RCK3dEntity(Context, name) {
    m_Opened = TRUE;
    m_Length = 0;
    m_FittingCoeff = 0.0f;
    m_StepCount = 100;
    m_Color = 0xFFFFFFFF;
    m_Loading = FALSE;
}

/**
 * @brief RCKCurve destructor
 */
RCKCurve::~RCKCurve() {}

/**
 * @brief Get the class ID for RCKCurve
 * @return The class ID (43)
 */
CK_CLASSID RCKCurve::GetClassID() {
    return m_ClassID;
}

/**
 * @brief Prepare curve for saving by handling dependencies
 * @param file The CKFile to save to
 * @param flags Save flags
 */
void RCKCurve::PreSave(CKFile *file, CKDWORD flags) {
    RCK3dEntity::PreSave(file, flags);

    file->SaveObjects(m_ControlPoints.Begin(), m_ControlPoints.Size());
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
    if (!file && !(flags & CK_STATESAVE_CURVEONLY))
        return baseChunk;

    // Create a new state chunk for curve data
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_CURVE, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write curve specific data
    chunk->WriteIdentifier(CK_STATESAVE_CURVEONLY); // Curve identifier

    // Save control points array
    m_ControlPoints.Save(chunk);

    // Write curve properties
    chunk->WriteFloat(m_FittingCoeff);
    chunk->WriteDword(m_StepCount);
    chunk->WriteDword(m_Opened);

    // If not saving to file, save control point sub-chunks
    if (!file) {
        chunk->WriteIdentifier(CK_STATESAVE_CURVESAVEPOINTS);
        int pointCount = m_ControlPoints.Size();
        chunk->WriteDword(pointCount);

        for (int i = 0; i < pointCount; ++i) {
            CKObject *point = m_ControlPoints[i];
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

    m_Loading = TRUE;

    // Handle different data versions
    if (chunk->GetDataVersion() < 5) {
        // Legacy format handling for data version < 5
        if (chunk->SeekIdentifier(CK_STATESAVE_CURVECONTROLPOINT)) {
            m_ControlPoints.Clear();
            m_ControlPoints.Load(m_Context, chunk);
        }

        // Load curve properties from legacy identifiers
        if (chunk->SeekIdentifier(CK_STATESAVE_CURVEFITCOEFF))
            m_FittingCoeff = chunk->ReadFloat();

        if (chunk->SeekIdentifier(CK_STATESAVE_CURVESTEPS))
            m_StepCount = (int) chunk->ReadDword();

        if (chunk->SeekIdentifier(CK_STATESAVE_CURVEOPEN))
            m_Opened = (CKBOOL) chunk->ReadDword();

        // Load control point sub-chunks in legacy format
        if (chunk->SeekIdentifier(CK_STATESAVE_CURVESAVEPOINTS)) {
            int pointCount = (int) chunk->ReadDword();
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
        if (chunk->SeekIdentifier(CK_STATESAVE_CURVEONLY)) {
            m_ControlPoints.Clear();
            m_ControlPoints.Load(m_Context, chunk);

            // Load curve properties
            m_FittingCoeff = chunk->ReadFloat();
            m_StepCount = (int) chunk->ReadDword();
            m_Opened = (CKBOOL) chunk->ReadDword();
        }

        // Load control point sub-chunks if not in file context
        if (!file && chunk->SeekIdentifier(CK_STATESAVE_CURVESAVEPOINTS)) {
            int pointCount = (int) chunk->ReadDword();
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
    m_Loading = FALSE;

    // Modify object flags
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);

    return CK_OK;
}

/**
 * @brief Check pre-deletion conditions
 */
void RCKCurve::CheckPreDeletion() {
    // Based on decompilation at 0x100160BC
    RCK3dEntity::CheckPreDeletion();

    // Remove points flagged for deletion.
    if (m_ControlPoints.Check()) {
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
}

/**
 * @brief Get memory occupation for the curve
 * @return Memory size in bytes
 */
int RCKCurve::GetMemoryOccupation() {
    // Based on decompilation at 0x100160f0: base + sizeof delta + pointer array
    return RCK3dEntity::GetMemoryOccupation() + (sizeof(RCKCurve) - sizeof(RCK3dEntity)) + m_ControlPoints.GetMemoryOccupation(FALSE);
}

/**
 * @brief Check if object is used by this curve
 * @param o The object to check
 * @param cid The class ID to check
 * @return TRUE if object is used, FALSE otherwise
 */
int RCKCurve::IsObjectUsed(CKObject *o, CK_CLASSID cid) {
    if (cid == CKCID_CURVEPOINT && m_ControlPoints.IsHere(o)) {
        return TRUE;
    }
    return RCK3dEntity::IsObjectUsed(o, cid);
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
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);

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

    m_ControlPoints = src.m_ControlPoints;

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
    // Based on decompilation at 0x1001415E
    if (!IsUpToDate()) {
        Update();
    }
    return RCK3dEntity::Render(Dev, Flags);
}

// =====================================================
// CKCurve-specific virtual methods
// =====================================================

float RCKCurve::GetLength() {
    // Based on decompilation at 0x10014e8a
    if (!IsUpToDate()) {
        Update();
    }
    return m_Length;
}

void RCKCurve::Open() {
    if (!m_Opened) {
        m_Opened = TRUE;
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
}

void RCKCurve::Close() {
    if (m_Opened) {
        m_Opened = FALSE;
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
}

CKBOOL RCKCurve::IsOpen() {
    return m_Opened;
}

CKERROR RCKCurve::GetPos(float step, VxVector *Pos, VxVector *Dir) {
    if (!Pos) {
        return CKERR_INVALIDPARAMETER;
    }

    // Ensure curve is updated
    if (!IsUpToDate()) {
        Update();
    }

    VxVector localPos;
    VxVector localDir;
    CKERROR err = GetLocalPos(step, &localPos, Dir ? &localDir : nullptr);
    if (err != CK_OK) {
        return err;
    }

    Vx3DMultiplyMatrixVector(Pos, m_WorldMatrix, &localPos);
    if (Dir) {
        Vx3DRotateVector(Dir, m_WorldMatrix, &localDir);
        Dir->Normalize();
    }
    return CK_OK;
}

CKERROR RCKCurve::GetLocalPos(float step, VxVector *Pos, VxVector *Dir) {
    if (!Pos) {
        return CKERR_INVALIDPARAMETER;
    }

    // Ensure curve is updated
    if (!IsUpToDate()) {
        Update();
    }

    auto normalizeStep = [this](float s) {
        if (m_Opened) {
            if (s > 1.0f) s = 1.0f;
            if (s < 0.0f) s = 0.0f;
            return s;
        }
        float intPart = floorf(s);
        s -= intPart;
        while (s < 0.0f) {
            s += 1.0f;
        }
        return s;
    };

    auto normalizeIndex = [this](int index, int count) {
        if (count <= 0) return 0;
        if (m_Opened) {
            if (index < 0) return 0;
            if (index >= count) return count - 1;
            return index;
        }
        index %= count;
        if (index < 0) index += count;
        return index;
    };

    const int count = m_ControlPoints.Size();
    if (count < 2) {
        return CKERR_INVALIDPARAMETER;
    }

    step = normalizeStep(step);

    const float targetLen = step * m_Length;

    int endIndex = m_Opened ? (count - 1) : 0;
    for (int i = 0; i < count; ++i) {
        RCKCurvePoint *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
        if (!pt) {
            continue;
        }
        const float ptLen = pt->GetLength();
        if (!(ptLen < targetLen)) {
            endIndex = i;
            break;
        }
    }

    int startIndex = endIndex - 1;
    if (startIndex < 0) {
        startIndex = m_Opened ? 0 : (count - 1);
    }

    RCKCurvePoint *startPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[startIndex]);
    RCKCurvePoint *endPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[endIndex]);
    if (!startPt || !endPt) {
        return CKERR_INVALIDPARAMETER;
    }

    VxVector p0;
    VxVector p1;
    startPt->GetFittedVector(&p0);
    endPt->GetFittedVector(&p1);

    VxVector t0;
    VxVector t1;
    startPt->GetTangents(nullptr, &t0);
    endPt->GetTangents(&t1, nullptr);

    float l0 = startPt->GetLength();
    float l1 = 0.0f;
    if (endIndex == 0) {
        l1 = m_Length;
    } else {
        l1 = endPt->GetLength();
    }

    const float segLen = l1 - l0;
    float u = 0.0f;
    if (segLen != 0.0f) {
        u = (targetLen - l0) / segLen;
    }

    const CKBOOL linear = startPt->IsLinear();

    auto lerp = [](const VxVector &a, const VxVector &b, float t) {
        VxVector r;
        r.x = a.x + (b.x - a.x) * t;
        r.y = a.y + (b.y - a.y) * t;
        r.z = a.z + (b.z - a.z) * t;
        return r;
    };

    auto hermite = [](const VxVector &p0, const VxVector &p1, const VxVector &m0, const VxVector &m1, float t) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = (2.0f * t3 - 3.0f * t2 + 1.0f);
        const float h10 = (t3 - 2.0f * t2 + t);
        const float h01 = (-2.0f * t3 + 3.0f * t2);
        const float h11 = (t3 - t2);
        VxVector r;
        r.x = h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x;
        r.y = h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y;
        r.z = h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z;
        return r;
    };

    if (linear) {
        *Pos = lerp(p0, p1, u);
        if (Dir) {
            Dir->x = p1.x - p0.x;
            Dir->y = p1.y - p0.y;
            Dir->z = p1.z - p0.z;
            Dir->Normalize();
        }
        return CK_OK;
    }

    *Pos = hermite(p0, p1, t0, t1, u);
    if (Dir) {
        float u2 = u + 0.01f;
        VxVector pos2 = hermite(p0, p1, t0, t1, u2);
        Dir->x = pos2.x - Pos->x;
        Dir->y = pos2.y - Pos->y;
        Dir->z = pos2.z - Pos->z;
        Dir->Normalize();
    }

    return CK_OK;
}

CKERROR RCKCurve::GetTangentsByIndex(int index, VxVector *InTangent, VxVector *OutTangent) {
    if (index < 0 || index >= m_ControlPoints.Size()) {
        return CKERR_INVALIDPARAMETER;
    }

    RCKCurvePoint *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[index]);
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    if (!pt->IsTCB()) {
        pt->GetTangents(InTangent, OutTangent);
        return CK_OK;
    }

    auto normalizeIndex = [this](int idx, int count) {
        if (count <= 0) return 0;
        if (m_Opened) {
            if (idx < 0) return 0;
            if (idx >= count) return count - 1;
            return idx;
        }
        idx %= count;
        if (idx < 0) idx += count;
        return idx;
    };

    const int count = m_ControlPoints.Size();

    VxVector p;
    pt->GetReservedVector(&p);

    const float tension = pt->GetTension();
    const float continuity = pt->GetContinuity();
    const float bias = pt->GetBias();

    const int prevIndex = normalizeIndex(index - 1, count);
    const int nextIndex = normalizeIndex(index + 1, count);

    RCKCurvePoint *prevPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[prevIndex]);
    RCKCurvePoint *nextPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[nextIndex]);
    if (!prevPt || !nextPt) {
        return CKERR_INVALIDPARAMETER;
    }

    VxVector pPrev;
    VxVector pNext;
    prevPt->GetReservedVector(&pPrev);
    nextPt->GetReservedVector(&pNext);

    VxVector dn;
    dn.x = p.x - pPrev.x;
    dn.y = p.y - pPrev.y;
    dn.z = p.z - pPrev.z;
    VxVector dp;
    dp.x = pNext.x - p.x;
    dp.y = pNext.y - p.y;
    dp.z = pNext.z - p.z;

    if (OutTangent) {
        const float wDp = (1.0f - tension) * (1.0f - continuity) * (1.0f - bias);
        const float wDn = (1.0f - tension) * (continuity + 1.0f) * (bias + 1.0f);
        OutTangent->x = (wDp * dp.x + wDn * dn.x) * 0.5f;
        OutTangent->y = (wDp * dp.y + wDn * dn.y) * 0.5f;
        OutTangent->z = (wDp * dp.z + wDn * dn.z) * 0.5f;
    }

    if (InTangent) {
        const float wDp = (1.0f - tension) * (continuity + 1.0f) * (1.0f - bias);
        const float wDn = (1.0f - tension) * (1.0f - continuity) * (bias + 1.0f);
        InTangent->x = (wDn * dn.x + wDp * dp.x) * 0.5f;
        InTangent->y = (wDn * dn.y + wDp * dp.y) * 0.5f;
        InTangent->z = (wDn * dn.z + wDp * dp.z) * 0.5f;
    }

    return CK_OK;
}

CKERROR RCKCurve::GetTangents(CKCurvePoint *pt, VxVector *InTangent, VxVector *OutTangent) {
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    int i = m_ControlPoints.GetPosition((CKObject *) pt);
    return GetTangentsByIndex(i, InTangent, OutTangent);
}

CKERROR RCKCurve::SetTangentsByIndex(int index, VxVector *InTangent, VxVector *OutTangent) {
    if (index < 0 || index >= m_ControlPoints.Size()) {
        return CKERR_INVALIDPARAMETER;
    }

    RCKCurvePoint *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[index]);
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    // Non-TCB points store explicit tangents directly.
    if (!pt->IsTCB()) {
        pt->SetTangents(InTangent, OutTangent);
        return CK_OK;
    }

    // TCB points: DLL derives a bias update from requested tangents.
    auto normalizeIndex = [this](int idx, int count) {
        if (count <= 0) return 0;
        if (m_Opened) {
            if (idx < 0) return 0;
            if (idx >= count) return count - 1;
            return idx;
        }
        idx %= count;
        if (idx < 0) idx += count;
        return idx;
    };

    const int count = m_ControlPoints.Size();
    const int prevIndex = normalizeIndex(index - 1, count);
    const int nextIndex = normalizeIndex(index + 1, count);

    RCKCurvePoint *prevPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[prevIndex]);
    RCKCurvePoint *nextPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[nextIndex]);
    if (!prevPt || !nextPt || !InTangent || !OutTangent) {
        return CKERR_INVALIDPARAMETER;
    }

    VxVector pPrev;
    VxVector pCur;
    VxVector pNext;
    prevPt->GetReservedVector(&pPrev);
    pt->GetReservedVector(&pCur);
    nextPt->GetReservedVector(&pNext);

    VxVector dn;
    dn.x = pCur.x - pPrev.x;
    dn.y = pCur.y - pPrev.y;
    dn.z = pCur.z - pPrev.z;
    VxVector dp;
    dp.x = pNext.x - pCur.x;
    dp.y = pNext.y - pCur.y;
    dp.z = pNext.z - pCur.z;

    VxVector chordSum;
    chordSum.x = dn.x + dp.x;
    chordSum.y = dn.y + dp.y;
    chordSum.z = dn.z + dp.z;

    VxVector chordDiff;
    chordDiff.x = dn.x - dp.x;
    chordDiff.y = dn.y - dp.y;
    chordDiff.z = dn.z - dp.z;

    VxVector tanSum;
    tanSum.x = InTangent->x + OutTangent->x;
    tanSum.y = InTangent->y + OutTangent->y;
    tanSum.z = InTangent->z + OutTangent->z;

    const float tension = pt->GetTension();

    float newBias = 0.0f;
    // Choose a stable component (matches DLL dominant-axis selection).
    const float ax = fabsf(chordDiff.x);
    const float ay = fabsf(chordDiff.y);
    const float az = fabsf(chordDiff.z);
    if (ax >= ay && ax >= az) {
        if (chordDiff.x != 0.0f) {
            newBias = (tanSum.x / (1.0f - tension) + chordSum.x) / chordDiff.x;
        }
    } else if (ay >= az) {
        if (chordDiff.y != 0.0f) {
            newBias = (tanSum.y / (1.0f - tension) + chordSum.y) / chordDiff.y;
        }
    } else {
        if (chordDiff.z != 0.0f) {
            newBias = (tanSum.z / (1.0f - tension) + chordSum.z) / chordDiff.z;
        }
    }

    pt->SetBias(newBias);
    return CK_OK;
}

CKERROR RCKCurve::SetTangents(CKCurvePoint *pt, VxVector *InTangent, VxVector *OutTangent) {
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    int i = m_ControlPoints.GetPosition((CKObject *) pt);
    return SetTangentsByIndex(i, InTangent, OutTangent);
}

void RCKCurve::SetFittingCoeff(float coeff) {
    m_FittingCoeff = coeff;
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
}

float RCKCurve::GetFittingCoeff() {
    return m_FittingCoeff;
}

CKERROR RCKCurve::RemoveControlPoint(CKCurvePoint *pt, CKBOOL DeletePoint) {
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    m_ControlPoints.Remove((CKObject *) pt);
    RCKCurvePoint *cp = (RCKCurvePoint *) pt;
    cp->SetCurve(nullptr);

    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    return CK_OK;
}

CKERROR RCKCurve::InsertControlPoint(CKCurvePoint *prev, CKCurvePoint *pt) {
    // Based on decompilation at 0x10014063
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    if (m_ControlPoints.IsHere((CKObject *) pt)) {
        return CKERR_ALREADYPRESENT;
    }

    int pos = m_ControlPoints.GetPosition((CKObject *) prev);
    if (pos < 0) {
        m_ControlPoints.PushBack((CKObject *) pt);
    } else {
        m_ControlPoints.Insert(pos, (CKObject *) pt);
    }

    RCKCurvePoint *cp = (RCKCurvePoint *) pt;
    cp->SetCurve((CKCurve *) this);

    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    return CK_OK;
}

CKERROR RCKCurve::AddControlPoint(CKCurvePoint *pt) {
    // Based on decompilation at 0x10013fcd
    if (!pt) {
        return CKERR_INVALIDPARAMETER;
    }

    if (m_ControlPoints.IsHere((CKObject *) pt)) {
        return CKERR_ALREADYPRESENT;
    }

    m_ControlPoints.PushBack((CKObject *) pt);
    RCKCurvePoint *cp = (RCKCurvePoint *) pt;
    cp->SetCurve((CKCurve *) this);

    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
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
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    return CK_OK;
}

CKERROR RCKCurve::SetStepCount(int count) {
    m_StepCount = count;
    ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    return CK_OK;
}

int RCKCurve::GetStepCount() {
    return m_StepCount;
}

CKERROR RCKCurve::CreateLineMesh() {
    // Based on decompilation at 0x10014F99
    char buffer[256];
    buffer[0] = '\0';

    CKSTRING name = GetName();
    if (name && name[0]) {
        strcpy(buffer, name);
    }
    strcat(buffer, "LineMesh");

    RCKMesh *mesh = (RCKMesh *) m_Context->CreateObject(CKCID_MESH, buffer, CK_OBJECTCREATION_SameDynamic);
    if (!mesh) {
        return CKERR_OUTOFMEMORY;
    }

    mesh->ModifyObjectFlags(CK_OBJECT_NOTTOBELISTEDANDSAVED, 0);
    SetCurrentMesh(mesh, TRUE);
    return CK_OK;
}

CKERROR RCKCurve::UpdateMesh() {
    if (!IsVisible()) {
        return CK_OK;
    }

    RCKMesh *mesh = m_CurrentMesh;
    if (mesh) {
        mesh->ModifyObjectFlags(0x23u, 0);
    } else {
        CreateLineMesh();
        mesh = m_CurrentMesh;
        if (!mesh) {
            return CKERR_NOTFOUND;
        }
    }

    VxVector zero(0.0f, 0.0f, 0.0f);

    const int cpCount = m_ControlPoints.Size();
    if (cpCount >= 2) {
        const float invTotalLen = (m_Length != 0.0f) ? (1.0f / m_Length) : 0.0f;

        int totalSteps = 0;
        RCKCurvePoint *prev = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[0]);
        for (int i = 1; i < cpCount; ++i) {
            RCKCurvePoint *cur = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
            if (prev && prev->IsLinear()) {
                ++totalSteps;
            } else if (prev && cur) {
                float segLen = cur->GetLength() - prev->GetLength();
                int segSteps = (int) floor((double) m_StepCount * (double) invTotalLen * (double) segLen);
                if (segSteps < 1) segSteps = 1;
                totalSteps += segSteps;
            }
            prev = cur;
        }

        if (!m_Opened && prev) {
            if (prev->IsLinear()) {
                ++totalSteps;
            } else {
                float segLen = m_Length - prev->GetLength();
                int segSteps = (int) floor((double) m_StepCount * (double) invTotalLen * (double) segLen);
                if (segSteps < 1) segSteps = 1;
                totalSteps += segSteps;
            }
        }

        mesh->SetVertexCount(totalSteps + 1);
        mesh->SetLineCount(totalSteps);

        CKDWORD posStride = 0;
        char *posPtr = (char *) mesh->GetPositionsPtr(&posStride);

        const int segmentCount = m_Opened ? (cpCount - 1) : cpCount;

        auto hermite = [](const VxVector &p0, const VxVector &p1, const VxVector &m0, const VxVector &m1, float t) {
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = (2.0f * t3 - 3.0f * t2 + 1.0f);
            const float h10 = (t3 - 2.0f * t2 + t);
            const float h01 = (-2.0f * t3 + 3.0f * t2);
            const float h11 = (t3 - t2);
            VxVector r;
            r.x = h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x;
            r.y = h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y;
            r.z = h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z;
            return r;
        };

        for (int i = 0; i < segmentCount; ++i) {
            RCKCurvePoint *pStart = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
            if (!pStart) {
                continue;
            }

            VxVector p0;
            pStart->GetFittedVector(&p0);
            VxVector m0;
            pStart->GetTangents(nullptr, &m0);
            const CKBOOL linear = pStart->IsLinear();

            const float l0 = pStart->GetLength();
            float l1 = m_Length;

            RCKCurvePoint *pEnd = nullptr;
            if (i + 1 < cpCount) {
                pEnd = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i + 1]);
                if (pEnd) {
                    l1 = pEnd->GetLength();
                }
            } else {
                pEnd = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[0]);
            }

            if (!pEnd) {
                continue;
            }

            VxVector p1;
            pEnd->GetFittedVector(&p1);
            VxVector m1;
            pEnd->GetTangents(&m1, nullptr);

            int segSteps = (int) floor((double) (l1 - l0) * (double) m_StepCount * (double) invTotalLen);
            if (segSteps < 1) segSteps = 1;

            if (linear || segSteps == 1) {
                *(VxVector *) posPtr = p0;
                posPtr += posStride;
            } else {
                const float invSegSteps = 1.0f / (float) segSteps;
                for (int s = 0; s < segSteps; ++s) {
                    const float t = (float) s * invSegSteps;
                    *(VxVector *) posPtr = hermite(p0, p1, m0, m1, t);
                    posPtr += posStride;
                }
            }
        }

        // Final vertex.
        RCKCurvePoint *lastPt = nullptr;
        if (m_Opened) {
            lastPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[cpCount - 1]);
        } else {
            lastPt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[0]);
        }
        if (lastPt) {
            lastPt->GetFittedVector((VxVector *) posPtr);
        }

        CKDWORD colStride = 0;
        CKDWORD specStride = 0;
        void *cols = mesh->GetColorsPtr(&colStride);
        void *spec = mesh->GetSpecularColorsPtr(&specStride);
        CKDWORD specColor = A_MASK;
        VxFillStructure(totalSteps + 1, cols, colStride, 4u, &m_Color);
        VxFillStructure(totalSteps + 1, spec, specStride, 4u, &specColor);

        mesh->CreateLineStrip(0, totalSteps, 0);
    } else {
        mesh->SetVertexCount(m_StepCount + 1);
        mesh->SetLineCount(m_StepCount);
        for (int i = 0; i <= m_StepCount; ++i) {
            mesh->SetVertexColor(i, m_Color);
            mesh->SetVertexSpecularColor(i, 0);
            mesh->SetVertexTextureCoordinates(i, 0, 0, -1);
            mesh->SetVertexPosition(i, &zero);
        }
        mesh->CreateLineStrip(0, m_StepCount, 0);
    }

    if (!m_Context->IsInInterfaceMode()) {
        mesh->Show(CKHIDE);
    }

    return CK_OK;
}

VxColor RCKCurve::GetColor() {
    return VxColor(m_Color);
}

void RCKCurve::SetColor(const VxColor &color) {
    m_Color = color.GetRGBA();
    UpdateMesh();
}

void RCKCurve::Update() {
    // Based on decompilation at 0x1001586D
    if (m_Loading) {
        return;
    }

    const int count = m_ControlPoints.Size();

    VxMatrix invCurveWorld;
    Vx3DInverseMatrix(invCurveWorld, m_WorldMatrix);

    // Cache curve-space positions into curve points (reserved + fitted).
    for (int i = 0; i < count; ++i) {
        RCKCurvePoint *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
        if (!pt) {
            continue;
        }
        VxVector worldPos;
        pt->GetPosition(&worldPos, nullptr);

        VxVector localPos;
        Vx3DMultiplyMatrixVector(&localPos, invCurveWorld, &worldPos);

        pt->SetReservedVector(&localPos);
        pt->SetFittedVector(&localPos);
    }

    // Compute tangents for all points (stores explicit computed tangents on the points).
    for (int i = 0; i < count; ++i) {
        VxVector inT;
        VxVector outT;
        GetTangentsByIndex(i, &inT, &outT);
        RCKCurvePoint *pt = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
        if (pt) {
            pt->SetTangents(&inT, &outT);
        }
    }

    // Optional fitting pass (only when coeff > 0).
    if (m_FittingCoeff > 0.0f) {
        auto normalizeIndex = [this](int idx, int cnt) {
            if (cnt <= 0) return 0;
            if (m_Opened) {
                if (idx < 0) return 0;
                if (idx >= cnt) return cnt - 1;
                return idx;
            }
            idx %= cnt;
            if (idx < 0) idx += cnt;
            return idx;
        };

        for (int i = 0; i < count; ++i) {
            const int prevIndex = normalizeIndex(i - 1, count);
            const int nextIndex = normalizeIndex(i + 1, count);

            RCKCurvePoint *prev = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[prevIndex]);
            RCKCurvePoint *cur = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
            RCKCurvePoint *next = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[nextIndex]);
            if (!prev || !cur || !next) {
                continue;
            }

            VxVector pPrev;
            VxVector pCur;
            VxVector pNext;
            prev->GetReservedVector(&pPrev);
            cur->GetReservedVector(&pCur);
            next->GetReservedVector(&pNext);

            VxVector mid;
            mid.x = (pPrev.x + pNext.x) * 0.5f;
            mid.y = (pPrev.y + pNext.y) * 0.5f;
            mid.z = (pPrev.z + pNext.z) * 0.5f;

            VxVector fitted;
            fitted.x = pCur.x + (mid.x - pCur.x) * m_FittingCoeff;
            fitted.y = pCur.y + (mid.y - pCur.y) * m_FittingCoeff;
            fitted.z = pCur.z + (mid.z - pCur.z) * m_FittingCoeff;

            cur->SetFittedVector(&fitted);
        }
    }

    // Length computation (per segment) and cumulative lengths on points.
    m_Length = 0.0f;
    const int segmentCount = m_Opened ? (count - 1) : count;
    if (count > 1) {
        auto hermite = [](const VxVector &p0, const VxVector &p1, const VxVector &m0, const VxVector &m1, float t) {
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float h00 = (2.0f * t3 - 3.0f * t2 + 1.0f);
            const float h10 = (t3 - 2.0f * t2 + t);
            const float h01 = (-2.0f * t3 + 3.0f * t2);
            const float h11 = (t3 - t2);
            VxVector r;
            r.x = h00 * p0.x + h10 * m0.x + h01 * p1.x + h11 * m1.x;
            r.y = h00 * p0.y + h10 * m0.y + h01 * p1.y + h11 * m1.y;
            r.z = h00 * p0.z + h10 * m0.z + h01 * p1.z + h11 * m1.z;
            return r;
        };

        for (int i = 0; i < segmentCount; ++i) {
            RCKCurvePoint *pStart = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i]);
            if (!pStart) {
                continue;
            }

            VxVector p0;
            pStart->GetFittedVector(&p0);
            VxVector m0;
            pStart->GetTangents(nullptr, &m0);

            pStart->SetCurveLength(m_Length);
            const CKBOOL linear = pStart->IsLinear();

            RCKCurvePoint *pEnd = nullptr;
            if (i + 1 < count) {
                pEnd = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[i + 1]);
            } else {
                pEnd = reinterpret_cast<RCKCurvePoint *>(m_ControlPoints[0]);
            }
            if (!pEnd) {
                continue;
            }

            VxVector p1;
            pEnd->GetFittedVector(&p1);
            VxVector m1;
            pEnd->GetTangents(&m1, nullptr);

            if (linear) {
                VxVector delta;
                delta.x = p1.x - p0.x;
                delta.y = p1.y - p0.y;
                delta.z = p1.z - p0.z;
                m_Length += delta.Magnitude();
            } else {
                VxVector prevPos = p0;
                for (int j = 1; j <= 100; ++j) {
                    const float t = (float) j / 100.0f;
                    VxVector pos = hermite(p0, p1, m0, m1, t);
                    VxVector delta;
                    delta.x = pos.x - prevPos.x;
                    delta.y = pos.y - prevPos.y;
                    delta.z = pos.z - prevPos.z;
                    m_Length += delta.Magnitude();
                    prevPos = pos;
                }
            }

            if (i != count - 1) {
                pEnd->SetCurveLength(m_Length);
            }
        }
    }

    UpdateMesh();
    ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
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
