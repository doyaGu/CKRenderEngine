#ifndef CKRE_SCENE_CAPTURE_SCENEUTIL_H
#define CKRE_SCENE_CAPTURE_SCENEUTIL_H

#include "SceneRegistry.h"

// Procedural content helpers. Everything goes through the public CK2 API so
// the same scenes run on our render engine and on the original DLLs.

typedef void (*TexturePixelFn)(int x, int y, int w, int h, void *user, CKDWORD *argb);

CKMaterial *SceneCreateMaterial(SceneContext &sc, const char *name, const VxColor &diffuse, CKTexture *texture = NULL);
CKTexture *SceneCreateTexture(SceneContext &sc, const char *name, int w, int h, TexturePixelFn fn, void *user);
CKTexture *SceneCreateCheckerTexture(SceneContext &sc, const char *name, int w, int h, int cell, CKDWORD colorA, CKDWORD colorB);
CKTexture *SceneCreateGradientTexture(SceneContext &sc, const char *name, int w, int h, CKDWORD left, CKDWORD right, CKDWORD top);
CKTexture *SceneCreateCutoutTexture(SceneContext &sc, const char *name, int w, int h);
CKTexture *SceneCreateAlphaRampTexture(SceneContext &sc, const char *name, int w, int h, CKDWORD rgb);

CKMesh *SceneCreateBoxMesh(SceneContext &sc, const char *name, const VxVector &size, CKMaterial *mat, float uvRepeat = 1.0f);
CKMesh *SceneCreatePlaneMesh(SceneContext &sc, const char *name, float width, float depth, int subdiv, float uvRepeat, CKMaterial *mat);
CKMesh *SceneCreateSphereMesh(SceneContext &sc, const char *name, float radius, int rings, int segments, CKMaterial *mat);
CKMesh *SceneCreateQuadMesh(SceneContext &sc, const char *name, float width, float height, CKMaterial *mat);

CK3dEntity *SceneCreateEntity(SceneContext &sc, const char *name, CKMesh *mesh, const VxVector &position);
CKCamera *SceneCreateCamera(SceneContext &sc, const char *name, const VxVector &position, const VxVector &target,
                            float fovDegrees, float nearPlane = 0.1f, float farPlane = 200.0f);
CKLight *SceneCreateLight(SceneContext &sc, const char *name, VXLIGHT_TYPE type, const VxColor &color,
                          const VxVector &position, const VxVector &direction, float range);
CK2dEntity *SceneCreate2dQuad(SceneContext &sc, const char *name, const VxRect &rect, CKMaterial *mat, bool background);

void SceneSetBackgroundColor(SceneContext &sc, CKDWORD argb);
void SceneSetAmbient(SceneContext &sc, CKDWORD argb);

// Immediate-mode screen-space quad for render callbacks. Coordinates are
// window pixels, z in 0..1. Uses the current render states except for the
// texture (unbound) and fill mode (solid).
void SceneDrawScreenQuad(CKRenderContext *rc, float x0, float y0, float x1, float y1, CKDWORD argb, float z = 0.5f);

// Frame-driven deterministic angle (degrees) for animated scenes.
float SceneFrameAngle(const SceneContext &sc, float degreesPerFrame);

#endif // CKRE_SCENE_CAPTURE_SCENEUTIL_H
