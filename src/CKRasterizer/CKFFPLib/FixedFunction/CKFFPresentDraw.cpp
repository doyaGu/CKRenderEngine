#include "CKFFPresentDraw.h"

#include "CKFFShaderInterface.h"

#include <string.h>

namespace {
struct CKFFPresentVertex {
    float X, Y, Z;
    float U, V;
};
}

CKERROR CKFFPresentDraw::Prepare(
    CKDWORD Texture, CKDWORD Width, CKDWORD Height,
    CKBOOL Linear, CKBOOL FXAA, float Sharpness,
    CKBOOL FlipV, CKDWORD Program, CKDWORD Layout,
    CKTransientVertexData &Vertices, CKDrawCommand &Draw)
{
    if (!Vertices.Data || Vertices.Count < 3 ||
        Vertices.Stride < sizeof(CKFFPresentVertex))
        return CKERR_OUTOFMEMORY;
    if (!Texture || !Width || !Height || !Program || !Layout)
        return CKERR_INVALIDPARAMETER;

    CKFFPresentVertex *vertices =
        (CKFFPresentVertex *)Vertices.Data;
    const float bottomV = FlipV ? 1.0f : 0.0f;
    const float extendedTopV = FlipV ? -1.0f : 2.0f;
    vertices[0] = {-1.0f, -1.0f, 0.0f, 0.0f, bottomV};
    vertices[1] = { 3.0f, -1.0f, 0.0f, 2.0f, bottomV};
    vertices[2] = {-1.0f,  3.0f, 0.0f, 0.0f, extendedTopV};

    CKSamplerDesc sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.MinFilter = Linear ? CKRST_FILTER_LINEAR
                               : CKRST_FILTER_NEAREST;
    sampler.MagFilter = sampler.MinFilter;
    sampler.MipFilter = CKRST_FILTER_NONE;
    sampler.AddressU = CKRST_ADDRESS_CLAMP;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_CLAMP;
    sampler.CompareFunc = CKRST_COMPARE_NONE;

    const float params[4] = {
        1.0f / (float)Width,
        1.0f / (float)Height,
        FXAA ? 1.0f : 0.0f,
        Sharpness
    };
    const CKERROR constants = CKFFSetConstants(
        &m_Constants, CKRST_BLOCK_PRESENT_PARAMS, params, 1);
    if (constants != CK_OK)
        return constants;

    CKFFPipelineState state;
    state.State = CKDrawStateBuilder()
        .Depth(FALSE, FALSE, VXCMP_ALWAYS)
        .Cull(VXCULL_NONE)
        .Build();
    m_Bindings[CKFF_SLOT_PRESENT].Texture = Texture;
    m_Bindings[CKFF_SLOT_PRESENT].Sampler = sampler;

    Draw = CKDrawCommand();
    Draw.Pipeline = state;
    Draw.Textures = &m_Bindings;
    Draw.Constants = &m_Constants;
    Draw.Program = Program;
    Draw.Layout = Layout;
    Draw.TransientVertices = &Vertices;
    Draw.VertexCount = 3;
    return CK_OK;
}
