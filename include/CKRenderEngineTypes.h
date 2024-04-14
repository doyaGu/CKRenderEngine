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
};

class CKCallbacksContainer {
public:

protected:
    XClassArray<VxCallBack> m_PreCallBacks;
    void *m_Args;
    XClassArray<VxCallBack> m_PostCallBacks;
};

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
    VxVector m_Normal;
    CKWORD m_MatIndex;
    CKWORD m_ChannelMask;
};

struct CKMaterialChannel {
    CKDWORD field_0;
    RCKMaterial *m_Material;
    VXBLEND_MODE m_SourceBlend;
    VXBLEND_MODE m_DestBlend;
    CKDWORD m_Flags;
    Vx2DVector m_UV;
};

struct CKMaterialGroup {
    RCKMaterial *m_Material;
    XVoidArray field_4;
    XVoidArray field_10;
    CKDWORD field_1C;
    CKDWORD field_20;
    CKDWORD field_24;
    CKDWORD field_28;
    CKDWORD field_2C;
    CKDWORD field_30;
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
