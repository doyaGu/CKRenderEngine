/**
 * @file CKMaterial.cpp
 * @brief Implementation of RCKMaterial class for the CKRenderEngine.
 *
 * This file implements the material system for 3D rendering including
 * color properties, textures, blend modes, and rendering states.
 *
 * Reverse engineered from CK2_3D.dll
 * - Constructor: 0x100628C1
 * - Destructor:  0x10062D34
 * - Save:        0x100652CF
 * - Load:        0x100655E1
 * - SetAsCurrent: 0x10064be0
 */

#include "RCKMaterial.h"

#include "VxMath.h"
#include "CKStateChunk.h"
#include "CKFile.h"
#include "CKContext.h"
#include "CKRenderManager.h"
#include "RCKRenderManager.h"
#include "CKParameter.h"
#include "CKTexture.h"
#include "RCKRenderContext.h"
#include "CKRenderedScene.h"
#include "CKRasterizer.h"
#include "CKParameterManager.h"
#include "RCK3dEntity.h"
#include "RCKLight.h"
#include "RCKSprite3D.h"

static VX_EFFECTTEXGEN ReadTexGenParameter(CKParameter *parameter) {
    if (!parameter)
        return VXEFFECT_TGREFLECT;

    CKDWORD value = VXEFFECT_TGREFLECT;
    if (parameter->GetValue(&value, TRUE) != CK_OK)
        return VXEFFECT_TGREFLECT;

    return (VX_EFFECTTEXGEN)value;
}

// Static class ID (initialized during registration)
CK_CLASSID RCKMaterial::m_ClassID = CKCID_MATERIAL;

//=============================================================================
// Global Variables
//=============================================================================

// This flag is set when transparency-related properties change
// It signals the render engine to update transparency sorting
CKBOOL g_UpdateTransparency = FALSE;

// Fog projection mode (from IDA: dword_10090CD0)
// Mode 0: Direct fog values (default)
// Mode 1: Projected fog values (z/w based)
// Mode 2: Projected fog values (1/w based)
int g_FogProjectionMode = 0;


//=============================================================================
// Construction/Destruction
//=============================================================================

/**
 * @brief Constructor for RCKMaterial.
 *
 * Initializes material with default values:
 * - Diffuse: (0.7, 0.7, 0.7, 1.0)
 * - Ambient: (0.3, 0.3, 0.3, 1.0)
 * - Specular: (0.0, 0.0, 0.0, 1.0) (disabled by default)
 * - Emissive: (0.0, 0.0, 0.0, 1.0)
 * - SpecularPower: 0.0 (specular disabled)
 * - SpecularColor cache: (0.5, 0.5, 0.5, 1.0)
 * - TextureBlendMode: VXTEXTUREBLEND_MODULATEALPHA
 * - Filter modes: LINEAR
 * - Source/Dest blend: Default non-blending
 * - ShadeMode: GOURAUD
 * - FillMode: SOLID
 * - No textures
 *
 * Based on decompilation at 0x100628C1.
 *
 * @param Context The CKContext for this material
 * @param name Optional name for the material
 */
RCKMaterial::RCKMaterial(CKContext *Context, CKSTRING name)
    : CKMaterial(Context, name) {
    // Initialize texture slots to null
    for (int i = 0; i < 4; ++i) {
        m_Textures[i] = nullptr;
    }

    // Initialize material colors with defaults
    // Diffuse: gray (0.7, 0.7, 0.7)
    m_MaterialData.Diffuse = VxColor(0.7f, 0.7f, 0.7f, 1.0f);

    // Ambient: dark gray (0.3, 0.3, 0.3)
    m_MaterialData.Ambient = VxColor(0.3f, 0.3f, 0.3f, 1.0f);
    // Specular: black (disabled with power=0)
    m_MaterialData.Specular = VxColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Emissive: black
    m_MaterialData.Emissive = VxColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Specular power (0 = specular disabled)
    m_MaterialData.SpecularPower = 0.0f;

    // Cached specular color for when specular is enabled
    m_SpecularColor = VxColor(0.5f, 0.5f, 0.5f, 1.0f);

    // Texture blend mode: modulate with alpha
    m_TextureBlendMode = VXTEXTUREBLEND_MODULATEALPHA;

    // Texture filtering: bilinear
    m_TextureMinMode = VXTEXTUREFILTER_LINEAR;
    m_TextureMagMode = VXTEXTUREFILTER_LINEAR;

    // Default blend factors (non-blending)
    m_SourceBlend = VXBLEND_ONE;
    m_DestBlend = VXBLEND_ZERO;

    // Shading and fill modes
    m_ShadeMode = VXSHADE_GOURAUD;
    m_FillMode = VXFILL_SOLID;

    // Texture addressing
    m_TextureAddressMode = VXTEXTURE_ADDRESSWRAP;
    m_TextureBorderColor = 0;

    // Flags initialization (from IDA at 0x100629E0-0x10062AAF):
    // Bit layout: 
    //   Bit 0:      TwoSided
    //   Bit 1:      ZWrite  
    //   Bit 2:      PerspectiveCorrection
    //   Bit 3:      AlphaBlend
    //   Bit 4:      AlphaTest
    //   Bit 5:      Sprite3DBatch
    //   Bits 8-13:  Effect (6 bits)
    //   Bits 14-18: ZFunc (5 bits)
    //   Bits 19-23: AlphaFunc (5 bits)
    //
    // Step 1: Low byte = 6 (ZWrite + PerspectiveCorrection)
    // Step 2: Clear effect bits (bits 8-13)
    // Step 3: Set ZFunc = 4 (VXCMP_LESSEQUAL) at bits 14-18 -> 0x10000
    // Step 4: Set AlphaFunc = 8 (VXCMP_ALWAYS) at bits 19-23 -> 0x400000
    // Final: 0x410006
    m_Flags = 0x410006;

    // Alpha reference value
    m_AlphaRef = 0;

    // Sprite batch (created when needed)
    m_Sprite3DBatch = nullptr;

    // Callback system
    m_Callback = nullptr;
    m_CallbackArgument = nullptr;

    // Effect parameter
    m_EffectParameter = nullptr;
}

/**
 * @brief Destructor for RCKMaterial.
 *
 * Cleans up the sprite batch if allocated.
 * Based on decompilation at 0x10062D34.
 */
RCKMaterial::~RCKMaterial() {
    // Clean up sprite batch if it exists
    if (m_Sprite3DBatch) {
        delete m_Sprite3DBatch;
        m_Sprite3DBatch = nullptr;
    }
}

//=============================================================================
// CKObject Virtual Methods
//=============================================================================

/**
 * @brief Returns the class ID for this material.
 *
 * Based on decompilation at 0x10065169.
 *
 * @return CKCID_MATERIAL
 */
CK_CLASSID RCKMaterial::GetClassID() {
    return m_ClassID;
}

/**
 * @brief Called before deletion to clean up texture references.
 *
 * Nullifies any texture references that are being deleted.
 * Based on decompilation at 0x10065179.
 */
void RCKMaterial::CheckPreDeletion() {
    CKObject::CheckPreDeletion();

    // Check each texture slot and clear if texture is being deleted
    for (int i = 0; i < 4; ++i) {
        if (m_Textures[i]) {
            if (m_Textures[i]->IsToBeDeleted()) {
                m_Textures[i] = nullptr;
            }
        }
    }
}

/**
 * @brief Returns the memory occupied by this material.
 *
 * Based on decompilation at 0x1006522B.
 *
 * @return Memory occupation in bytes
 */
int RCKMaterial::GetMemoryOccupation() {
    return CKBeObject::GetMemoryOccupation() + (sizeof(RCKMaterial) - sizeof(CKBeObject));
}

/**
 * @brief Checks if a specific object is used by this material.
 *
 * Checks textures for class ID 31 (CKCID_TEXTURE).
 * Based on decompilation at 0x100651D6.
 *
 * @param obj The object to check
 * @param cid The class ID to filter by
 * @return TRUE if the object is used
 */
CKBOOL RCKMaterial::IsObjectUsed(CKObject *obj, CK_CLASSID cid) {
    if (cid == CKCID_TEXTURE) {
        for (int i = 0; i < 4; ++i) {
            if (obj == m_Textures[i]) {
                return TRUE;
            }
        }
    }
    return CKBeObject::IsObjectUsed(obj, cid);
}

//=============================================================================
// Serialization
//=============================================================================

/**
 * @brief Pre-save callback to ensure referenced objects are saved.
 *
 * Saves the effect parameter and all textures.
 * Based on decompilation at 0x1006524B.
 *
 * @param file The file being saved to
 * @param flags Save flags
 */
void RCKMaterial::PreSave(CKFile *file, CKDWORD flags) {
    CKBeObject::PreSave(file, flags);

    // Save effect parameter if effect is set
    if (GetEffect() != VXEFFECT_NONE) {
        file->SaveObject(m_EffectParameter);
    }

    // Save all textures
    for (int i = 0; i < 4; ++i) {
        if (m_Textures[i]) {
            file->SaveObject(m_Textures[i], flags);
        }
    }
}

/**
 * @brief Saves material data to a state chunk.
 *
 * Serialization format:
 * - Identifier 0x1000: Main material data
 *   - Diffuse color (packed DWORD)
 *   - Ambient color (packed DWORD)
 *   - Specular color (packed DWORD)
 *   - Emissive color (packed DWORD)
 *   - Specular power (float)
 *   - Texture 0 (object reference)
 *   - Border color (DWORD)
 *   - Packed modes (texture blend, filter, address, shade, fill, blend factors)
 *   - Packed flags (material flags, alpha ref)
 * - Identifier 0x4000: Effect index (if effect set, no parameter)
 * - Identifier 0x10000: Effect with parameter
 *   - Effect parameter (object reference)
 *   - Effect index
 * - Identifier 0x2000: Additional textures (1-3)
 *
 * Based on decompilation at 0x100652CF.
 *
 * @param file The file context (may be NULL)
 * @param flags Save flags
 * @return The created state chunk
 */
CKStateChunk *RCKMaterial::Save(CKFile *file, CKDWORD flags) {
    // Call base class save
    CKStateChunk *baseChunk = CKBeObject::Save(file, flags);

    // Return early if no file and not in specific save modes
    if (!file && !(flags & CK_STATESAVE_MATERIALONLY)) {
        return baseChunk;
    }

    // Create material-specific state chunk
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_MATERIAL, file);
    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    // Write main material data with identifier 0x1000
    chunk->WriteIdentifier(CK_STATESAVE_MATDATA);

    // Pack and write colors
    CKDWORD diffuseColor = RGBAFTOCOLOR(&m_MaterialData.Diffuse);
    CKDWORD ambientColor = RGBAFTOCOLOR(&m_MaterialData.Ambient);
    CKDWORD specularColor = RGBAFTOCOLOR(&m_SpecularColor);
    CKDWORD emissiveColor = RGBAFTOCOLOR(&m_MaterialData.Emissive);

    chunk->WriteDword(diffuseColor);
    chunk->WriteDword(ambientColor);
    chunk->WriteDword(specularColor);
    chunk->WriteDword(emissiveColor);

    // Write specular power
    chunk->WriteFloat(m_MaterialData.SpecularPower);

    // Write primary texture
    chunk->WriteObject(m_Textures[0]);

    // Write border color
    chunk->WriteDword(m_TextureBorderColor);

    // Pack rendering modes into single DWORD:
    // Bits 0-3:   TextureBlendMode
    // Bits 4-7:   TextureMinMode
    // Bits 8-11:  TextureMagMode
    // Bits 12-15: SourceBlend
    // Bits 16-19: DestBlend
    // Bits 20-23: ShadeMode
    // Bits 24-27: FillMode
    // Bits 28-31: TextureAddressMode
    CKDWORD packedModes =
        (m_TextureBlendMode & VXTEXTUREBLEND_MASK) |
        ((m_TextureMinMode & VXTEXTUREFILTER_MASK) << 4) |
        ((m_TextureMagMode & VXTEXTUREFILTER_MASK) << 8) |
        ((m_SourceBlend & VXBLEND_MASK) << 12) |
        ((m_DestBlend & VXBLEND_MASK) << 16) |
        ((m_ShadeMode & VXSHADE_MASK) << 20) |
        ((m_FillMode & VXFILL_MASK) << 24) |
        ((m_TextureAddressMode & VXTEXTURE_ADDRESSMASK) << 28);

    // Pack flags into single DWORD:
    // Bits 0-7:   Material flags (low byte of m_Flags)
    // Bits 8-12:  ZFunc (from m_Flags bits 14-18, 5 bits)
    // Bits 16-20: AlphaFunc (from m_Flags bits 19-23, 5 bits)
    // Bits 24-31: AlphaRef
    CKDWORD packedFlags =
        (m_Flags & 0xFF) |
        (((m_Flags >> 14) & 0x1F) << 8) |
        (((m_Flags >> 19) & 0x1F) << 16) |
        ((m_AlphaRef & 0xFF) << 24);

    chunk->WriteDword(packedModes);
    chunk->WriteDword(packedFlags);

    // Write effect data if effect is set
    VX_EFFECT effect = GetEffect();
    if (effect != VXEFFECT_NONE) {
        if (m_EffectParameter) {
            chunk->WriteIdentifier(CK_STATESAVE_MATDATA5);
            chunk->WriteObject(m_EffectParameter);
        } else {
            chunk->WriteIdentifier(CK_STATESAVE_MATDATA3);
        }
        chunk->WriteDword(effect);
    }

    // Write additional textures if any are set
    if (effect != VXEFFECT_NONE) {
        if (m_Textures[1] || m_Textures[2] || m_Textures[3]) {
            chunk->WriteIdentifier(CK_STATESAVE_MATDATA2);
            chunk->WriteObject(m_Textures[1]);
            chunk->WriteObject(m_Textures[2]);
            chunk->WriteObject(m_Textures[3]);
        }
    }

    chunk->CloseChunk();
    return chunk;
}

/**
 * @brief Loads material data from a state chunk.
 *
 * Supports both legacy format (data version < 5) and current format.
 * Based on decompilation at 0x100655E1.
 *
 * @param chunk The state chunk containing material data
 * @param file The file context (may be NULL)
 * @return CK_OK on success, error code on failure
 */
CKERROR RCKMaterial::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) {
        return CKERR_INVALIDPARAMETER;
    }

    // Call base class load
    CKBeObject::Load(chunk, file);

    // Clear texture slots
    for (int i = 0; i < 4; ++i) {
        m_Textures[i] = nullptr;
    }

    // Read main material data
    if (chunk->SeekIdentifier(CK_STATESAVE_MATDATA)) {
        int dataVersion = chunk->GetDataVersion();

        if (dataVersion < 5) {
            // Legacy format - read individual components
            chunk->ReadAndFillBuffer_LEndian(sizeof(VxColor), &m_MaterialData.Diffuse);
            chunk->ReadAndFillBuffer_LEndian(sizeof(VxColor), &m_MaterialData.Ambient);
            chunk->ReadAndFillBuffer_LEndian(sizeof(VxColor), &m_SpecularColor);
            chunk->ReadAndFillBuffer_LEndian(sizeof(VxColor), &m_MaterialData.Emissive);

            float power = chunk->ReadFloat();
            SetPower(power);

            // Read primary texture
            CKObject *texObj = chunk->ReadObject(m_Context);
            if (CKIsChildClassOf(texObj, CKCID_TEXTURE)) {
                m_Textures[0] = static_cast<CKTexture *>(texObj);
            }

            // Read individual settings
            CKDWORD lowFlags = chunk->ReadDword();
            m_Flags = (m_Flags & ~0xFF) | (lowFlags & 0xFF);

            m_TextureBlendMode = chunk->ReadDword();
            m_TextureMinMode = chunk->ReadDword();
            m_TextureMagMode = chunk->ReadDword();
            m_SourceBlend = (VXBLEND_MODE) chunk->ReadDword();
            m_DestBlend = (VXBLEND_MODE) chunk->ReadDword();
            m_ShadeMode = chunk->ReadDword();
            m_FillMode = chunk->ReadDword();
            m_TextureAddressMode = chunk->ReadDword();
            m_TextureBorderColor = chunk->ReadDword();

            // Read ZFunc
            CKDWORD zFunc = chunk->ReadDword();
            m_Flags = (m_Flags & ~0x7C000) | ((zFunc & 0x1F) << 14);

            // Set default AlphaFunc
            SetAlphaFunc(VXCMP_ALWAYS);

            m_AlphaRef = 0;

            // Clear perspective correction flag
            m_Flags &= ~0x3F00;

            // Ensure ZFunc is valid
            if (GetZFunc() == 0) {
                SetZFunc(VXCMP_LESSEQUAL);
            }

            // Legacy version < 4 compatibility fixes
            if (dataVersion < 4) {
                // Fix old blend modes
                if ((m_Flags & 0xFF) == 1) {
                    m_Flags = (m_Flags & ~0xFF) | 7;
                } else if ((m_Flags & 0xFF) == 0) {
                    m_Flags = (m_Flags & ~0xFF) | 6;
                }

                // Auto-enable alpha blending for transparent diffuse
                if (m_MaterialData.Diffuse.a < 1.0f &&
                    m_SourceBlend == VXBLEND_ONE &&
                    m_DestBlend == VXBLEND_ZERO) {
                    m_SourceBlend = VXBLEND_SRCALPHA;
                    m_DestBlend = VXBLEND_INVSRCALPHA;
                }

                // Enable alpha blend if dest blend is not zero
                if (m_DestBlend != VXBLEND_ZERO) {
                    m_Flags = (m_Flags & ~0xFF) | ((m_Flags & 0xFF) | 8);
                    m_Flags = (m_Flags & ~0x2);
                }
            }
        } else {
            // Current format - read packed data
            CKDWORD diffuseColor = chunk->ReadDword();
            CKDWORD ambientColor = chunk->ReadDword();
            CKDWORD specularColor = chunk->ReadDword();
            CKDWORD emissiveColor = chunk->ReadDword();

            m_MaterialData.Diffuse = VxColor(diffuseColor);
            m_MaterialData.Ambient = VxColor(ambientColor);
            m_SpecularColor = VxColor(specularColor);
            m_MaterialData.Emissive = VxColor(emissiveColor);

            float power = chunk->ReadFloat();
            SetPower(power);

            // Read primary texture
            CKObject *texObj = chunk->ReadObject(m_Context);
            if (CKIsChildClassOf(texObj, CKCID_TEXTURE)) {
                m_Textures[0] = static_cast<CKTexture *>(texObj);
            }

            // Read border color
            m_TextureBorderColor = chunk->ReadDword();

            // Read packed modes
            CKDWORD packedModes = chunk->ReadDword();
            CKDWORD packedFlags = chunk->ReadDword();

            // Unpack modes
            m_TextureBlendMode = packedModes & 0xF;
            m_TextureMinMode = (packedModes >> 4) & 0xF;
            m_TextureMagMode = (packedModes >> 8) & 0xF;
            m_SourceBlend = (VXBLEND_MODE) ((packedModes >> 12) & 0xF);
            m_DestBlend = (VXBLEND_MODE) ((packedModes >> 16) & 0xF);
            m_ShadeMode = (packedModes >> 20) & 0xF;
            m_FillMode = (packedModes >> 24) & 0xF;
            m_TextureAddressMode = packedModes >> 28;

            // Unpack flags (matches CK2_3D.dll: low byte + 4-bit ZFunc + 4-bit AlphaFunc + AlphaRef byte)
            m_Flags = (m_Flags & ~0xFF) | (packedFlags & 0xFF);
            m_Flags = (m_Flags & ~0x7C000) | ((((packedFlags >> 8) & 0x1F) << 14));
            m_Flags = (m_Flags & ~0xF80000) | ((((packedFlags >> 16) & 0x1F) << 19));
            m_AlphaRef = (packedFlags >> 24) & 0xFF;

            // Ensure AlphaFunc is valid
            if (GetAlphaFunc() == 0) {
                SetAlphaFunc(VXCMP_ALWAYS);
            }
        }

        // Clamp shade mode to valid range
        if (m_ShadeMode > VXSHADE_GOURAUD) {
            m_ShadeMode = VXSHADE_GOURAUD;
        }
    }

    // Read additional textures
    if (chunk->SeekIdentifier(CK_STATESAVE_MATDATA2)) {
        m_Textures[1] = static_cast<CKTexture *>(chunk->ReadObject(m_Context));
        m_Textures[2] = static_cast<CKTexture *>(chunk->ReadObject(m_Context));
        m_Textures[3] = static_cast<CKTexture *>(chunk->ReadObject(m_Context));
    }

    // Read effect (without parameter)
    if (chunk->SeekIdentifier(CK_STATESAVE_MATDATA3)) {
        VX_EFFECT effect = static_cast<VX_EFFECT>(chunk->ReadDword());
        SetEffect(effect);
    }

    // Read effect with parameter
    if (chunk->SeekIdentifier(CK_STATESAVE_MATDATA5)) {
        m_EffectParameter = static_cast<CKParameter *>(chunk->ReadObject(m_Context));
        VX_EFFECT effect = static_cast<VX_EFFECT>(chunk->ReadDword());
        SetEffect(effect);
    }

    return CK_OK;
}

//=============================================================================
// Dependencies
//=============================================================================

/**
 * @brief Prepares dependencies for copy/save operations.
 *
 * Based on decompilation at 0x10065DE0.
 */
CKERROR RCKMaterial::PrepareDependencies(CKDependenciesContext &context) {
    CKERROR err = CKBeObject::PrepareDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    // Check if textures should be included
    if (context.GetClassDependencies(CKCID_MATERIAL) & 1) {
        for (int i = 0; i < 4; ++i) {
            if (m_Textures[i]) {
                m_Textures[i]->PrepareDependencies(context);
            }
        }
    }

    // Include effect parameter
    if (m_EffectParameter) {
        m_EffectParameter->PrepareDependencies(context);
    }

    return context.FinishPrepareDependencies(this, m_ClassID);
}

/**
 * @brief Remaps dependencies after copy/paste operations.
 *
 * Based on decompilation at 0x10065EA0.
 */
CKERROR RCKMaterial::RemapDependencies(CKDependenciesContext &context) {
    CKERROR err = CKBeObject::RemapDependencies(context);
    if (err != CK_OK) {
        return err;
    }

    // Remap textures if needed
    if (context.GetClassDependencies(CKCID_MATERIAL) & 1) {
        for (int i = 0; i < 4; ++i) {
            m_Textures[i] = static_cast<CKTexture *>(context.Remap(m_Textures[i]));
        }
    }

    // Remap effect parameter
    m_EffectParameter = static_cast<CKParameter *>(context.Remap(m_EffectParameter));

    return CK_OK;
}

/**
 * @brief Copies material data from another object.
 *
 * Based on decompilation at 0x10065F34.
 * The DLL copies:
 * 1. Textures array (16 bytes via memcpy)
 * 2. 125 bytes (0x7D) from m_MaterialData through m_AlphaRef
 * 3. m_EffectParameter separately
 *
 * Note: m_Sprite3DBatch, m_Callback, and m_CallbackArgument are NOT copied.
 */
CKERROR RCKMaterial::Copy(CKObject &o, CKDependenciesContext &context) {
    CKERROR err = CKBeObject::Copy(o, context);
    if (err != CK_OK) {
        return err;
    }

    RCKMaterial *src = static_cast<RCKMaterial *>(&o);

    // Get dependency settings (called but result not used in original)
    context.GetClassDependencies(CKCID_MATERIAL);

    // Copy texture references (16 bytes)
    memcpy(m_Textures, src->m_Textures, sizeof(m_Textures));

    // Copy 125 bytes (0x7D) from m_MaterialData through first byte of m_AlphaRef
    // This is equivalent to copying all the material properties
    m_MaterialData = src->m_MaterialData;
    m_SpecularColor = src->m_SpecularColor;
    m_TextureBlendMode = src->m_TextureBlendMode;
    m_TextureMinMode = src->m_TextureMinMode;
    m_TextureMagMode = src->m_TextureMagMode;
    m_SourceBlend = src->m_SourceBlend;
    m_DestBlend = src->m_DestBlend;
    m_ShadeMode = src->m_ShadeMode;
    m_FillMode = src->m_FillMode;
    m_TextureAddressMode = src->m_TextureAddressMode;
    m_TextureBorderColor = src->m_TextureBorderColor;
    m_Flags = src->m_Flags;
    m_AlphaRef = src->m_AlphaRef;
    // Note: m_Sprite3DBatch, m_Callback, m_CallbackArgument are NOT copied per DLL

    // Copy effect parameter
    m_EffectParameter = src->m_EffectParameter;

    return CK_OK;
}

//=============================================================================
// Static Class Methods
//=============================================================================

/**
 * @brief Returns the class name for this material type.
 */
CKSTRING RCKMaterial::GetClassName() {
    return "Material";
}

/**
 * @brief Returns the dependencies count for the specified mode.
 */
int RCKMaterial::GetDependenciesCount(int mode) {
    int count;
    switch (mode) {
    case 1:
        count = 1;
        break;
    case 2:
        count = 1;
        break;
    case 3:
        count = 0;
        break;
    case 4:
        count = 1;
        break;
    default:
        count = 0;
        break;
    }
    return count;
}

/**
 * @brief Returns a dependency name.
 */
CKSTRING RCKMaterial::GetDependencies(int i, int mode) {
    if (i != 0) {
        return nullptr;
    } else {
        return "Texture";
    }
}

/**
 * @brief Registers the RCKMaterial class with the CK system.
 *
 * Based on decompilation at 0x10065D1E.
 */
void RCKMaterial::Register() {
    // Register notification from textures
    CKCLASSNOTIFYFROMCID(RCKMaterial, CKCID_TEXTURE);

    // Register associated parameter GUID
    CKPARAMETERFROMCLASS(RCKMaterial, CKPGUID_MATERIAL);

    // Register default options
    CKCLASSDEFAULTOPTIONS(RCKMaterial, CK_GENERALOPTIONS_CANUSECURRENTOBJECT);
}

/**
 * @brief Creates a new instance of RCKMaterial.
 *
 * Based on decompilation at 0x10065D70.
 *
 * @param Context The CKContext for the new material
 * @return New RCKMaterial instance or nullptr on failure
 */
RCKMaterial *RCKMaterial::CreateInstance(CKContext *Context) {
    return new RCKMaterial(Context, nullptr);
}

//=============================================================================
// Color Properties
//=============================================================================

/**
 * @brief Gets the specular power (shininess).
 *
 * Based on decompilation at 0x100664A0.
 */
float RCKMaterial::GetPower() {
    return m_MaterialData.SpecularPower;
}

/**
 * @brief Sets the specular power and updates MaterialData.Specular.
 *
 * When power >= 0.05, Specular is set to m_SpecularColor.
 * When power < 0.05, Specular is set to black (specular disabled).
 *
 * Based on decompilation at 0x100664C0.
 */
void RCKMaterial::SetPower(float Value) {
    m_MaterialData.SpecularPower = Value;

    // Update specular color based on power threshold
    if (m_MaterialData.SpecularPower >= 0.05f) {
        m_MaterialData.Specular = m_SpecularColor;
    } else {
        m_MaterialData.Specular = VxColor(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

/**
 * @brief Gets the ambient color.
 *
 * Based on decompilation at 0x100663C0.
 */
const VxColor &RCKMaterial::GetAmbient() {
    return m_MaterialData.Ambient;
}

/**
 * @brief Sets the ambient color.
 *
 * Based on decompilation at 0x100663E0.
 */
void RCKMaterial::SetAmbient(const VxColor &Color) {
    m_MaterialData.Ambient = Color;
}

/**
 * @brief Gets the diffuse color.
 *
 * Based on decompilation at 0x10066410.
 */
const VxColor &RCKMaterial::GetDiffuse() {
    return m_MaterialData.Diffuse;
}

/**
 * @brief Sets the diffuse color and notifies sprite batch.
 *
 * Based on decompilation at 0x10066430.
 */
void RCKMaterial::SetDiffuse(const VxColor &Color) {
    m_MaterialData.Diffuse = Color;

    // Invalidate sprite batch vertex data when diffuse color changes
    // Based on sub_10066480 which sets m_VertexCount = 0
    if (m_Sprite3DBatch) {
        m_Sprite3DBatch->m_VertexCount = 0;
    }
}

/**
 * @brief Gets the specular color (cached value).
 *
 * Based on decompilation at 0x10066530.
 */
const VxColor &RCKMaterial::GetSpecular() {
    return m_SpecularColor;
}

/**
 * @brief Sets the specular color and updates MaterialData.Specular.
 *
 * Based on decompilation at 0x10066550.
 */
void RCKMaterial::SetSpecular(const VxColor &Color) {
    m_SpecularColor = Color;

    // Update actual specular based on power threshold
    if (m_MaterialData.SpecularPower >= 0.05f) {
        m_MaterialData.Specular = m_SpecularColor;
    } else {
        m_MaterialData.Specular = VxColor(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Invalidate sprite batch vertex data when specular color changes
    // Based on sub_10066480 which sets m_VertexCount = 0
    if (m_Sprite3DBatch) {
        m_Sprite3DBatch->m_VertexCount = 0;
    }
}

/**
 * @brief Gets the emissive color.
 *
 * Based on decompilation at 0x100665F0.
 */
const VxColor &RCKMaterial::GetEmissive() {
    return m_MaterialData.Emissive;
}

/**
 * @brief Sets the emissive color.
 *
 * Based on decompilation at 0x10066610.
 */
void RCKMaterial::SetEmissive(const VxColor &Color) {
    m_MaterialData.Emissive = Color;
}

//=============================================================================
// Texture Management
//=============================================================================

/**
 * @brief Gets the texture at the specified slot.
 *
 * Based on decompilation at 0x100668E0.
 */
CKTexture *RCKMaterial::GetTexture(int TexIndex) {
    return m_Textures[TexIndex];
}

/**
 * @brief Sets the texture at the specified slot.
 *
 * Based on decompilation at 0x10062E8D.
 */
void RCKMaterial::SetTexture(int TexIndex, CKTexture *Tex) {
    m_Textures[TexIndex] = Tex;
}

/**
 * @brief Sets the primary texture (slot 0).
 */
void RCKMaterial::SetTexture0(CKTexture *Tex) {
    m_Textures[0] = Tex;
}

//=============================================================================
// Texture Blend Mode
//=============================================================================

/**
 * @brief Sets the texture blend mode.
 *
 * Based on decompilation at 0x10066640.
 */
void RCKMaterial::SetTextureBlendMode(VXTEXTURE_BLENDMODE BlendMode) {
    m_TextureBlendMode = BlendMode;
}

/**
 * @brief Gets the texture blend mode.
 *
 * Based on decompilation at 0x10066660.
 */
VXTEXTURE_BLENDMODE RCKMaterial::GetTextureBlendMode() {
    return static_cast<VXTEXTURE_BLENDMODE>(m_TextureBlendMode);
}

//=============================================================================
// Texture Filter Mode
//=============================================================================

/**
 * @brief Sets the texture minification filter mode.
 *
 * Based on decompilation at 0x10066680.
 */
void RCKMaterial::SetTextureMinMode(VXTEXTURE_FILTERMODE FilterMode) {
    m_TextureMinMode = FilterMode;
}

/**
 * @brief Gets the texture minification filter mode.
 *
 * Based on decompilation at 0x100666A0.
 */
VXTEXTURE_FILTERMODE RCKMaterial::GetTextureMinMode() {
    return static_cast<VXTEXTURE_FILTERMODE>(m_TextureMinMode);
}

/**
 * @brief Sets the texture magnification filter mode.
 *
 * Based on decompilation at 0x100666C0.
 */
void RCKMaterial::SetTextureMagMode(VXTEXTURE_FILTERMODE FilterMode) {
    m_TextureMagMode = FilterMode;
}

/**
 * @brief Gets the texture magnification filter mode.
 *
 * Based on decompilation at 0x100666E0.
 */
VXTEXTURE_FILTERMODE RCKMaterial::GetTextureMagMode() {
    return static_cast<VXTEXTURE_FILTERMODE>(m_TextureMagMode);
}

//=============================================================================
// Texture Address Mode
//=============================================================================

/**
 * @brief Sets the texture address mode.
 *
 * Based on decompilation at 0x10066700.
 */
void RCKMaterial::SetTextureAddressMode(VXTEXTURE_ADDRESSMODE Mode) {
    m_TextureAddressMode = Mode;
}

/**
 * @brief Gets the texture address mode.
 *
 * Based on decompilation at 0x10066720.
 */
VXTEXTURE_ADDRESSMODE RCKMaterial::GetTextureAddressMode() {
    return static_cast<VXTEXTURE_ADDRESSMODE>(m_TextureAddressMode);
}

//=============================================================================
// Texture Border Color
//=============================================================================

/**
 * @brief Sets the texture border color.
 *
 * Based on decompilation at 0x10066740.
 */
void RCKMaterial::SetTextureBorderColor(CKDWORD Color) {
    m_TextureBorderColor = Color;
}

/**
 * @brief Gets the texture border color.
 *
 * Based on decompilation at 0x10066760.
 */
CKDWORD RCKMaterial::GetTextureBorderColor() {
    return m_TextureBorderColor;
}

//=============================================================================
// Blend Factors
//=============================================================================

/**
 * @brief Sets the source blend factor.
 *
 * Based on decompilation at 0x10066780.
 */
void RCKMaterial::SetSourceBlend(VXBLEND_MODE BlendMode) {
    m_SourceBlend = BlendMode;
}

/**
 * @brief Gets the source blend factor.
 *
 * Based on decompilation at 0x100667A0.
 */
VXBLEND_MODE RCKMaterial::GetSourceBlend() {
    return static_cast<VXBLEND_MODE>(m_SourceBlend);
}

/**
 * @brief Sets the destination blend factor.
 *
 * Updates transparency flag when blend mode changes.
 * Based on decompilation at 0x10062EA7.
 */
void RCKMaterial::SetDestBlend(VXBLEND_MODE BlendMode) {
    if (BlendMode != m_DestBlend) {
        g_UpdateTransparency = TRUE;
    }
    m_DestBlend = BlendMode;
}

/**
 * @brief Gets the destination blend factor.
 *
 * Based on decompilation at 0x100667C0.
 */
VXBLEND_MODE RCKMaterial::GetDestBlend() {
    return static_cast<VXBLEND_MODE>(m_DestBlend);
}

//=============================================================================
// Two-Sided
//=============================================================================

/**
 * @brief Checks if the material is two-sided.
 *
 * Based on decompilation at 0x10062CBF.
 */
CKBOOL RCKMaterial::IsTwoSided() {
    return (m_Flags & 1) != 0;
}

/**
 * @brief Sets the two-sided flag.
 *
 * Based on decompilation at 0x10062ED8.
 */
void RCKMaterial::SetTwoSided(CKBOOL TwoSided) {
    if (TwoSided) {
        m_Flags |= 1;
    } else {
        m_Flags &= ~1;
    }
}

//=============================================================================
// Z Buffer
//=============================================================================

/**
 * @brief Checks if Z buffer writing is enabled.
 *
 * Based on decompilation at 0x10062CDB.
 */
CKBOOL RCKMaterial::ZWriteEnabled() {
    return (m_Flags & 2) != 0;
}

/**
 * @brief Enables or disables Z buffer writing.
 *
 * Based on decompilation at 0x10062FE9.
 */
void RCKMaterial::EnableZWrite(CKBOOL ZWrite) {
    if (ZWrite) {
        m_Flags |= 2;
    } else {
        m_Flags &= ~2;
    }
}

//=============================================================================
// Alpha Blending
//=============================================================================

/**
 * @brief Checks if alpha blending is enabled.
 *
 * Based on decompilation at 0x10062CF7.
 */
CKBOOL RCKMaterial::AlphaBlendEnabled() {
    return (m_Flags & 8) != 0;
}

/**
 * @brief Enables or disables alpha blending.
 *
 * Updates transparency flag when blend state changes.
 * Based on decompilation at 0x10062F47.
 */
void RCKMaterial::EnableAlphaBlend(CKBOOL Blend) {
    CKDWORD oldFlags = m_Flags & 0xFF;

    if (Blend) {
        m_Flags |= 8;
    } else {
        m_Flags &= ~8;
    }

    // Notify transparency change
    if (oldFlags != (m_Flags & 0xFF)) {
        g_UpdateTransparency = TRUE;
    }
}

//=============================================================================
// Internal helpers (non-virtual)
//=============================================================================

void RCKMaterial::PatchForChannelRender(VXBLEND_MODE sourceBlend, VXBLEND_MODE destBlend, VXBLEND_MODE &savedSourceBlend, VXBLEND_MODE &savedDestBlend, CKDWORD &savedFlags) {
    savedSourceBlend = m_SourceBlend;
    savedDestBlend = m_DestBlend;
    savedFlags = m_Flags;

    m_SourceBlend = sourceBlend;
    m_DestBlend = destBlend;

    // Match IDA low-byte mutations used during channel rendering:
    // - set AlphaBlend bit (0x08)
    // - clear ZWrite bit (0x02)
    const CKDWORD low = m_Flags & 0xFFu;
    const CKDWORD newLow = (low | 0x08u) & ~0x2u;
    m_Flags = (m_Flags & ~0xFFu) | newLow;
}

void RCKMaterial::RestoreAfterChannelRender(VXBLEND_MODE savedSourceBlend, VXBLEND_MODE savedDestBlend, CKDWORD savedFlags) {
    m_SourceBlend = savedSourceBlend;
    m_DestBlend = savedDestBlend;
    m_Flags = savedFlags;
}

//=============================================================================
// Z Comparison
//=============================================================================

/**
 * @brief Gets the Z comparison function.
 *
 * Based on decompilation at 0x10062C08.
 */
VXCMPFUNC RCKMaterial::GetZFunc() {
    return static_cast<VXCMPFUNC>((m_Flags >> 14) & 0x1F);
}

/**
 * @brief Sets the Z comparison function.
 *
 * Based on decompilation at 0x10062C22.
 */
void RCKMaterial::SetZFunc(VXCMPFUNC ZFunc) {
    m_Flags = (m_Flags & ~0x7C000) | ((ZFunc & 0x1F) << 14);
}

//=============================================================================
// Perspective Correction
//=============================================================================

/**
 * @brief Checks if perspective correction is enabled.
 *
 * Based on decompilation at 0x10062CA3.
 */
CKBOOL RCKMaterial::PerspectiveCorrectionEnabled() {
    return (m_Flags & 4) != 0;
}

/**
 * @brief Enables or disables perspective correction.
 *
 * Based on decompilation at 0x10063058.
 */
void RCKMaterial::EnablePerspectiveCorrection(CKBOOL Perspective) {
    if (Perspective) {
        m_Flags |= 4;
    } else {
        m_Flags &= ~4;
    }
}

//=============================================================================
// Fill Mode
//=============================================================================

/**
 * @brief Sets the fill mode.
 *
 * Based on decompilation at 0x100667E0.
 */
void RCKMaterial::SetFillMode(VXFILL_MODE FillMode) {
    m_FillMode = FillMode;
}

/**
 * @brief Gets the fill mode.
 *
 * Based on decompilation at 0x10066800.
 */
VXFILL_MODE RCKMaterial::GetFillMode() {
    return static_cast<VXFILL_MODE>(m_FillMode);
}

//=============================================================================
// Shade Mode
//=============================================================================

/**
 * @brief Sets the shade mode.
 *
 * Based on decompilation at 0x10066820.
 */
void RCKMaterial::SetShadeMode(VXSHADE_MODE ShadeMode) {
    m_ShadeMode = ShadeMode;
}

/**
 * @brief Gets the shade mode.
 *
 * Based on decompilation at 0x10066840.
 */
VXSHADE_MODE RCKMaterial::GetShadeMode() {
    return static_cast<VXSHADE_MODE>(m_ShadeMode);
}

//=============================================================================
// Set As Current
//=============================================================================

/**
 * @brief Sets this material as the current material for rendering.
 *
 * This method applies all material settings to the render context including:
 * - Material data (diffuse, ambient, specular, emissive)
 * - Texture settings (filtering, addressing, blend mode)
 * - Alpha blending and testing
 * - Render states (cull mode, shade mode, fill mode, z-write)
 * - Special effects (texture generation, bump mapping, etc.)
 *
 * Based on decompilation at 0x10064be0.
 *
 * @param context The render context to apply material to
 * @param Lit Whether to apply lighting material data
 * @param TextureStage The texture stage to apply texture to
 * @return TRUE if material was successfully set
 */
CKBOOL RCKMaterial::SetAsCurrent(CKRenderContext *context, CKBOOL Lit, int TextureStage) {
    RCKRenderContext *dev = static_cast<RCKRenderContext *>(context);

    if (m_Callback) {
        if (m_Callback(dev, this, m_CallbackArgument)) {
            return TRUE;
        }
    }

    CKFixedFunctionPipeline &ffp = dev->m_FFPipeline;

    // --- Material data ---
    if (Lit) {
        ffp.SetMaterial(&m_MaterialData);
    }

    // --- Cull mode ---
    if (IsTwoSided()) {
        ffp.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
    }

    // --- Fill mode ---
    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, m_FillMode);

    // --- Shade mode ---
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, m_ShadeMode);

    // --- Alpha blending ---
    if (AlphaBlendEnabled()) {
        ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
        ffp.SetRenderState(VXRENDERSTATE_SRCBLEND, (CKDWORD)m_SourceBlend);
        ffp.SetRenderState(VXRENDERSTATE_DESTBLEND, (CKDWORD)m_DestBlend);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    }

    // --- Depth ---
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, ZWriteEnabled() ? TRUE : FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZFUNC, (CKDWORD)GetZFunc());

    // --- Alpha test ---
    if (AlphaTestEnabled()) {
        ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
        ffp.SetRenderState(VXRENDERSTATE_ALPHAFUNC, (CKDWORD)GetAlphaFunc());
        ffp.SetRenderState(VXRENDERSTATE_ALPHAREF, m_AlphaRef);
    } else {
        ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    }

    // --- Texture ---
    CKTexture *tex = m_Textures[TextureStage];
    if (tex) {
        CKBOOL clamped = (m_TextureAddressMode == VXTEXTURE_ADDRESSCLAMP);
        tex->SetAsCurrent(context, clamped, TextureStage);

        ffp.SetTextureStageState(TextureStage, CKRST_TSS_MAGFILTER, m_TextureMagMode);
        ffp.SetTextureStageState(TextureStage, CKRST_TSS_MINFILTER, m_TextureMinMode);
        ffp.SetTextureStageState(TextureStage, CKRST_TSS_ADDRESS, m_TextureAddressMode);
        ffp.SetTextureStageState(TextureStage, CKRST_TSS_TEXTUREMAPBLEND, m_TextureBlendMode);
    } else {
        ffp.SetTexture(TextureStage, 0);
    }

    ffp.SetTextureStageState(TextureStage, CKRST_TSS_TEXCOORDINDEX,
                             CKFFPackTexcoordIndex((CKDWORD)TextureStage, CKFF_TEXGEN_NONE));
    ffp.SetTextureStageState(TextureStage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_NONE);

    VX_EFFECT effect = GetEffect();
    if (effect == VXEFFECT_TEXGEN) {
        TexGenEffect(dev, ReadTexGenParameter(m_EffectParameter), nullptr, TextureStage);
    }

    return TRUE;
}

//=============================================================================
// Effect Helper Methods
//=============================================================================

/**
 * @brief Applies texture generation effect.
 *
 * Based on decompilation at 0x1006425a. Handles various texture coordinate
 * generation modes including transforms, reflection, chrome, and planar mapping.
 *
 * @param dev The render context
 * @param texGen The texture generation mode
 * @param refEntity Reference entity for texture generation
 * @param stage Texture stage
 * @return Effect result flags (bit 0 = coords set, bit 1 = texture set)
 */
CKDWORD RCKMaterial::TexGenEffect(RCKRenderContext *dev, VX_EFFECTTEXGEN texGen, RCK3dEntity *refEntity, int stage) {
    if (!dev || stage < 0)
        return 0;

    (void) refEntity;

    CKDWORD generation = CKFF_TEXGEN_NONE;
    switch (texGen) {
    case VXEFFECT_TGREFLECT:
    case VXEFFECT_TGCUBEMAP_REFLECT:
        generation = CKFF_TEXGEN_CAMERASPACEREFLECTION;
        break;
    case VXEFFECT_TGCHROME:
    case VXEFFECT_TGCUBEMAP_NORMALS:
        generation = CKFF_TEXGEN_CAMERASPACENORMAL;
        break;
    case VXEFFECT_TGPLANAR:
    case VXEFFECT_TGCUBEMAP_SKYMAP:
    case VXEFFECT_TGCUBEMAP_POSITIONS:
        generation = CKFF_TEXGEN_CAMERASPACEPOSITION;
        break;
    case VXEFFECT_TGTRANSFORM:
    case VXEFFECT_TGNONE:
    default:
        generation = CKFF_TEXGEN_NONE;
        break;
    }

    CKFixedFunctionPipeline &ffp = dev->m_FFPipeline;
    ffp.SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
                             CKFFPackTexcoordIndex((CKDWORD)stage, generation));
    ffp.SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS,
                             generation == CKFF_TEXGEN_NONE ? CKRST_TTF_NONE : CKRST_TTF_COUNT2);
    return generation == CKFF_TEXGEN_NONE ? 0 : 1;
}

/**
 * @brief Applies bump mapping environment effect.
 *
 * Based on decompilation at 0x1006387e.
 * Sets up bump mapping with environment reflection on multiple texture stages.
 *
 * @param dev The render context
 * @return Effect result flags (2 for multi-texture effect)
 */
CKDWORD RCKMaterial::BumpMapEnvEffect(RCKRenderContext *dev) {
    // TODO: Phase 2 - route through FFPipeline (SetTextureStageState, SetTransformMatrix)
    (void) dev;
    return 0;
}

/**
 * @brief Applies DP3 (dot product 3) effect for per-pixel lighting.
 *
 * Based on decompilation at 0x10063e1e.
 * Uses dot product operation to compute per-pixel lighting with normal maps.
 *
 * @param dev The render context
 * @param stage Starting texture stage
 * @return Effect result flags (2 for multi-texture effect)
 */
CKDWORD RCKMaterial::DP3Effect(RCKRenderContext *dev, int stage) {
    // TODO: Phase 2 - route through FFPipeline (SetRenderState, SetTextureStageState, SetTransformMatrix)
    (void) dev;
    (void) stage;
    return 0;
}

/**
 * @brief Applies multi-texture blending effect.
 *
 * Based on decompilation at 0x10063303.
 * Blends multiple textures with configurable operations and texture generation.
 *
 * @param dev The render context
 * @param stage Starting texture stage
 * @return Effect result flags (2 for multi-texture effect)
 */
CKDWORD RCKMaterial::BlendTexturesEffect(RCKRenderContext *dev, int stage) {
    // TODO: Phase 2 - route through FFPipeline (SetTextureStageState, SetTransformMatrix)
    (void) dev;
    (void) stage;
    return 0;
}

/**
 * @brief Checks if this material has alpha transparency.
 *
 * Returns TRUE if blending is enabled and dest blend is not zero.
 */
CKBOOL RCKMaterial::IsAlphaTransparent() {
    return AlphaBlendEnabled() && (m_DestBlend != VXBLEND_ZERO);
}

//=============================================================================
// Alpha Testing
//=============================================================================

/**
 * @brief Checks if alpha testing is enabled.
 *
 * Based on decompilation at 0x10062C87.
 */
CKBOOL RCKMaterial::AlphaTestEnabled() {
    return (m_Flags & 0x10) != 0;
}

/**
 * @brief Enables or disables alpha testing.
 *
 * Updates transparency flag when alpha test state changes.
 * Based on decompilation at 0x100630C7.
 */
void RCKMaterial::EnableAlphaTest(CKBOOL Enable) {
    CKDWORD oldFlags = m_Flags & 0xFF;

    if (Enable) {
        m_Flags |= 0x10;
    } else {
        m_Flags &= ~0x10;
    }

    // Notify transparency change
    if (oldFlags != (m_Flags & 0xFF)) {
        g_UpdateTransparency = TRUE;
    }
}

/**
 * @brief Gets the alpha comparison function.
 *
 * Based on decompilation at 0x10062BBE.
 */
VXCMPFUNC RCKMaterial::GetAlphaFunc() {
    return static_cast<VXCMPFUNC>((m_Flags >> 19) & 0x1F);
}

/**
 * @brief Sets the alpha comparison function.
 *
 * Based on decompilation at 0x10062BD8.
 */
void RCKMaterial::SetAlphaFunc(VXCMPFUNC AlphaFunc) {
    m_Flags = (m_Flags & ~0xF80000) | ((AlphaFunc & 0x1F) << 19);
}

/**
 * @brief Gets the alpha reference value.
 *
 * Based on decompilation at 0x10062B83.
 */
CKBYTE RCKMaterial::GetAlphaRef() {
    return static_cast<CKBYTE>(m_AlphaRef & 0xFF);
}

/**
 * @brief Sets the alpha reference value.
 *
 * Based on decompilation at 0x10062B97.
 */
void RCKMaterial::SetAlphaRef(CKBYTE AlphaRef) {
    m_AlphaRef = AlphaRef;
}

//=============================================================================
// Callback
//=============================================================================

/**
 * @brief Sets the material callback function.
 *
 * Based on decompilation at 0x10066860.
 */
void RCKMaterial::SetCallback(CK_MATERIALCALLBACK Fct, void *Argument) {
    m_Callback = Fct;
    m_CallbackArgument = Argument;
}

/**
 * @brief Gets the material callback function.
 *
 * Based on decompilation at 0x10066890.
 */
CK_MATERIALCALLBACK RCKMaterial::GetCallback(void **Argument) {
    if (Argument) {
        *Argument = m_CallbackArgument;
    }
    return m_Callback;
}

//=============================================================================
// Effects
//=============================================================================

/**
 * @brief Sets a special effect on the material.
 *
 * Creates or updates the effect parameter based on the effect type.
 * Based on decompilation at 0x10063169.
 */
void RCKMaterial::SetEffect(VX_EFFECT Effect) {
    // Clear effect bits and set new effect
    m_Flags = (m_Flags & ~0x3F00) | ((Effect & 0x3F) << 8);

    VX_EFFECT currentEffect = GetEffect();
    if (currentEffect != VXEFFECT_NONE) {
        // Get render manager for effect description
        CKRenderManager *renderManager = m_Context->GetRenderManager();
        if (renderManager) {
            const VxEffectDescription &effectDesc = renderManager->GetEffectDescription(Effect);

            // Check if parameter type is valid (non-null GUID)
            if (effectDesc.ParameterType.d1 != 0 || effectDesc.ParameterType.d2 != 0) {
                if (m_EffectParameter) {
                    // Update existing parameter
                    m_EffectParameter->SetName(const_cast<CKSTRING>(effectDesc.ParameterDescription.CStr()));

                    CKGUID currentGuid = m_EffectParameter->GetGUID();
                    if (currentGuid != effectDesc.ParameterType) {
                        m_EffectParameter->SetGUID(effectDesc.ParameterType);
                        m_EffectParameter->
                            SetStringValue(const_cast<CKSTRING>(effectDesc.ParameterDefaultValue.CStr()));
                    }
                } else {
                    // Create new parameter
                    CK_OBJECTCREATION_OPTIONS options = IsDynamic()
                                                            ? CK_OBJECTCREATION_DYNAMIC
                                                            : CK_OBJECTCREATION_NONAMECHECK;

                    m_EffectParameter = static_cast<CKParameter *>(
                        m_Context->CreateObject(CKCID_PARAMETER,
                                                const_cast<CKSTRING>(effectDesc.ParameterDescription.CStr()),
                                                options, nullptr));

                    if (m_EffectParameter) {
                        m_EffectParameter->SetGUID(effectDesc.ParameterType);
                    }
                }
            }
        }
    } else {
        // Destroy effect parameter when effect is cleared
        if (m_EffectParameter) {
            m_Context->DestroyObject(m_EffectParameter, CK_DESTROY_TEMPOBJECT, nullptr);
            m_EffectParameter = nullptr;
        }
    }
}

/**
 * @brief Gets the current effect.
 *
 * Based on decompilation at 0x100668C0.
 */
VX_EFFECT RCKMaterial::GetEffect() {
    return static_cast<VX_EFFECT>((m_Flags >> 8) & 0x3F);
}

/**
 * @brief Gets the effect parameter.
 *
 * Based on decompilation at 0x10066900.
 */
CKParameter *RCKMaterial::GetEffectParameter() {
    return m_EffectParameter;
}

//=============================================================================
// Sprite3D Batch Methods
//=============================================================================

/**
 * @brief Adds a sprite to this material's sprite batch.
 *
 * Based on decompilation at 0x10062D99.
 * Creates a new batch if none exists, fills the batch with sprite data,
 * and sets the batch flag on first addition.
 *
 * @param sprite The sprite to add to the batch
 * @return TRUE if this is the first sprite in the batch (batch was just created/started),
 *         FALSE if batch already existed and sprite was just added
 */
CKBOOL RCKMaterial::AddSprite3DBatch(RCKSprite3D *sprite) {
    // Create batch if it doesn't exist
    if (!m_Sprite3DBatch) {
        m_Sprite3DBatch = new CKSprite3DBatch();
    }

    // Fill the batch with sprite data
    sprite->FillBatch(m_Sprite3DBatch);

    // Check if batch flag (0x20) is already set
    if ((m_Flags & 0x20) != 0) {
        return FALSE; // Batch already existed
    }

    // Set batch flag (0x20) in the low byte (IDA preserves the high bytes)
    const CKDWORD high = (m_Flags & ~0xFF);
    const CKBYTE low = (CKBYTE) m_Flags;
    m_Flags = high | (CKDWORD) (low | 0x20);
    return TRUE;
}

/**
 * @brief Flushes (clears) this material's Sprite3D batch.
 *
 * Based on decompilation at 0x10062B17.
 */
void RCKMaterial::FlushSprite3DBatch() {
    // Clear Sprite3D batch flag (bit 5 in low byte)
    m_Flags &= ~0x20;

    if (!m_Sprite3DBatch) {
        return;
    }

    m_Sprite3DBatch->m_Indices.Resize(0);
    m_Sprite3DBatch->m_Vertices.Resize(0);
    m_Sprite3DBatch->m_Flags = 0;
}
