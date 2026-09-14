#ifndef CKFFPRESENTDRAW_H
#define CKFFPRESENTDRAW_H

#include "CKRasterizerContextData.h"

// Builds the CPU-side fullscreen draw used for resolve, scaling and copies.
// The containing Concrete Rasterizer Context allocates transient geometry and
// submits the resulting command directly to its native graphics API.
class CKFFPresentDraw {
public:
    CKERROR Prepare(CKDWORD Texture, CKDWORD Width, CKDWORD Height,
                    CKBOOL Linear, CKBOOL FXAA, float Sharpness,
                    CKBOOL FlipV, CKDWORD Program, CKDWORD Layout,
                    CKTransientVertexData &Vertices, CKDrawCommand &Draw);

private:
    CKFFConstantSet m_Constants;
    CKFFTextureBindings m_Bindings;
};

#endif // CKFFPRESENTDRAW_H
