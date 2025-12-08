#include "RCKPlace.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCK3dEntity.h"
#include "CK3dEntity.h"
#include "CKCamera.h"

/**
 * @brief RCKPlace constructor
 * @param Context The CKContext instance
 * @param name Optional name for the place
 */
RCKPlace::RCKPlace(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_DefaultCamera(0),
      field_1B0(0) {
    // Initialize place properties
}

/**
 * @brief RCKPlace destructor
 */
RCKPlace::~RCKPlace() {
    // Cleanup any place-specific resources
}

/**
 * @brief Get the class ID for RCKPlace
 * @return The class ID (22)
 */
CK_CLASSID RCKPlace::GetClassID() {
    return 22;
}


/**
 * @brief Save the place data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKPlace::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // Create a new state chunk for place data
    CKStateChunk *chunk = CreateCKStateChunk(22, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Save default camera if flag is set
    if ((flags & 0x2000) != 0) {
        chunk->WriteIdentifier(0x2000u); // Default camera identifier
        CKObject *defaultCamera = m_Context->GetObject(m_DefaultCamera);
        chunk->WriteObject(defaultCamera);
    }

    // Save field_1B0 if saving to file and flag is set
    if (file && (flags & 0x8000) != 0) {
        chunk->WriteIdentifier(0x8000u); // Field 1B0 identifier
        CKObject *field1B0Obj = m_Context->GetObject(field_1B0);
        chunk->WriteObject(field1B0Obj);
    }

    // Save portals if they exist and flag is set
    if (m_Portals.Size() > 0 && (flags & 0x1000) != 0) {
        chunk->WriteIdentifier(0x1000u); // Portals identifier
        chunk->WriteInt(m_Portals.Size());

        // Save each portal entry (place, portal geometry)
        for (int i = 0; i < m_Portals.Size(); i++) {
            chunk->WriteObject(reinterpret_cast<CKObject *>(m_Portals[i].place));
            chunk->WriteObject(reinterpret_cast<CKObject *>(m_Portals[i].portal));
        }
    }

    // Close the chunk
    chunk->CloseChunk();

    return chunk;
}

/**
 * @brief Load place data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKPlace::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    // Update moveable flags based on visibility and other properties
    CKDWORD moveableFlags = GetMoveableFlags();
    if (IsVisible())
        moveableFlags |= 2u;
    else
        moveableFlags &= ~2u;

    // Update 2D entity flag (implementation specific)
    // if (Is2dEntity())
    //     moveableFlags |= 0x10000000u;
    // else
    //     moveableFlags &= ~0x10000000u;

    SetMoveableFlags(moveableFlags);

    // Only process file-specific data if loading from file
    if (!file)
        return CK_OK;

    // Load children objects
    if (chunk->SeekIdentifier(0x4000u)) {
        XObjectArray children;
        children.Load(chunk);

        for (unsigned int i = 0; i < children.Size(); ++i) {
            CK3dEntity *child = reinterpret_cast<CK3dEntity *>(children.GetObject(m_Context, i));
            if (child) {
                // Simplified child addition logic
                // In the original decompilation, this checks if the parent is not in the children array
                // For now, we'll add the child directly
                AddChild(child, true);
            }
        }
    }

    // Load default camera
    if (chunk->SeekIdentifier(0x2000u))
        m_DefaultCamera = chunk->ReadObjectID();

    // Load field_1B0
    if (chunk->SeekIdentifier(0x8000u))
        field_1B0 = chunk->ReadObjectID();

    // Load portals
    if (chunk->SeekIdentifier(0x1000u)) {
        int portalCount = chunk->ReadInt();
        m_Portals.Clear();
        m_Portals.Resize(portalCount);

        for (int j = 0; j < portalCount; ++j) {
            CKObject *placeObj = chunk->ReadObject(m_Context);
            CKObject *portalObj = chunk->ReadObject(m_Context);

            m_Portals[j].place = reinterpret_cast<CKPlace *>(placeObj);
            m_Portals[j].portal = reinterpret_cast<CK3dEntity *>(portalObj);
        }
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the place
 * @return Memory size in bytes
 */
int RCKPlace::GetMemoryOccupation() {
    return sizeof(RCKPlace) + m_Portals.Size() * sizeof(CKPortalEntry);
}

/**
 * @brief Copy place data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKPlace::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    RCK3dEntity::Copy(o, context);

    // Copy place specific data
    RCKPlace &target = (RCKPlace &) o;
    target.m_DefaultCamera = m_DefaultCamera;
    target.field_1B0 = field_1B0;
    target.m_Portals = m_Portals; // Assuming proper copy semantics for m_Portals

    return CK_OK;
}

// Static class registration methods
CKSTRING RCKPlace::GetClassName() {
    return (CKSTRING) "Place";
}

int RCKPlace::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKPlace::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKPlace::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_3DENTITY);
    CKClassNeedNotificationFrom(m_ClassID, CKCID_LEVEL);
    CKClassNeedNotificationFrom(m_ClassID, CKCID_CAMERA);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_PLACE);
}

CKPlace *RCKPlace::CreateInstance(CKContext *Context) {
    return reinterpret_cast<CKPlace *>(new RCKPlace(Context));
}

// Static class ID
CK_CLASSID RCKPlace::m_ClassID = CKCID_PLACE;

// CKPlace implementation stubs

CKCamera *RCKPlace::GetDefaultCamera() {
    return (CKCamera *) m_Context->GetObject(m_DefaultCamera);
}

void RCKPlace::SetDefaultCamera(CKCamera *cam) {
    m_DefaultCamera = cam ? cam->GetID() : 0;
}

void RCKPlace::AddPortal(CKPlace *place, CK3dEntity *portal) {
    // Portal can be NULL (place visible from anywhere in this place)
    // Portal geometry must have VX_PORTAL flag if specified
    if (portal && (portal->GetFlags() & 0x80000) == 0)
        return;

    // Check if this portal pair already exists
    CKPortalEntry searchEntry;
    searchEntry.place = place;
    searchEntry.portal = nullptr;

    // First check: does this (place, nullptr) exist?
    for (int i = 0; i < m_Portals.Size(); i++) {
        if (m_Portals[i].place == place && m_Portals[i].portal == nullptr)
            return; // Already have a "see everywhere" portal to this place
    }

    // Second check: does this exact (place, portal) exist?
    searchEntry.portal = portal;
    for (int i = 0; i < m_Portals.Size(); i++) {
        if (m_Portals[i].place == place && m_Portals[i].portal == portal)
            return; // Already exists
    }

    // Add the portal pair to this place
    CKPortalEntry entry;
    entry.place = place;
    entry.portal = portal;
    m_Portals.PushBack(entry);

    // Add the reverse portal to the destination place (bidirectional)
    if (place) {
        RCKPlace *destPlace = reinterpret_cast<RCKPlace *>(place);
        CKPortalEntry reverseEntry;
        reverseEntry.place = reinterpret_cast<CKPlace *>(this);
        reverseEntry.portal = portal;
        destPlace->m_Portals.PushBack(reverseEntry);
    }
}

void RCKPlace::RemovePortal(CKPlace *place, CK3dEntity *portal) {
    // Remove the portal pair from this place
    CKPortalEntry searchEntry;
    searchEntry.place = place;
    searchEntry.portal = portal;

    for (int i = 0; i < m_Portals.Size(); i++) {
        if (m_Portals[i].place == place && m_Portals[i].portal == portal) {
            m_Portals.RemoveAt(i);
            break;
        }
    }

    // Remove the reverse portal from the destination place
    if (place) {
        RCKPlace *destPlace = reinterpret_cast<RCKPlace *>(place);
        for (int i = 0; i < destPlace->m_Portals.Size(); i++) {
            if (destPlace->m_Portals[i].place == reinterpret_cast<CKPlace *>(this) &&
                destPlace->m_Portals[i].portal == portal) {
                destPlace->m_Portals.RemoveAt(i);
                break;
            }
        }
    }
}

int RCKPlace::GetPortalCount() {
    return m_Portals.Size();
}

CKPlace *RCKPlace::GetPortal(int i, CK3dEntity **portal) {
    if (i < 0 || i >= m_Portals.Size()) {
        if (portal) *portal = nullptr;
        return nullptr;
    }

    const CKPortalEntry &entry = m_Portals[i];
    if (portal)
        *portal = entry.portal;
    return entry.place;
}

VxRect &RCKPlace::ViewportClip() {
    // Return reference to clipping rect
    return m_ClippingRect;
}

CKBOOL RCKPlace::ComputeBestFitBBox(CKPlace *p2, VxMatrix &BBoxMatrix) {
    // TODO: Implement ComputeBestFitBBox
    return FALSE;
}
