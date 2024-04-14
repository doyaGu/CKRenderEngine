#ifndef CKRENDEREDSCENE_H
#define CKRENDEREDSCENE_H

#include "CKContext.h"

class RCKRenderContext;
class RCKMaterial;
class RCK3dEntity;
class RCKCamera;

class CKRenderedScene {
public:

protected:
    RCKRenderContext *m_RenderContext;
    CKContext *m_Context;
    RCKMaterial *m_BackgroundMaterial;
    RCK3dEntity *m_3dEntity;
    RCKCamera *m_AttachedCamera;
    XArray<int> field_14;
    XArray<int> m_Cameras;
    XArray<int> m_Lights;
    CKDWORD m_FogMode;
    float m_FogStart;
    float m_FogEnd;
    float m_FogDensity;
    CKDWORD m_FogColor;
    CKDWORD m_AmbientLight;
    CKDWORD field_50;
    XArray<int> field_54;
};

#endif // CKRENDEREDSCENE_H