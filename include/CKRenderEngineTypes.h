#ifndef CKRENDERENGINETYPES_H
#define CKRENDERENGINETYPES_H

#include "VxDefines.h"
#include "VxColor.h"
#include "VxVector.h"
#include "Vx2dVector.h"
#include "VxFrustum.h"
#include "XArray.h"
#include "XClassArray.h"
#include "CKTypes.h"
#include "CKRasterizerTypes.h"

class CKRenderContext;

class RCKRenderManager;
class RCKRenderContext;
class RCKKinematicChain;
class RCKMaterial;
class RCKTexture;
class RCKMesh;
class RCKPatchMesh;
class RCKAnimation;
class RCKKeyedAnimation;
class RCKObjectAnimation;
class RCKLayer;
class RCKRenderObject;
class RCK2dEntity;
class RCK3dEntity;
class RCKCamera;
class RCKLight;
class RCKCurvePoint;
class RCKCurve;
class RCK3dObject;
class RCKSprite3D;
class RCKCharacter;
class RCKPlace;
class RCKGrid;
class RCKBodyPart;
class RCKTargetCamera;
class RCKTargetLight;
class RCKSprite;
class RCKSpriteText;

struct VxCallBack {
    void *callback;
    void *argument;
    CKBOOL temp;
    CKBOOL beforeTransparent;
};

class CKCallbacksContainer {
public:
    CKCallbacksContainer() : m_CallBack(nullptr) {}

    CKBOOL AddPreCallback(void *Function,
                          void *Argument,
                          CKBOOL Temporary,
                          CKRenderManager *renderManager);
    CKBOOL RemovePreCallback(void *Function, void *Argument);

    CKBOOL SetCallBack(void *Function, void *Argument);
    CKBOOL RemoveCallBack();

    CKBOOL AddPostCallback(void *Function,
                           void *Argument,
                           CKBOOL Temporary,
                           CKRenderManager *renderManager,
                           CKBOOL BeforeTransparent = FALSE);
    CKBOOL RemovePostCallback(void *Function, void *Argument);

    void Clear();
    void ClearPreCallbacks();
    void ClearPostCallbacks();

    void ExecutePreCallbacks(CKRenderContext *dev, CKBOOL temporaryOnly = FALSE);
    void ExecutePostCallbacks(CKRenderContext *dev, CKBOOL temporaryOnly = FALSE, CKBOOL beforeTransparent = FALSE);
    
    void ExecuteCallbackList(XClassArray<VxCallBack> &callbacks,
                             CKRenderContext *context,
                             CKBOOL removeTemporary,
                             int stageFilter);

    VxCallBack *m_CallBack;
    XClassArray<VxCallBack> m_PreCallBacks;
    XClassArray<VxCallBack> m_PostCallBacks;
};

/**
 * @brief Progressive mesh structure for LOD (Level of Detail) support.
 * 
 * Used by CKMesh to store progressive mesh data for dynamic level of detail.
 * Size: 0x2C (44 bytes)
 */
struct CKProgressiveMesh {
    int m_Field0;               // 0x00: Unknown field
    int m_MorphEnabled;         // 0x04: Morph enabled flag
    int m_MorphStep;            // 0x08: Morph step value
    XArray<CKDWORD> m_Data;     // 0x0C: Progressive mesh data array
    
    CKProgressiveMesh() : m_Field0(0), m_MorphEnabled(0), m_MorphStep(0) {}
};

// Forward declaration for progressive mesh callback
class CK3dEntity;
class CKMesh;
void ProgressiveMeshPreRenderCallback(CKRenderContext *ctx, CK3dEntity *entity, CKMesh *mesh, void *data);

struct VxOption {
    CKDWORD Value;
    XString Key;

    void Set(XString &key, CKDWORD &value) {
        Key = key;
        Value = value;
    }

    void Set(const char *key, CKDWORD value) {
        Key = key;
        Value = value;
    }
};

struct VxDriverDescEx {
    CKBOOL CapsUpToDate;
    CKDWORD DriverId;
    char DriverDesc[512];
    char DriverDesc2[512];
    CKBOOL Hardware;
    CKDWORD DisplayModeCount;
    VxDisplayMode *DisplayModes;
    XSArray<VxImageDescEx> TextureFormats;
    Vx2DCapsDesc Caps2D;
    Vx3DCapsDesc Caps3D;
    CKRasterizer *Rasterizer;
    CKRasterizerDriver *RasterizerDriver;
};

struct VxColors {
    CKDWORD Color;
    CKDWORD Specular;
};

struct VxVertex {
    VxVector m_Position;
    VxVector m_Normal;
    Vx2DVector m_UV;
};

struct CKFace {
    CKWORD m_VertexIndex[3];
    VxVector m_Normal;
    CKWORD m_MatIndex;
    CKWORD m_ChannelMask;
};

struct CKMaterialChannel {
    Vx2DVector *m_uv;           // 0x00 - UV pointer for this channel
    CKMaterial *m_Material;    // 0x04
    VXBLEND_MODE m_SourceBlend; // 0x08
    VXBLEND_MODE m_DestBlend;   // 0x0C
    CKDWORD m_Flags;           // 0x10
    XVoidArray *m_FaceIndices; // 0x14 - Pointer to face indices array for this channel (or nullptr if all faces use it)
    CKDWORD field_18;          // 0x18 - Extra field for alignment to 28 bytes
};

struct CKPrimitiveEntry {
    VXPRIMITIVETYPE m_Type;
    XArray<CKWORD> m_Indices;
    CKDWORD m_IndexBufferOffset;
};

struct CKMaterialGroup {
    RCKMaterial *m_Material;     // 0x00
    XClassArray<CKPrimitiveEntry> field_4;  // 0x04 - Primitive entries (size 12)
    XVoidArray field_10;         // 0x10 - (size 12)
    CKDWORD field_1C;            // 0x1C
    CKDWORD field_20;            // 0x20
    CKDWORD field_24;            // 0x24
    CKDWORD field_28;            // 0x28 - Start vertex
    CKDWORD field_2C;            // 0x2C - Vertex count
    CKDWORD field_30;            // 0x30 - Vertex buffer data ptr
};

struct CKSprite3DBatch {
    CKDWORD field_0;
    CKDWORD field_4;
    CKDWORD field_8;
    CKDWORD field_C;
    CKDWORD field_10;
    CKDWORD field_14;
    CKDWORD field_18;
    CKDWORD field_1C;
    CKDWORD field_20;
};

struct CKKeyframeData {
    CKDWORD m_PositionController;
    CKDWORD m_ScaleController;
    CKDWORD m_RotationController;
    CKDWORD m_ScaleAxisController;
    void *m_MorphController;
    float m_Length;
    CKDWORD field_18;
    RCKObjectAnimation *m_ObjectAnimation;
};

#endif // CKRENDERENGINETYPES_H
