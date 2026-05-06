#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDrawStateCache.h"
#include "TestTriangleMultiset.h"

namespace {

void Test_DPFlags_PretransformedTexturedColor_UsesPositionT() {
    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(CKRST_DP_CL_VCT, false, true);
    TestCheck((fmt & CKFF_VF_POSITIONT) != 0, "Pre-transformed CL/VCT vertices must use POSITIONT");
    TestCheck((fmt & CKFF_VF_POSITION) == 0, "Pre-transformed vertices must not use 3D POSITION");
    TestCheck((fmt & CKFF_VF_COLOR0) != 0, "CL/VCT must include diffuse color");
    TestCheck((fmt & CKFF_VF_TEXCOORD0) != 0, "CL/VCT must include stage 0 texcoord");
    TestCheck((fmt & CKFF_VF_COLOR1) != 0, "Canonical PositionT layout must include default specular color");
    TestCheck(CKVertexLayoutCache::ComputeStride(fmt) == 32, "POSITIONT + COLOR0 + COLOR1 + TEXCOORD0 stride must be 32 bytes");
}

void Test_DPFlags_TransformedNormalTextured_UsesPosition3() {
    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(CKRST_DP_TR_CL_VNT, true, true);
    TestCheck((fmt & CKFF_VF_POSITION) != 0, "Transformed vertices must use 3D POSITION");
    TestCheck((fmt & CKFF_VF_POSITIONT) == 0, "Transformed vertices must not use POSITIONT");
    TestCheck((fmt & CKFF_VF_NORMAL) != 0, "TR/CL/VNT must include normal");
    TestCheck((fmt & CKFF_VF_TEXCOORD0) != 0, "TR/CL/VNT must include stage 0 texcoord");
    TestCheck((fmt & CKFF_VF_COLOR0) != 0, "Canonical 3D layout must include default diffuse color");
    TestCheck((fmt & CKFF_VF_COLOR1) != 0, "Canonical 3D layout must include default specular color");
    TestCheck(CKVertexLayoutCache::ComputeStride(fmt) == 40, "POSITION + NORMAL + COLOR0 + COLOR1 + TEXCOORD0 stride must be 40 bytes");
}

void Test_PrimitiveFanConversion_ProducesTriangleList() {
    CKWORD src[] = {0, 1, 2, 3};
    CKWORD dst[6] = {};
    const int out = CKTransientGeometry::ConvertPrimitiveToTriangleList(VX_TRIANGLEFAN, src, 4, dst);
    TestCheck(out == 6, "Four-vertex fan must produce two triangles");
    TestCheck(dst[0] == 0 && dst[1] == 1 && dst[2] == 2, "Fan first triangle mismatch");
    TestCheck(dst[3] == 0 && dst[4] == 2 && dst[5] == 3, "Fan second triangle mismatch");
}

void Test_PrimitiveStripConversion_AlternatesWinding() {
    CKWORD src[] = {0, 1, 2, 3};
    CKWORD dst[6] = {};
    const int out = CKTransientGeometry::ConvertPrimitiveToTriangleList(VX_TRIANGLESTRIP, src, 4, dst);
    TestCheck(out == 6, "Four-vertex strip must produce two triangles");
    TestCheck(dst[0] == 0 && dst[1] == 1 && dst[2] == 2, "Strip first triangle mismatch");
    TestCheck(dst[3] == 2 && dst[4] == 1 && dst[5] == 3, "Strip second triangle winding mismatch");
}

void Test_Interleave_IgnoresColorPointersWithoutDPFlags() {
    float pos[3] = {1.0f, 2.0f, 3.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2] = {0.25f, 0.75f};
    CKDWORD blackDiffuse = 0xFF000000;
    CKDWORD whiteSpecular = 0xFFFFFFFF;

    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.Flags = CKRST_DP_TR_CL_VNT;
    data.VertexCount = 1;
    data.PositionPtr = pos;
    data.PositionStride = sizeof(pos);
    data.NormalPtr = normal;
    data.NormalStride = sizeof(normal);
    data.TexCoordPtr = uv;
    data.TexCoordStride = sizeof(uv);
    data.ColorPtr = &blackDiffuse;
    data.ColorStride = sizeof(blackDiffuse);
    data.SpecularColorPtr = &whiteSpecular;
    data.SpecularColorStride = sizeof(whiteSpecular);

    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(data.Flags, true, true);
    const CKDWORD stride = CKVertexLayoutCache::ComputeStride(fmt);
    CKBYTE buffer[64] = {};
    CKTransientGeometry::InterleaveVertices(buffer, stride, 1, fmt, &data);

    CKDWORD color0 = 0;
    CKDWORD color1 = 0;
    memcpy(&color0, buffer + 32, sizeof(color0));
    memcpy(&color1, buffer + 36, sizeof(color1));

    TestCheck(color0 == 0xFFFFFFFF, "COLOR0 must default to white when CKRST_DP_DIFFUSE is absent");
    TestCheck(color1 == 0x00000000, "COLOR1 must default to black when CKRST_DP_SPECULAR is absent");
}

void Test_DrawState_DefaultMaterialSourcesMatchFFP() {
    CKDrawStateCache cache;

    TestCheck(cache.GetRenderState(VXRENDERSTATE_COLORVERTEX) == TRUE,
              "FFP default COLORVERTEX must be enabled");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX) == TRUE,
              "FFP default diffuse source must be vertex diffuse");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX) == TRUE,
              "FFP default specular source must be vertex specular");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_AMBIENTFROMVERTEX) == FALSE,
              "FFP default ambient source must be material ambient");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_EMISSIVEFROMVERTEX) == FALSE,
              "FFP default emissive source must be material emissive");
}

void Test_DrawState_FixesBothSrcAlphaBlendPair() {
    CKDrawStateCache cache;
    cache.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    cache.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_BOTHSRCALPHA);
    cache.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_ZERO);

    const CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);
    const CKDWORD src = (state.Lo >> 16) & 0xF;
    const CKDWORD dst = (state.Lo >> 20) & 0xF;

    TestCheck(src == VXBLEND_SRCALPHA, "BOTHSRCALPHA source must become SRCALPHA");
    TestCheck(dst == VXBLEND_INVSRCALPHA, "BOTHSRCALPHA destination must become INVSRCALPHA");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("DP flags pre-transformed textured color", &Test_DPFlags_PretransformedTexturedColor_UsesPositionT);
    tests.Run("DP flags transformed normal textured", &Test_DPFlags_TransformedNormalTextured_UsesPosition3);
    tests.Run("Primitive fan conversion", &Test_PrimitiveFanConversion_ProducesTriangleList);
    tests.Run("Primitive strip conversion", &Test_PrimitiveStripConversion_AlternatesWinding);
    tests.Run("Interleave ignores undeclared color streams", &Test_Interleave_IgnoresColorPointersWithoutDPFlags);
    tests.Run("Draw state default material sources", &Test_DrawState_DefaultMaterialSourcesMatchFFP);
    tests.Run("Draw state fixes BOTHSRCALPHA blend pair", &Test_DrawState_FixesBothSrcAlphaBlendPair);
    return tests.ExitCode();
}
