#include "RCKObjectAnimation.h"
#include "RCKKeyedAnimation.h"
#include "RCK3dEntity.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"

//=============================================================================
// Constructor/Destructor
//=============================================================================

RCKObjectAnimation::RCKObjectAnimation(CKContext *Context, CKSTRING name)
    : CKObjectAnimation(Context, name),
      m_KeyframeData(nullptr),
      m_Flags(0),
      m_Entity(nullptr),
      m_CurrentStep(0.0f),
      m_Length(0.0f),
      m_MergeFactor(0.0f),
      m_Anim1(nullptr),
      m_Anim2(nullptr),
      m_IsMerged(FALSE) {
}

RCKObjectAnimation::~RCKObjectAnimation() {
}

CK_CLASSID RCKObjectAnimation::GetClassID() {
    return CKCID_OBJECTANIMATION;
}

//=============================================================================
// Serialization
//=============================================================================

CKStateChunk *RCKObjectAnimation::Save(CKFile *file, CKDWORD flags) {
    // Call base class save first
    CKStateChunk *baseChunk = CKObject::Save(file, flags);

    // If no file and no flags set, just return base chunk
    if (!file && (flags & 0x7FFFFFF) == 0)
        return baseChunk;

    // Create new chunk for ObjectAnimation data
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_OBJECTANIMATION, file);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Initialize root position vector
    VxVector rootPos(0.0f, 0.0f, 0.0f);

    // Get root position from parent animation if this is root
    void *appData = GetAppData();
    if (appData) {
        rootPos = *static_cast<VxVector *>(appData);
    }

    // Check if we should share animation data with another ObjectAnimation
    RCKObjectAnimation *sharedAnim = nullptr;

    if (m_KeyframeData && m_KeyframeData->m_ObjectAnimation != this) {
        if (file) {
            // Check if the shared animation is being saved
            // This is a simplified version - original checks file objects
            sharedAnim = m_KeyframeData->m_ObjectAnimation;
        } else {
            sharedAnim = m_KeyframeData->m_ObjectAnimation;
        }
    }

    if (sharedAnim) {
        // Write reference to shared animation data (identifier 0x2000000)
        chunk->WriteIdentifier(0x2000000);
        chunk->WriteObject(sharedAnim);
        chunk->WriteFloat(rootPos.x);
        chunk->WriteFloat(rootPos.y);
        chunk->WriteFloat(rootPos.z);
        // Write 4 reserved floats
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteDword(m_Flags);
        chunk->WriteObject(m_Entity);

        // Write merged animation data if flag 0x80 is set
        if (m_Flags & 0x80) {
            chunk->WriteFloat(m_MergeFactor);
            chunk->WriteObject(m_Anim1);
            chunk->WriteObject(m_Anim2);
        }
    } else {
        // Mark this as the owner of the keyframe data when saving
        if (file && m_KeyframeData) {
            m_KeyframeData->m_ObjectAnimation = this;
        }

        // Write full keyframe data (identifier 0x4000000)
        chunk->WriteIdentifier(0x4000000);
        chunk->WriteFloat(rootPos.x);
        chunk->WriteFloat(rootPos.y);
        chunk->WriteFloat(rootPos.z);
        // Write 4 reserved floats
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteFloat(0.0f);
        chunk->WriteDword(m_Flags);
        chunk->WriteObject(m_Entity);

        // Write animation length from keyframe data
        if (m_KeyframeData)
            chunk->WriteFloat(m_KeyframeData->m_Length);
        else
            chunk->WriteFloat(m_Length);

        // Write merged animation data if flag 0x80 is set
        if (m_Flags & 0x80) {
            chunk->WriteFloat(m_MergeFactor);
            chunk->WriteObject(m_Anim1);
            chunk->WriteObject(m_Anim2);
        }

        // Write controller data (position, rotation, scale, scaleAxis, morph)
        if (m_KeyframeData) {
            // Position controller
            if (m_KeyframeData->m_PositionController) {
                CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_PositionController);
                int size = ctrl->DumpKeysTo(nullptr) >> 2;
                if (size > 0) {
                    chunk->WriteDword(ctrl->m_Type);
                    chunk->WriteDword(size);
                    void *buffer = chunk->LockWriteBuffer(size);
                    ctrl->DumpKeysTo(buffer);
                    chunk->Skip(size);
                }
            }

            // Rotation controller
            if (m_KeyframeData->m_RotationController) {
                CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_RotationController);
                int size = ctrl->DumpKeysTo(nullptr) >> 2;
                if (size > 0) {
                    chunk->WriteDword(ctrl->m_Type);
                    chunk->WriteDword(size);
                    void *buffer = chunk->LockWriteBuffer(size);
                    ctrl->DumpKeysTo(buffer);
                    chunk->Skip(size);
                }
            }

            // Scale controller
            if (m_KeyframeData->m_ScaleController) {
                CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleController);
                int size = ctrl->DumpKeysTo(nullptr) >> 2;
                if (size > 0) {
                    chunk->WriteDword(ctrl->m_Type);
                    chunk->WriteDword(size);
                    void *buffer = chunk->LockWriteBuffer(size);
                    ctrl->DumpKeysTo(buffer);
                    chunk->Skip(size);
                }
            }

            // Scale axis controller
            if (m_KeyframeData->m_ScaleAxisController) {
                CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleAxisController);
                int size = ctrl->DumpKeysTo(nullptr) >> 2;
                if (size > 0) {
                    chunk->WriteDword(ctrl->m_Type);
                    chunk->WriteDword(size);
                    void *buffer = chunk->LockWriteBuffer(size);
                    ctrl->DumpKeysTo(buffer);
                    chunk->Skip(size);
                }
            }

            // Morph controller
            if (m_KeyframeData->m_MorphController) {
                CKMorphController *ctrl = reinterpret_cast<CKMorphController *>(m_KeyframeData->m_MorphController);
                int size = ctrl->DumpKeysTo(nullptr) >> 2;
                if (size > 0) {
                    chunk->WriteDword(ctrl->m_Type);
                    chunk->WriteDword(size);
                    void *buffer = chunk->LockWriteBuffer(size);
                    ctrl->DumpKeysTo(buffer);
                    chunk->Skip(size);
                }
            }
        }

        // Write terminator
        chunk->WriteDword(0);
    }

    chunk->CloseChunk();
    return chunk;
}

CKERROR RCKObjectAnimation::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Load base class data
    CKObject::Load(chunk, file);

    // Clear existing data
    Clear();

    VxVector rootPos(0.0f, 0.0f, 0.0f);

    if (chunk->GetDataVersion() >= 1) {
        // New format (data version >= 1)
        if (chunk->SeekIdentifier(0x2000000)) {
            // Shared animation reference format
            CKObjectAnimation *sharedAnim = (CKObjectAnimation *) chunk->ReadObject(m_Context);
            rootPos.x = chunk->ReadFloat();
            rootPos.y = chunk->ReadFloat();
            rootPos.z = chunk->ReadFloat();
            // Skip 4 reserved floats
            chunk->ReadFloat();
            chunk->ReadFloat();
            chunk->ReadFloat();
            chunk->ReadFloat();

            m_Flags = chunk->ReadDword();
            m_Entity = (RCK3dEntity *) chunk->ReadObject(m_Context);

            if (m_Flags & 0x80) {
                m_MergeFactor = chunk->ReadFloat();
                m_Anim1 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
                m_Anim2 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
            }

            // Share data from the referenced animation
            if (sharedAnim)
                ShareDataFrom(sharedAnim);
        } else if (chunk->SeekIdentifier(0x4000000)) {
            // Full keyframe data format
            // Initialize keyframe data
            if (!m_KeyframeData) {
                m_KeyframeData = new CKKeyframeData();
                memset(m_KeyframeData, 0, sizeof(CKKeyframeData));
            }
            m_KeyframeData->m_ObjectAnimation = this;

            rootPos.x = chunk->ReadFloat();
            rootPos.y = chunk->ReadFloat();
            rootPos.z = chunk->ReadFloat();
            // Skip 4 reserved floats
            chunk->ReadFloat();
            chunk->ReadFloat();
            chunk->ReadFloat();
            chunk->ReadFloat();

            m_Flags = chunk->ReadDword();
            m_Entity = (RCK3dEntity *) chunk->ReadObject(m_Context);
            float length = chunk->ReadFloat();
            m_KeyframeData->m_Length = length;

            if (m_Flags & 0x80) {
                m_MergeFactor = chunk->ReadFloat();
                m_Anim1 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
                m_Anim2 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
            }

            // Read controllers
            CKANIMATION_CONTROLLER ctrlType;
            while ((ctrlType = (CKANIMATION_CONTROLLER) chunk->ReadDword()) != 0) {
                CKAnimController *ctrl = CreateController(ctrlType);
                CKDWORD dataSize = chunk->ReadDword();
                if (ctrl) {
                    void *buffer = chunk->LockReadBuffer();
                    ctrl->ReadKeysFrom(buffer);
                }
                chunk->Skip(dataSize);
            }
        } else if (chunk->SeekIdentifier(0x1000)) {
            // Legacy format (identifier 0x1000)
            if (!m_KeyframeData) {
                m_KeyframeData = new CKKeyframeData();
                memset(m_KeyframeData, 0, sizeof(CKKeyframeData));
            }
            m_KeyframeData->m_ObjectAnimation = this;

            rootPos.x = chunk->ReadFloat();
            rootPos.y = chunk->ReadFloat();
            rootPos.z = chunk->ReadFloat();
            chunk->ReadFloat(); // skip
            chunk->ReadFloat();
            chunk->ReadFloat();
            chunk->ReadFloat();

            // Legacy format has different order
            int field1 = chunk->ReadInt();
            int field2 = chunk->ReadInt();
            m_Flags = chunk->ReadDword();
            m_Entity = (RCK3dEntity *) chunk->ReadObject(m_Context);
            float length = chunk->ReadFloat();
            m_KeyframeData->m_Length = length;

            if (m_Flags & 0x80) {
                m_MergeFactor = chunk->ReadFloat();
                m_Anim1 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
                m_Anim2 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
            }

            // The legacy format has complex controller loading
            // For now, we'll handle basic linear controllers
            // This is a simplified implementation
        }
    } else {
        // Very old format (data version < 1)
        if (!m_KeyframeData) {
            m_KeyframeData = new CKKeyframeData();
            memset(m_KeyframeData, 0, sizeof(CKKeyframeData));
        }

        chunk->SeekIdentifier(0x10000); // Skip old identifier if present

        // Read various old format identifiers
        if (chunk->SeekIdentifier(0x40000))
            m_Flags = chunk->ReadDword();

        if (chunk->SeekIdentifier(0x80000))
            m_Entity = (RCK3dEntity *) chunk->ReadObject(m_Context);

        if (chunk->SeekIdentifier(0x2000))
            m_KeyframeData->m_Length = chunk->ReadFloat();

        if (chunk->SeekIdentifier(0x100000)) {
            m_MergeFactor = chunk->ReadFloat();
            if (chunk->ReadInt())
                m_Flags |= 0x80;
            else
                m_Flags &= ~0x80;

            m_Anim1 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
            m_Anim2 = (RCKObjectAnimation *) chunk->ReadObject(m_Context);
        }

        if (chunk->SeekIdentifier(0x1000)) {
            rootPos.x = chunk->ReadFloat();
            rootPos.y = chunk->ReadFloat();
            rootPos.z = chunk->ReadFloat();
        }
    }

    // Store root position as app data if non-zero
    static VxVector zeroVec(0.0f, 0.0f, 0.0f);
    if (rootPos.x != 0.0f || rootPos.y != 0.0f || rootPos.z != 0.0f) {
        VxVector *appData = new VxVector(rootPos);
        SetAppData(appData);
    }

    return CK_OK;
}

int RCKObjectAnimation::GetMemoryOccupation() {
    int size = sizeof(RCKObjectAnimation);

    if (m_KeyframeData) {
        size += sizeof(CKKeyframeData);
        // Add controller sizes if they exist
        if (m_KeyframeData->m_PositionController) {
            CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_PositionController);
            size += ctrl->DumpKeysTo(nullptr);
        }
        if (m_KeyframeData->m_RotationController) {
            CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_RotationController);
            size += ctrl->DumpKeysTo(nullptr);
        }
        if (m_KeyframeData->m_ScaleController) {
            CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleController);
            size += ctrl->DumpKeysTo(nullptr);
        }
    }

    return size;
}

CKERROR RCKObjectAnimation::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKObject::Copy(o, context);
    if (err != CK_OK)
        return err;

    RCKObjectAnimation *src = (RCKObjectAnimation *) &o;

    // Check if we should copy controllers or share data
    CKDWORD deps = context.GetClassDependencies(CKCID_OBJECTANIMATION);
    if (deps & 1) {
        // Deep copy - clone the keyframe data
        Clone(src);
    } else {
        // Share data
        ShareDataFrom(src);
    }

    // Copy remaining fields
    m_Flags = src->m_Flags;
    m_MergeFactor = src->m_MergeFactor;
    m_CurrentStep = src->m_CurrentStep;
    m_Length = src->m_Length;
    m_IsMerged = src->m_IsMerged;

    return CK_OK;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Controllers
//=============================================================================

CKAnimController *RCKObjectAnimation::CreateController(CKANIMATION_CONTROLLER ControlType) {
    // CreateController delegates to CKKeyframeData::CreateController
    // The controller allocation and initialization happens there
    // TODO: Implement CKKeyframeData::CreateController when controller classes are available
    // For now, ensure m_KeyframeData exists and return nullptr
    if (!m_KeyframeData)
        return nullptr;

    // The original implementation:
    // 1. Deletes existing controller of the same base type
    // 2. Allocates a new controller based on ControlType
    // 3. Sets the controller's length from m_KeyframeData->m_Length
    // 4. Returns the new controller

    return nullptr;
}

CKBOOL RCKObjectAnimation::DeleteController(CKANIMATION_CONTROLLER ControlType) {
    if (!m_KeyframeData)
        return FALSE;

    // Get the base controller type
    CKDWORD baseType = ControlType & CKANIMATION_CONTROLLER_MASK;
    CKAnimController *controller = nullptr;

    switch (baseType) {
    case CKANIMATION_CONTROLLER_POS:
        controller = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_PositionController);
        if (controller) {
            delete controller;
            m_KeyframeData->m_PositionController = 0;
        }
        break;

    case CKANIMATION_CONTROLLER_ROT:
        controller = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_RotationController);
        if (controller) {
            delete controller;
            m_KeyframeData->m_RotationController = 0;
        }
        break;

    case CKANIMATION_CONTROLLER_SCL:
        controller = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleController);
        if (controller) {
            delete controller;
            m_KeyframeData->m_ScaleController = 0;
        }
        break;

    case CKANIMATION_CONTROLLER_SCLAXIS:
        controller = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleAxisController);
        if (controller) {
            delete controller;
            m_KeyframeData->m_ScaleAxisController = 0;
        }
        break;

    case CKANIMATION_CONTROLLER_MORPH:
        if (m_KeyframeData->m_MorphController) {
            delete reinterpret_cast<CKMorphController *>(m_KeyframeData->m_MorphController);
            m_KeyframeData->m_MorphController = nullptr;
        }
        break;

    default:
        return FALSE;
    }

    return TRUE;
}

CKAnimController *RCKObjectAnimation::GetPositionController() {
    if (m_KeyframeData)
        return reinterpret_cast<CKAnimController *>(m_KeyframeData->m_PositionController);
    return nullptr;
}

CKAnimController *RCKObjectAnimation::GetScaleController() {
    if (m_KeyframeData)
        return reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleController);
    return nullptr;
}

CKAnimController *RCKObjectAnimation::GetRotationController() {
    if (m_KeyframeData)
        return reinterpret_cast<CKAnimController *>(m_KeyframeData->m_RotationController);
    return nullptr;
}

CKAnimController *RCKObjectAnimation::GetScaleAxisController() {
    if (m_KeyframeData)
        return reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleAxisController);
    return nullptr;
}

CKMorphController *RCKObjectAnimation::GetMorphController() {
    if (m_KeyframeData)
        return reinterpret_cast<CKMorphController *>(m_KeyframeData->m_MorphController);
    return nullptr;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Evaluation
//=============================================================================

CKBOOL RCKObjectAnimation::EvaluatePosition(float Time, VxVector &Pos) {
    // Flag 0x04 disables position evaluation
    if (m_Flags & 0x04) {
        Pos.Set(0.0f, 0.0f, 0.0f);
        return FALSE;
    }

    // Check for merged animation (flag 0x80)
    if (m_Flags & 0x80) {
        if (m_MergeFactor == 0.0f) {
            // Use only first animation
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
            return m_Anim1->EvaluatePosition(anim1Time, Pos);
        } else if (m_MergeFactor == 1.0f) {
            // Use only second animation
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;
            return m_Anim2->EvaluatePosition(anim2Time, Pos);
        } else {
            // Blend between both animations
            VxVector pos2;
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
            float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;

            CKBOOL res1 = m_Anim1->EvaluatePosition(anim1Time, Pos);
            CKBOOL res2 = m_Anim2->EvaluatePosition(anim2Time, pos2);

            if (res1 && res2) {
                // Interpolate: Pos = Pos * (1 - factor) + pos2 * factor
                Pos = Pos * (1.0f - m_MergeFactor) + pos2 * m_MergeFactor;
                return TRUE;
            } else if (res1) {
                return TRUE;
            } else if (res2) {
                Pos = pos2;
                return TRUE;
            }
            return FALSE;
        }
    }

    // Direct controller evaluation
    if (m_KeyframeData && m_KeyframeData->m_PositionController) {
        CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_PositionController);
        return ctrl->Evaluate(Time, &Pos);
    }

    Pos.Set(0.0f, 0.0f, 0.0f);
    return FALSE;
}

CKBOOL RCKObjectAnimation::EvaluateScale(float Time, VxVector &Scl) {
    // Flag 0x10 disables scale evaluation
    if (m_Flags & 0x10) {
        Scl.Set(1.0f, 1.0f, 1.0f);
        return FALSE;
    }

    // Check for merged animation (flag 0x80)
    if (m_Flags & 0x80) {
        VxVector scale2;
        float normalizedTime = Time / m_KeyframeData->m_Length;
        float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
        float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;

        CKBOOL res1 = m_Anim1->EvaluateScale(anim1Time, Scl);
        CKBOOL res2 = m_Anim2->EvaluateScale(anim2Time, scale2);

        if (res1 && res2) {
            // Interpolate: Scl = Scl * (1 - factor) + scale2 * factor
            Scl = Scl * (1.0f - m_MergeFactor) + scale2 * m_MergeFactor;
            return TRUE;
        } else if (res1) {
            return TRUE;
        } else if (res2) {
            Scl = scale2;
            return TRUE;
        }
        return FALSE;
    }

    // Direct controller evaluation
    if (m_KeyframeData && m_KeyframeData->m_ScaleController) {
        CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleController);
        return ctrl->Evaluate(Time, &Scl);
    }

    Scl.Set(1.0f, 1.0f, 1.0f);
    return FALSE;
}

CKBOOL RCKObjectAnimation::EvaluateRotation(float Time, VxQuaternion &Rot) {
    // Flag 0x08 disables rotation evaluation
    if (m_Flags & 0x08) {
        Rot = VxQuaternion(); // Identity quaternion
        return FALSE;
    }

    // Check for merged animation (flag 0x80)
    if (m_Flags & 0x80) {
        if (m_MergeFactor == 0.0f) {
            // Use only first animation
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
            return m_Anim1->EvaluateRotation(anim1Time, Rot);
        } else if (m_MergeFactor == 1.0f) {
            // Use only second animation
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;
            return m_Anim2->EvaluateRotation(anim2Time, Rot);
        } else {
            // Blend between both animations using Slerp
            VxQuaternion rot1, rot2;
            float normalizedTime = Time / m_KeyframeData->m_Length;
            float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
            float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;

            CKBOOL res1 = m_Anim1->EvaluateRotation(anim1Time, rot1);
            CKBOOL res2 = m_Anim2->EvaluateRotation(anim2Time, rot2);

            if (res1 && res2) {
                // Slerp interpolation
                Rot = Slerp(m_MergeFactor, rot1, rot2);
                return TRUE;
            } else if (res1) {
                Rot = rot1;
                return TRUE;
            } else if (res2) {
                Rot = rot2;
                return TRUE;
            }
            return FALSE;
        }
    }

    // Direct controller evaluation
    if (m_KeyframeData && m_KeyframeData->m_RotationController) {
        CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_RotationController);
        return ctrl->Evaluate(Time, &Rot);
    }

    Rot = VxQuaternion(); // Identity quaternion
    return FALSE;
}

CKBOOL RCKObjectAnimation::EvaluateScaleAxis(float Time, VxQuaternion &ScaleAxis) {
    // Flag 0x40 disables scale axis evaluation
    if (m_Flags & 0x40)
        return FALSE;

    // Check for merged animation (flag 0x80)
    if (m_Flags & 0x80) {
        VxQuaternion axis1, axis2;
        float normalizedTime = Time / m_KeyframeData->m_Length;
        float anim1Time = normalizedTime * m_Anim1->m_KeyframeData->m_Length;
        float anim2Time = normalizedTime * m_Anim2->m_KeyframeData->m_Length;

        CKBOOL res1 = m_Anim1->EvaluateScaleAxis(anim1Time, axis1);
        CKBOOL res2 = m_Anim2->EvaluateScaleAxis(anim2Time, axis2);

        if (res1 && res2) {
            // Slerp interpolation
            ScaleAxis = Slerp(m_MergeFactor, axis1, axis2);
            return TRUE;
        } else if (res1) {
            ScaleAxis = axis1;
            return TRUE;
        } else if (res2) {
            ScaleAxis = axis2;
            return TRUE;
        }
        return FALSE;
    }

    // Direct controller evaluation
    if (m_KeyframeData && m_KeyframeData->m_ScaleAxisController) {
        CKAnimController *ctrl = reinterpret_cast<CKAnimController *>(m_KeyframeData->m_ScaleAxisController);
        return ctrl->Evaluate(Time, &ScaleAxis);
    }

    return FALSE;
}

CKBOOL RCKObjectAnimation::EvaluateMorphTarget(float Time, int VertexCount, VxVector *Vertices, CKDWORD VStride,
                                               VxCompressedVector *Normals) {
    // Check for merged animation (flag 0x80)
    if (m_Flags & 0x80) {
        // For morphing, we need to evaluate both and blend the vertex data
        // This is complex - for now return FALSE
        // TODO: Implement morph blending
        return FALSE;
    }

    // Direct morph controller evaluation
    if (m_KeyframeData && m_KeyframeData->m_MorphController) {
        CKMorphController *ctrl = reinterpret_cast<CKMorphController *>(m_KeyframeData->m_MorphController);
        return ctrl->Evaluate(Time, VertexCount, Vertices, VStride, Normals);
    }

    return FALSE;
}

CKBOOL RCKObjectAnimation::EvaluateKeys(float step, VxQuaternion *rot, VxVector *pos, VxVector *scale,
                                        VxQuaternion *ScaleRot) {
    // Clamp step to [0, 1]
    if (step > 1.0f)
        step = 1.0f;
    if (step < 0.0f)
        step = 0.0f;

    // Convert step to time
    float Time = step * m_KeyframeData->m_Length;

    // Evaluate each component if requested
    if (pos)
        EvaluatePosition(Time, *pos);
    if (rot)
        EvaluateRotation(Time, *rot);
    if (scale)
        EvaluateScale(Time, *scale);
    if (ScaleRot)
        EvaluateScaleAxis(Time, *ScaleRot);

    return TRUE;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Has Info
//=============================================================================

CKBOOL RCKObjectAnimation::HasMorphNormalInfo() {
    // Check if morph controller exists with normal data
    if (m_KeyframeData && m_KeyframeData->m_MorphController) {
        // TODO: Check if morph has normal info
    }
    return FALSE;
}

CKBOOL RCKObjectAnimation::HasMorphInfo() {
    if (m_Flags & 0x80) {
        // Merged animation - check both sub-animations
        if (m_Anim1 && m_Anim1->HasMorphInfo())
            return TRUE;
        if (m_Anim2 && m_Anim2->HasMorphInfo())
            return TRUE;
        return FALSE;
    }
    return m_KeyframeData && m_KeyframeData->m_MorphController != nullptr;
}

CKBOOL RCKObjectAnimation::HasScaleInfo() {
    if (m_Flags & 0x80) {
        // Merged animation - check both sub-animations
        if (m_Anim1 && m_Anim1->HasScaleInfo())
            return TRUE;
        if (m_Anim2 && m_Anim2->HasScaleInfo())
            return TRUE;
        return FALSE;
    }
    return m_KeyframeData && m_KeyframeData->m_ScaleController != 0;
}

CKBOOL RCKObjectAnimation::HasPositionInfo() {
    if (m_Flags & 0x80) {
        // Merged animation - check both sub-animations
        if (m_Anim1 && m_Anim1->HasPositionInfo())
            return TRUE;
        if (m_Anim2 && m_Anim2->HasPositionInfo())
            return TRUE;
        return FALSE;
    }
    return m_KeyframeData && m_KeyframeData->m_PositionController != 0;
}

CKBOOL RCKObjectAnimation::HasRotationInfo() {
    if (m_Flags & 0x80) {
        // Merged animation - check both sub-animations
        if (m_Anim1 && m_Anim1->HasRotationInfo())
            return TRUE;
        if (m_Anim2 && m_Anim2->HasRotationInfo())
            return TRUE;
        return FALSE;
    }
    return m_KeyframeData && m_KeyframeData->m_RotationController != 0;
}

CKBOOL RCKObjectAnimation::HasScaleAxisInfo() {
    if (m_Flags & 0x80) {
        // Merged animation - check both sub-animations
        if (m_Anim1 && m_Anim1->HasScaleAxisInfo())
            return TRUE;
        if (m_Anim2 && m_Anim2->HasScaleAxisInfo())
            return TRUE;
        return FALSE;
    }
    return m_KeyframeData && m_KeyframeData->m_ScaleAxisController != 0;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Adding Keys
//=============================================================================

void RCKObjectAnimation::AddPositionKey(float TimeStep, VxVector *pos) {
}

void RCKObjectAnimation::AddRotationKey(float TimeStep, VxQuaternion *rot) {
}

void RCKObjectAnimation::AddScaleKey(float TimeStep, VxVector *scl) {
}

void RCKObjectAnimation::AddScaleAxisKey(float TimeStep, VxQuaternion *sclaxis) {
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Comparison and Sharing
//=============================================================================

CKBOOL RCKObjectAnimation::Compare(CKObjectAnimation *anim, float threshold) {
    return FALSE;
}

CKBOOL RCKObjectAnimation::ShareDataFrom(CKObjectAnimation *anim) {
    RCKObjectAnimation *srcAnim = (RCKObjectAnimation *) anim;

    // Can't share from self
    if (srcAnim == this)
        return FALSE;

    // Release current keyframe data if we own it
    if (m_KeyframeData) {
        // Decrement reference count
        if (--m_KeyframeData->field_18 <= 0 && m_KeyframeData) {
            // Free the keyframe data
            delete m_KeyframeData;
        }
        m_KeyframeData = nullptr;
    }

    // Share from source animation or create new
    if (srcAnim) {
        // Share the keyframe data with source
        m_KeyframeData = srcAnim->m_KeyframeData;
        if (m_KeyframeData)
            ++m_KeyframeData->field_18; // Increment reference count
    } else {
        // Create new keyframe data
        m_KeyframeData = new CKKeyframeData();
        memset(m_KeyframeData, 0, sizeof(CKKeyframeData));
        m_KeyframeData->m_ObjectAnimation = this;
    }

    return TRUE;
}

CKObjectAnimation *RCKObjectAnimation::Shared() {
    if (m_KeyframeData)
        return m_KeyframeData->m_ObjectAnimation;
    return nullptr;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Flags
//=============================================================================

void RCKObjectAnimation::SetFlags(CKDWORD flags) {
    m_Flags = flags;
}

CKDWORD RCKObjectAnimation::GetFlags() {
    return m_Flags;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Clearing
//=============================================================================

void RCKObjectAnimation::Clear() {
    m_Flags = 0;
    m_Entity = nullptr;
    m_Anim1 = nullptr;
    m_Anim2 = nullptr;
    m_MergeFactor = 0.5f;
    m_CurrentStep = 0.0f;
}

void RCKObjectAnimation::ClearAll() {
    Clear();
    // Also clear keyframe data if we own it
    if (m_KeyframeData && m_KeyframeData->m_ObjectAnimation == this) {
        // Clear all controllers
        m_KeyframeData->m_PositionController = 0;
        m_KeyframeData->m_RotationController = 0;
        m_KeyframeData->m_ScaleController = 0;
        m_KeyframeData->m_ScaleAxisController = 0;
        m_KeyframeData->m_MorphController = nullptr;
        m_KeyframeData->m_Length = 0.0f;
    }
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Merged Animations
//=============================================================================

float RCKObjectAnimation::GetMergeFactor() {
    return m_MergeFactor;
}

void RCKObjectAnimation::SetMergeFactor(float factor) {
    m_MergeFactor = factor;
}

CKBOOL RCKObjectAnimation::IsMerged() {
    return m_IsMerged;
}

CKObjectAnimation *RCKObjectAnimation::CreateMergedAnimation(CKObjectAnimation *subanim2, CKBOOL Dynamic) {
    return nullptr;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Animation Length
//=============================================================================

void RCKObjectAnimation::SetLength(float nbframe) {
    if (m_KeyframeData)
        m_KeyframeData->m_Length = nbframe;
    m_Length = nbframe;
}

float RCKObjectAnimation::GetLength() {
    if (m_KeyframeData)
        return m_KeyframeData->m_Length;
    return m_Length;
}

void RCKObjectAnimation::GetVelocity(float step, VxVector *vel) {
    if (vel) {
        vel->x = 0.0f;
        vel->y = 0.0f;
        vel->z = 0.0f;
    }
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Current Position
//=============================================================================

CKERROR RCKObjectAnimation::SetStep(float step, CKKeyedAnimation *anim) {
    m_CurrentStep = step;
    return CK_OK;
}

CKERROR RCKObjectAnimation::SetFrame(float frame, CKKeyedAnimation *anim) {
    if (m_Length > 0.0f)
        m_CurrentStep = frame / m_Length;
    return CK_OK;
}

float RCKObjectAnimation::GetCurrentStep() {
    return m_CurrentStep;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - 3D Entity
//=============================================================================

void RCKObjectAnimation::Set3dEntity(CK3dEntity *ent) {
    m_Entity = reinterpret_cast<RCK3dEntity *>(ent);
}

CK3dEntity *RCKObjectAnimation::Get3dEntity() {
    return reinterpret_cast<CK3dEntity *>(m_Entity);
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Morph
//=============================================================================

int RCKObjectAnimation::GetMorphVertexCount() {
    return 0;
}

//=============================================================================
// CKObjectAnimation Virtual Methods - Transition and Clone
//=============================================================================

void RCKObjectAnimation::CreateTransition(float length, CKObjectAnimation *AnimIn, float StepFrom,
                                          CKObjectAnimation *AnimOut, float StepTo, CKBOOL Veloc, CKBOOL DontTurn,
                                          CKAnimKey *startingset) {
}

void RCKObjectAnimation::Clone(CKObjectAnimation *anim) {
}

//=============================================================================
// Static Class Methods (for class registration)
// Based on IDA Pro analysis of original CK2_3D.dll
//=============================================================================

CK_CLASSID RCKObjectAnimation::m_ClassID = CKCID_OBJECTANIMATION;

CKSTRING RCKObjectAnimation::GetClassName() {
    return (CKSTRING) "ObjectAnimation";
}

int RCKObjectAnimation::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKObjectAnimation::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKObjectAnimation::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(RCKObjectAnimation::m_ClassID, CKCID_3DENTITY);
    CKClassNeedNotificationFrom(RCKObjectAnimation::m_ClassID, CKCID_OBJECTANIMATION);
    CKClassRegisterAssociatedParameter(RCKObjectAnimation::m_ClassID, CKPGUID_OBJECTANIMATION);
    CKClassRegisterDefaultOptions(RCKObjectAnimation::m_ClassID, CK_GENERALOPTIONS_NODUPLICATENAMECHECK);
}

CKObjectAnimation *RCKObjectAnimation::CreateInstance(CKContext *Context) {
    return new RCKObjectAnimation(Context, nullptr);
}
