#include "RCKLayer.h"

#include "VxColor.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKGridManager.h"

#include <climits>
#include <new>

static CKBOOL LayerCellCount(CKGrid *grid, int &count) {
    count = 0;
    if (!grid)
        return TRUE;
    const int width = grid->GetWidth();
    const int length = grid->GetLength();
    if (width < 0 || length < 0 || (length && width > INT_MAX / length))
        return FALSE;
    count = width * length;
    return static_cast<size_t>(count) <= static_cast<size_t>(-1) / sizeof(CKSquare);
}

/**
 * @brief RCKLayer constructor
 * @param Context The CKContext instance
 * @param name Name for the layer
 * @param owner Owner grid ID
 */
RCKLayer::RCKLayer(CKContext *Context, CKSTRING name, CK_ID owner)
    : CKLayer(Context, name),
      m_Grid(nullptr),
      m_Type(1),
      m_Format(0),
      m_Flags(1),
      m_SquareArray(nullptr),
      m_SquareCount(0) {
    // Get owner grid from context
    m_Grid = reinterpret_cast<CKGrid *>(m_Context->GetObject(owner));

    int count;
    if (LayerCellCount(m_Grid, count) && count > 0) {
        m_SquareArray = new(std::nothrow) CKSquare[count]();
        if (m_SquareArray)
            m_SquareCount = count;
    }
}

/**
 * @brief RCKLayer destructor
 */
RCKLayer::~RCKLayer() {
    // Cleanup square array unconditionally (operator delete handles nullptr)
    delete[] m_SquareArray;
}

/**
 * @brief Get the class ID for RCKLayer
 * @return The class ID (51)
 */
CK_CLASSID RCKLayer::GetClassID() {
    return m_ClassID;
}

//=============================================================================
// CKLayer Virtual Methods
//=============================================================================

void RCKLayer::SetType(int Type) {
    m_Type = static_cast<CKDWORD>(Type);
}

int RCKLayer::GetType() {
    return static_cast<int>(m_Type);
}

void RCKLayer::SetFormat(int Format) {
    m_Format = static_cast<CKDWORD>(Format);
}

int RCKLayer::GetFormat() {
    return static_cast<int>(m_Format);
}

void RCKLayer::SetValue(int x, int y, void *val) {
    // Original directly accesses array without null checks
    const int width = m_Grid->GetWidth();
    const int idx = y * width + x;
    m_SquareArray[idx].ival = *static_cast<CKDWORD *>(val);
}

void RCKLayer::GetValue(int x, int y, void *val) {
    // Original directly accesses array without null checks
    const int width = m_Grid->GetWidth();
    const int idx = y * width + x;
    *static_cast<CKDWORD *>(val) = m_SquareArray[idx].ival;
}

CKBOOL RCKLayer::SetValue2(int x, int y, void *val) {
    const int width = m_Grid->GetWidth();
    const int length = m_Grid->GetLength();
    // Original checks bounds: x >= width || x < 0 || y >= length || y < 0
    if (x >= width || x < 0 || y >= length || y < 0)
        return FALSE;
    m_SquareArray[y * width + x].ival = *static_cast<CKDWORD *>(val);
    return TRUE;
}

CKBOOL RCKLayer::GetValue2(int x, int y, void *val) {
    const int width = m_Grid->GetWidth();
    const int length = m_Grid->GetLength();
    // Original checks bounds: x >= width || x < 0 || y >= length || y < 0
    if (x >= width || x < 0 || y >= length || y < 0) {
        *static_cast<CKDWORD *>(val) = 0;
        return FALSE;
    }
    *static_cast<CKDWORD *>(val) = m_SquareArray[y * width + x].ival;
    return TRUE;
}

CKSquare *RCKLayer::GetSquareArray() {
    return m_SquareArray;
}

void RCKLayer::SetSquareArray(CKSquare *sqarray) {
    // Native transfers the current pointer to the layer without releasing
    // the prior pointer. A replacing caller manages that previous allocation.
    m_SquareArray = sqarray;
    // The caller's array size is unknown; it must match the grid, as before.
    m_SquareCount = -1;
}

void RCKLayer::FitSquareArray() {
    FitSquareArrayForLoad();
}

CKERROR RCKLayer::FitSquareArrayForLoad() {
    if (!m_Grid || m_Format != 0 || m_SquareCount < 0)
        return CK_OK;
    // Zero dimensions mean the owner grid has not been loaded yet.
    int count;
    if (!LayerCellCount(m_Grid, count))
        return CKERR_INVALIDPARAMETER;
    if (count == 0)
        return CK_OK;
    if (count == m_SquareCount)
        return CK_OK;
    CKSquare *squares = new(std::nothrow) CKSquare[count]();
    if (!squares)
        return CKERR_OUTOFMEMORY;
    const int kept = m_SquareCount < count ? m_SquareCount : count;
    if (kept > 0)
        memcpy(squares, m_SquareArray, static_cast<size_t>(kept) * sizeof(CKSquare));
    delete[] m_SquareArray;
    m_SquareArray = squares;
    m_SquareCount = count;
    return CK_OK;
}

void RCKLayer::SetVisible(CKBOOL vis) {
    if (vis)
        m_Flags |= LAYER_STATE_VISIBLE;
    else
        m_Flags &= ~LAYER_STATE_VISIBLE;
}

CKBOOL RCKLayer::IsVisible() {
    return (m_Flags & LAYER_STATE_VISIBLE) != 0;
}

void RCKLayer::InitOwner(CK_ID owner) {
    CKGrid *grid = reinterpret_cast<CKGrid *>(m_Context->GetObject(owner));
    int count;
    if (!LayerCellCount(grid, count))
        return;
    CKSquare *squares = count > 0 ? new(std::nothrow) CKSquare[count]() : nullptr;
    if (count > 0 && !squares)
        return;
    // Prepare the new owner's cells before committing its dimensions/owner.
    delete[] m_SquareArray;
    m_Grid = grid;
    m_SquareArray = squares;
    m_SquareCount = count;
}

void RCKLayer::SetOwner(CK_ID owner) {
    // Original simply sets m_Grid from context lookup without class validation
    m_Grid = reinterpret_cast<CKGrid *>(m_Context->GetObject(owner));
}

CK_ID RCKLayer::GetOwner() {
    return m_Grid ? m_Grid->GetID() : 0;
}

void RCKLayer::CheckPreDeletion() {
    if (m_Grid && m_Grid->IsToBeDeleted())
        m_Grid = nullptr;
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
    CKStateChunk *chunk = CreateCKStateChunk(m_ClassID, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Native Save requires the grid manager even for memory-only saves.
    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr) {
        DeleteCKStateChunk(chunk);
        return nullptr;
    }

    // Only write layer payload if saving to file or flag 0x10 is set
    if (file || (flags & 0x10u) != 0) {
        chunk->WriteIdentifier(0x10u);
        chunk->WriteObject(reinterpret_cast<CKObject *>(m_Grid));

        // When saving to memory, write the raw type index
        if (!file) {
            chunk->WriteInt(static_cast<int>(m_Type));
        }

        chunk->WriteInt(static_cast<int>(m_Format));

        // When saving to a file, write version and metadata
        if (file) {
            chunk->WriteInt(3); // version

            VxColor color;
            color.Set(0.0f, 0.0f, 0.0f, 0.0f);
            if (gridMgr)
                gridMgr->GetAssociatedColor(m_Type, &color);
            chunk->WriteDword(color.GetRGBA());

            CKGUID paramGuid = CKGUID(0, 0);
            if (gridMgr)
                paramGuid = gridMgr->GetAssociatedParam(m_Type);
            chunk->WriteGuid(paramGuid);
        }

        chunk->WriteInt(static_cast<int>(m_Flags));

        // Write square array data if format is 0 and grid exists
        if (m_Format == 0 && m_Grid) {
            int count;
            if (!LayerCellCount(m_Grid, count) || count > INT_MAX / static_cast<int>(sizeof(CKDWORD)) - 1) {
                DeleteCKStateChunk(chunk);
                return nullptr;
            }
            // CKSquare also contains a pointer; its host size can exceed the
            // four-byte values stored by the original Win32 engine.
            CKDWORD *values = count > 0 ? new(std::nothrow) CKDWORD[count] : nullptr;
            if (count > 0 && !values) {
                DeleteCKStateChunk(chunk);
                return nullptr;
            }
            // Cells loaded before the grid was resized may be fewer than count.
            const int stored = m_SquareArray ? (m_SquareCount < 0 ? count : m_SquareCount) : 0;
            for (int i = 0; i < count; ++i)
                values[i] = i < stored ? m_SquareArray[i].dval : 0;
            try {
                chunk->WriteBuffer_LEndian(count * sizeof(CKDWORD), values);
            } catch (const std::bad_alloc &) {
                delete[] values;
                DeleteCKStateChunk(chunk);
                return nullptr;
            }
            delete[] values;
        }

        // Mark type as used in file
        if (file) {
            // m_Remap holds one entry per type registered at PreSave.
            if (gridMgr && gridMgr->m_Remap && m_Type >= 0 && m_Type < gridMgr->GetLayerTypeCount() &&
                !gridMgr->m_Remap[m_Type])
                gridMgr->m_Remap[m_Type] = gridMgr->m_RemapCount++;
        }
    }

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
        CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
        if (!gridMgr)
            return CKERR_NOTINITIALIZED;

        // Determine layer type from this object's name (Virtools convention)
        const CKSTRING layerName = GetName();
        m_Type = gridMgr->GetTypeFromName(layerName);
        if (m_Type == 0)
            m_Type = gridMgr->RegisterType(layerName);

        // Load grid reference
        m_Grid = reinterpret_cast<CKGrid *>(chunk->ReadObject(m_Context));

        if (!file) {
            // Skip type index in runtime chunks (type is re-derived from layer name)
            chunk->ReadInt();
        }

        // Load format
        m_Format = chunk->ReadInt();

        // Handle file-specific loading
        if (file) {
            const int version = chunk->ReadInt();
            if (version >= 1) {
                const CKDWORD colorValue = chunk->ReadDword();
                VxColor layerColor(colorValue);
                gridMgr->SetAssociatedColor(static_cast<int>(m_Type), &layerColor);

                if (version < 2) {
                    m_Flags = 1; // LAYER_STATE_VISIBLE
                } else {
                    if (version < 3) {
                        // Historical default associated param GUID for older files
                        gridMgr->SetAssociatedParam(static_cast<int>(m_Type), CKGUID(0x5A5716FD, 0x44E276D7));
                    } else {
                        const CKGUID paramGuid = chunk->ReadGuid();
                        gridMgr->SetAssociatedParam(static_cast<int>(m_Type), paramGuid);
                    }
                    m_Flags = static_cast<CKDWORD>(chunk->ReadInt());
                }
            }
        } else {
            // Load flags directly for non-file context
            m_Flags = static_cast<CKDWORD>(chunk->ReadInt());
        }

        // Prepare the complete owner-sized array before replacing old cells.
        CKSquare *squares = nullptr;
        int count = 0;
        if (!m_Format) {
            void *raw = nullptr;
            int bufferSize;
            try {
                bufferSize = chunk->ReadBuffer(&raw);
            } catch (const std::bad_alloc &) {
                return CKERR_OUTOFMEMORY;
            }
            const int stored = raw && bufferSize > 0 ? bufferSize / sizeof(CKDWORD) : 0;
            count = stored;
            int ownerCount;
            if (!LayerCellCount(m_Grid, ownerCount)) {
                CKDeletePointer(raw);
                return CKERR_INVALIDPARAMETER;
            }
            // Pending grid dimensions leave the serialized capacity intact.
            // Known dimensions retain the prefix and zero-fill its extension.
            if (ownerCount > 0)
                count = ownerCount;
            if (count > 0) {
                squares = new(std::nothrow) CKSquare[count]();
                if (!squares) {
                    CKDeletePointer(raw);
                    return CKERR_OUTOFMEMORY;
                }
                CKDWORD *values = static_cast<CKDWORD *>(raw);
                const int copied = stored < count ? stored : count;
                CKConvertEndianArray32(values, copied);
                for (int i = 0; i < copied; ++i)
                    squares[i].dval = values[i];
            }
            CKDeletePointer(raw);
        }
        delete[] m_SquareArray;
        m_SquareArray = squares;
        m_SquareCount = count;
    }

    return CK_OK;
}

CKERROR RCKLayer::PrepareDependencies(CKDependenciesContext &context) {
    // The native layer uses CKObject's implementation. Its owner is a
    // reference to remap, not an object to copy or delete with the layer.
    return CKObject::PrepareDependencies(context);
}

CKERROR RCKLayer::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKObject::RemapDependencies(context);
    if (err != CK_OK)
        return err;

    m_Grid = static_cast<CKGrid *>(context.Remap(reinterpret_cast<CKObject *>(m_Grid)));
    return CK_OK;
}

/**
 * @brief Get memory occupation for the layer
 * @return Memory size in bytes
 */
int RCKLayer::GetMemoryOccupation() {
    return CKObject::GetMemoryOccupation() + (sizeof(RCKLayer) - sizeof(CKObject));
}

/**
 * @brief Copy layer data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 */
CKERROR RCKLayer::Copy(CKObject &o, CKDependenciesContext &context) {
    if (this == &o)
        return CK_OK;
    RCKLayer *src = static_cast<RCKLayer *>(&o);
    int count;
    if (!LayerCellCount(src->m_Grid, count))
        return CKERR_INVALIDPARAMETER;
    CKSquare *squares = count > 0 ? new(std::nothrow) CKSquare[count]() : nullptr;
    if (count > 0 && !squares)
        return CKERR_OUTOFMEMORY;
    const CKDWORD classDeps = context.GetClassDependencies(CKCID_LAYER);
    if (squares) {
        const int stored = !src->m_SquareArray ? 0 : (src->m_SquareCount < 0 ? count : src->m_SquareCount);
        const int copied = stored < count ? stored : count;
        if ((classDeps & 1) && copied > 0)
            memcpy(squares, src->m_SquareArray, static_cast<size_t>(copied) * sizeof(CKSquare));
    }
    const CKERROR err = CKObject::Copy(o, context);
    if (err != CK_OK) {
        delete[] squares;
        return err;
    }
    delete[] m_SquareArray;
    m_Grid = src->m_Grid;
    m_Type = src->m_Type;
    m_Format = src->m_Format;
    m_Flags = src->m_Flags;
    m_SquareArray = squares;
    m_SquareCount = count;
    return CK_OK;
}

// Static class registration methods
CKSTRING RCKLayer::GetClassName() {
    return "Grid Layer";
}

int RCKLayer::GetDependenciesCount(int mode) {
    return mode == CK_DEPENDENCIES_COPY ? 1 : 0;
}

CKSTRING RCKLayer::GetDependencies(int i, int mode) {
    return mode == CK_DEPENDENCIES_COPY && i == 0 ? "Layer Data" : nullptr;
}

void RCKLayer::Register() {
    // Based on IDA decompilation
    CKCLASSNOTIFYFROMCID(RCKLayer, CKCID_GRID);
    CKCLASSDEFAULTOPTIONS(RCKLayer, CK_GENERALOPTIONS_NODUPLICATENAMECHECK);
    CKCLASSDEFAULTCOPYDEPENDENCIES(RCKLayer, CK_DEPENDENCIES_COPY);
}

CKLayer *RCKLayer::CreateInstance(CKContext *Context) {
    return new(std::nothrow) RCKLayer(Context, nullptr, 0);
}

// Static class ID
CK_CLASSID RCKLayer::m_ClassID = CKCID_LAYER;
