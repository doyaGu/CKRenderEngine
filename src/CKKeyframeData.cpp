#include "RCKKeyframeData.h"

#include "VxMath.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>
#include <new>

//===================================================================
// Controller Clone helpers
//===================================================================

// Native transform-controller Clone releases destination keys before copying,
// so self-Clone reads freed storage. Build the copy first; self-Clone and
// allocation failure leave the destination unchanged.
template <class Key>
static bool CopyControllerKeys(const Key *source, int count, Key *&copy) {
    copy = nullptr;
    if (count < 0 || (count > 0 && !source) ||
        static_cast<size_t>(count) > static_cast<size_t>(-1) / sizeof(Key))
        return false;
    if (count == 0)
        return true;
    copy = new(std::nothrow) Key[count];
    if (!copy)
        return false;
    memcpy(copy, source, count * sizeof(Key));
    return true;
}

//===================================================================
// Helper functions for TCB spline interpolation
//===================================================================

// Validate the complete record before writing. The public Dump API requires
// an output buffer of at least the size returned by the null-buffer query.
template <class Key>
static int DumpFixedControllerKeys(const Key *keys, int count, void *buffer) {
    const int headerSize = static_cast<int>(sizeof(int));
    const int keySize = static_cast<int>(sizeof(Key));
    if (count < 0 || count > (INT_MAX - headerSize) / keySize ||
        (count > 0 && !keys))
        return 0;
    float previousTime = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float time = keys[i].TimeStep;
        if (!std::isfinite(time) || (i > 0 && time < previousTime))
            return 0;
        previousTime = time;
    }
    const int size = headerSize + count * keySize;
    if (buffer) {
        memcpy(buffer, &count, sizeof(count));
        if (count > 0)
            memcpy(static_cast<char *>(buffer) + headerSize, keys,
                   static_cast<size_t>(count) * sizeof(Key));
    }
    return size;
}

// The public raw API still requires a complete buffer. File Load supplies the
// actual record capacity through the private bounded readers. Prepare keys
// first so rejected input and allocation failure retain all existing state.
template <class Key>
static int ReadFixedControllerKeys(void *buffer, int bufferSize, Key *&copy, int &count) {
    copy = nullptr;
    count = 0;
    if (!buffer || bufferSize < static_cast<int>(sizeof(int)))
        return 0;
    int keyCount;
    memcpy(&keyCount, buffer, sizeof(keyCount));
    const int keySize = static_cast<int>(sizeof(Key));
    if (keyCount < 0 || keyCount > (bufferSize - static_cast<int>(sizeof(int))) / keySize)
        return 0;
    const char *keys = static_cast<const char *>(buffer) + sizeof(int);
    float previousTime = 0.0f;
    for (int i = 0; i < keyCount; ++i) {
        float time;
        memcpy(&time, keys + static_cast<size_t>(i) * sizeof(Key), sizeof(time));
        if (!std::isfinite(time) || (i > 0 && time < previousTime))
            return 0;
        previousTime = time;
    }
    if (keyCount > 0) {
        copy = new(std::nothrow) Key[keyCount];
        if (!copy)
            return 0;
        memcpy(copy, keys, static_cast<size_t>(keyCount) * sizeof(Key));
    }
    count = keyCount;
    return static_cast<int>(sizeof(int)) + keyCount * keySize;
}

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
        controller = new (std::nothrow) RCKLinearPositionController();
        break;
    case CKANIMATION_CONTROLLER_ROT:
    case CKANIMATION_LINROT_CONTROL:
        controller = new (std::nothrow) RCKLinearRotationController();
        break;
    case CKANIMATION_CONTROLLER_SCL:
    case CKANIMATION_LINSCL_CONTROL:
        controller = new (std::nothrow) RCKLinearScaleController();
        break;
    case CKANIMATION_LINSCLAXIS_CONTROL:
        controller = new (std::nothrow) RCKLinearScaleAxisController();
        break;
    case CKANIMATION_TCBPOS_CONTROL:
        controller = new (std::nothrow) RCKTCBPositionController();
        break;
    case CKANIMATION_TCBROT_CONTROL:
        controller = new (std::nothrow) RCKTCBRotationController();
        break;
    case CKANIMATION_TCBSCL_CONTROL:
        controller = new (std::nothrow) RCKTCBScaleController();
        break;
    case CKANIMATION_TCBSCLAXIS_CONTROL:
        controller = new (std::nothrow) RCKTCBScaleAxisController();
        break;
    case CKANIMATION_BEZIERPOS_CONTROL:
        controller = new (std::nothrow) RCKBezierPositionController();
        break;
    case CKANIMATION_BEZIERSCL_CONTROL:
        controller = new (std::nothrow) RCKBezierScaleController();
        break;
    case CKANIMATION_CONTROLLER_MORPH:
    case CKANIMATION_MORPH_CONTROL:
        controller = new (std::nothrow) RCKMorphController();
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

    // Independent weights retain both endpoints across wide finite ranges.
    const double t1 = m_Keys[low].TimeStep;
    const double t2 = m_Keys[high].TimeStep;
    const double duration = t2 - t1;
    const double leftWeight = (t2 - TimeStep) / duration;
    const double rightWeight = (TimeStep - t1) / duration;

    const VxVector &p1 = m_Keys[low].Pos;
    const VxVector &p2 = m_Keys[high].Pos;

    result->x = static_cast<float>(static_cast<double>(p1.x) * leftWeight +
                                   static_cast<double>(p2.x) * rightWeight);
    result->y = static_cast<float>(static_cast<double>(p1.y) * leftWeight +
                                   static_cast<double>(p2.y) * rightWeight);
    result->z = static_cast<float>(static_cast<double>(p1.z) * leftWeight +
                                   static_cast<double>(p2.z) * rightWeight);

    return TRUE;
}

int RCKLinearPositionController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    const CKPositionKey newKey = *static_cast<CKPositionKey *>(key);
    const float time = newKey.TimeStep;
    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Pos = newKey.Pos;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }
    if (m_NbKeys < 0 || m_NbKeys == INT_MAX ||
        static_cast<size_t>(m_NbKeys + 1) > static_cast<size_t>(-1) / sizeof(CKPositionKey))
        return -1;

    const int newCount = m_NbKeys + 1;
    CKPositionKey *newKeys = new(std::nothrow) CKPositionKey[newCount];
    if (!newKeys)
        return -1;
    if (insertIdx > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(insertIdx) * sizeof(CKPositionKey));
    if (insertIdx < m_NbKeys)
        memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx],
               static_cast<size_t>(m_NbKeys - insertIdx) * sizeof(CKPositionKey));
    newKeys[insertIdx] = newKey;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
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

    const int newCount = m_NbKeys - 1;
    if (newCount == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        m_NbKeys = 0;
        return;
    }

    if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKPositionKey))
        return;
    CKPositionKey *newKeys = new(std::nothrow) CKPositionKey[newCount];
    if (!newKeys)
        return;
    if (index > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(index) * sizeof(CKPositionKey));
    if (index < newCount)
        memcpy(&newKeys[index], &m_Keys[index + 1], static_cast<size_t>(newCount - index) * sizeof(CKPositionKey));
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
}

int RCKLinearPositionController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKLinearPositionController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKLinearPositionController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKPositionKey *keys;
    int count;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;
    delete[] m_Keys;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKLinearPositionController *other = static_cast<RCKLinearPositionController *>(control);
    CKPositionKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;

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

    // Keep finite time differences wide until the Slerp parameter is stored.
    const double t1 = m_Keys[low].TimeStep;
    const double t2 = m_Keys[high].TimeStep;
    const float t = static_cast<float>((TimeStep - t1) / (t2 - t1));

    *result = Slerp(t, m_Keys[low].Rot, m_Keys[high].Rot);

    return TRUE;
}

int RCKLinearRotationController::AddKey(CKKey *key) {
    if (!key)
        return -1;

    const CKRotationKey newKey = *static_cast<CKRotationKey *>(key);
    const float time = newKey.TimeStep;
    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Rot = newKey.Rot;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }
    if (m_NbKeys < 0 || m_NbKeys == INT_MAX ||
        static_cast<size_t>(m_NbKeys + 1) > static_cast<size_t>(-1) / sizeof(CKRotationKey))
        return -1;

    const int newCount = m_NbKeys + 1;
    CKRotationKey *newKeys = new(std::nothrow) CKRotationKey[newCount];
    if (!newKeys)
        return -1;
    if (insertIdx > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(insertIdx) * sizeof(CKRotationKey));
    if (insertIdx < m_NbKeys)
        memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx],
               static_cast<size_t>(m_NbKeys - insertIdx) * sizeof(CKRotationKey));
    newKeys[insertIdx] = newKey;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
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

    const int newCount = m_NbKeys - 1;
    if (newCount == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        m_NbKeys = 0;
        return;
    }

    if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKRotationKey))
        return;
    CKRotationKey *newKeys = new(std::nothrow) CKRotationKey[newCount];
    if (!newKeys)
        return;
    if (index > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(index) * sizeof(CKRotationKey));
    if (index < newCount)
        memcpy(&newKeys[index], &m_Keys[index + 1], static_cast<size_t>(newCount - index) * sizeof(CKRotationKey));
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
}

int RCKLinearRotationController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKLinearRotationController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKLinearRotationController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKRotationKey *keys;
    int count;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;
    delete[] m_Keys;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKLinearRotationController *other = static_cast<RCKLinearRotationController *>(control);
    CKRotationKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;

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

    const CKScaleKey newKey = *static_cast<CKScaleKey *>(key);
    const float time = newKey.TimeStep;
    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Pos = newKey.Pos;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }
    if (m_NbKeys < 0 || m_NbKeys == INT_MAX ||
        static_cast<size_t>(m_NbKeys + 1) > static_cast<size_t>(-1) / sizeof(CKScaleKey))
        return -1;

    const int newCount = m_NbKeys + 1;
    CKScaleKey *newKeys = new(std::nothrow) CKScaleKey[newCount];
    if (!newKeys)
        return -1;
    if (insertIdx > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(insertIdx) * sizeof(CKScaleKey));
    if (insertIdx < m_NbKeys)
        memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx],
               static_cast<size_t>(m_NbKeys - insertIdx) * sizeof(CKScaleKey));
    newKeys[insertIdx] = newKey;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
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

    const int newCount = m_NbKeys - 1;
    if (newCount == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        m_NbKeys = 0;
        return;
    }

    if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKScaleKey))
        return;
    CKScaleKey *newKeys = new(std::nothrow) CKScaleKey[newCount];
    if (!newKeys)
        return;
    if (index > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(index) * sizeof(CKScaleKey));
    if (index < newCount)
        memcpy(&newKeys[index], &m_Keys[index + 1], static_cast<size_t>(newCount - index) * sizeof(CKScaleKey));
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
}

int RCKLinearScaleController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKLinearScaleController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKLinearScaleController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKScaleKey *keys;
    int count;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;
    delete[] m_Keys;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKLinearScaleController *other = static_cast<RCKLinearScaleController *>(control);
    CKScaleKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;

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

    const CKScaleAxisKey newKey = *static_cast<CKScaleAxisKey *>(key);
    const float time = newKey.TimeStep;
    int insertIdx = m_NbKeys;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (m_Keys[i].TimeStep == time) {
            m_Keys[i].Rot = newKey.Rot;
            return i;
        }
        if (m_Keys[i].TimeStep > time) {
            insertIdx = i;
            break;
        }
    }
    if (m_NbKeys < 0 || m_NbKeys == INT_MAX ||
        static_cast<size_t>(m_NbKeys + 1) > static_cast<size_t>(-1) / sizeof(CKScaleAxisKey))
        return -1;

    const int newCount = m_NbKeys + 1;
    CKScaleAxisKey *newKeys = new(std::nothrow) CKScaleAxisKey[newCount];
    if (!newKeys)
        return -1;
    if (insertIdx > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(insertIdx) * sizeof(CKScaleAxisKey));
    if (insertIdx < m_NbKeys)
        memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx],
               static_cast<size_t>(m_NbKeys - insertIdx) * sizeof(CKScaleAxisKey));
    newKeys[insertIdx] = newKey;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
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

    const int newCount = m_NbKeys - 1;
    if (newCount == 0) {
        delete[] m_Keys;
        m_Keys = nullptr;
        m_NbKeys = 0;
        return;
    }

    if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKScaleAxisKey))
        return;
    CKScaleAxisKey *newKeys = new(std::nothrow) CKScaleAxisKey[newCount];
    if (!newKeys)
        return;
    if (index > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(index) * sizeof(CKScaleAxisKey));
    if (index < newCount)
        memcpy(&newKeys[index], &m_Keys[index + 1], static_cast<size_t>(newCount - index) * sizeof(CKScaleAxisKey));
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
}

int RCKLinearScaleAxisController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKLinearScaleAxisController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKLinearScaleAxisController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKScaleAxisKey *keys;
    int count;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;
    delete[] m_Keys;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKLinearScaleAxisController *other = static_cast<RCKLinearScaleAxisController *>(control);
    CKScaleAxisKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;

    return TRUE;
}

//===================================================================
// RCKTCBPositionController Implementation
//===================================================================

// Native duplicate edits change only the value, retaining time and spline parameters.
static void TCBReplaceKeyValue(CKTCBPositionKey &destination, const CKTCBPositionKey &source) {
    destination.Pos = source.Pos;
}

static void TCBReplaceKeyValue(CKTCBRotationKey &destination, const CKTCBRotationKey &source) {
    destination.Rot = source.Rot;
}

// Snapshot the typed input before any reallocation: it may be controller-owned.
// Equal-time replacement keeps the original time bits and spline parameters.
template <class Key>
static int TCBAddControllerKey(Key *&keys, int &count, const Key *key) {
    if (!key)
        return -1;
    Key value = *key;
    int insertion = count;
    for (int i = 0; i < count; ++i) {
        if (keys[i].TimeStep == value.TimeStep) {
            TCBReplaceKeyValue(keys[i], value);
            return i;
        }
        if (keys[i].TimeStep > value.TimeStep) {
            insertion = i;
            break;
        }
    }
    if (count < 0 || count == INT_MAX ||
        static_cast<size_t>(count) + 1 > static_cast<size_t>(-1) / sizeof(Key))
        return -1;
    Key *replacement = new(std::nothrow) Key[count + 1];
    if (!replacement)
        return -1;
    if (insertion > 0)
        memcpy(replacement, keys, static_cast<size_t>(insertion) * sizeof(Key));
    if (insertion < count)
        memcpy(replacement + insertion + 1, keys + insertion, static_cast<size_t>(count - insertion) * sizeof(Key));
    replacement[insertion] = value;
    delete[] keys;
    keys = replacement;
    ++count;
    return insertion;
}

// Preserve current storage if replacement allocation fails.
template <class Key>
static bool TCBRemoveControllerKey(Key *&keys, int &count, int index) {
    if (index < 0 || index >= count)
        return false;
    const int newCount = count - 1;
    Key *replacement = nullptr;
    if (newCount > 0) {
        if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(Key))
            return false;
        replacement = new(std::nothrow) Key[newCount];
        if (!replacement)
            return false;
        if (index > 0)
            memcpy(replacement, keys, static_cast<size_t>(index) * sizeof(Key));
        if (index < newCount)
            memcpy(replacement + index, keys + index + 1, static_cast<size_t>(newCount - index) * sizeof(Key));
    }
    delete[] keys;
    keys = replacement;
    count = newCount;
    return true;
}

static void TCBComputeTangentValues(const CKTCBPositionKey *keys, int count, VxVector *tangents) {
    ComputeTCBVectorTangents(keys, count, tangents);
}

static void TCBComputeTangentValues(const CKTCBRotationKey *keys, int count, VxQuaternion *tangents) {
    ComputeTCBQuaternionTangents(keys, count, tangents);
}

template <class Key, class Tangent>
static bool TCBPrepareTangentArray(const Key *keys, int count, Tangent *&replacement) {
    replacement = nullptr;
    if (count < 2 || count > INT_MAX / 2 ||
        static_cast<size_t>(count) > static_cast<size_t>(-1) / (sizeof(Tangent) * 2))
        return false;
    replacement = new(std::nothrow) Tangent[count * 2];
    if (!replacement)
        return false;
    TCBComputeTangentValues(keys, count, replacement);
    return true;
}

RCKTCBPositionController::RCKTCBPositionController()
    : CKAnimController(CKANIMATION_TCBPOS_CONTROL), m_Keys(nullptr), m_Tangents(nullptr) {
    m_Length = 0.0f;
}

RCKTCBPositionController::~RCKTCBPositionController() {
    delete[] m_Keys;
    delete[] m_Tangents;
}

void RCKTCBPositionController::ComputeTangents() {
    if (m_NbKeys < 2) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }
    VxVector *replacement;
    if (!TCBPrepareTangentArray(m_Keys, m_NbKeys, replacement))
        return;
    delete[] m_Tangents;
    m_Tangents = replacement;
}

CKBOOL RCKTCBPositionController::Evaluate(float TimeStep, void *res) {
    if (m_NbKeys <= 0)
        return FALSE;

    VxVector *result = static_cast<VxVector *>(res);

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
    // Endpoint copies need no tangent allocation. Interior interpolation
    // must not proceed if lazy cache construction fails.
    if (!m_Tangents)
        ComputeTangents();
    if (!m_Tangents)
        return FALSE;

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = low + ((high - low) >> 1);
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    // Hermite spline interpolation
    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    const float interval = t2 - t1;
    const float elapsed = TimeStep - t1;
    float t = (!std::isfinite(interval) || !std::isfinite(elapsed))
        ? static_cast<float>((static_cast<double>(TimeStep) - t1) / (static_cast<double>(t2) - t1))
        : elapsed / interval;

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
    const int index = TCBAddControllerKey(m_Keys, m_NbKeys, static_cast<CKTCBPositionKey *>(key));
    if (index >= 0) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
    return index;
}

CKKey *RCKTCBPositionController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBPositionController::RemoveKey(int index) {
    if (TCBRemoveControllerKey(m_Keys, m_NbKeys, index)) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
}

int RCKTCBPositionController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKTCBPositionController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKTCBPositionController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKTCBPositionKey *keys = nullptr;
    int count = 0;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;

    delete[] m_Keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKTCBPositionController *other = static_cast<RCKTCBPositionController *>(control);
    CKTCBPositionKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;

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
    if (m_NbKeys < 2) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }
    VxQuaternion *replacement;
    if (!TCBPrepareTangentArray(m_Keys, m_NbKeys, replacement))
        return;
    delete[] m_Tangents;
    m_Tangents = replacement;
}

CKBOOL RCKTCBRotationController::Evaluate(float TimeStep, void *res) {
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

    // At an interior key, the original evaluates the segment ending there.
    // Slerp can preserve a different quaternion sign on the adjacent segment.
    // Endpoint copies need no tangent allocation. Interior interpolation
    // must not proceed if lazy cache construction fails.
    if (!m_Tangents)
        ComputeTangents();
    if (!m_Tangents)
        return FALSE;

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = low + ((high - low) >> 1);
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    const float interval = t2 - t1;
    const float elapsed = TimeStep - t1;
    float t = (!std::isfinite(interval) || !std::isfinite(elapsed))
        ? static_cast<float>((static_cast<double>(TimeStep) - t1) / (static_cast<double>(t2) - t1))
        : elapsed / interval;

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
    const int index = TCBAddControllerKey(m_Keys, m_NbKeys, static_cast<CKTCBRotationKey *>(key));
    if (index >= 0) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
    return index;
}

CKKey *RCKTCBRotationController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBRotationController::RemoveKey(int index) {
    if (TCBRemoveControllerKey(m_Keys, m_NbKeys, index)) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
}

int RCKTCBRotationController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKTCBRotationController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKTCBRotationController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKTCBRotationKey *keys = nullptr;
    int count = 0;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;

    delete[] m_Keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKTCBRotationController *other = static_cast<RCKTCBRotationController *>(control);
    CKTCBRotationKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;

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
    if (m_NbKeys < 2) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }
    VxVector *replacement;
    if (!TCBPrepareTangentArray(m_Keys, m_NbKeys, replacement))
        return;
    delete[] m_Tangents;
    m_Tangents = replacement;
}

CKBOOL RCKTCBScaleController::Evaluate(float TimeStep, void *res) {
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

    // Endpoint copies need no tangent allocation. Interior interpolation
    // must not proceed if lazy cache construction fails.
    if (!m_Tangents)
        ComputeTangents();
    if (!m_Tangents)
        return FALSE;

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = low + ((high - low) >> 1);
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    const float interval = t2 - t1;
    const float elapsed = TimeStep - t1;
    float t = (!std::isfinite(interval) || !std::isfinite(elapsed))
        ? static_cast<float>((static_cast<double>(TimeStep) - t1) / (static_cast<double>(t2) - t1))
        : elapsed / interval;

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
    const int index = TCBAddControllerKey(m_Keys, m_NbKeys, static_cast<CKTCBPositionKey *>(key));
    if (index >= 0) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
    return index;
}

CKKey *RCKTCBScaleController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBScaleController::RemoveKey(int index) {
    if (TCBRemoveControllerKey(m_Keys, m_NbKeys, index)) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
}

int RCKTCBScaleController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKTCBScaleController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKTCBScaleController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKTCBScaleKey *keys = nullptr;
    int count = 0;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;

    delete[] m_Keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKTCBScaleController *other = static_cast<RCKTCBScaleController *>(control);
    CKTCBScaleKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;

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
    if (m_NbKeys < 2) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
        return;
    }
    VxQuaternion *replacement;
    if (!TCBPrepareTangentArray(m_Keys, m_NbKeys, replacement))
        return;
    delete[] m_Tangents;
    m_Tangents = replacement;
}

CKBOOL RCKTCBScaleAxisController::Evaluate(float TimeStep, void *res) {
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

    // At an interior key, the original evaluates the segment ending there.
    // Slerp can preserve a different quaternion sign on the adjacent segment.
    // Endpoint copies need no tangent allocation. Interior interpolation
    // must not proceed if lazy cache construction fails.
    if (!m_Tangents)
        ComputeTangents();
    if (!m_Tangents)
        return FALSE;

    int low = 0;
    int high = m_NbKeys - 1;
    while (low < high - 1) {
        int mid = low + ((high - low) >> 1);
        if (m_Keys[mid].TimeStep < TimeStep)
            low = mid;
        else
            high = mid;
    }

    float t1 = m_Keys[low].TimeStep;
    float t2 = m_Keys[high].TimeStep;
    const float interval = t2 - t1;
    const float elapsed = TimeStep - t1;
    float t = (!std::isfinite(interval) || !std::isfinite(elapsed))
        ? static_cast<float>((static_cast<double>(TimeStep) - t1) / (static_cast<double>(t2) - t1))
        : elapsed / interval;

    t = ApplyEaseParameters(t, m_Keys[low].easefrom, m_Keys[high].easeto);

    VxQuaternion &q1 = m_Keys[low].Rot;
    VxQuaternion &q2 = m_Keys[high].Rot;

    *result = Squad(t, q1, m_Tangents[low * 2 + 1], m_Tangents[high * 2], q2);

    return TRUE;
}

int RCKTCBScaleAxisController::AddKey(CKKey *key) {
    const int index = TCBAddControllerKey(m_Keys, m_NbKeys, static_cast<CKTCBRotationKey *>(key));
    if (index >= 0) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
    return index;
}

CKKey *RCKTCBScaleAxisController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKTCBScaleAxisController::RemoveKey(int index) {
    if (TCBRemoveControllerKey(m_Keys, m_NbKeys, index)) {
        delete[] m_Tangents;
        m_Tangents = nullptr;
    }
}

int RCKTCBScaleAxisController::DumpKeysTo(void *Buffer) {
    return DumpFixedControllerKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKTCBScaleAxisController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKTCBScaleAxisController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    CKTCBScaleAxisKey *keys = nullptr;
    int count = 0;
    const int size = ReadFixedControllerKeys(Buffer, BufferSize, keys, count);
    if (!size)
        return 0;

    delete[] m_Keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;
    m_Keys = keys;
    m_NbKeys = count;
    return size;
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

    if (control == this)
        return TRUE;

    RCKTCBScaleAxisController *other = static_cast<RCKTCBScaleAxisController *>(control);
    CKTCBScaleAxisKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    delete[] m_Tangents;
    m_Tangents = nullptr;

    return TRUE;
}

//===================================================================
// RCKBezierPositionController Implementation
//===================================================================

// Snapshot the input before any replacement or release: callers may pass an
// owned key. Commit count and storage only after allocation succeeds.
static int AddBezierKey(CKBezierPositionKey *&keys, int &count, CKKey *input) {
    if (!input || count < 0 || count == INT_MAX ||
        static_cast<size_t>(count) >= static_cast<size_t>(-1) / sizeof(CKBezierPositionKey))
        return -1;
    CKBezierPositionKey key = *static_cast<CKBezierPositionKey *>(input);
    int index = count;
    for (int i = 0; i < count; ++i) {
        if (keys[i].TimeStep == key.TimeStep) {
            // 0x1004DC9B changes only the value. Existing tangent modes,
            // derivatives and the exact time bits remain attached to the key.
            keys[i].Pos = key.Pos;
            return i;
        }
        if (keys[i].TimeStep > key.TimeStep) {
            index = i;
            break;
        }
    }
    CKBezierPositionKey *replacement = new(std::nothrow) CKBezierPositionKey[count + 1];
    if (!replacement)
        return -1;
    if (index > 0)
        memcpy(replacement, keys, static_cast<size_t>(index) * sizeof(*keys));
    replacement[index] = key;
    if (index < count)
        memcpy(replacement + index + 1, keys + index,
               static_cast<size_t>(count - index) * sizeof(*keys));
    delete[] keys;
    keys = replacement;
    ++count;
    return index;
}

// Prepare the shorter key array before releasing the old storage.
static bool RemoveBezierKey(CKBezierPositionKey *&keys, int &count, int index) {
    if (index < 0 || index >= count)
        return false;
    if (count == 1) {
        delete[] keys;
        keys = nullptr;
        count = 0;
        return true;
    }
    CKBezierPositionKey *replacement = new(std::nothrow) CKBezierPositionKey[count - 1];
    if (!replacement)
        return false;
    if (index > 0)
        memcpy(replacement, keys, static_cast<size_t>(index) * sizeof(*keys));
    if (index < count - 1)
        memcpy(replacement + index, keys + index + 1,
               static_cast<size_t>(count - index - 1) * sizeof(*keys));
    delete[] keys;
    keys = replacement;
    --count;
    return true;
}

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
    const int index = AddBezierKey(m_Keys, m_NbKeys, key);
    if (index >= 0)
        m_TangentsComputed = FALSE;
    return index;
}

CKKey *RCKBezierPositionController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKBezierPositionController::RemoveKey(int index) {
    if (RemoveBezierKey(m_Keys, m_NbKeys, index))
        m_TangentsComputed = FALSE;
}

// Position and scale share these routines in the original DLL (0x1004F4BE,
// 0x1004F65A). Each key has a 20-byte prefix and only explicit tangent vectors.
static int DumpBezierKeys(CKBezierPositionKey *keys, int count, void *buffer) {
    if (count < 0 || (count > 0 && !keys) ||
        count > (INT_MAX - static_cast<int>(sizeof(int))) / 20 ||
        static_cast<size_t>(count) > static_cast<size_t>(-1) / sizeof(CKBezierPositionKey))
        return 0;

    // Validate every variable-size key before touching the caller's output.
    // The raw API requires a complete output buffer; optional tangent flags
    // determine its size without computing or changing cached tangents.
    int size = sizeof(int);
    float previousTime = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (!std::isfinite(keys[i].TimeStep) || (i > 0 && keys[i].TimeStep < previousTime))
            return 0;
        previousTime = keys[i].TimeStep;
        int keySize = 20;
        if (keys[i].Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS)
            keySize += sizeof(VxVector);
        if (keys[i].Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS)
            keySize += sizeof(VxVector);
        if (keySize > INT_MAX - size)
            return 0;
        size += keySize;
    }

    char *bytes = static_cast<char *>(buffer);
    if (!bytes)
        return size;
    memcpy(bytes, &count, sizeof(count));
    int offset = sizeof(int);
    for (int i = 0; i < count; ++i) {
        memcpy(bytes + offset, &keys[i], 20);
        offset += 20;
        if (keys[i].Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(bytes + offset, &keys[i].In, sizeof(VxVector));
            offset += sizeof(VxVector);
        }
        if (keys[i].Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(bytes + offset, &keys[i].Out, sizeof(VxVector));
            offset += sizeof(VxVector);
        }
    }
    return size;
}

static int ReadBezierKeys(CKBezierPositionKey *&keys, int &count, void *buffer, int bufferSize) {
    if (!buffer || bufferSize < static_cast<int>(sizeof(int)))
        return 0;
    const char *bytes = static_cast<const char *>(buffer);
    int newCount;
    memcpy(&newCount, bytes, sizeof(newCount));
    if (newCount < 0 || newCount > (bufferSize - static_cast<int>(sizeof(int))) / 20 ||
        static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKBezierPositionKey))
        return 0;

    // Optional tangents change the stride. Validate the complete payload before
    // allocating or replacing existing keys; memcpy also permits unaligned data.
    int size = sizeof(int);
    float previousTime = 0.0f;
    for (int i = 0; i < newCount; ++i) {
        if (bufferSize - size < 20)
            return 0;
        CKBezierPositionKey prefix;
        memcpy(&prefix, bytes + size, 20);
        size += 20;
        if (!std::isfinite(prefix.TimeStep) || (i > 0 && prefix.TimeStep < previousTime))
            return 0;
        previousTime = prefix.TimeStep;
        if (prefix.Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) {
            if (bufferSize - size < static_cast<int>(sizeof(VxVector)))
                return 0;
            size += sizeof(VxVector);
        }
        if (prefix.Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) {
            if (bufferSize - size < static_cast<int>(sizeof(VxVector)))
                return 0;
            size += sizeof(VxVector);
        }
    }

    CKBezierPositionKey *replacement = nullptr;
    if (newCount > 0) {
        replacement = new(std::nothrow) CKBezierPositionKey[newCount];
        if (!replacement)
            return 0;
    }
    int offset = sizeof(int);
    for (int i = 0; i < newCount; ++i) {
        CKBezierPositionKey &key = replacement[i];
        memcpy(&key, bytes + offset, 20);
        offset += 20;
        key.In = VxVector(0.0f, 0.0f, 0.0f);
        key.Out = VxVector(0.0f, 0.0f, 0.0f);
        if (key.Flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(&key.In, bytes + offset, sizeof(VxVector));
            offset += sizeof(VxVector);
        }
        if (key.Flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) {
            memcpy(&key.Out, bytes + offset, sizeof(VxVector));
            offset += sizeof(VxVector);
        }
    }
    delete[] keys;
    keys = replacement;
    count = newCount;
    return size;
}

int RCKBezierPositionController::DumpKeysTo(void *Buffer) {
    return DumpBezierKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKBezierPositionController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKBezierPositionController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    const int size = ReadBezierKeys(m_Keys, m_NbKeys, Buffer, BufferSize);
    if (size)
        m_TangentsComputed = FALSE;
    return size;
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

    if (control == this)
        return TRUE;

    RCKBezierPositionController *other = static_cast<RCKBezierPositionController *>(control);
    CKBezierPositionKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    m_TangentsComputed = FALSE;

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
    const int index = AddBezierKey(m_Keys, m_NbKeys, key);
    if (index >= 0)
        m_TangentsComputed = FALSE;
    return index;
}

CKKey *RCKBezierScaleController::GetKey(int index) {
    if (index < 0 || index >= m_NbKeys)
        return nullptr;
    return &m_Keys[index];
}

void RCKBezierScaleController::RemoveKey(int index) {
    if (RemoveBezierKey(m_Keys, m_NbKeys, index))
        m_TangentsComputed = FALSE;
}

int RCKBezierScaleController::DumpKeysTo(void *Buffer) {
    return DumpBezierKeys(m_Keys, m_NbKeys, Buffer);
}

int RCKBezierScaleController::ReadKeysFrom(void *Buffer) {
    return ReadKeysFromBuffer(Buffer, INT_MAX);
}

int RCKBezierScaleController::ReadKeysFromBuffer(void *Buffer, int BufferSize) {
    const int size = ReadBezierKeys(m_Keys, m_NbKeys, Buffer, BufferSize);
    if (size)
        m_TangentsComputed = FALSE;
    return size;
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

    if (control == this)
        return TRUE;

    RCKBezierScaleController *other = static_cast<RCKBezierScaleController *>(control);
    CKBezierScaleKey *keys;
    if (!CopyControllerKeys(other->m_Keys, other->m_NbKeys, keys))
        return FALSE;

    CKAnimController::Clone(control);
    delete[] m_Keys;
    m_Keys = keys;
    m_TangentsComputed = FALSE;

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

    // Prepare all new storage before changing the key count or releasing old
    // keys. The source payload may alias any of the existing owned arrays.
    if (m_NbKeys == INT_MAX || static_cast<size_t>(m_NbKeys + 1) >
        static_cast<size_t>(-1) / sizeof(CKMorphKey) || static_cast<size_t>(m_VertexCount) >
        static_cast<size_t>(-1) / sizeof(VxVector))
        return -1;
    const int newCount = m_NbKeys + 1;
    CKMorphKey *newKeys = new(std::nothrow) CKMorphKey[newCount];
    if (!newKeys)
        return -1;
    VxVector *positions = new(std::nothrow) VxVector[m_VertexCount];
    if (!positions) {
        delete[] newKeys;
        return -1;
    }
    VxCompressedVector *normals = nullptr;
    if (AllocateNormals || source.NormArray) {
        normals = new(std::nothrow) VxCompressedVector[m_VertexCount];
        if (!normals) {
            delete[] positions;
            delete[] newKeys;
            return -1;
        }
    }
    if (source.PosArray && m_VertexCount > 0)
        memcpy(positions, source.PosArray, static_cast<size_t>(m_VertexCount) * sizeof(VxVector));
    if (source.NormArray && m_VertexCount > 0)
        memcpy(normals, source.NormArray, static_cast<size_t>(m_VertexCount) * sizeof(VxCompressedVector));

    if (m_Keys) {
        if (insertIdx > 0)
            memcpy(newKeys, m_Keys, static_cast<size_t>(insertIdx) * sizeof(CKMorphKey));
        if (insertIdx < m_NbKeys)
            memcpy(&newKeys[insertIdx + 1], &m_Keys[insertIdx], static_cast<size_t>(m_NbKeys - insertIdx) * sizeof(CKMorphKey));
    }

    CKMorphKey &inserted = newKeys[insertIdx];
    inserted.TimeStep = time;
    // The controller owns independent arrays. Source normals imply storage
    // even when AllocateNormals is false; zero-sized arrays retain presence.
    inserted.PosArray = positions;
    inserted.NormArray = normals;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;

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

    const int newCount = m_NbKeys - 1;
    if (newCount == 0) {
        delete[] m_Keys[index].PosArray;
        delete[] m_Keys[index].NormArray;
        delete[] m_Keys;
        m_Keys = nullptr;
        m_NbKeys = 0;
        return;
    }

    if (static_cast<size_t>(newCount) > static_cast<size_t>(-1) / sizeof(CKMorphKey))
        return;
    CKMorphKey *newKeys = new(std::nothrow) CKMorphKey[newCount];
    if (!newKeys)
        return;

    if (index > 0)
        memcpy(newKeys, m_Keys, static_cast<size_t>(index) * sizeof(CKMorphKey));
    if (index < newCount)
        memcpy(&newKeys[index], &m_Keys[index + 1], static_cast<size_t>(newCount - index) * sizeof(CKMorphKey));

    // Only the removed key's payloads are released; surviving payload pointers
    // are transferred after the replacement key array is complete.
    delete[] m_Keys[index].PosArray;
    delete[] m_Keys[index].NormArray;
    delete[] m_Keys;
    m_Keys = newKeys;
    m_NbKeys = newCount;
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
    if (m_NbKeys < 0 || (m_NbKeys > 0 && !m_Keys))
        return 0;
    // The wire format has one normal-presence flag for the whole controller.
    // Native takes it from key 0 and reads every key's normals, but the public
    // AddKey overloads can mix presence. Write normals only when every key has
    // them, so mixed controllers keep their positions instead of failing Save.
    CKBOOL hasNormals = m_NbKeys > 0;
    for (int i = 0; i < m_NbKeys; ++i) {
        if (!m_Keys[i].NormArray)
            hasNormals = FALSE;
    }
    const int size = MorphWireSize(m_NbKeys, m_VertexCount, hasNormals);
    if (!size)
        return 0;
    // Reject payloads Load cannot accept before querying or writing any bytes.
    for (int i = 0; i < m_NbKeys; ++i) {
        if ((m_VertexCount > 0 && !m_Keys[i].PosArray) ||
            !std::isfinite(m_Keys[i].TimeStep) || (i > 0 && m_Keys[i].TimeStep < m_Keys[i - 1].TimeStep))
            return 0;
    }

    if (Buffer) {
        char *buf = static_cast<char *>(Buffer);

        memcpy(buf, &m_NbKeys, sizeof(int));
        buf += sizeof(int);

        memcpy(buf, &m_VertexCount, sizeof(int));
        buf += sizeof(int);

        memcpy(buf, &hasNormals, sizeof(CKBOOL));
        buf += sizeof(CKBOOL);

        for (int i = 0; i < m_NbKeys; ++i) {
            memcpy(buf, &m_Keys[i].TimeStep, sizeof(float));
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
        // The SDK key helper only checks arrays present on its left side.
        // Payload presence is observable even for zero vertices, so controller
        // equality must compare it in both directions.
        if ((m_Keys[i].PosArray != nullptr) != (other->m_Keys[i].PosArray != nullptr) ||
            (m_Keys[i].NormArray != nullptr) != (other->m_Keys[i].NormArray != nullptr))
            return FALSE;
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
