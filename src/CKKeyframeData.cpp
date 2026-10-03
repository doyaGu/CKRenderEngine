#include "RCKKeyframeData.h"

#include "VxMath.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
#include <new>

//===================================================================
// Helper functions for TCB spline interpolation
//===================================================================

// 0x10050AB1 remaps normalized segment time with quadratic ends and a
// linear middle. Arguments are the left key's easefrom and right key's easeto.
static float ApplyEaseParameters(float t, float easeFrom, float easeTo) {
    const float easeTotal = easeFrom + easeTo;
    if (t == 0.0f || t == 1.0f || easeTotal == 0.0f)
        return t;
    if (easeTotal > 1.0f) {
        const float inverseTotal = 1.0f / easeTotal;
        easeFrom *= inverseTotal;
        easeTo *= inverseTotal;
    }

    const float scale = 1.0f / (2.0f - easeFrom - easeTo);
    if (t < easeFrom)
        return (scale / easeFrom) * t * t;
    if (t < 1.0f - easeTo)
        return (2.0f * t - easeFrom) * scale;
    const float remaining = 1.0f - t;
    return 1.0f - (scale / easeTo) * remaining * remaining;
}

struct TCBTimeWeights {
    float previous = 1.0f;
    float next = 1.0f;
};

static TCBTimeWeights ComputeTCBTimeWeights(float previousTime, float time, float nextTime, float continuity) {
    const float scale = 2.0f / (nextTime - previousTime);
    TCBTimeWeights weights;
    weights.previous = (time - previousTime) * scale;
    weights.next = (nextTime - time) * scale;
    const float magnitude = std::fabs(continuity);
    weights.previous = weights.previous + magnitude - magnitude * weights.previous;
    weights.next = weights.next + magnitude - magnitude * weights.next;
    return weights;
}

struct TCBCoefficients {
    float incomingPrevious, incomingNext, outgoingPrevious, outgoingNext;
};

static TCBCoefficients ComputeTCBCoefficients(float tension, float continuity, float bias, TCBTimeWeights weights) {
    const float halfTension = (1.0f - tension) * 0.5f;
    const float continuityMinus = 1.0f - continuity;
    const float continuityPlus = 2.0f - continuityMinus;
    const float biasMinus = 1.0f - bias;
    const float biasPlus = 2.0f - biasMinus;
    const float minus = halfTension * continuityMinus;
    const float plus = halfTension * continuityPlus;
    return {minus * biasPlus * weights.previous, plus * biasMinus * weights.previous,
            plus * biasPlus * weights.next, minus * biasMinus * weights.next};
}

// 0x10050BA1, 0x10050E2E, 0x10050EC3 and 0x10050F98. Position and
// scale share this implementation in the original DLL as well.
static void ComputeTCBVectorTangents(const CKTCBPositionKey *keys, int count, VxVector *tangents) {
    if (count == 2) {
        const VxVector delta = keys[1].Pos - keys[0].Pos;
        tangents[1] = (1.0f - keys[0].tension) * delta;
        tangents[2] = (1.0f - keys[1].tension) * delta;
        return;
    }
    for (int i = 1; i < count - 1; ++i) {
        const auto &key = keys[i];
        const auto weights = ComputeTCBTimeWeights(keys[i - 1].TimeStep, key.TimeStep, keys[i + 1].TimeStep, key.continuity);
        const auto factors = ComputeTCBCoefficients(key.tension, key.continuity, key.bias, weights);
        const VxVector previous = key.Pos - keys[i - 1].Pos;
        const VxVector next = keys[i + 1].Pos - key.Pos;
        tangents[2 * i] = factors.incomingPrevious * previous + factors.incomingNext * next;
        tangents[2 * i + 1] = factors.outgoingPrevious * previous + factors.outgoingNext * next;
    }
    tangents[1] = ((keys[1].Pos - keys[0].Pos) * 3.0f - tangents[2]) * ((1.0f - keys[0].tension) * 0.5f);
    // The original last endpoint uses the previous INCOMING tangent and a
    // negative multiplier. Preserve this asymmetric rule (runtime verified).
    tangents[2 * (count - 1)] = ((keys[count - 2].Pos - keys[count - 1].Pos) * 3.0f - tangents[2 * (count - 2)]) *
                              (-(1.0f - keys[count - 1].tension) * 0.5f);
}

// 0x10051040: logarithmic quaternion differences with hemisphere correction,
// followed by weighted exponentials relative to the current key.
static void ComputeTCBQuaternionTangents(const CKTCBRotationKey *keys, int count, VxQuaternion *tangents) {
    for (int i = 0; i < count; ++i) {
        const auto &key = keys[i];
        VxQuaternion previous, next;
        if (i > 0) {
            VxQuaternion neighbor = keys[i - 1].Rot;
            if (DotProduct(neighbor, key.Rot) < 0.0f) neighbor = -neighbor;
            previous = LnDif(neighbor, key.Rot);
        }
        if (i + 1 < count) {
            VxQuaternion neighbor = keys[i + 1].Rot;
            if (DotProduct(neighbor, key.Rot) < 0.0f) neighbor = -neighbor;
            next = LnDif(key.Rot, neighbor);
        }
        if (i == 0) previous = next;
        if (i + 1 == count) next = previous;
        TCBTimeWeights weights;
        if (i > 0 && i + 1 < count)
            weights = ComputeTCBTimeWeights(keys[i - 1].TimeStep, key.TimeStep, keys[i + 1].TimeStep, key.continuity);
        const auto factors = ComputeTCBCoefficients(key.tension, key.continuity, key.bias, weights);
        const VxQuaternion incoming = 0.5f * ((1.0f - factors.incomingPrevious) * previous - factors.incomingNext * next);
        const VxQuaternion outgoing = 0.5f * (factors.outgoingPrevious * previous + (factors.outgoingNext - 1.0f) * next);
        tangents[2 * i] = key.Rot * Exp(incoming);
        tangents[2 * i + 1] = key.Rot * Exp(outgoing);
    }
}

//===================================================================
// CKKeyframeData Implementation
//===================================================================

CKKeyframeData::CKKeyframeData()
    : m_PositionController(nullptr),
      m_ScaleController(nullptr),
      m_RotationController(nullptr),
      m_ScaleAxisController(nullptr),
      m_MorphController(nullptr),
      m_Length(100.0f),
      m_RefCount(1),
      m_ObjectAnimation(nullptr) {}

CKKeyframeData::~CKKeyframeData() {
    delete m_PositionController;
    delete m_ScaleController;
    delete m_RotationController;
    delete m_ScaleAxisController;
    delete m_MorphController;
}

CKAnimController *CKKeyframeData::CreateController(CKANIMATION_CONTROLLER type) {
    CKAnimController *controller = nullptr;

    switch (type) {
    case CKANIMATION_CONTROLLER_POS:
    case CKANIMATION_LINPOS_CONTROL:
        controller = new RCKLinearPositionController();
        break;
    case CKANIMATION_CONTROLLER_ROT:
    case CKANIMATION_LINROT_CONTROL:
        controller = new RCKLinearRotationController();
        break;
    case CKANIMATION_CONTROLLER_SCL:
    case CKANIMATION_LINSCL_CONTROL:
        controller = new RCKLinearScaleController();
        break;
    case CKANIMATION_LINSCLAXIS_CONTROL:
        controller = new RCKLinearScaleAxisController();
        break;
    case CKANIMATION_TCBPOS_CONTROL:
        controller = new RCKTCBPositionController();
        break;
    case CKANIMATION_TCBROT_CONTROL:
        controller = new RCKTCBRotationController();
        break;
    case CKANIMATION_TCBSCL_CONTROL:
        controller = new RCKTCBScaleController();
        break;
    case CKANIMATION_TCBSCLAXIS_CONTROL:
        controller = new RCKTCBScaleAxisController();
        break;
    case CKANIMATION_BEZIERPOS_CONTROL:
        controller = new RCKBezierPositionController();
        break;
    case CKANIMATION_BEZIERSCL_CONTROL:
        controller = new RCKBezierScaleController();
        break;
    case CKANIMATION_CONTROLLER_MORPH:
    case CKANIMATION_MORPH_CONTROL:
        controller = new RCKMorphController();
        break;
    default:
        return nullptr;
    }

    if (controller) {
        controller->SetLength(m_Length);
    }

    return controller;
}

//===================================================================
// RCKLinearPositionController Implementation
//===================================================================

RCKLinearPositionController::RCKLinearPositionController()
    : CKAnimController(CKANIMATION_LINPOS_CONTROL), m_Keys(nullptr) {
    m_Length = 0.0f;
}

RCKLinearPositionController::~RCKLinearPositionController() {
    delete[] m_Keys;
}

CKBOOL RCKLinearPositionController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    // Before first key - return first key value
    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    // After last key - return last key value
    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    // 0x1004B8DF selects the segment ending at an exact interior key.
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    // Linear interpolation between keys[low] and keys[high]
    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    VxVector &p1 = m_Keys[low].Pos;
    VxVector &p2 = m_Keys[high].Pos;

    result->x = p1.x + (p2.x - p1.x) * t;
    result->y = p1.y + (p2.y - p1.y) * t;
    result->z = p1.z + (p2.z - p1.z) * t;

    return TRUE;
}

int RCKLinearPositionController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKPositionKey *posKey = static_cast<CKPositionKey *>(key);
    float time = posKey->TimeStep;

    // Find insertion position (maintain sorted order)
    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        // If key already exists at this time, update it
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Pos = posKey->Pos;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    // Allocate new array and copy existing keys
    ++m_NbKeys;
    CKPositionKey *newKeys = new CKPositionKey[m_NbKeys];

    if (m_Keys) {
        // Copy keys before insertion point
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKPositionKey));

        // Copy keys after insertion point
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKPositionKey));

        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *posKey;

    return insertIdx;
}

CKKey *RCKLinearPositionController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKLinearPositionController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKPositionKey *newKeys = new CKPositionKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKPositionKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKPositionKey));

    delete[] m_Keys;
    m_Keys = newKeys;
}

int RCKLinearPositionController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKPositionKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKPositionKey));
    }

    return size;
}

int RCKLinearPositionController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKPositionKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKPositionKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKPositionKey);
}

CKBOOL RCKLinearPositionController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKLinearPositionController *other = static_cast<RCKLinearPositionController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKLinearPositionController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKLinearPositionController *other = static_cast<RCKLinearPositionController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKPositionKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKPositionKey));
    }

    return TRUE;
}

//===================================================================
// RCKLinearRotationController Implementation
//===================================================================

RCKLinearRotationController::RCKLinearRotationController()
    : CKAnimController(CKANIMATION_LINROT_CONTROL), m_Keys(nullptr) {
    m_Length = 0.0f;
}

RCKLinearRotationController::~RCKLinearRotationController() {
    delete[] m_Keys;
}

CKBOOL RCKLinearRotationController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxQuaternion *result = static_cast<VxQuaternion *>(res);

    // Before first key
    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Rot;
        return TRUE;
    }

    // After last key
    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Rot;
        return TRUE;
    }

    // 0x1004C150 uses the preceding segment at exact interior keys. Slerp
    // can return the opposite quaternion sign from the stored right key.
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    // Spherical linear interpolation (Slerp) between quaternions
    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    *result = Slerp(t, m_Keys[low].Rot, m_Keys[high].Rot);

    return TRUE;
}

int RCKLinearRotationController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKRotationKey *rotKey = static_cast<CKRotationKey *>(key);
    float time = rotKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Rot = rotKey->Rot;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKRotationKey *newKeys = new CKRotationKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKRotationKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKRotationKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *rotKey;

    return insertIdx;
}

CKKey *RCKLinearRotationController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKLinearRotationController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKRotationKey *newKeys = new CKRotationKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKRotationKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKRotationKey));

    delete[] m_Keys;
    m_Keys = newKeys;
}

int RCKLinearRotationController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKRotationKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKRotationKey));
    }

    return size;
}

int RCKLinearRotationController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKRotationKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKRotationKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKRotationKey);
}

CKBOOL RCKLinearRotationController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKLinearRotationController *other = static_cast<RCKLinearRotationController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKLinearRotationController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKLinearRotationController *other = static_cast<RCKLinearRotationController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKRotationKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKRotationKey));
    }

    return TRUE;
}

//===================================================================
// RCKLinearScaleController Implementation
//===================================================================

RCKLinearScaleController::RCKLinearScaleController()
    : CKAnimController(CKANIMATION_LINSCL_CONTROL), m_Keys(nullptr) {
    m_Length = 0.0f;
}

RCKLinearScaleController::~RCKLinearScaleController() {
    delete[] m_Keys;
}

CKBOOL RCKLinearScaleController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2_time = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2_time - t1);

    VxVector &p1 = m_Keys[low].Pos;
    VxVector &p2 = m_Keys[high].Pos;

    result->x = p1.x + (p2.x - p1.x) * t;
    result->y = p1.y + (p2.y - p1.y) * t;
    result->z = p1.z + (p2.z - p1.z) * t;

    return TRUE;
}

int RCKLinearScaleController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKScaleKey *scaleKey = static_cast<CKScaleKey *>(key);
    float time = scaleKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Pos = scaleKey->Pos;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKScaleKey *newKeys = new CKScaleKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKScaleKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKScaleKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *scaleKey;

    return insertIdx;
}

CKKey *RCKLinearScaleController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKLinearScaleController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKScaleKey *newKeys = new CKScaleKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKScaleKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKScaleKey));

    delete[] m_Keys;
    m_Keys = newKeys;
}

int RCKLinearScaleController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKScaleKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKScaleKey));
    }

    return size;
}

int RCKLinearScaleController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKScaleKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKScaleKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKScaleKey);
}

CKBOOL RCKLinearScaleController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKLinearScaleController *other = static_cast<RCKLinearScaleController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKLinearScaleController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKLinearScaleController *other = static_cast<RCKLinearScaleController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKScaleKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKScaleKey));
    }

    return TRUE;
}

//===================================================================
// RCKLinearScaleAxisController Implementation
//===================================================================

RCKLinearScaleAxisController::RCKLinearScaleAxisController()
    : CKAnimController(CKANIMATION_LINSCLAXIS_CONTROL), m_Keys(nullptr) {
    m_Length = 0.0f;
}

RCKLinearScaleAxisController::~RCKLinearScaleAxisController() {
    delete[] m_Keys;
}

CKBOOL RCKLinearScaleAxisController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxQuaternion *result = static_cast<VxQuaternion *>(res);

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Rot;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Rot;
        return TRUE;
    }

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    *result = Slerp(t, m_Keys[low].Rot, m_Keys[high].Rot);

    return TRUE;
}

int RCKLinearScaleAxisController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKScaleAxisKey *scaleAxisKey = static_cast<CKScaleAxisKey *>(key);
    float time = scaleAxisKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Rot = scaleAxisKey->Rot;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKScaleAxisKey *newKeys = new CKScaleAxisKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKScaleAxisKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKScaleAxisKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *scaleAxisKey;

    return insertIdx;
}

CKKey *RCKLinearScaleAxisController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKLinearScaleAxisController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKScaleAxisKey *newKeys = new CKScaleAxisKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKScaleAxisKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKScaleAxisKey));

    delete[] m_Keys;
    m_Keys = newKeys;
}

int RCKLinearScaleAxisController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKScaleAxisKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKScaleAxisKey));
    }

    return size;
}

int RCKLinearScaleAxisController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKScaleAxisKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKScaleAxisKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKScaleAxisKey);
}

CKBOOL RCKLinearScaleAxisController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKLinearScaleAxisController *other = static_cast<RCKLinearScaleAxisController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKLinearScaleAxisController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKLinearScaleAxisController *other = static_cast<RCKLinearScaleAxisController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKScaleAxisKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKScaleAxisKey));
    }

    return TRUE;
}

//===================================================================
// RCKTCBPositionController Implementation
//===================================================================

RCKTCBPositionController::RCKTCBPositionController()
    : CKAnimController(CKANIMATION_TCBPOS_CONTROL), m_Keys(nullptr), m_Tangents(nullptr) {
    m_Length = 0.0f;
}

RCKTCBPositionController::~RCKTCBPositionController() {
    delete[] m_Keys;
    delete[] m_Tangents;
}

void RCKTCBPositionController::ComputeTangents() {
    delete[] m_Tangents;
    m_Tangents = nullptr;
    if (m_NbKeys < 2)
        return;
    m_Tangents = new VxVector[m_NbKeys * 2];
    ComputeTCBVectorTangents(m_Keys, m_NbKeys, m_Tangents);
}

CKBOOL RCKTCBPositionController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    // Compute tangents if not already computed
    if (!m_Tangents)
        ComputeTangents();

    // Before first key
    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    // After last key
    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    // Binary search for interval
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    // Hermite spline interpolation
    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    // Apply ease parameters
    t = ApplyEaseParameters(t, m_Keys[low].easefrom, m_Keys[high].easeto);

    // Hermite basis functions
    float t2_val = t * t;
    float t3 = t2_val * t;

    float h1 = 2.0f * t3 - 3.0f * t2_val + 1.0f; // (2t^3 - 3t^2 + 1)
    float h2 = -2.0f * t3 + 3.0f * t2_val;       // (-2t^3 + 3t^2)
    float h3 = t3 - 2.0f * t2_val + t;           // (t^3 - 2t^2 + t)
    float h4 = t3 - t2_val;                      // (t^3 - t^2)

    VxVector &p1 = m_Keys[low].Pos;
    VxVector &p2 = m_Keys[high].Pos;
    VxVector &m1 = m_Tangents[low * 2 + 1]; // outgoing tangent of low key
    VxVector &m2 = m_Tangents[high * 2];    // incoming tangent of high key

    result->x = h1 * p1.x + h2 * p2.x + h3 * m1.x + h4 * m2.x;
    result->y = h1 * p1.y + h2 * p2.y + h3 * m1.y + h4 * m2.y;
    result->z = h1 * p1.z + h2 * p2.z + h3 * m1.z + h4 * m2.z;

    return TRUE;
}

int RCKTCBPositionController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKTCBPositionKey *tcbKey = static_cast<CKTCBPositionKey *>(key);
    float time = tcbKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *tcbKey;
            // Invalidate tangents
            delete[] m_Tangents;
            m_Tangents = nullptr;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKTCBPositionKey *newKeys = new CKTCBPositionKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKTCBPositionKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKTCBPositionKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *tcbKey;

    // Invalidate tangents
    delete[] m_Tangents;
    m_Tangents = nullptr;

    return insertIdx;
}

CKKey *RCKTCBPositionController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBPositionController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }

    CKTCBPositionKey *newKeys = new CKTCBPositionKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKTCBPositionKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKTCBPositionKey));

    delete[] m_Keys;
    m_Keys = newKeys;

    delete[] m_Tangents;
    m_Tangents = nullptr;
}

int RCKTCBPositionController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKTCBPositionKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKTCBPositionKey));
    }

    return size;
}

int RCKTCBPositionController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKTCBPositionKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKTCBPositionKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKTCBPositionKey);
}

CKBOOL RCKTCBPositionController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKTCBPositionController *other = static_cast<RCKTCBPositionController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKTCBPositionController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKTCBPositionController *other = static_cast<RCKTCBPositionController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKTCBPositionKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKTCBPositionKey));
    }

    return TRUE;
}

//===================================================================
// RCKTCBRotationController Implementation
//===================================================================

RCKTCBRotationController::RCKTCBRotationController()
    : CKAnimController(CKANIMATION_TCBROT_CONTROL), m_Keys(nullptr), m_Tangents(nullptr) {
    m_Length = 0.0f;
}

RCKTCBRotationController::~RCKTCBRotationController() {
    delete[] m_Keys;
    delete[] m_Tangents;
}

void RCKTCBRotationController::ComputeTangents() {
    delete[] m_Tangents;
    m_Tangents = nullptr;
    if (m_NbKeys < 2)
        return;
    m_Tangents = new VxQuaternion[m_NbKeys * 2];
    ComputeTCBQuaternionTangents(m_Keys, m_NbKeys, m_Tangents);
}

CKBOOL RCKTCBRotationController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxQuaternion *result = static_cast<VxQuaternion *>(res);

    if (!m_Tangents)
        ComputeTangents();

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Rot;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Rot;
        return TRUE;
    }

    // At an interior key, the original evaluates the segment ending there.
    // Slerp can preserve a different quaternion sign on the adjacent segment.
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    // Apply ease parameters
    t = ApplyEaseParameters(t, m_Keys[low].easefrom, m_Keys[high].easeto);

    // Use Squad (spherical quadrangle) interpolation for smooth rotation
    VxQuaternion &q1 = m_Keys[low].Rot;
    VxQuaternion &q2 = m_Keys[high].Rot;

    // Squad interpolation
    *result = Squad(t, q1, m_Tangents[low * 2 + 1], m_Tangents[high * 2], q2);

    return TRUE;
}

int RCKTCBRotationController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKTCBRotationKey *tcbKey = static_cast<CKTCBRotationKey *>(key);
    float time = tcbKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *tcbKey;
            delete[] m_Tangents;
            m_Tangents = nullptr;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKTCBRotationKey *newKeys = new CKTCBRotationKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKTCBRotationKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKTCBRotationKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *tcbKey;

    delete[] m_Tangents;
    m_Tangents = nullptr;

    return insertIdx;
}

CKKey *RCKTCBRotationController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBRotationController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }

    CKTCBRotationKey *newKeys = new CKTCBRotationKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKTCBRotationKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKTCBRotationKey));

    delete[] m_Keys;
    m_Keys = newKeys;

    delete[] m_Tangents;
    m_Tangents = nullptr;
}

int RCKTCBRotationController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKTCBRotationKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKTCBRotationKey));
    }

    return size;
}

int RCKTCBRotationController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKTCBRotationKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKTCBRotationKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKTCBRotationKey);
}

CKBOOL RCKTCBRotationController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKTCBRotationController *other = static_cast<RCKTCBRotationController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKTCBRotationController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKTCBRotationController *other = static_cast<RCKTCBRotationController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKTCBRotationKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKTCBRotationKey));
    }

    return TRUE;
}

//===================================================================
// RCKTCBScaleController Implementation
//===================================================================

RCKTCBScaleController::RCKTCBScaleController()
    : CKAnimController(CKANIMATION_TCBSCL_CONTROL), m_Keys(nullptr), m_Tangents(nullptr) {
    m_Length = 0.0f;
}

RCKTCBScaleController::~RCKTCBScaleController() {
    delete[] m_Keys;
    delete[] m_Tangents;
}

void RCKTCBScaleController::ComputeTangents() {
    delete[] m_Tangents;
    m_Tangents = nullptr;
    if (m_NbKeys < 2)
        return;
    m_Tangents = new VxVector[m_NbKeys * 2];
    ComputeTCBVectorTangents(m_Keys, m_NbKeys, m_Tangents);
}

CKBOOL RCKTCBScaleController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    if (!m_Tangents)
        ComputeTangents();

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    t = ApplyEaseParameters(t, m_Keys[low].easefrom, m_Keys[high].easeto);

    float t2_val = t * t;
    float t3 = t2_val * t;

    float h1 = 2.0f * t3 - 3.0f * t2_val + 1.0f;
    float h2 = -2.0f * t3 + 3.0f * t2_val;
    float h3 = t3 - 2.0f * t2_val + t;
    float h4 = t3 - t2_val;

    VxVector &p1 = m_Keys[low].Pos;
    VxVector &p2 = m_Keys[high].Pos;
    VxVector &m1 = m_Tangents[low * 2 + 1];
    VxVector &m2 = m_Tangents[high * 2];

    result->x = h1 * p1.x + h2 * p2.x + h3 * m1.x + h4 * m2.x;
    result->y = h1 * p1.y + h2 * p2.y + h3 * m1.y + h4 * m2.y;
    result->z = h1 * p1.z + h2 * p2.z + h3 * m1.z + h4 * m2.z;

    return TRUE;
}

int RCKTCBScaleController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKTCBScaleKey *tcbKey = static_cast<CKTCBScaleKey *>(key);
    float time = tcbKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *tcbKey;
            delete[] m_Tangents;
            m_Tangents = nullptr;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKTCBScaleKey *newKeys = new CKTCBScaleKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKTCBScaleKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKTCBScaleKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *tcbKey;

    delete[] m_Tangents;
    m_Tangents = nullptr;

    return insertIdx;
}

CKKey *RCKTCBScaleController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBScaleController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }

    CKTCBScaleKey *newKeys = new CKTCBScaleKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKTCBScaleKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1],
               (m_NbKeys - index) * sizeof(CKTCBScaleKey));

    delete[] m_Keys;
    m_Keys = newKeys;

    delete[] m_Tangents;
    m_Tangents = nullptr;
}

int RCKTCBScaleController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKTCBScaleKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKTCBScaleKey));
    }

    return size;
}

int RCKTCBScaleController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKTCBScaleKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKTCBScaleKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKTCBScaleKey);
}

CKBOOL RCKTCBScaleController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKTCBScaleController *other = static_cast<RCKTCBScaleController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKTCBScaleController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKTCBScaleController *other = static_cast<RCKTCBScaleController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKTCBScaleKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKTCBScaleKey));
    }

    return TRUE;
}

//===================================================================
// RCKTCBScaleAxisController Implementation
//===================================================================

RCKTCBScaleAxisController::RCKTCBScaleAxisController()
    : CKAnimController(CKANIMATION_TCBSCLAXIS_CONTROL), m_Keys(nullptr), m_Tangents(nullptr) {
    m_Length = 0.0f;
}

RCKTCBScaleAxisController::~RCKTCBScaleAxisController() {
    delete[] m_Keys;
    delete[] m_Tangents;
}

void RCKTCBScaleAxisController::ComputeTangents() {
    delete[] m_Tangents;
    m_Tangents = nullptr;
    if (m_NbKeys < 2)
        return;
    m_Tangents = new VxQuaternion[m_NbKeys * 2];
    ComputeTCBQuaternionTangents(m_Keys, m_NbKeys, m_Tangents);
}

CKBOOL RCKTCBScaleAxisController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxQuaternion *result = static_cast<VxQuaternion *>(res);

    if (!m_Tangents)
        ComputeTangents();

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Rot;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Rot;
        return TRUE;
    }

    // At an interior key, the original evaluates the segment ending there.
    // Slerp can preserve a different quaternion sign on the adjacent segment.
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2 - t1);

    t = ApplyEaseParameters(t, m_Keys[low].easefrom, m_Keys[high].easeto);

    VxQuaternion &q1 = m_Keys[low].Rot;
    VxQuaternion &q2 = m_Keys[high].Rot;

    *result = Squad(t, q1, m_Tangents[low * 2 + 1], m_Tangents[high * 2], q2);

    return TRUE;
}

int RCKTCBScaleAxisController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKTCBScaleAxisKey *tcbKey = static_cast<CKTCBScaleAxisKey *>(key);
    float time = tcbKey->TimeStep;


    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *tcbKey;
            delete[] m_Tangents;
            m_Tangents = nullptr;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKTCBScaleAxisKey *newKeys = new CKTCBScaleAxisKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKTCBScaleAxisKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKTCBScaleAxisKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *tcbKey;

    delete[] m_Tangents;
    m_Tangents = nullptr;

    return insertIdx;
}

CKKey *RCKTCBScaleAxisController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBScaleAxisController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }

    CKTCBScaleAxisKey *newKeys = new CKTCBScaleAxisKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKTCBScaleAxisKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKTCBScaleAxisKey));

    delete[] m_Keys;
    m_Keys = newKeys;

    delete[] m_Tangents;
    m_Tangents = nullptr;
}

int RCKTCBScaleAxisController::DumpKeysTo(void *Buffer) {
    int size = sizeof(int) + m_NbKeys * sizeof(CKTCBScaleAxisKey);

    if (Buffer) {
        int *buf = static_cast<int *>(Buffer);
        *buf++ = m_NbKeys;
        memcpy(buf, m_Keys, m_NbKeys * sizeof(CKTCBScaleAxisKey));
    }

    return size;
}

int RCKTCBScaleAxisController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    int *buf = static_cast<int *>(Buffer);
    m_NbKeys = *buf++;

    if (m_NbKeys > 0) {
        m_Keys = new CKTCBScaleAxisKey[m_NbKeys];
        memcpy(m_Keys, buf, m_NbKeys * sizeof(CKTCBScaleAxisKey));
    }

    return sizeof(int) + m_NbKeys * sizeof(CKTCBScaleAxisKey);
}

CKBOOL RCKTCBScaleAxisController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKTCBScaleAxisController *other = static_cast<RCKTCBScaleAxisController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKTCBScaleAxisController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKTCBScaleAxisController *other = static_cast<RCKTCBScaleAxisController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKTCBScaleAxisKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKTCBScaleAxisKey));
    }

    return TRUE;
}

//===================================================================
// RCKBezierPositionController Implementation
//===================================================================

// 0x1004DFE5 measures time, including the wrap interval at either endpoint.
static float BezierKeyTimeSpan(const CKBezierPositionKey *keys, float length, int first, int second) {
    return second <= first ? keys[second].TimeStep + length - keys[first].TimeStep
                           : keys[second].TimeStep - keys[first].TimeStep;
}

// 0x1004E300 and 0x1004E5BD extrapolate endpoint derivatives. These helpers
// also serve linear keys whose neighboring tangent is not linear.
static void ComputeBezierForward(CKBezierPositionKey *keys, int count, float length, int index) {
    auto &key = keys[index];
    if (count == 1) {
        key.In = key.Out = VxVector(0, 0, 0);
        return;
    }
    float span = BezierKeyTimeSpan(keys, length, index, index + 1);
    if (span == 0.0f) span = 1.0f;
    const VxVector delta = keys[index + 1].Pos - key.Pos;
    if (index == count - 2) {
        key.Out = delta / span;
    } else {
        float nextSpan = BezierKeyTimeSpan(keys, length, index + 1, index + 2);
        if (nextSpan == 0.0f) nextSpan = 1.0f;
        const float ratio = span / (span + nextSpan);
        key.Out = ((ratio + 1.0f) / span) * delta -
                  (ratio / nextSpan) * (keys[index + 2].Pos - keys[index + 1].Pos);
    }
    if (index == 0) key.In = VxVector(0, 0, 0);
}

static void ComputeBezierBackward(CKBezierPositionKey *keys, int count, float length, int index) {
    auto &key = keys[index];
    if (count == 1) {
        key.In = key.Out = VxVector(0, 0, 0);
        return;
    }
    float span = BezierKeyTimeSpan(keys, length, index - 1, index);
    if (span == 0.0f) span = 1.0f;
    const VxVector delta = key.Pos - keys[index - 1].Pos;
    if (index == 1) {
        key.In = -delta / span;
    } else {
        float previousSpan = BezierKeyTimeSpan(keys, length, index - 2, index - 1);
        if (previousSpan == 0.0f) previousSpan = 1.0f;
        const float ratio = previousSpan / (previousSpan + span);
        key.In = -(((ratio - 1.0f) / previousSpan) * (keys[index - 1].Pos - keys[index - 2].Pos) +
                   ((2.0f - ratio) / span) * delta);
    }
    if (index == count - 1) key.Out = VxVector(0, 0, 0);
}

// 0x1004E891 only computes sides marked AutoSmooth. If the opposite side has
// another mode, smoothing follows that side's already computed derivative.
static void ComputeBezierSmooth(CKBezierPositionKey *keys, int count, float length, int index) {
    auto &key = keys[index];
    const auto inMode = key.Flags.GetInTangentMode();
    const auto outMode = key.Flags.GetOutTangentMode();
    const bool smoothIn = inMode == BEZIER_KEY_AUTOSMOOTH;
    const bool smoothOut = outMode == BEZIER_KEY_AUTOSMOOTH;
    if (!smoothIn && !smoothOut) return;
    const int previous = (index + count - 1) % count;
    const int next = (index + 1) % count;
    const float previousSpan = BezierKeyTimeSpan(keys, length, previous, index);
    const float nextSpan = BezierKeyTimeSpan(keys, length, index, next);
    if (previousSpan == 0.0f || nextSpan == 0.0f || index == 0 || index == count - 1) {
        if (smoothOut) {
            if (index == 0) ComputeBezierForward(keys, count, length, index);
            else key.Out = VxVector(0, 0, 0);
        }
        if (smoothIn) {
            if (index == count - 1) ComputeBezierBackward(keys, count, length, index);
            else key.In = VxVector(0, 0, 0);
        }
    } else if (smoothIn && smoothOut) {
        const float ratio = previousSpan / (previousSpan + nextSpan);
        const VxVector tangent = ((1.0f - ratio) / previousSpan) * (key.Pos - keys[previous].Pos) +
                                 (ratio / nextSpan) * (keys[next].Pos - key.Pos);
        key.In = -tangent;
        key.Out = tangent;
    } else {
        const auto otherMode = smoothIn ? outMode : inMode;
        VxVector &tangent = smoothIn ? key.In : key.Out;
        switch (otherMode) {
        case BEZIER_KEY_LINEAR:
        case BEZIER_KEY_FAST:
        case BEZIER_KEY_SLOW:
        case BEZIER_KEY_TANGENTS:
            tangent = -(smoothIn ? key.Out : key.In);
            break;
        case BEZIER_KEY_STEP:
            tangent = VxVector(0, 0, 0);
            break;
        default:
            break;
        }
    }
}

// 0x1004ED59 is shared by position and scale. Mode dispatch precedes the
// AutoSmooth pass for each key, preserving the original endpoint side effects.
static void ComputeBezierTangents(CKBezierPositionKey *keys, int count, float length) {
    for (int i = 0; i < count; ++i) {
        auto &key = keys[i];
        const auto inMode = key.Flags.GetInTangentMode();
        const auto outMode = key.Flags.GetOutTangentMode();
        const int previous = (i + count - 1) % count;
        const int next = (i + 1) % count;
        switch (inMode) {
        case BEZIER_KEY_LINEAR:
        case BEZIER_KEY_FAST:
            if (inMode == BEZIER_KEY_FAST || i == 0 || keys[previous].Flags.GetOutTangentMode() == BEZIER_KEY_LINEAR) {
                float span = BezierKeyTimeSpan(keys, length, previous, i);
                if (span == 0.0f) span = 1.0f;
                const float scale = (inMode == BEZIER_KEY_FAST ? 2.0f : 1.0f) / span;
                key.In = scale * (keys[previous].Pos - key.Pos);
            } else {
                ComputeBezierBackward(keys, count, length, i);
            }
            break;
        case BEZIER_KEY_STEP:
        case BEZIER_KEY_SLOW:
            key.In = VxVector(0, 0, 0);
            break;
        default:
            break;
        }
        switch (outMode) {
        case BEZIER_KEY_LINEAR:
        case BEZIER_KEY_FAST:
            if (outMode == BEZIER_KEY_FAST || i == count - 1 || keys[next].Flags.GetInTangentMode() == BEZIER_KEY_LINEAR) {
                float span = BezierKeyTimeSpan(keys, length, i, next);
                if (span == 0.0f) span = 1.0f;
                const float scale = (outMode == BEZIER_KEY_FAST ? 2.0f : 1.0f) / span;
                key.Out = scale * (keys[next].Pos - key.Pos);
            } else {
                ComputeBezierForward(keys, count, length, i);
            }
            break;
        case BEZIER_KEY_STEP:
        case BEZIER_KEY_SLOW:
            key.Out = VxVector(0, 0, 0);
            break;
        default:
            break;
        }
        if (inMode == BEZIER_KEY_AUTOSMOOTH || outMode == BEZIER_KEY_AUTOSMOOTH)
            ComputeBezierSmooth(keys, count, length, i);
    }
}

RCKBezierPositionController::RCKBezierPositionController()
    : CKAnimController(CKANIMATION_BEZIERPOS_CONTROL), m_Keys(nullptr), m_TangentsComputed(FALSE) {
    m_Length = 0.0f;
}

RCKBezierPositionController::~RCKBezierPositionController() {
    delete[] m_Keys;
}

float RCKBezierPositionController::ComputeKeyDistance(int key1, int key2) {
    if (key1 < 0 || key1 >= m_NbKeys || key2 < 0 || key2 >= m_NbKeys)
        return 0.0f;
    return BezierKeyTimeSpan(m_Keys, m_Length, key1, key2);
}

void RCKBezierPositionController::ComputeBezierPts() {
    ComputeBezierTangents(m_Keys, m_NbKeys, m_Length);
    m_TangentsComputed = TRUE;
}

void RCKBezierPositionController::ComputeBezierPts(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;
    ComputeBezierSmooth(m_Keys, m_NbKeys, m_Length, index);
}

CKBOOL RCKBezierPositionController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    if (!m_TangentsComputed)
        ComputeBezierPts();

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    // Binary search
    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2_time = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2_time - t1);

    if (m_Keys[low].Flags.GetOutTangentMode() == BEZIER_KEY_STEP ||
        m_Keys[high].Flags.GetInTangentMode() == BEZIER_KEY_STEP) {
        *result = t == 1.0f ? m_Keys[high].Pos : m_Keys[low].Pos;
        return TRUE;
    }

    // Stored tangents are derivatives. The original constant is 0x3EAAAAAA.
    const float handleScale = (t2_time - t1) * 0.33333331f;
    VxVector &p0 = m_Keys[low].Pos;
    VxVector &p3 = m_Keys[high].Pos;
    VxVector p1 = p0 + m_Keys[low].Out * handleScale;
    VxVector p2 = p3 + m_Keys[high].In * handleScale;

    float omt = 1.0f - t;
    float omt2 = omt * omt;
    float omt3 = omt2 * omt;
    float t2 = t * t;
    float t3 = t2 * t;

    result->x = omt3 * p0.x + 3.0f * omt2 * t * p1.x + 3.0f * omt * t2 * p2.x + t3 * p3.x;
    result->y = omt3 * p0.y + 3.0f * omt2 * t * p1.y + 3.0f * omt * t2 * p2.y + t3 * p3.y;
    result->z = omt3 * p0.z + 3.0f * omt2 * t * p1.z + 3.0f * omt * t2 * p2.z + t3 * p3.z;

    return TRUE;
}

int RCKBezierPositionController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKBezierPositionKey *bezKey = static_cast<CKBezierPositionKey *>(key);
    float time = bezKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *bezKey;
            m_TangentsComputed = FALSE;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKBezierPositionKey *newKeys = new CKBezierPositionKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKBezierPositionKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKBezierPositionKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *bezKey;
    m_TangentsComputed = FALSE;

    return insertIdx;
}

CKKey *RCKBezierPositionController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKBezierPositionController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKBezierPositionKey *newKeys = new CKBezierPositionKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKBezierPositionKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKBezierPositionKey));

    delete[] m_Keys;
    m_Keys = newKeys;
    m_TangentsComputed = FALSE;
}

// Position and scale share these routines in the original DLL (0x1004F4BE,
// 0x1004F65A). Each key has a 20-byte prefix and only explicit tangent vectors.
static int DumpBezierKeys(CKBezierPositionKey *keys, int count, void *buffer) {
    int size = sizeof(int);
    char *bytes = static_cast<char *>(buffer);
    if (bytes) memcpy(bytes, &count, sizeof(count));
    for (int i = 0; i < count; ++i) {
        if (bytes) memcpy(bytes + size, &keys[i], 20);
        size += 20;
        if (keys[i].Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) {
            if (bytes) memcpy(bytes + size, &keys[i].In, sizeof(VxVector));
            size += sizeof(VxVector);
        }
        if (keys[i].Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) {
            if (bytes) memcpy(bytes + size, &keys[i].Out, sizeof(VxVector));
            size += sizeof(VxVector);
        }
    }
    return size;
}

static int ReadBezierKeys(CKBezierPositionKey *&keys, int &count, void *buffer) {
    const char *bytes = static_cast<const char *>(buffer);
    delete[] keys;
    keys = nullptr;
    memcpy(&count, bytes, sizeof(count));
    int size = sizeof(int);
    if (count > 0) keys = new CKBezierPositionKey[count];
    for (int i = 0; i < count; ++i) {
        memcpy(&keys[i], bytes + size, 20);
        size += 20;
        if (keys[i].Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(&keys[i].In, bytes + size, sizeof(VxVector));
            size += sizeof(VxVector);
        }
        if (keys[i].Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(&keys[i].Out, bytes + size, sizeof(VxVector));
            size += sizeof(VxVector);
        }
    }
    return size;
}

int RCKBezierPositionController::DumpKeysTo(void *Buffer) {
    return DumpBezierKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKBezierPositionController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;
    m_TangentsComputed = FALSE;
    return ReadBezierKeys(m_Keys, m_NbKeys, Buffer);
}

CKBOOL RCKBezierPositionController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKBezierPositionController *other = static_cast<RCKBezierPositionController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKBezierPositionController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKBezierPositionController *other = static_cast<RCKBezierPositionController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    m_TangentsComputed = FALSE;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKBezierPositionKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKBezierPositionKey));
    }

    return TRUE;
}

//===================================================================
// RCKBezierScaleController Implementation
//===================================================================

RCKBezierScaleController::RCKBezierScaleController()
    : CKAnimController(CKANIMATION_BEZIERSCL_CONTROL), m_Keys(nullptr), m_TangentsComputed(FALSE) {
    m_Length = 0.0f;
}

RCKBezierScaleController::~RCKBezierScaleController() {
    delete[] m_Keys;
}

float RCKBezierScaleController::ComputeKeyDistance(int key1, int key2) {
    if (key1 < 0 || key1 >= m_NbKeys || key2 < 0 || key2 >= m_NbKeys)
        return 0.0f;
    return BezierKeyTimeSpan(m_Keys, m_Length, key1, key2);
}

void RCKBezierScaleController::ComputeBezierPts() {
    ComputeBezierTangents(m_Keys, m_NbKeys, m_Length);
    m_TangentsComputed = TRUE;
}

void RCKBezierScaleController::ComputeBezierPts(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;
    ComputeBezierSmooth(m_Keys, m_NbKeys, m_Length, index);
}

CKBOOL RCKBezierScaleController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

    if (!m_TangentsComputed)
        ComputeBezierPts();

    if (TimeStep <= m_Keys[0].TimeStep) {
        *result = m_Keys[0].Pos;
        return TRUE;
    }

    if (TimeStep >= m_Keys[m_NbKeys - 1].TimeStep) {
        *result = m_Keys[m_NbKeys - 1].Pos;
        return TRUE;
    }

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = (low + high) >> 1;
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2_time = m_Keys[high].TimeStep;
    float t = (TimeStep - t1) / (t2_time - t1);

    if (m_Keys[low].Flags.GetOutTangentMode() == BEZIER_KEY_STEP ||
        m_Keys[high].Flags.GetInTangentMode() == BEZIER_KEY_STEP) {
        *result = t == 1.0f ? m_Keys[high].Pos : m_Keys[low].Pos;
        return TRUE;
    }

    // Stored tangents are derivatives. The original constant is 0x3EAAAAAA.
    const float handleScale = (t2_time - t1) * 0.33333331f;
    VxVector &p0 = m_Keys[low].Pos;
    VxVector &p3 = m_Keys[high].Pos;
    VxVector p1 = p0 + m_Keys[low].Out * handleScale;
    VxVector p2 = p3 + m_Keys[high].In * handleScale;

    float omt = 1.0f - t;
    float omt2 = omt * omt;
    float omt3 = omt2 * omt;
    float t2 = t * t;
    float t3 = t2 * t;

    result->x = omt3 * p0.x + 3.0f * omt2 * t * p1.x + 3.0f * omt * t2 * p2.x + t3 * p3.x;
    result->y = omt3 * p0.y + 3.0f * omt2 * t * p1.y + 3.0f * omt * t2 * p2.y + t3 * p3.y;
    result->z = omt3 * p0.z + 3.0f * omt2 * t * p1.z + 3.0f * omt * t2 * p2.z + t3 * p3.z;

    return TRUE;
}

int RCKBezierScaleController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    CKBezierScaleKey *bezKey = static_cast<CKBezierScaleKey *>(key);
    float time = bezKey->TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i] = *bezKey;
            m_TangentsComputed = FALSE;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKBezierScaleKey *newKeys = new CKBezierScaleKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKBezierScaleKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKBezierScaleKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    m_Keys[insertIdx] = *bezKey;
    m_TangentsComputed = FALSE;

    return insertIdx;
}

CKKey *RCKBezierScaleController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKBezierScaleController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKBezierScaleKey *newKeys = new CKBezierScaleKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKBezierScaleKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKBezierScaleKey));

    delete[] m_Keys;
    m_Keys = newKeys;
    m_TangentsComputed = FALSE;
}

int RCKBezierScaleController::DumpKeysTo(void *Buffer) {
    return DumpBezierKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKBezierScaleController::ReadKeysFrom(void *Buffer) {
    if (!Buffer)
        return 0;

    m_TangentsComputed = FALSE;
    return ReadBezierKeys(m_Keys, m_NbKeys, Buffer);
}

CKBOOL RCKBezierScaleController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKBezierScaleController *other = static_cast<RCKBezierScaleController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKBezierScaleController::Clone(CKAnimController *control) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (!CKAnimController::Clone(control))
        return FALSE;

    RCKBezierScaleController *other = static_cast<RCKBezierScaleController *>(control);

    delete[] m_Keys;
    m_Keys = nullptr;
    m_TangentsComputed = FALSE;

    if (other->m_NbKeys > 0) {
        m_Keys = new CKBezierScaleKey[other->m_NbKeys];
        memcpy(m_Keys, other->m_Keys, other->m_NbKeys * sizeof(CKBezierScaleKey));
    }

    return TRUE;
}

//===================================================================
// RCKMorphController Implementation
//===================================================================

RCKMorphController::RCKMorphController()
    : CKMorphController(), m_Keys(nullptr), m_VertexCount(0) {
    m_Length = 0.0f;
}

RCKMorphController::~RCKMorphController() {
    // Clean up all keys and their allocated data
    for (int i = 0; i < m_NbKeys; ++i) {
        delete[] m_Keys[i].PosArray;
        delete[] m_Keys[i].NormArray;
    }
    delete[] m_Keys;
}

CKBOOL RCKMorphController::Evaluate(float TimeStep, void *res) {
    // For morph controller, use the full Evaluate with vertex data
    return FALSE;
}

CKBOOL RCKMorphController::Evaluate(float TimeStep, int VertexCount, void *VertexPtr,
                                    CKDWORD VertexStride, VxCompressedVector *NormalPtr) {
    if (m_NbKeys <= 0 || !m_Keys || VertexCount < 0 || VertexCount > m_VertexCount || !std::isfinite(TimeStep))
        return FALSE;

    // Native 0x1004FF81 assumes ordered finite times and a valid request count.
    // Reject invalid state before writing either output, including when an
    // endpoint would otherwise conceal a malformed later key.
    for (int i = 0; i < m_NbKeys; ++i) {
        if (!std::isfinite(m_Keys[i].TimeStep) || (i > 0 && m_Keys[i].TimeStep < m_Keys[i - 1].TimeStep))
            return FALSE;
    }
    if (VertexCount == 0)
        return TRUE;

    // Find the key index
    int keyIdx = -1;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep >= TimeStep) {
            keyIdx = i;
            break;
        }
    }

    if (keyIdx < 0) {
        // Past last key - use last key
        keyIdx = m_NbKeys - 1;
        CKMorphKey &key = m_Keys[keyIdx];

        if (VertexPtr && key.PosArray) {
            if (VertexStride == sizeof(VxVector)) {
                memcpy(VertexPtr, key.PosArray, VertexCount * sizeof(VxVector));
            } else {
                VxCopyStructure(VertexCount, VertexPtr, VertexStride, sizeof(VxVector), key.PosArray, sizeof(VxVector));
            }
        }

        if (NormalPtr && key.NormArray) {
            memcpy(NormalPtr, key.NormArray, VertexCount * sizeof(VxCompressedVector));
        }

        return TRUE;
    }

    if (keyIdx == 0) {
        // Before or at first key
        CKMorphKey &key = m_Keys[0];

        if (VertexPtr && key.PosArray) {
            if (VertexStride == sizeof(VxVector)) {
                memcpy(VertexPtr, key.PosArray, VertexCount * sizeof(VxVector));
            } else {
                VxCopyStructure(VertexCount, VertexPtr, VertexStride, sizeof(VxVector), key.PosArray, sizeof(VxVector));
            }
        }

        if (NormalPtr && key.NormArray) {
            memcpy(NormalPtr, key.NormArray, VertexCount * sizeof(VxCompressedVector));
        }

        return TRUE;
    }

    // Interpolate between keys
    CKMorphKey &key1 = m_Keys[keyIdx - 1];
    CKMorphKey &key2 = m_Keys[keyIdx];

    float t1 = key1.TimeStep;
    float t2 = key2.TimeStep;
    const double interval = static_cast<double>(t2) - t1;
    const double t = (static_cast<double>(TimeStep) - t1) / interval;
    const double leftWeight = (static_cast<double>(t2) - TimeStep) / interval;

    if (VertexPtr && key1.PosArray && key2.PosArray) {
        char *destPtr = static_cast<char *>(VertexPtr);
        for (int i = 0; i < VertexCount; ++i) {
            VxVector *dest = reinterpret_cast<VxVector *>(destPtr);
            VxVector &p1 = key1.PosArray[i];
            VxVector &p2 = key2.PosArray[i];

            // Independent wide weights avoid overflowing float differences,
            // cancellation at the right endpoint, and loss of a tiny phase
            // that still makes a representable contribution.
            dest->x = static_cast<float>(p1.x * leftWeight + p2.x * t);
            dest->y = static_cast<float>(p1.y * leftWeight + p2.y * t);
            dest->z = static_cast<float>(p1.z * leftWeight + p2.z * t);

            destPtr += VertexStride;
        }
    }

    if (NormalPtr && key1.NormArray && key2.NormArray) {
        // CK2_3D.dll 0x10051510 wraps the angular component and uses a
        // 16-bit fixed-point coefficient, including its rounding behavior.
        for (int i = 0; i < VertexCount; ++i) {
            NormalPtr[i].Slerp(static_cast<float>(t), key1.NormArray[i], key2.NormArray[i]);
        }
    }

    return TRUE;
}

int RCKMorphController::AddKey(float TimeStep, CKBOOL AllocateNormals) {
    CKMorphKey newKey;
    newKey.TimeStep = TimeStep;
    newKey.PosArray = nullptr;
    newKey.NormArray = nullptr;

    return AddKey(&newKey, AllocateNormals);
}

int RCKMorphController::AddKey(CKKey *key, CKBOOL AllocateNormals) {
    if (!key)
        return -1;

    const CKMorphKey source = *static_cast<CKMorphKey *>(key);
    const float time = source.TimeStep;

    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            // 0x1004F9FD returns the existing index without changing the key,
            // even when the caller requests a different normal allocation.
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }

    ++m_NbKeys;
    CKMorphKey *newKeys = new CKMorphKey[m_NbKeys];

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, insertIdx * sizeof(CKMorphKey));
        if (insertIdx < m_NbKeys - 1)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], (m_NbKeys - 1 - insertIdx) * sizeof(CKMorphKey));
        delete[] m_Keys;
    }

    m_Keys = newKeys;
    CKMorphKey &inserted = m_Keys[insertIdx];
    inserted.TimeStep = time;
    // The controller owns independent arrays. Source normals imply storage
    // even when AllocateNormals is false; zero-sized arrays retain presence.
    inserted.PosArray = new VxVector[m_VertexCount];
    inserted.NormArray = (AllocateNormals || source.NormArray) ? new VxCompressedVector[m_VertexCount] : nullptr;
    if (source.PosArray && m_VertexCount > 0)
        memcpy(inserted.PosArray, source.PosArray, m_VertexCount * sizeof(VxVector));
    if (source.NormArray && m_VertexCount > 0)
        memcpy(inserted.NormArray, source.NormArray, m_VertexCount * sizeof(VxCompressedVector));

    return insertIdx;
}

CKKey *RCKMorphController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKMorphController::RemoveKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return;

    // Free memory for the key being removed
    delete[] m_Keys[index].PosArray;
    delete[] m_Keys[index].NormArray;

    --m_NbKeys;
    if (m_NbKeys == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        return;
    }

    CKMorphKey *newKeys = new CKMorphKey[m_NbKeys];

    if (index > 0)
        memcpy(newKeys, m_Keys, index * sizeof(CKMorphKey));
    if (index < m_NbKeys)
        memcpy(&newKeys[index], &m_Keys[index + 1], (m_NbKeys - index) * sizeof(CKMorphKey));

    delete[] m_Keys;
    m_Keys = newKeys;
}

static int MorphWireSize(int keyCount, int vertexCount, CKBOOL normals) {
    if (keyCount < 0 || vertexCount < 0)
        return 0;
    const int headerBytes = 3 * sizeof(int);
    if (keyCount == 0)
        return headerBytes;
    const size_t maxKeyBytes = (INT_MAX - headerBytes) / keyCount;
    const size_t vertexBytes = sizeof(VxVector) + (normals ? sizeof(VxCompressedVector) : 0);
    if (maxKeyBytes < sizeof(float) || static_cast<size_t>(vertexCount) >
        (maxKeyBytes - sizeof(float)) / vertexBytes)
        return 0;
    return headerBytes + keyCount * static_cast<int>(sizeof(float) + vertexCount * vertexBytes);
}

int RCKMorphController::DumpKeysTo(void *Buffer) {
    // 0x100505D9: normal presence is shared by all keys and follows the two
    // counts. Even an empty controller writes this complete three-DWORD header.
    const CKBOOL hasNormals = m_NbKeys > 0 && m_Keys[0].NormArray != nullptr;
    const int size = sizeof(int) * 3 + m_NbKeys * (sizeof(float) +
        m_VertexCount * (sizeof(VxVector) + (hasNormals ? sizeof(VxCompressedVector) : 0)));

    if (Buffer) {
        char *buf = static_cast<char *>(Buffer);

        *reinterpret_cast<int *>(buf) = m_NbKeys;
        buf += sizeof(int);

        *reinterpret_cast<int *>(buf) = m_VertexCount;
        buf += sizeof(int);

        *reinterpret_cast<CKBOOL *>(buf) = hasNormals;
        buf += sizeof(CKBOOL);

        for (int i = 0; i < m_NbKeys; ++i) {
            *reinterpret_cast<float *>(buf) = m_Keys[i].TimeStep;
            buf += sizeof(float);

            if (m_VertexCount > 0) {
                memcpy(buf, m_Keys[i].PosArray, m_VertexCount * sizeof(VxVector));
                buf += m_VertexCount * sizeof(VxVector);
            }

            if (hasNormals && m_VertexCount > 0) {
                memcpy(buf, m_Keys[i].NormArray, m_VertexCount * sizeof(VxCompressedVector));
                buf += m_VertexCount * sizeof(VxCompressedVector);
            }
        }
    }

    return size;
}

int RCKMorphController::ReadKeysFrom(void *Buffer) {
    // This legacy API has no capacity argument; callers must supply a complete
    // buffer. ObjectAnimation uses the bounded internal entry below.
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKMorphController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    if (!Buffer || BufferSize < static_cast<int>(3 * sizeof(int)))
        return 0;
    int header[3];
    memcpy(header, Buffer, sizeof(header));
    const int size = MorphWireSize(header[0], header[1], header[2] != 0);
    if (!size || size > BufferSize || static_cast<size_t>(header[0]) >
        static_cast<size_t>(-1) / sizeof(CKMorphKey))
        return 0;
    const size_t positionBytes = static_cast<size_t>(header[1]) * sizeof(VxVector);
    const size_t normalBytes = header[2] ? static_cast<size_t>(header[1]) * sizeof(VxCompressedVector) : 0;
    const size_t keyBytes = sizeof(float) + positionBytes + normalBytes;
    const char *buf = static_cast<const char *>(Buffer) + sizeof(header);
    float previousTime = 0.0f;
    for (int i = 0; i < header[0]; ++i) {
        float time;
        memcpy(&time, buf + i * keyBytes, sizeof(time));
        if (!std::isfinite(time) || (i > 0 && time < previousTime))
            return 0;
        previousTime = time;
    }

    RCKMorphController replacement;
    replacement.m_VertexCount = header[1];
    if (header[0] > 0) {
        replacement.m_Keys = new(std::nothrow) CKMorphKey[header[0]];
        if (!replacement.m_Keys)
            return 0;
        for (int i = 0; i < header[0]; ++i) {
            replacement.m_Keys[i].PosArray = nullptr;
            replacement.m_Keys[i].NormArray = nullptr;
        }
        replacement.m_NbKeys = header[0];
        for (int i = 0; i < header[0]; ++i) {
            CKMorphKey &key = replacement.m_Keys[i];
            memcpy(&key.TimeStep, buf, sizeof(float));
            buf += sizeof(float);
            key.PosArray = new(std::nothrow) VxVector[header[1]];
            if (!key.PosArray)
                return 0;
            if (positionBytes)
                memcpy(key.PosArray, buf, positionBytes);
            buf += positionBytes;
            if (header[2]) {
                key.NormArray = new(std::nothrow) VxCompressedVector[header[1]];
                if (!key.NormArray)
                    return 0;
                if (normalBytes)
                    memcpy(key.NormArray, buf, normalBytes);
                buf += normalBytes;
            }
        }
    }

    CKMorphKey *oldKeys = m_Keys;
    const int oldCount = m_NbKeys;
    m_Keys = replacement.m_Keys;
    m_NbKeys = replacement.m_NbKeys;
    m_VertexCount = replacement.m_VertexCount;
    replacement.m_Keys = oldKeys;
    replacement.m_NbKeys = oldCount;
    // Keep this controller's length, as the native raw reader does.
    return size;
}

CKBOOL RCKMorphController::Compare(CKAnimController *control, float Threshold) {
    if (!control || control->GetType() != m_Type)
        return FALSE;

    RCKMorphController *other = static_cast<RCKMorphController *>(control);
    if (m_NbKeys != other->m_NbKeys)
        return FALSE;
    if (m_VertexCount != other->m_VertexCount)
        return FALSE;

    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].Compare(other->m_Keys[i], m_VertexCount, Threshold))
            return FALSE;
    }

    return TRUE;
}

CKBOOL RCKMorphController::Clone(CKAnimController *control) {
    // Native 0x1005027A releases destination storage first. Preserve valid deep
    // copy semantics while making self-copy and allocation failure harmless.
    if (!control || control->GetType() != m_Type)
        return FALSE;

    if (control == this)
        return TRUE;

    RCKMorphController *other = static_cast<RCKMorphController *>(control);
    if (!MorphWireSize(other->m_NbKeys, other->m_VertexCount, TRUE) ||
        (other->m_NbKeys > 0 && !other->m_Keys) || static_cast<size_t>(other->m_NbKeys) >
        static_cast<size_t>(-1) / sizeof(CKMorphKey))
        return FALSE;
    RCKMorphController replacement;
    replacement.m_VertexCount = other->m_VertexCount;
    if (other->m_NbKeys > 0) {
        replacement.m_Keys = new(std::nothrow) CKMorphKey[other->m_NbKeys];
        if (!replacement.m_Keys)
            return FALSE;
        for (int i = 0; i < other->m_NbKeys; ++i) {
            replacement.m_Keys[i].PosArray = nullptr;
            replacement.m_Keys[i].NormArray = nullptr;
        }
        replacement.m_NbKeys = other->m_NbKeys;
        for (int i = 0; i < other->m_NbKeys; ++i) {
            CKMorphKey &key = replacement.m_Keys[i];
            key.TimeStep = other->m_Keys[i].TimeStep;

            // Array presence is observable even with zero vertices (notably
            // the serialized normal flag), so retain zero-sized allocations.
            if (other->m_Keys[i].PosArray) {
                key.PosArray = new(std::nothrow) VxVector[replacement.m_VertexCount];
                if (!key.PosArray)
                    return FALSE;
                if (replacement.m_VertexCount > 0)
                    memcpy(key.PosArray, other->m_Keys[i].PosArray, replacement.m_VertexCount * sizeof(VxVector));
            }

            if (other->m_Keys[i].NormArray) {
                key.NormArray = new(std::nothrow) VxCompressedVector[replacement.m_VertexCount];
                if (!key.NormArray)
                    return FALSE;
                if (replacement.m_VertexCount > 0)
                    memcpy(key.NormArray, other->m_Keys[i].NormArray, replacement.m_VertexCount * sizeof(VxCompressedVector));
            }
        }
    }

    CKMorphKey *oldKeys = m_Keys;
    const int oldCount = m_NbKeys;
    m_Keys = replacement.m_Keys;
    m_NbKeys = replacement.m_NbKeys;
    m_VertexCount = replacement.m_VertexCount;
    m_Length = other->m_Length;
    replacement.m_Keys = oldKeys;
    replacement.m_NbKeys = oldCount;

    return TRUE;
}

void RCKMorphController::SetMorphVertexCount(int count) {
    // Native 0x10052560 only changes the count. Keep owned payloads coherent
    // instead: count changes invalidate payload pointers, but retain key objects.
    if (count < 0 || count == m_VertexCount || m_VertexCount < 0 || m_NbKeys < 0)
        return;
    // Reserve room for normal-bearing wire keys, including at least one future
    // key in an empty controller. DumpKeysTo returns a signed-int byte count.
    const int keys = m_NbKeys > 0 ? m_NbKeys : 1;
    const size_t maxKeyBytes = (INT_MAX - 3 * sizeof(int)) / keys;
    if (maxKeyBytes < sizeof(float) || static_cast<size_t>(count) >
        (maxKeyBytes - sizeof(float)) / (sizeof(VxVector) + sizeof(VxCompressedVector)))
        return;
    if (m_NbKeys == 0) {
        m_VertexCount = count;
        return;
    }
    if (!m_Keys)
        return;

    CKMorphKey *resized = new(std::nothrow) CKMorphKey[m_NbKeys];
    if (!resized)
        return;
    for (int i = 0; i < m_NbKeys; ++i) {
        resized[i].PosArray = nullptr;
        resized[i].NormArray = nullptr;
    }
    const int retained = count < m_VertexCount ? count : m_VertexCount;
    CKBOOL allocated = TRUE;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].PosArray) {
            resized[i].PosArray = new(std::nothrow) VxVector[count];
            if (!resized[i].PosArray) { allocated = FALSE; break; }
            if (retained > 0)
                memcpy(resized[i].PosArray, m_Keys[i].PosArray, retained * sizeof(VxVector));
            for (int vertex = retained; vertex < count; ++vertex)
                resized[i].PosArray[vertex].Set(0.0f, 0.0f, 0.0f);
        }
        if (m_Keys[i].NormArray) {
            resized[i].NormArray = new(std::nothrow) VxCompressedVector[count];
            if (!resized[i].NormArray) { allocated = FALSE; break; }
            if (retained > 0)
                memcpy(resized[i].NormArray, m_Keys[i].NormArray, retained * sizeof(VxCompressedVector));
            for (int vertex = retained; vertex < count; ++vertex) {
                resized[i].NormArray[vertex].xa = 0;
                resized[i].NormArray[vertex].ya = 0;
            }
        }
    }
    if (allocated) {
        // Commit only after every replacement is ready. Array presence, key
        // addresses, times and controller length remain unchanged, even at zero.
        for (int i = 0; i < m_NbKeys; ++i) {
            delete[] m_Keys[i].PosArray;
            delete[] m_Keys[i].NormArray;
            m_Keys[i].PosArray = resized[i].PosArray;
            m_Keys[i].NormArray = resized[i].NormArray;
        }
        m_VertexCount = count;
    } else {
        for (int i = 0; i < m_NbKeys; ++i) {
            delete[] resized[i].PosArray;
            delete[] resized[i].NormArray;
        }
    }
    delete[] resized;
}
