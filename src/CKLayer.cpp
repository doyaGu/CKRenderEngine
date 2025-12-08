#include "RCKLayer.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "RCKGrid.h"
#include "VxColor.h"

/**
 * @brief RCKLayer constructor
 * @param Context The CKContext instance
 * @param name Optional name for the layer
 */
RCKLayer::RCKLayer(CKContext *Context, CKSTRING name)
    : CKLayer(Context, name),
      m_Grid(nullptr),
      m_Type(0),
      m_Format(0),
      m_Flags(0),
      m_SquareArray(nullptr) {
    // Initialize layer properties
}

/**
 * @brief RCKLayer destructor
 */
RCKLayer::~RCKLayer() {
    // Cleanup square array if it exists
    if (m_SquareArray) {
        delete[] (char *) m_SquareArray;
        m_SquareArray = nullptr;
    }
}

/**
 * @brief Get the class ID for RCKLayer
 * @return The class ID (51)
 */
CK_CLASSID RCKLayer::GetClassID() {
    return 51;
}

/**
 * @brief Save the layer data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 */
CKStateChunk *RCKLayer::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = CKObject::Save(file, flags);

    // Create a new state chunk for layer data
    CKStateChunk *chunk = CreateCKStateChunk(51, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Get the appropriate manager by GUID
    // CKGUID managerGuid(2130724753, 0);
    // CKObject *manager = m_Context->GetManagerByGuid(managerGuid);
    // Note: Manager functionality is commented out due to missing CKGUID implementation
    // This would normally handle color and type management

    // Save layer data if file context or specific flags are set
    if (file || (flags & 0x10) != 0) {
        chunk->WriteIdentifier(0x10u); // Layer identifier

        // Write layer properties
        chunk->WriteObject(m_Grid);

        // Write type only if not saving to file
        if (!file) {
            chunk->WriteInt(m_Type);
        }

        chunk->WriteInt(m_Format);

        // Handle file-specific saving
        if (file) {
            // Write version indicator
            chunk->WriteInt(3);

            // Get and write color data
            VxColor layerColor;
            // Implementation would get color from manager
            // manager->GetColor(m_Type, &layerColor);
            CKDWORD colorValue = 0; // Placeholder for color conversion
            chunk->WriteDword(colorValue);

            // Get and write GUID data
            // CKGUID layerGuid;
            // Implementation would get GUID from manager
            // manager->GetGuid(m_Type, &layerGuid);
            // chunk->WriteGuid(layerGuid);
        }

        chunk->WriteInt(m_Flags);

        // Save square array data if format is 0 and grid exists
        if (!m_Format && m_Grid) {
            int gridWidth = m_Grid->GetWidth();
            int gridLength = m_Grid->GetLength();
            int bufferSize = 4 * gridLength * gridWidth;
            chunk->WriteBuffer_LEndian(bufferSize, m_SquareArray);
        }

        // Update manager counters if saving to file
        if (file) {
            // Implementation would update manager counters
            // This is handled by the manager internally
        }
    }

    // Close the chunk
    chunk->CloseChunk();

    return chunk;
}

/**
 * @brief Load layer data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 */
CKERROR RCKLayer::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    CKObject::Load(chunk, file);

    // Load layer data if identifier is found
    if (chunk->SeekIdentifier(0x10u)) {
        // Get the appropriate manager by GUID
        // CKGUID managerGuid(2130724753, 0);
        // CKObject *manager = m_Context->GetManagerByGuid(managerGuid);
        // Note: Manager functionality is commented out due to missing CKGUID implementation
        // This would normally handle color and type management

        // Determine layer type (implementation specific)
        int layerType = 0; // Placeholder for type determination
        m_Type = layerType;

        // If type is 0, get default type from manager
        if (!m_Type) {
            // Implementation would get default type from manager
            // m_Type = manager->GetDefaultType(layerType);
        }

        // Load grid reference
        m_Grid = reinterpret_cast<RCKGrid *>(chunk->ReadObject(m_Context));

        // Skip type if not in file context
        if (!file) {
            chunk->ReadInt();
        }

        // Load format
        m_Format = chunk->ReadInt();

        // Handle file-specific loading
        if (file) {
            int version = chunk->ReadInt();
            if (version >= 1) {
                // Load color data
                CKDWORD colorValue = chunk->ReadDword();
                VxColor layerColor;
                // Implementation would convert color value
                // ConvertColorValue(colorValue, &layerColor);

                // Set color in manager
                // manager->SetColor(m_Type, &layerColor);

                // Handle different versions
                if (version < 2) {
                    m_Flags = 1;
                } else {
                    if (version < 3) {
                        // Set default GUID for older versions
                        // CKGUID defaultGuid(1515656957, 1155692247);
                        // manager->SetGuid(m_Type, defaultGuid);
                    } else {
                        // Load GUID from chunk
                        // CKGUID layerGuid;
                        // chunk->ReadGuid(&layerGuid);
                        // manager->SetGuid(m_Type, layerGuid);
                    }

                    // Load flags
                    m_Flags = chunk->ReadInt();
                }
            }
        } else {
            // Load flags directly for non-file context
            m_Flags = chunk->ReadInt();
        }

        // Cleanup existing square array
        if (m_SquareArray) {
            delete[] (char *) m_SquareArray;
            m_SquareArray = nullptr;
        }

        // Load square array data if format is 0
        if (!m_Format) {
            int bufferSize = chunk->ReadBuffer(reinterpret_cast<void **>(&m_SquareArray));
            if (bufferSize > 0) {
                CKConvertEndianArray32(m_SquareArray, bufferSize / sizeof(CKDWORD));
            }
        }
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the layer
 * @return Memory size in bytes
 */
int RCKLayer::GetMemoryOccupation() {
    int squareArraySize = 0;
    if (m_SquareArray && m_Grid) {
        squareArraySize = 4 * m_Grid->GetLength() * m_Grid->GetWidth();
    }
    return sizeof(RCKLayer) + squareArraySize;
}

/**
 * @brief Copy layer data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKLayer::Copy(CKObject &o, CKDependenciesContext &context) {
    // Base class copy
    CKLayer::Copy(o, context);

    // Copy layer specific data
    RCKLayer &target = (RCKLayer &) o;
    target.m_Grid = m_Grid;
    target.m_Type = m_Type;
    target.m_Format = m_Format;
    target.m_Flags = m_Flags;

    // Copy square array if it exists
    if (m_SquareArray && m_Grid) {
        int arraySize = 4 * m_Grid->GetLength() * m_Grid->GetWidth();
        target.m_SquareArray = new char[arraySize];
        memcpy(target.m_SquareArray, m_SquareArray, arraySize);
    } else {
        target.m_SquareArray = nullptr;
    }

    return CK_OK;
}

// Static class registration methods
CKSTRING RCKLayer::GetClassName() {
    return "Layer";
}

int RCKLayer::GetDependenciesCount(int mode) {
    return 0; // No dependencies
}

CKSTRING RCKLayer::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKLayer::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_GRID);
    CKClassRegisterDefaultOptions(m_ClassID, CK_GENERALOPTIONS_NODUPLICATENAMECHECK);
    CKClassRegisterDefaultDependencies(m_ClassID, 1, CK_DEPENDENCIES_COPY);
}

CKLayer *RCKLayer::CreateInstance(CKContext *Context) {
    // Abstract class - cannot instantiate directly
    // return reinterpret_cast<CKLayer *>(new RCKLayer(Context));
    return nullptr;
}

// Static class ID
CK_CLASSID RCKLayer::m_ClassID = CKCID_LAYER;
