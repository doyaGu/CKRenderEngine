#include "RCKBodyPart.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "RCK3dObject.h"
#include "CKCharacter.h"
#include "CKAnimation.h"

/**
 * @brief RCKBodyPart constructor
 * @param Context The CKContext instance
 * @param name Optional name for the body part
 */
RCKBodyPart::RCKBodyPart(CKContext *Context, CKSTRING name)
    : RCK3dObject(Context, name) {
    // Initialize body part specific fields
    field_1A8 = 0;
    field_1AC = 0;
    field_1B0 = 0;
    field_1B4 = 0;
    field_1B8 = 0;
    field_1BC = 0;
    field_1C0 = 0;
    field_1C4 = 0;
    field_1C8 = 0;
    field_1CC = 0;
    field_1D0 = 0;
    field_1D4 = 0;
}

/**
 * @brief RCKBodyPart destructor
 */
RCKBodyPart::~RCKBodyPart() {
    // Cleanup resources if needed
}

/**
 * @brief Get the class ID for RCKBodyPart
 * @return The class ID (42)
 */
CK_CLASSID RCKBodyPart::GetClassID() {
    return 42;
}

/**
 * @brief Save the body part data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKBodyPart::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // If no file and no special flags, return base chunk only
    if (!file && (flags & 0x7F000000) == 0)
        return baseChunk;

    // Create a new state chunk for body part data
    CKStateChunk *chunk = CreateCKStateChunk(42, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write body part specific data
    chunk->WriteIdentifier(0x4000000u); // Body part identifier

    // Write the referenced object at offset 424
    // Note: This offset needs to be verified with actual class layout
    CKObject **refObject = reinterpret_cast<CKObject **>(reinterpret_cast<uint8_t *>(this) + 424);
    chunk->WriteObject(*refObject);

    // Write additional data if certain flags are set
    if ((m_ObjectFlags & 0x40000) != 0) {
        // Write 40 bytes of data starting from offset 432
        void *dataPtr = reinterpret_cast<void *>(reinterpret_cast<uint8_t *>(this) + 432);
        chunk->WriteBufferNoSize_LEndian(40, dataPtr);
    }

    // Close or update the chunk based on class ID
    if (GetClassID() == 42)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Load body part data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKBodyPart::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    // Handle different data versions
    if (chunk->GetDataVersion() < 5) {
        // Legacy format handling for data version < 5
        if (chunk->SeekIdentifier(0x1000000u)) {
            // Read legacy vector data and populate fields
            VxVector vectors[3];
            // Read vector data directly using individual component reads
            for (int i = 0; i < 3; ++i) {
                vectors[i].x = chunk->ReadFloat();
                vectors[i].y = chunk->ReadFloat();
                vectors[i].z = chunk->ReadFloat();
            }

            // Map legacy data to current fields
            // Note: This mapping may need adjustment based on actual field layout
            field_1A8 = *(CKDWORD *) &vectors[0].x;
            field_1AC = *(CKDWORD *) &vectors[0].y;
            field_1B0 = *(CKDWORD *) &vectors[0].z;
            field_1B4 = *(CKDWORD *) &vectors[1].x;
            field_1B8 = *(CKDWORD *) &vectors[1].y;
            field_1BC = *(CKDWORD *) &vectors[1].z;
            field_1C0 = *(CKDWORD *) &vectors[2].x;
            field_1C4 = *(CKDWORD *) &vectors[2].y;
            field_1C8 = *(CKDWORD *) &vectors[2].z;

            // Set flags based on vector components
            CKDWORD flags = 0;
            for (int i = 0; i < 3; ++i) {
                if (*(float *) &vectors[0][i] != 0.0f)
                    flags |= (1 << (i - 1));
                if (*(float *) &vectors[1][i] != 0.0f)
                    flags |= (16 << (i - 1));
                if (*(float *) &vectors[2][i] != 0.0f)
                    flags |= (256 << (i - 1));
            }
            field_1CC = flags;
        }

        // Load referenced object
        if (chunk->SeekIdentifier(0x4000000u)) {
            CKObject **refObject = reinterpret_cast<CKObject **>(reinterpret_cast<uint8_t *>(this) + 424);
            *refObject = chunk->ReadObject(m_Context);
        }
    } else {
        // Modern format for data version >= 5
        if (chunk->SeekIdentifier(0x4000000u)) {
            // Load referenced object
            CKObject **refObject = reinterpret_cast<CKObject **>(reinterpret_cast<uint8_t *>(this) + 424);
            *refObject = chunk->ReadObject(m_Context);

            // Load additional data if flags are set
            // Note: This flag check needs to be verified with actual field layout
            if ((m_ObjectFlags & 0x40000) != 0) {
                // Read 40 bytes of data directly into the target location
                // This reads 10 floats (40 bytes) from the chunk
                float *dataPtr = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(this) + 432);
                for (int i = 0; i < 10; ++i) {
                    dataPtr[i] = chunk->ReadFloat();
                }
            }
        }
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the body part
 * @return Memory size in bytes
 */
int RCKBodyPart::GetMemoryOccupation() {
    return sizeof(RCKBodyPart);
}

/**
 * @brief Copy body part data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKBodyPart::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    RCK3dObject::Copy(o, context);

    // Copy body part specific data
    RCKBodyPart &target = (RCKBodyPart &) o;
    target.field_1A8 = field_1A8;
    target.field_1AC = field_1AC;
    target.field_1B0 = field_1B0;
    target.field_1B4 = field_1B4;
    target.field_1B8 = field_1B8;
    target.field_1BC = field_1BC;
    target.field_1C0 = field_1C0;
    target.field_1C4 = field_1C4;
    target.field_1C8 = field_1C8;
    target.field_1CC = field_1CC;
    target.field_1D0 = field_1D0;
    target.field_1D4 = field_1D4;

    return CK_OK;
}

// Static class registration methods
CKSTRING RCKBodyPart::GetClassName() {
    return (CKSTRING) "BodyPart";
}

int RCKBodyPart::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKBodyPart::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKBodyPart::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_CHARACTER);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_BODYPART);
}

CKBodyPart *RCKBodyPart::CreateInstance(CKContext *Context) {
    // Note: This should return CKBodyPart* but we're implementing RCKBodyPart
    // The actual implementation may need to handle the type conversion differently
    return reinterpret_cast<CKBodyPart *>(new RCKBodyPart(Context));
}

// Additional body part methods
CKCharacter *RCKBodyPart::GetCharacter() const {
    // Return the character this body part belongs to
    return nullptr; // Would need member variable to track this
}

void RCKBodyPart::SetExclusiveAnimation(const CKAnimation *anim) {
    // Set exclusive animation for this body part
}

CKAnimation *RCKBodyPart::GetExclusiveAnimation() const {
    return nullptr;
}

void RCKBodyPart::GetRotationJoint(CKIkJoint *joint) const {
    if (joint) {
        // Initialize joint with default values
        memset(joint, 0, sizeof(CKIkJoint));
    }
}

void RCKBodyPart::SetRotationJoint(const CKIkJoint *joint) {
    // Set rotation joint constraints
}

CKERROR RCKBodyPart::FitToJoint() {
    // Fit body part orientation to match joint constraints
    return CK_OK;
}

// Static class ID
CK_CLASSID RCKBodyPart::m_ClassID = CKCID_BODYPART;
