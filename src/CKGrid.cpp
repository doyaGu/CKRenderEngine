#include "RCKGrid.h"

#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKObject.h"
#include "CKBeObject.h"
#include "CKGridManager.h"
#include "CKScene.h"
#include "CKLevel.h"
#include "CKAttributeManager.h"
#include "RCK3dEntity.h"
#include "RCKLayer.h"
#include "RCKMaterial.h"
#include "RCKMesh.h"
#include "RCKTexture.h"

#include <new>

// Static class ID
CK_CLASSID RCKGrid::m_ClassID = CKCID_GRID;

/**
 * @brief RCKGrid constructor
 * @param Context The CKContext instance
 * @param name Optional name for the grid
 */
RCKGrid::RCKGrid(CKContext *Context, CKSTRING name)
    : RCK3dEntity(Context, name),
      m_Width(0),
      m_Length(0),
      m_Priority(0),
      m_OrientationMode(CKGRID_FREE),
      m_Mesh(nullptr) {
    // Set default scale as per original implementation
    VxVector scale(1.0f, 10.0f, 1.0f);
    SetScale(&scale, FALSE, TRUE);

    // Set Grid attribute
    CKAttributeManager *attrMgr = Context->GetAttributeManager();
    if (attrMgr) {
        CKAttributeType attrType = attrMgr->GetAttributeTypeByName("Grid");
        SetAttribute(attrType, 0);
    }
}

/**
 * @brief RCKGrid destructor
 */
RCKGrid::~RCKGrid() {
    // IDA: 0x100179a1 - destructor just clears the layers array
    // The base class destructor handles mesh cleanup
    m_Layers.Clear();
}

/**
 * @brief Get the class ID for RCKGrid
 * @return The class ID (50)
 */
CK_CLASSID RCKGrid::GetClassID() {
    return 50;
}

/**
 * @brief Update the bounding box for the grid
 * @param World If TRUE, also update world bounding box
 * IDA: 0x100179f8
 */
void RCKGrid::UpdateBox(CKBOOL World) {
    // Set local bounding box from (0,0,0) to (width, 1, length)
    m_LocalBoundingBox.Min.Set(0.0f, 0.0f, 0.0f);
    m_LocalBoundingBox.Max.Set((float) m_Width, 1.0f, (float) m_Length);

    if (World) {
        m_WorldBoundingBox.TransformFrom(m_LocalBoundingBox, m_WorldMatrix);
    }

    m_MoveableFlags |= 4;
}

/**
 * @brief Pre-save callback to prepare layers for saving
 * @param file The CKFile being saved to
 * @param flags Save flags
 * IDA: 0x100197e1
 */
void RCKGrid::PreSave(CKFile *file, CKDWORD flags) {
    // Clear mesh array and restore current mesh if present
    m_Meshes.Clear();
    if (m_CurrentMesh) {
        m_Meshes.PushBack(m_CurrentMesh);
    }

    // Call base class PreSave
    RCK3dEntity::PreSave(file, flags);

    // Save all layer objects to the file
    for (int i = 0; i < m_Layers.Size(); ++i) {
        CKObject *layerObject = m_Context->GetObject(m_Layers[i]);
        if (layerObject) {
            file->SaveObject(layerObject, flags);
        }
    }
}


/**
 * @brief Save the grid data to a state chunk
 * @param file The CKFile to save to (can be nullptr for standalone saving)
 * @param flags Save flags
 * @return A new CKStateChunk containing the saved data
 * IDA: 0x1001987d
 */
CKStateChunk *RCKGrid::Save(CKFile *file, CKDWORD flags) {
    // Call base class Save first
    CKStateChunk *baseChunk = RCK3dEntity::Save(file, flags);

    // If no file and no special flags, return base chunk only
    if (!file && !(flags & CK_STATESAVE_GRIDONLY))
        return baseChunk;

    // Create a new state chunk for grid data
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_GRID, file);
    chunk->StartWrite();

    // Add the base class chunk
    chunk->AddChunkAndDelete(baseChunk);

    // Write grid specific data
    chunk->WriteIdentifier(CK_STATESAVE_GRIDDATA); // Grid identifier

    // Write grid properties
    chunk->WriteInt(m_Width);
    chunk->WriteInt(m_Length);
    chunk->WriteInt(0); // Reserved field
    chunk->WriteInt(m_Priority);
    chunk->WriteDword(m_OrientationMode);

    // Write file context flag if saving to file
    if (file) {
        chunk->WriteInt(1);
    }

    // Save layers using XObjectArray::Save with context
    m_Layers.Save(chunk, m_Context);

    // If not saving to file, save layer sub-chunks
    if (!file) {
        for (int i = 0; i < m_Layers.Size(); ++i) {
            CKObject *layerObject = m_Context->GetObject(m_Layers[i]);
            CKStateChunk *layerChunk = layerObject ? layerObject->Save(nullptr, flags) : nullptr;
            if (layerObject && !layerChunk) {
                DeleteCKStateChunk(chunk);
                return nullptr;
            }
            // Load consumes one subchunk for every ID, including missing ones.
            chunk->WriteSubChunk(layerChunk);
            DeleteCKStateChunk(layerChunk);
        }
    }

    // Close or update the chunk based on class ID
    if (GetClassID() == CKCID_GRID)
        chunk->CloseChunk();
    else
        chunk->UpdateDataSize();

    return chunk;
}

/**
 * @brief Load grid data from a state chunk
 * @param chunk The CKStateChunk containing the saved data
 * @param file The CKFile being loaded from
 * @return CKERROR indicating success or failure
 * IDA: 0x10019a11
 */
CKERROR RCKGrid::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk)
        return CKERR_INVALIDPARAMETER;

    // Call base class Load first
    RCK3dEntity::Load(chunk, file);

    // Load grid data if identifier is found
    if (chunk->SeekIdentifier(CK_STATESAVE_GRIDDATA)) {
        // Read grid properties
        const CKDWORD width = chunk->ReadInt();
        const CKDWORD length = chunk->ReadInt();
        if (m_Width != width || m_Length != length)
            DestroyMeshTexture();
        m_Width = width;
        m_Length = length;
        chunk->ReadInt(); // Skip reserved field
        m_Priority = chunk->ReadInt();
        m_OrientationMode = chunk->ReadDword();

        // Read file context flag if loading from file
        if (file) {
            chunk->ReadInt();
        }

        // Load layers using XObjectArray::Load
        m_Layers.Load(chunk);

        // If not loading from file, load layer sub-chunks
        if (!file) {
            for (int i = 0; i < m_Layers.Size(); ++i) {
                CKObject *layerObject = m_Context->GetObject(m_Layers[i]);
                CKStateChunk *subChunk = chunk->ReadSubChunk();
                CKERROR error = CK_OK;
                if (layerObject) {
                    error = layerObject->Load(subChunk, nullptr);
                }
                DeleteCKStateChunk(subChunk);
                if (error != CK_OK)
                    return error;
            }
        }

        // Check and validate loaded layers
        m_Layers.Check(m_Context);

        // Layers loaded before these dimensions sized their cells from the file.
        for (int i = 0; i < m_Layers.Size(); ++i) {
            CKObject *layerObject = m_Context->GetObject(m_Layers[i]);
            if (layerObject && layerObject->GetClassID() == CKCID_LAYER) {
                CKERROR error = static_cast<RCKLayer *>(layerObject)->FitSquareArrayForLoad();
                if (error != CK_OK)
                    return error;
            }
        }
    }

    return CK_OK;
}

/**
 * @brief Get memory occupation for the grid
 * @return Memory size in bytes
 * IDA: 0x1001970b
 */
int RCKGrid::GetMemoryOccupation() {
    // Base class memory + grid specific fields
    int size = RCK3dEntity::GetMemoryOccupation() + (sizeof(RCKGrid) - sizeof(RCK3dEntity));

    // Add memory for layers array internal storage
    size += m_Layers.GetMemoryOccupation(FALSE);

    // Add memory for layer cell data: sizeof(CKDWORD) * width * length * layerCount
    size += sizeof(CKDWORD) * m_Width * m_Length * m_Layers.Size();

    return size;
}

/**
 * @brief Check if a specific object is used by this grid
 * @param obj The object to check
 * @param cid The class ID to check for
 * @return TRUE if the object is used
 * IDA: 0x1001976c
 */
CKBOOL RCKGrid::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    // Check if it's a layer and exists in our layers array
    if (cid == CKCID_LAYER && m_Layers.FindObject(obj)) {
        return TRUE;
    }
    // Otherwise delegate to base class
    return RCK3dEntity::IsObjectUsed(obj, cid);
}

CKERROR RCKGrid::PrepareDependencies(CKDependenciesContext &context) {
    // IDA: 0x10019c98
    // If operation mode is CK_DEPENDENCIES_COPY (1), clear current mesh first
    if (context.IsInMode(CK_DEPENDENCIES_COPY))
        SetCurrentMesh(nullptr, TRUE);

    CKERROR err = RCK3dEntity::PrepareDependencies(context);
    if (err != CK_OK)
        return err;

    // Get class dependencies for CKCID_GRID
    CKDWORD classDeps = context.GetClassDependencies(CKCID_GRID);

    // If not copy mode or layers dependency flag set, prepare layers
    if (!context.IsInMode(CK_DEPENDENCIES_COPY) || (classDeps & 1) != 0)
        m_Layers.Prepare(context);

    // If operation mode is CK_DEPENDENCIES_DELETE (2), prepare mesh and materials for deletion
    if (context.IsInMode(CK_DEPENDENCIES_DELETE)) {
        if (m_Mesh) {
            m_Mesh->PrepareDependencies(context);
            CKMaterial *mat0 = m_Mesh->GetFaceMaterial(0);
            if (mat0) {
                mat0->PrepareDependencies(context);
                CKTexture *tex = mat0->GetTexture(0);
                if (tex)
                    tex->PrepareDependencies(context);
            }
            CKMaterial *mat2 = m_Mesh->GetFaceMaterial(2);
            if (mat2)
                mat2->PrepareDependencies(context);
        }
        m_Mesh = nullptr;
    }

    return context.FinishPrepareDependencies(this, m_ClassID);
}

CKERROR RCKGrid::RemapDependencies(CKDependenciesContext &context) {
    // IDA: 0x10019e09
    CKERROR err = RCK3dEntity::RemapDependencies(context);
    if (err != CK_OK)
        return err;

    // If layers dependency flag set, remap layers
    CKDWORD classDeps = context.GetClassDependencies(CKCID_GRID);
    if ((classDeps & 1) != 0)
        m_Layers.Remap(context);

    // If visible, construct mesh texture
    if (IsVisible())
        ConstructMeshTexture(0.5f);

    return CK_OK;
}

/**
 * @brief Copy grid data to another object
 * @param o The target object
 * @param context Dependencies context
 * @return CKERROR indicating success or failure
 * IDA: 0x10019e7f
 */
CKERROR RCKGrid::Copy(CKObject &o, CKDependenciesContext &context) {
    if (&o == this)
        return CK_OK;

    // Virtools convention: Copy is called on the destination object, and 'o' is the source.
    // Base class copy first.
    CKERROR err = RCK3dEntity::Copy(o, context);
    if (err != CK_OK)
        return err;

    // Entity copying replaces the public mesh references, but the old private
    // Grid display is still ours to release after the base copy succeeds.
    DestroyMeshTexture();

    RCKGrid &src = static_cast<RCKGrid &>(o);

    // Get class dependencies for CKCID_GRID
    CKDWORD classDeps = context.GetClassDependencies(CKCID_GRID);

    // Save source mesh before copy (may be cleared by base)
    RCKMesh *srcMesh = src.m_Mesh;

    m_Width = src.m_Width;
    m_Length = src.m_Length;
    m_Priority = src.m_Priority;
    m_OrientationMode = src.m_OrientationMode;
    m_Mesh = nullptr; // IDA: Always set to null

    // If operation mode is CK_DEPENDENCIES_COPY (1), restore source's mesh
    if (context.IsInMode(CK_DEPENDENCIES_COPY))
        src.SetCurrentMesh(srcMesh, TRUE);

    // If layers dependency flag set, copy layers array
    if ((classDeps & 1) != 0)
        m_Layers = src.m_Layers;

    return CK_OK;
}

// Static class registration methods
CKSTRING RCKGrid::GetClassName() {
    return "Grid";
}

int RCKGrid::GetDependenciesCount(int mode) {
    return mode == CK_DEPENDENCIES_COPY ? 1 : 0;
}

CKSTRING RCKGrid::GetDependencies(int i, int mode) {
    return mode == CK_DEPENDENCIES_COPY && i == 0 ? "Layers" : nullptr;
}

void RCKGrid::Register() {
    // Based on IDA decompilation at 0x10019bd4
    CKCLASSNOTIFYFROMCID(RCKGrid, CKCID_LAYER);
    CKPARAMETERFROMCLASS(RCKGrid, CKPGUID_3DENTITY);
    CKCLASSDEFAULTCOPYDEPENDENCIES(RCKGrid, CK_DEPENDENCIES_COPY);
}

CKGrid *RCKGrid::CreateInstance(CKContext *Context) {
    return reinterpret_cast<CKGrid *>(new RCKGrid(Context));
}

// =====================================================
// Additional Grid Methods
// =====================================================

void RCKGrid::PostLoad() {
    // IDA: If visible, construct mesh texture with 0.5f opacity
    if (IsVisible())
        ConstructMeshTexture(0.5f);
    RCK3dEntity::PostLoad();
}

void RCKGrid::Show(CK_OBJECT_SHOWOPTION show) {
    // IDA: if becoming visible and not currently visible, construct mesh
    // if becoming hidden and currently visible, destroy mesh
    if ((show & CKSHOW) != 0) {
        if (!IsVisible())
            ConstructMeshTexture(0.5f);
    } else {
        if (IsVisible())
            DestroyMeshTexture();
    }
    RCK3dEntity::Show(show);
}

void RCKGrid::CheckPostDeletion() {
    // IDA: Call base then check layers array
    CKObject::CheckPostDeletion();
    m_Layers.Check(m_Context);
}

void RCKGrid::ConstructMeshTexture(float scale) {
    // IDA: 0x10017a83
    // If mesh already exists, just set it as current
    if (m_Mesh) {
        SetCurrentMesh(m_Mesh, TRUE);
        return;
    }

    CKGridManager *gridMgr = (CKGridManager *) m_Context->GetManagerByGuid(GRID_MANAGER_GUID);
    if (!gridMgr)
        return;

    RCKMesh *mesh = nullptr;
    CKMaterial *material = nullptr;
    CKMaterial *material2 = nullptr;
    CKTexture *texture = nullptr;
    CKLayer **layers = nullptr;
    VxColor *layerColors = nullptr;
    float uScale, vScale;
    int layerCount;

    // Create the mesh
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s mesh", GetName());
    m_Mesh = (RCKMesh *) m_Context->CreateObject(CKCID_MESH, buffer, CK_OBJECTCREATION_NONAMECHECK);
    mesh = m_Mesh;
    if (!m_Mesh)
        return;

    float width = (float) m_Width;
    float length = (float) m_Length;
    const float eps = 0.0001f;

    // Set up 12 vertices for the grid visualization
    m_Mesh->SetVertexCount(12);

    VxVector v;
    // Main quad vertices (0-3): the filled grid area
    v.Set(0.0f, 1.0f, 0.0f);
    m_Mesh->SetVertexPosition(0, &v);
    v.Set(0.0f, 1.0f, length);
    m_Mesh->SetVertexPosition(1, &v);
    v.Set(width, 1.0f, length);
    m_Mesh->SetVertexPosition(2, &v);
    v.Set(width, 1.0f, 0.0f);
    m_Mesh->SetVertexPosition(3, &v);

    // Border vertices (4-11): wireframe outline
    v.Set(0.0f, 0.0f, eps);
    m_Mesh->SetVertexPosition(4, &v);
    v.Set(eps, 0.0f, length);
    m_Mesh->SetVertexPosition(5, &v);
    v.Set(width, 0.0f, length - eps);
    m_Mesh->SetVertexPosition(6, &v);
    v.Set(width - eps, 0.0f, 0.0f);
    m_Mesh->SetVertexPosition(7, &v);
    v.Set(eps, 0.0f, 0.0f);
    m_Mesh->SetVertexPosition(8, &v);
    v.Set(0.0f, 0.0f, length - eps);
    m_Mesh->SetVertexPosition(9, &v);
    v.Set(width - eps, 0.0f, length);
    m_Mesh->SetVertexPosition(10, &v);
    v.Set(width, 0.0f, eps);
    m_Mesh->SetVertexPosition(11, &v);

    // Set lighting mode to prelit
    m_Mesh->SetLitMode(VX_PRELITMESH);

    // Set up 10 faces
    m_Mesh->SetFaceCount(10);
    // Main quad (2 triangles)
    m_Mesh->SetFaceVertexIndex(0, 0, 1, 2);
    m_Mesh->SetFaceVertexIndex(1, 0, 2, 3);
    // Border faces (8 triangles for wireframe edges)
    m_Mesh->SetFaceVertexIndex(2, 5, 9, 1);
    m_Mesh->SetFaceVertexIndex(3, 6, 10, 2);
    m_Mesh->SetFaceVertexIndex(4, 7, 11, 3);
    m_Mesh->SetFaceVertexIndex(5, 4, 8, 0);
    m_Mesh->SetFaceVertexIndex(6, 4, 5, 9);
    m_Mesh->SetFaceVertexIndex(7, 5, 6, 10);
    m_Mesh->SetFaceVertexIndex(8, 6, 7, 11);
    m_Mesh->SetFaceVertexIndex(9, 7, 4, 8);

    // Set vertex colors
    VxColor white(1.0f, 1.0f, 1.0f, scale);
    CKDWORD whiteColor = RGBAFTOCOLOR(white.r, white.g, white.b, white.a);
    for (int i = 0; i < 4; ++i) {
        m_Mesh->SetVertexColor(i, whiteColor);
        m_Mesh->SetVertexSpecularColor(i, A_MASK);
    }

    // Orange color for border vertices
    CKDWORD orangeColor = RGBAFTOCOLOR(1.0f, 0.5f, 0.1f, white.a);
    for (int i = 4; i < 12; ++i) {
        m_Mesh->SetVertexColor(i, orangeColor);
        m_Mesh->SetVertexSpecularColor(i, A_MASK);
    }

    // Build face normals
    m_Mesh->BuildFaceNormals();
    m_Mesh->SetLitMode(VX_PRELITMESH);

    // Create main material (with alpha blend for transparency)
    snprintf(buffer, sizeof(buffer), "%s material", GetName());
    material = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, buffer, CK_OBJECTCREATION_NONAMECHECK);
    if (!material)
        goto display_failed;
    m_Mesh->SetFaceMaterial(0, material);
    m_Mesh->SetFaceMaterial(1, material);

    material->EnableAlphaBlend(TRUE);
    material->EnableZWrite(FALSE);
    material->SetSourceBlend(VXBLEND_SRCALPHA);
    material->SetDestBlend(VXBLEND_INVSRCALPHA);
    material->SetTwoSided(TRUE);
    material->SetDiffuse(white);
    material->SetTextureMagMode(VXTEXTUREFILTER_NEAREST);
    material->SetTextureMinMode(VXTEXTUREFILTER_NEAREST);
    material->SetTextureBlendMode(VXTEXTUREBLEND_MODULATEALPHA);

    // Create wireframe material for border
    snprintf(buffer, sizeof(buffer), "%s material2", GetName());
    material2 = (CKMaterial *) m_Context->CreateObject(CKCID_MATERIAL, buffer, CK_OBJECTCREATION_NONAMECHECK);
    if (!material2)
        goto display_failed;
    for (int i = 2; i < 10; ++i)
        m_Mesh->SetFaceMaterial(i, material2);

    material2->SetFillMode(VXFILL_WIREFRAME);
    material2->SetTwoSided(TRUE);
    white.a = 1.0f;
    material2->SetDiffuse(white);

    // Create texture for grid visualization
    snprintf(buffer, sizeof(buffer), "%s texture", GetName());
    texture = (CKTexture *) m_Context->CreateObject(CKCID_TEXTURE, buffer, CK_OBJECTCREATION_NONAMECHECK);
    if (!texture)
        goto display_failed;

    // Calculate texture size based on grid dimensions (power of 2, max 256)
    int texWidth, texHeight;
    if (m_Width > 64) texWidth = 256;
    else if (m_Width > 32) texWidth = 128;
    else if (m_Width > 16) texWidth = 64;
    else if (m_Width > 8) texWidth = 32;
    else texWidth = 16;

    if (m_Length > 64) texHeight = 256;
    else if (m_Length > 32) texHeight = 128;
    else if (m_Length > 16) texHeight = 64;
    else if (m_Length > 8) texHeight = 32;
    else texHeight = 16;

    if (!texture->Create(texWidth, texHeight, 32))
        goto display_failed;
    texture->SetDesiredVideoFormat(_16_BGR565);

    // Calculate texture coordinates
    uScale = (float) (m_Width * 2) / (float) texWidth;
    vScale = (float) (m_Length * 2) / (float) texHeight;

    m_Mesh->SetVertexTextureCoordinates(0, 0.0f, 0.0f, -1);
    m_Mesh->SetVertexTextureCoordinates(1, 0.0f, vScale, -1);
    m_Mesh->SetVertexTextureCoordinates(2, uScale, vScale, -1);
    m_Mesh->SetVertexTextureCoordinates(3, uScale, 0.0f, -1);

    // Get grid manager for layer color info
    layerCount = GetLayerCount();
    if (layerCount > 0) {
        layers = new(std::nothrow) CKLayer *[layerCount];
        layerColors = new(std::nothrow) VxColor[layerCount];
        if (!layers || !layerColors)
            goto display_failed;
        memset(layerColors, 0, sizeof(VxColor) * layerCount);
    }
    if (gridMgr) {
        // Gather layer information and colors
        for (int i = 0; i < layerCount; ++i) {
            layers[i] = (CKLayer *) m_Layers.GetObject(m_Context, i);
            if (layers[i] && layers[i]->IsVisible()) {
                gridMgr->GetAssociatedColor(layers[i]->GetType(), &layerColors[i]);
            }
        }

        // Lock texture surface and fill with layer data
        CKBYTE *surfacePtr = texture->LockSurfacePtr();
        if (!surfacePtr)
            goto display_failed;
        if (surfacePtr) {
            // Each cell covers 2x2 pixels of a texture capped at 256 pixels.
            // The original also fills cells past 128, which wraps into the next
            // row and finally writes past the surface; draw only those that fit.
            const CKDWORD cellsWide = m_Width < (CKDWORD) texWidth / 2 ? m_Width : (CKDWORD) texWidth / 2;
            const CKDWORD cellsLong = m_Length < (CKDWORD) texHeight / 2 ? m_Length : (CKDWORD) texHeight / 2;
            CKBYTE *rowPtr = surfacePtr;
            for (CKDWORD y = 0; y < cellsLong; ++y) {
                CKBYTE *pixelPtr = rowPtr;
                for (CKDWORD x = 0; x < cellsWide; ++x) {
                    long long r = 0, g = 0, b = 0;

                    // Accumulate color from all layers at this cell
                    for (int j = 0; j < layerCount; ++j) {
                        if (layers[j]) {
                            int value = 0;
                            layers[j]->GetValue(x, y, &value);
                            if (gridMgr->GetAssociatedParam(layers[j]->GetType()) == CKPGUID_LINKERGRAPH_ENUM)
                                value *= 85;
                            // The Win32 DLL truncates each layer's contribution
                            // before adding it, using the x87 product precision.
                            r += static_cast<long long>(static_cast<double>(value) * layerColors[j].r);
                            g += static_cast<long long>(static_cast<double>(value) * layerColors[j].g);
                            b += static_cast<long long>(static_cast<double>(value) * layerColors[j].b);
                        }
                    }

                    // Saturate the final sum after all layers can cancel;
                    // positive contributions must not wrap before this clamp.
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;

                    // Write 2x2 pixels (grid cells are 2 pixels in texture)
                    // Match the native signed-cell RGB sum with defined 32-bit
                    // modular arithmetic; overlapping negative components add.
                    CKDWORD color = A_MASK | ((static_cast<CKDWORD>(r) << 16) +
                                              (static_cast<CKDWORD>(g) << 8) + static_cast<CKDWORD>(b));
                    *(CKDWORD *) pixelPtr = color;
                    *(CKDWORD *) (pixelPtr + 4) = color;
                    *(CKDWORD *) (pixelPtr + 4 * texWidth) = color;
                    *(CKDWORD *) (pixelPtr + 4 * texWidth + 4) = color;

                    pixelPtr += 8;
                }
                rowPtr += 8 * texWidth;
            }
            texture->ReleaseSurfacePtr();

            // 0x10017A83 adds 0x23: private interface resources excluded from saving.
            const CKDWORD generatedFlags = CK_OBJECT_INTERFACEOBJ | CK_OBJECT_PRIVATE | CK_OBJECT_NOTTOBESAVED;
            m_Mesh->ModifyObjectFlags(generatedFlags, 0);
            material->ModifyObjectFlags(generatedFlags, 0);
            material2->ModifyObjectFlags(generatedFlags, 0);
            texture->ModifyObjectFlags(generatedFlags, 0);

            // Add to current level
            CKLevel *level = m_Context->GetCurrentLevel();
            if (level) {
                level->AddObject(m_Mesh);
                level->AddObject(material);
                level->AddObject(material2);
                level->AddObject(texture);
            }

            // Set texture on material (channel 0)
            material->SetTexture0(texture);

            // Set mesh as transparent and add to entity
            m_Mesh->SetTransparent(TRUE);
            AddMesh(m_Mesh);
            SetCurrentMesh(m_Mesh, TRUE);
        }
    }

    delete[] layers;
    delete[] layerColors;
    return;

display_failed:
    // Keep the existing entity mesh until the new display is complete. The
    // texture may not be bound yet, so release directly tracked resources.
    m_Mesh = nullptr;
    delete[] layers;
    delete[] layerColors;
    if (texture)
        m_Context->DestroyObject(texture);
    if (material)
        m_Context->DestroyObject(material);
    if (material2)
        m_Context->DestroyObject(material2);
    if (mesh)
        m_Context->DestroyObject(mesh);
}

void RCKGrid::DestroyMeshTexture() {
    // IDA decompilation at 0x10018d00
    if (m_Context->IsInClearAll())
        return;

    if (!m_Mesh) {
        m_Mesh = nullptr;
        return;
    }

    CKMaterial *mat0 = m_Mesh->GetFaceMaterial(0);
    CKMaterial *mat2 = m_Mesh->GetFaceMaterial(2);

    if (mat0) {
        CKTexture *tex = mat0->GetTexture(0);
        if (tex)
            m_Context->DestroyObject(tex);
        m_Context->DestroyObject(mat0);
    }

    if (mat2 && mat2 != mat0)
        m_Context->DestroyObject(mat2);

    m_Context->DestroyObject(m_Mesh);
    m_Mesh = nullptr;
}

CKBOOL RCKGrid::IsActive() {
    CKScene *scene = m_Context ? m_Context->GetCurrentScene() : nullptr;
    if (!scene)
        return FALSE;
    return (scene->GetObjectFlags(this) & 8) != 0;
}

void RCKGrid::SetHeightValidity(float val) {
    VxVector s;
    GetScale(&s, TRUE);
    s.y = val;
    SetScale(&s, FALSE, TRUE);
}

float RCKGrid::GetHeightValidity() {
    VxVector s;
    GetScale(&s, TRUE);
    return s.y;
}

int RCKGrid::GetWidth() {
    return m_Width;
}

int RCKGrid::GetLength() {
    return m_Length;
}

void RCKGrid::SetDimensions(int width, int length, float cellWidth, float cellLength) {
    // The SDK resize workflow recreates layers; cached display geometry must
    // also be recreated for the new number of cells.
    if (m_Width != static_cast<CKDWORD>(width) || m_Length != static_cast<CKDWORD>(length))
        DestroyMeshTexture();

    m_Width = width;
    m_Length = length;

    VxVector s;
    GetScale(&s, TRUE);
    if (width > 0)
        s.x = static_cast<float>(cellWidth / static_cast<double>(width));
    if (length > 0)
        s.z = static_cast<float>(cellLength / static_cast<double>(length));
    SetScale(&s, FALSE, TRUE);
}

float RCKGrid::Get2dCoordsFrom3dPos(const VxVector *pos, int *x, int *y) {
    if (!pos || !x || !y)
        return 0.0f;

    VxVector local;
    InverseTransform(&local, pos, nullptr);

    *x = static_cast<int>(local.x);
    *y = static_cast<int>(local.z);
    return local.y;
}

void RCKGrid::Get3dPosFrom2dCoords(VxVector *pos, int x, int y) {
    if (!pos)
        return;

    VxVector local(static_cast<float>(static_cast<double>(x) + 0.5), 0.0f,
        static_cast<float>(static_cast<double>(y) + 0.5));
    Transform(pos, &local, nullptr);
}

/**
 * @brief Add a classification attribute by type
 * @param classType The attribute type to add
 * @return CK_OK on success, CKERR_INVALIDPARAMETER on failure
 * IDA: 0x1001901e
 */
CKERROR RCKGrid::AddClassification(int classType) {
    if (SetAttribute(classType, 0))
        return CK_OK;
    return CKERR_NOTFOUND;
}

/**
 * @brief Add a classification attribute by name
 * @param name The attribute name to add
 * @return CK_OK on success, CKERR_INVALIDPARAMETER on failure
 * IDA: 0x10019046
 */
CKERROR RCKGrid::AddClassificationByName(CKSTRING name) {
    if (!name)
        return CKERR_INVALIDPARAMETER;

    CKAttributeManager *attrMgr = m_Context->GetAttributeManager();
    if (!attrMgr)
        return CKERR_INVALIDPARAMETER;

    const CKAttributeType attrType = attrMgr->GetAttributeTypeByName(name);
    if (!attrMgr->IsAttributeIndexValid(attrType))
        return CKERR_NOTFOUND;

    if (SetAttribute(attrType, 0))
        return CK_OK;
    return CKERR_NOTFOUND;
}

/**
 * @brief Remove a classification attribute by type
 * @param classType The attribute type to remove
 * @return CK_OK on success, CKERR_INVALIDPARAMETER on failure
 * IDA: 0x10019081
 */
CKERROR RCKGrid::RemoveClassification(int classType) {
    if (RemoveAttribute(classType))
        return CK_OK;
    return CKERR_NOTFOUND;
}

/**
 * @brief Remove a classification attribute by name
 * @param name The attribute name to remove
 * @return CK_OK on success, CKERR_INVALIDPARAMETER on failure
 * IDA: 0x100190a7
 */
CKERROR RCKGrid::RemoveClassificationByName(CKSTRING name) {
    if (!name)
        return CKERR_INVALIDPARAMETER;

    CKAttributeManager *attrMgr = m_Context->GetAttributeManager();
    if (!attrMgr)
        return CKERR_INVALIDPARAMETER;

    const CKAttributeType attrType = attrMgr->GetAttributeTypeByName(name);
    if (!attrMgr->IsAttributeIndexValid(attrType))
        return CKERR_NOTFOUND;

    if (RemoveAttribute(attrType))
        return CK_OK;
    return CKERR_NOTFOUND;
}

/**
 * @brief Check if entity has compatible classification
 * @param entity The entity to check against
 * @return TRUE if classifications are compatible
 * IDA: 0x100190e0
 */
CKBOOL RCKGrid::HasCompatibleClass(CK3dEntity *entity) {
    if (!entity)
        return FALSE;

    // Get Grid Manager
    CKGridManager *gridMgr = (CKGridManager *) m_Context->GetManagerByGuid(GRID_MANAGER_GUID);
    if (!gridMgr)
        return FALSE;

    // Get the grid classification category from the manager
    int gridCategory = gridMgr->GetGridClassificationCategory();

    CKAttributeManager *attrMgr = m_Context->GetAttributeManager();

    // Get entity's attribute list
    int attrCount = entity->GetAttributeCount();
    CKAttributeVal *attrList = new CKAttributeVal[attrCount];
    entity->GetAttributeList(attrList);

    // Check each attribute of the entity
    while (attrCount > 0) {
        --attrCount;
        int attrType = attrList[attrCount].AttribType;
        int attrCategory = attrMgr->GetAttributeCategoryIndex(attrType);

        // If attribute is in grid category and this grid has it too
        if (attrCategory == gridCategory && HasAttribute(attrType)) {
            delete[] attrList;
            return TRUE;
        }
    }

    delete[] attrList;
    return FALSE;
}

void RCKGrid::SetGridPriority(int priority) {
    m_Priority = priority;
}

int RCKGrid::GetGridPriority() {
    return m_Priority;
}

void RCKGrid::SetOrientationMode(CK_GRIDORIENTATION mode) {
    m_OrientationMode = mode;
}

CK_GRIDORIENTATION RCKGrid::GetOrientationMode() {
    return static_cast<CK_GRIDORIENTATION>(m_OrientationMode);
}

/**
 * @brief Add a layer by type
 * @param type The layer type
 * @param format The layer format (0 = normal)
 * @return The created layer, or nullptr on failure
 * IDA: 0x10019256
 */
CKLayer *RCKGrid::AddLayer(int type, int format) {
    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr)
        return nullptr;

    // Validate type exists in manager
    CKSTRING layerName = gridMgr->GetTypeName(type);
    if (!layerName)
        return nullptr;

    // Check if layer with this type already exists
    if (GetLayer(type))
        return nullptr;

    // Only format 0 is supported
    if (format != 0)
        return nullptr;

    // Create the layer
    CKLayer *layer = reinterpret_cast<CKLayer *>(m_Context->CreateObject(CKCID_LAYER, layerName, CK_OBJECTCREATION_NONAMECHECK));
    if (!layer)
        return nullptr;

    layer->InitOwner(GetID());
    if (layer->GetOwner() != GetID()) {
        m_Context->DestroyObject(layer, CK_DESTROY_NONOTIFY);
        return nullptr;
    }
    layer->SetType(type);
    layer->SetFormat(0);
    layer->SetName(layerName, 0);
    m_Layers.PushBack(layer->GetID());
    return layer;
}

/**
 * @brief Add a layer by type name
 * @param name The layer type name
 * @param format The layer format (0 = normal)
 * @return The created layer, or nullptr on failure
 * IDA: 0x1001935d
 */
CKLayer *RCKGrid::AddLayerByName(CKSTRING name, int format) {
    if (!name)
        return nullptr;

    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr)
        return nullptr;

    // Get type from name (vtable+120 = GetTypeFromName)
    const int type = gridMgr->GetTypeFromName(name);
    if (!type)
        return nullptr;

    // Check if layer with this type already exists
    if (GetLayer(type))
        return nullptr;

    // Only format 0 is supported
    if (format != 0)
        return nullptr;

    // Create layer with the provided name
    CKLayer *layer = reinterpret_cast<CKLayer *>(m_Context->CreateObject(CKCID_LAYER, name, CK_OBJECTCREATION_NONAMECHECK));
    if (!layer)
        return nullptr;

    layer->InitOwner(GetID());
    if (layer->GetOwner() != GetID()) {
        m_Context->DestroyObject(layer, CK_DESTROY_NONOTIFY);
        return nullptr;
    }
    layer->SetType(type);
    layer->SetFormat(0);
    layer->SetName(name, 0);
    m_Layers.PushBack(layer->GetID());
    return layer;
}

CKLayer *RCKGrid::GetLayer(int type) {
    for (int i = 0; i < m_Layers.Size(); ++i) {
        CKLayer *layer = reinterpret_cast<CKLayer *>(m_Context->GetObject(m_Layers[i]));
        if (layer && layer->GetType() == type)
            return layer;
    }
    return nullptr;
}

CKLayer *RCKGrid::GetLayerByName(CKSTRING name) {
    if (!name)
        return nullptr;

    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr)
        return nullptr;

    const int type = gridMgr->GetTypeFromName(name);
    if (!type)
        return nullptr;

    return GetLayer(type);
}

int RCKGrid::GetLayerCount() {
    return m_Layers.Size();
}

CKLayer *RCKGrid::GetLayerByIndex(int index) {
    if (index < 0 || index >= m_Layers.Size())
        return nullptr;
    return reinterpret_cast<CKLayer *>(m_Context->GetObject(m_Layers[index]));
}

CKERROR RCKGrid::RemoveLayer(int type) {
    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr)
        return CKERR_NOTFOUND;

    // Native queries the name but removal depends on the existing layer.
    gridMgr->GetTypeName(type);

    CKLayer *layer = GetLayer(type);
    if (!layer)
        return CKERR_NOTFOUND;

    m_Layers.Remove(layer->GetID());
    m_Context->DestroyObject(layer, CK_DESTROY_NONOTIFY);
    return CK_OK;
}

/**
 * @brief Remove a layer by type name
 * @param name The layer type name to remove
 * @return CK_OK
 * IDA: 0x100195e9
 */
CKERROR RCKGrid::RemoveLayerByName(CKSTRING name) {
    if (!name)
        return CKERR_NOTFOUND;

    CKGridManager *gridMgr = reinterpret_cast<CKGridManager *>(m_Context->GetManagerByGuid(GRID_MANAGER_GUID));
    if (!gridMgr)
        return CKERR_NOTFOUND;

    const int type = gridMgr->GetTypeFromName(name);
    if (type == 0)
        return CKERR_NOTFOUND;

    RemoveLayer(type);
    return CK_OK;
}

CKERROR RCKGrid::RemoveAllLayers() {
    for (int i = 0; i < m_Layers.Size(); ++i) {
        CKObject *layerObject = m_Context->GetObject(m_Layers[i]);
        if (layerObject)
            m_Context->DestroyObject(layerObject, CK_DESTROY_NONOTIFY);
    }
    m_Layers.Clear();
    return CK_OK;
}
