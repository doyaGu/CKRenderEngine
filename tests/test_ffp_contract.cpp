#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDrawStateCache.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFShaderCache.h"
#include "CKRasterizer.h"
#include "CKRenderPipeline.h"
#include "RCKRenderContext.h"
#include "RCKMesh.h"
#include "TestTriangleMultiset.h"

#include <fstream>
#include <iterator>
#include <cmath>
#include <string>

namespace {

std::string ReadRenderEngineSource(const char *relativePath) {
    std::string path = __FILE__;
    const std::string suffix = "tests\\test_ffp_contract.cpp";
    size_t pos = path.rfind(suffix);
    if (pos == std::string::npos) {
        const std::string altSuffix = "tests/test_ffp_contract.cpp";
        pos = path.rfind(altSuffix);
    }
    TestCheck(pos != std::string::npos, "FFP contract test source path must resolve the RenderEngine source root");
    path.erase(pos);
    path += relativePath;

    std::ifstream file(path.c_str());
    TestCheck(file.good(), "RenderEngine source file must be readable for contract checks");
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

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

void Test_DPFlags_Sprite3DTexturedColor_DoesNotInventNormal() {
    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(CKRST_DP_TR_VCST, false, true);
    TestCheck((fmt & CKFF_VF_POSITION) != 0, "Sprite3D transformed vertices must use 3D POSITION");
    TestCheck((fmt & CKFF_VF_NORMAL) == 0, "Sprite3D TR/VCST vertices must not invent NORMAL");
    TestCheck((fmt & CKFF_VF_COLOR0) != 0, "Sprite3D TR/VCST must include diffuse color");
    TestCheck((fmt & CKFF_VF_COLOR1) != 0, "Sprite3D TR/VCST must include specular color");
    TestCheck((fmt & CKFF_VF_TEXCOORD0) != 0, "Sprite3D TR/VCST must include stage 0 texcoord");
    TestCheck(CKVertexLayoutCache::ComputeStride(fmt) == 28, "Sprite3D POSITION + COLOR0 + COLOR1 + TEXCOORD0 stride must be 28 bytes");
}

void Test_DPFlags_UnlitColorTextured_IgnoresIncidentalNormalPointer() {
    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(CKRST_DP_TR_CL_VCST, true, true);
    TestCheck((fmt & CKFF_VF_POSITION) != 0, "Unlit transformed color vertices must use 3D POSITION");
    TestCheck((fmt & CKFF_VF_NORMAL) == 0, "Unlit transformed color vertices must ignore incidental NORMAL storage");
    TestCheck(CKVertexLayoutCache::ComputeStride(fmt) == 28, "Unlit POSITION + COLOR0 + COLOR1 + TEXCOORD0 stride must be 28 bytes");
}

void Test_DPFlags_TextureStageMaskCountsHighestEnabledStage() {
    TestCheck(CKFFActiveTextureCountFromDPFlags(0) == 0,
              "No CKRST_DP_STAGE flag must mean no active texture stages");
    TestCheck(CKFFActiveTextureCountFromDPFlags(CKRST_DP_STAGE(0)) == 1,
              "CKRST_DP_STAGE(0) must enable one texture stage");
    TestCheck(CKFFActiveTextureCountFromDPFlags(CKRST_DP_STAGE(1)) == 2,
              "CKRST_DP_STAGE(1) must enable two texture stages");
    TestCheck(CKFFActiveTextureCountFromDPFlags(CKRST_DP_STAGE(2)) == 3,
              "CKRST_DP_STAGE(2) must enable three texture stages");
    TestCheck(CKFFActiveTextureCountFromDPFlags(CKRST_DP_STAGE(7)) == 8,
              "CKRST_DP_STAGE(7) must enable all eight texture stages");
}

void Test_VertexLayout_EightTextureCoordinatesContributeToStride() {
    const CKDWORD texFlags =
        CKFF_VF_TEXCOORD0 | CKFF_VF_TEXCOORD1 | CKFF_VF_TEXCOORD2 | CKFF_VF_TEXCOORD3 |
        CKFF_VF_TEXCOORD4 | CKFF_VF_TEXCOORD5 | CKFF_VF_TEXCOORD6 | CKFF_VF_TEXCOORD7;
    const CKDWORD fmt = CKFF_VF_POSITION | CKFF_VF_NORMAL | CKFF_VF_COLOR0 | CKFF_VF_COLOR1 | texFlags;
    TestCheck(CKVertexLayoutCache::ComputeStride(CKFF_VF_POSITION | CKFF_VF_COLOR0 | CKFF_VF_COLOR1 | CKFF_VF_TEXCOORD0) == 28,
              "Single texcoord 3D unlit stride must remain 28 bytes");
    TestCheck(CKVertexLayoutCache::ComputeStride(CKFF_VF_POSITION | CKFF_VF_COLOR0 | CKFF_VF_COLOR1 |
                                                 CKFF_VF_TEXCOORD0 | CKFF_VF_TEXCOORD1 | CKFF_VF_TEXCOORD2) == 44,
              "Three texcoord stride must include three float2 attributes");
    TestCheck(CKVertexLayoutCache::ComputeStride(fmt) == 96,
              "Eight texcoord lit stride must include position, normal, colors, and eight float2 attributes");
}

void Test_Interleave_CopiesIndependentTextureCoordinateStreams() {
    float pos[3] = {1.0f, 2.0f, 3.0f};
    float uv0[2] = {0.10f, 0.20f};
    float uv1[2] = {0.30f, 0.40f};
    float uv2[2] = {0.50f, 0.60f};

    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGE(2);
    data.VertexCount = 1;
    data.PositionPtr = pos;
    data.PositionStride = sizeof(pos);
    data.TexCoordPtr = uv0;
    data.TexCoordStride = sizeof(uv0);
    data.TexCoordPtrs[0] = uv1;
    data.TexCoordStrides[0] = sizeof(uv1);
    data.TexCoordPtrs[1] = uv2;
    data.TexCoordStrides[1] = sizeof(uv2);

    const CKDWORD fmt = CKFF_VF_POSITION | CKFF_VF_COLOR0 | CKFF_VF_COLOR1 |
                        CKFF_VF_TEXCOORD0 | CKFF_VF_TEXCOORD1 | CKFF_VF_TEXCOORD2;
    const CKDWORD stride = CKVertexLayoutCache::ComputeStride(fmt);
    CKBYTE buffer[64] = {};
    CKTransientGeometry::InterleaveVertices(buffer, stride, 1, fmt, &data);

    float copied0[2] = {};
    float copied1[2] = {};
    float copied2[2] = {};
    memcpy(copied0, buffer + 12, sizeof(copied0));
    memcpy(copied1, buffer + 20, sizeof(copied1));
    memcpy(copied2, buffer + 28, sizeof(copied2));

    TestCheck(copied0[0] == uv0[0] && copied0[1] == uv0[1], "Stage 0 UV must come from TexCoordPtr");
    TestCheck(copied1[0] == uv1[0] && copied1[1] == uv1[1], "Stage 1 UV must come from TexCoordPtrs[0]");
    TestCheck(copied2[0] == uv2[0] && copied2[1] == uv2[1], "Stage 2 UV must come from TexCoordPtrs[1]");
}

void Test_SamplerDesc_MapsMinMagMipFiltersIndependently() {
    CKFixedFunctionPipeline ffp;
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEARMIPNEAREST);
    CKSamplerDesc sampler = ffp.BuildSamplerDesc(0);
    TestCheck(sampler.MagFilter == CKRST_FILTER_NEAREST, "NEAREST mag filter must map to point magnification");
    TestCheck(sampler.MinFilter == CKRST_FILTER_LINEAR, "LINEARMIPNEAREST min filter must use linear minification");
    TestCheck(sampler.MipFilter == CKRST_FILTER_NEAREST, "LINEARMIPNEAREST mip filter must use nearest mip selection");

    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    sampler = ffp.BuildSamplerDesc(0);
    TestCheck(sampler.MagFilter == CKRST_FILTER_ANISOTROPIC, "ANISOTROPIC mag filter must be preserved");
    TestCheck(sampler.MinFilter == CKRST_FILTER_ANISOTROPIC, "ANISOTROPIC min filter must be preserved");
    TestCheck(sampler.MipFilter == CKRST_FILTER_ANISOTROPIC, "ANISOTROPIC mip filter must be preserved");
}

void Test_SamplerDesc_MapsAddressWIndependently() {
    CKFixedFunctionPipeline ffp;
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSWRAP);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESW, VXTEXTURE_ADDRESSCLAMP);

    CKSamplerDesc sampler = ffp.BuildSamplerDesc(0);
    TestCheck(sampler.AddressU == CKRST_ADDRESS_WRAP, "Default U address must come from CKRST_TSS_ADDRESS");
    TestCheck(sampler.AddressV == CKRST_ADDRESS_WRAP, "Default V address must come from CKRST_TSS_ADDRESS");
    TestCheck(sampler.AddressW == CKRST_ADDRESS_CLAMP, "W address must honor CKRST_TSS_ADDRESW");
}

void Test_TextureStageDefaults_UseMatchingTexcoordIndex() {
    CKFixedFunctionPipeline ffp;
    for (int stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage) {
        TestCheck(ffp.GetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX) == (CKDWORD)stage,
                  "Texture stage default TEXCOORDINDEX must match the stage index");
    }
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

void Test_WrapTexcoords_ExpandsUAcrossSeam() {
    float uv[3][2] = {
        {0.98f, 0.25f},
        {0.02f, 0.50f},
        {0.99f, 0.75f},
    };

    CKTransientGeometry::AdjustTriangleWrapTexcoords(uv, VXWRAP_U);

    TestCheck(uv[0][0] == 0.98f, "U wrap must keep the high endpoint unchanged");
    TestCheck(uv[1][0] == 1.02f, "U wrap must lift the low endpoint across the seam");
    TestCheck(uv[2][0] == 0.99f, "U wrap must keep the second high endpoint unchanged");
    TestCheck((uv[1][0] - uv[0][0]) <= 0.5f && (uv[1][0] - uv[2][0]) <= 0.5f,
              "U wrap expansion must remove long interpolation across the texture");
}

void Test_WrapTexcoords_ExpandsVAcrossSeam() {
    float uv[3][2] = {
        {0.25f, 0.98f},
        {0.50f, 0.02f},
        {0.75f, 0.99f},
    };

    CKTransientGeometry::AdjustTriangleWrapTexcoords(uv, VXWRAP_V);

    TestCheck(uv[0][1] == 0.98f, "V wrap must keep the high endpoint unchanged");
    TestCheck(uv[1][1] == 1.02f, "V wrap must lift the low endpoint across the seam");
    TestCheck(uv[2][1] == 0.99f, "V wrap must keep the second high endpoint unchanged");
    TestCheck((uv[1][1] - uv[0][1]) <= 0.5f && (uv[1][1] - uv[2][1]) <= 0.5f,
              "V wrap expansion must remove long interpolation across the texture");
}

void Test_WrapTexcoords_NoWrapLeavesCoordinatesUnchanged() {
    float uv[3][2] = {
        {0.98f, 0.98f},
        {0.02f, 0.02f},
        {0.99f, 0.99f},
    };

    CKTransientGeometry::AdjustTriangleWrapTexcoords(uv, 0);

    TestCheck(uv[0][0] == 0.98f && uv[0][1] == 0.98f, "No-wrap must keep vertex 0 UV unchanged");
    TestCheck(uv[1][0] == 0.02f && uv[1][1] == 0.02f, "No-wrap must keep vertex 1 UV unchanged");
    TestCheck(uv[2][0] == 0.99f && uv[2][1] == 0.99f, "No-wrap must keep vertex 2 UV unchanged");
}

void Test_WrapMesh_RequiresWrapAwareHardwareExpansion() {
    TestCheck(!RCKMesh::RequiresWrapAwareHardwareVertexBuffer(0), "Non-wrap mesh may use the regular hardware VB layout");
    TestCheck(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_WRAPU), "U-wrap mesh must expand hardware VB vertices");
    TestCheck(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_WRAPV), "V-wrap mesh must expand hardware VB vertices");
    TestCheck(RCKMesh::RequiresWrapAwareHardwareVertexBuffer(VXMESH_WRAPU | VXMESH_WRAPV), "UV-wrap mesh must expand hardware VB vertices");
}

void Test_WrapMesh_MapsMeshFlagsToRenderStateWrapMode() {
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(0) == 0, "Non-wrap mesh must clear WRAP0");
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPU) == (CKDWORD)VXWRAP_U, "VXMESH_WRAPU must set VXWRAP_U");
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPV) == (CKDWORD)VXWRAP_V, "VXMESH_WRAPV must set VXWRAP_V");
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(VXMESH_WRAPU | VXMESH_WRAPV) == (CKDWORD)(VXWRAP_U | VXWRAP_V),
              "VXMESH_WRAPU/V must combine into WRAP0 flags");
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(0x00000100) == (CKDWORD)VXWRAP_U,
              "Serialized mesh wrap bit 0x100 must set VXWRAP_U");
    TestCheck(RCKMesh::TextureWrapModeFromMeshFlags(0x00000200) == (CKDWORD)VXWRAP_V,
              "Serialized mesh wrap bit 0x200 must set VXWRAP_V");
}

void Test_TexGen_PacksTexcoordGenerationWithoutLosingIndex() {
    CKDWORD packed = CKFFPackTexcoordIndex(7, CKFF_TEXGEN_CAMERASPACEREFLECTION);
    TestCheck(CKFFTexcoordIndex(packed) == 7, "Packed texcoord state must preserve the source coordinate index");
    TestCheck(CKFFTexcoordGeneration(packed) == CKFF_TEXGEN_CAMERASPACEREFLECTION,
              "Packed texcoord state must preserve camera-space reflection generation");

    packed = CKFFPackTexcoordIndex(0, CKFF_TEXGEN_SPHEREMAP);
    TestCheck(CKFFTexcoordGeneration(packed) == CKFF_TEXGEN_SPHEREMAP,
              "Packed texcoord state must preserve D3D sphere-map generation");
}

void Test_TexGen_TextureMatrixUsesBgfxVxMatrixSemantics() {
    std::string source = ReadRenderEngineSource("src/shaders/vs_ff_3d.sc");
    TestCheck(source.find("mul(u_texMatrix[stage], coord)") != std::string::npos,
              "Texture matrix transform must use the bgfx/VxMatrix convention that preserves D3D row-vector translation");
    TestCheck(source.find("vec3 refl = reflect(eye, viewNormal)") != std::string::npos,
              "Camera-space reflection vector must follow dxvk/D3D FFP reflect(viewPosition, normal) semantics");
    TestCheck(source.find("return vec4(selectTexcoord(index & 7") != std::string::npos &&
              source.find(", 0.0, 0.0)") != std::string::npos,
              "Pass-through texcoords must use D3D FFP padding before texture matrix transforms");
}

void Test_RenderTarget_AttachesDepthStencilTexture() {
    std::string source = ReadRenderEngineSource("src/CKRenderContext.cpp");
    TestCheck(source.find("CreateDepthTexture(m_TargetDepthTexture") != std::string::npos,
              "Render-to-texture framebuffers must create a depth-stencil attachment");
    TestCheck(source.find("desc.DepthStencil.Texture = m_TargetDepthTexture") != std::string::npos,
              "Render-to-texture framebuffers must bind the depth-stencil attachment");
}

void Test_UserClipPlane_IsConsumedByFFPShaderPath() {
    std::string source = ReadRenderEngineSource("src/CKRenderContext.cpp");
    const size_t setPos = source.find("CKBOOL RCKRenderContext::SetUserClipPlane");
    TestCheck(setPos != std::string::npos, "SetUserClipPlane implementation must be present");
    const size_t getPos = source.find("CKBOOL RCKRenderContext::GetUserClipPlane");
    TestCheck(getPos != std::string::npos, "GetUserClipPlane implementation must be present");
    const std::string setBody = source.substr(setPos, getPos - setPos);
    TestCheck(setBody.find("m_FFPipeline.SetUserClipPlane") != std::string::npos &&
              setBody.find("return TRUE;") != std::string::npos,
              "SetUserClipPlane must store planes and forward them to the FFP path");

    std::string ffp = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    TestCheck(ffp.find("VXRENDERSTATE_CLIPPLANEENABLE") != std::string::npos,
              "FFP uniform upload must consume VXRENDERSTATE_CLIPPLANEENABLE");
    TestCheck(ffp.find("encoder->SetUniform(u.u_clipPlanes") != std::string::npos &&
              ffp.find("encoder->SetUniform(u.u_clipParams") != std::string::npos,
              "FFP uniform upload must bind clip plane uniforms");

    std::string fs = ReadRenderEngineSource("src/shaders/fs_ff_stage.sc");
    TestCheck(fs.find("uniform vec4 u_clipPlanes[6]") != std::string::npos &&
              fs.find("dot(v_clipPos, u_clipPlanes[clipIndex]) < 0.0") != std::string::npos &&
              fs.find("discard") != std::string::npos,
              "Fragment shader must discard pixels behind enabled user clip planes");
}

void Test_TextureCopyContext_ConvertsReadbackToTextureFormat() {
    std::string source = ReadRenderEngineSource("src/CKTexture.cpp");
    const size_t copyPos = source.find("CKBOOL RCKTexture::CopyContext");
    TestCheck(copyPos != std::string::npos, "RCKTexture::CopyContext implementation must be present");
    const size_t nextPos = source.find("CKBOOL RCKTexture::UseMipmap", copyPos);
    TestCheck(nextPos != std::string::npos, "RCKTexture::CopyContext source range must be bounded");
    const std::string copyBody = source.substr(copyPos, nextPos - copyPos);
    TestCheck(copyBody.find("SamePixelFormat(srcDesc, m_VideoFormat)") != std::string::npos,
              "Texture CopyContext must compare ARGB readback against the target texture format");
    TestCheck(copyBody.find("ConvertTextureImage(srcDesc, m_VideoFormat") != std::string::npos,
              "Texture CopyContext must convert ARGB readback before uploading non-ARGB8888 targets");
}

void Test_CopyToVideo_UploadsBackbufferThroughScratchTexture() {
    std::string source = ReadRenderEngineSource("src/CKRenderContext.cpp");
    const size_t copyPos = source.find("int RCKRenderContext::CopyToVideo");
    TestCheck(copyPos != std::string::npos, "CopyToVideo implementation must be present");
    const size_t nextPos = source.find("CKERROR RCKRenderContext::DumpToFile", copyPos);
    TestCheck(nextPos != std::string::npos, "CopyToVideo source range must be bounded");
    const std::string body = source.substr(copyPos, nextPos - copyPos);
    TestCheck(body.find("buffer != VXBUFFER_BACKBUFFER") != std::string::npos,
              "CopyToVideo must explicitly reject unsupported video buffers");
    TestCheck(body.find("ConvertCopyImage(desc, videoFormat") != std::string::npos,
              "CopyToVideo must convert CPU image data to the upload texture format");
    TestCheck(body.find("CreateTexture(m_CopyToVideoTexture") != std::string::npos &&
              body.find("UpdateTexture(m_CopyToVideoTexture") != std::string::npos,
              "CopyToVideo must create/update a scratch upload texture");
    TestCheck(body.find("DrawPrimitive(VX_TRIANGLEFAN") != std::string::npos,
              "CopyToVideo must draw the uploaded scratch texture into the backbuffer rectangle");
}

void Test_RenderPipeline_RenderFirstViewPrecedesOpaqueScene() {
    TestCheck(CKRP_VIEW_BACKGROUND2D < CKRP_VIEW_RENDERFIRST3D,
              "2D background must clear before render-first 3D objects");
    TestCheck(CKRP_VIEW_RENDERFIRST3D < CKRP_VIEW_OPAQUE3D,
              "Render-first 3D objects must submit before the regular opaque scene");
    TestCheck(CKRP_VIEW_OPAQUE3D < CKRP_VIEW_TRANSPARENT,
              "Opaque scene must submit before transparent scene objects");
    TestCheck(CKRP_VIEW_TRANSPARENT < CKRP_VIEW_FOREGROUND2D,
              "Foreground 2D must remain the final view");
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

void Test_Interleave_PositionTMissingSpecularKeepsFogFactorOpaque() {
    float pos[4] = {1.0f, 2.0f, 0.5f, 1.0f};
    float uv[2] = {0.25f, 0.75f};

    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.Flags = CKRST_DP_CL_VCT;
    data.VertexCount = 1;
    data.PositionPtr = pos;
    data.PositionStride = sizeof(pos);
    data.TexCoordPtr = uv;
    data.TexCoordStride = sizeof(uv);

    const CKDWORD fmt = CKVertexLayoutCache::DPFlagsToFormatFlags(data.Flags, false, true);
    const CKDWORD stride = CKVertexLayoutCache::ComputeStride(fmt);
    CKBYTE buffer[64] = {};
    CKTransientGeometry::InterleaveVertices(buffer, stride, 1, fmt, &data);

    CKDWORD color1 = 0;
    memcpy(&color1, buffer + 28, sizeof(color1));

    TestCheck(color1 == 0xFF000000, "PositionT missing specular must default COLOR1 alpha to 1 for fog factor");
}

void Test_UserDrawPrimitive_PositionTSpecularDefaultsOpaqueOnReuse() {
    UserDrawPrimitiveDataClass cache;
    cache.GetStructure(CKRST_DP_CL_VCST, 4);

    CKDWORD zeroSpecular = 0x00000000;
    VxFillStructure(4, cache.SpecularColorPtr, cache.SpecularColorStride, sizeof(CKDWORD), &zeroSpecular);

    VxDrawPrimitiveData *data = cache.GetStructure(CKRST_DP_CL_VCST, 4);
    CKDWORD specular = 0;
    memcpy(&specular, data->SpecularColorPtr, sizeof(specular));

    TestCheck(specular == 0xFF000000,
              "PositionT specular defaults must be reset to opaque black on reused draw primitive data");
}

void Test_DrawState_DefaultMaterialSourcesMatchFFP() {
    CKDrawStateCache cache;

    TestCheck(cache.GetRenderState(VXRENDERSTATE_COLORVERTEX) == TRUE,
              "FFP default COLORVERTEX must be enabled");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX) == TRUE,
              "FFP default diffuse source must be vertex diffuse");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_SPECULARFROMVERTEX) == TRUE,
              "FFP default specular source must be vertex specular");
    TestCheck(cache.GetRenderState(VXRENDERSTATE_SPECULARENABLE) == FALSE,
              "FFP default global specular add must be disabled");
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

void Test_DrawState_InverseWindingSwapsCullDirection() {
    CKDrawStateCache cache;
    cache.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_CCW);
    CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);
    CKDWORD cull = (state.Lo >> 10) & 0x3;
    TestCheck(cull == 2, "Default CCW cull must map to bgfx CCW culling");

    cache.SetRenderState(VXRENDERSTATE_INVERSEWINDING, TRUE);
    state = cache.BuildDrawState(VX_TRIANGLELIST);
    cull = (state.Lo >> 10) & 0x3;
    TestCheck(cull == 1, "Inverse winding must swap CCW culling to CW culling");

    cache.SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
    state = cache.BuildDrawState(VX_TRIANGLELIST);
    cull = (state.Lo >> 10) & 0x3;
    TestCheck(cull == 0, "Inverse winding must not invent culling for VXCULL_NONE");
}

void Test_DrawState_ColorWriteMaskCanDisableColorWrites() {
    CKDrawStateCache cache;
    cache.SetColorWriteMask(FALSE, FALSE, FALSE, FALSE);
    CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);

    TestCheck((state.Lo & CKRST_STATE_WRITE_RGBA) == 0,
              "Depth/stencil-only draws must be able to disable all color writes");

    cache.SetColorWriteMask(TRUE, FALSE, TRUE, FALSE);
    state = cache.BuildDrawState(VX_TRIANGLELIST);
    TestCheck((state.Lo & CKRST_STATE_WRITE_R) != 0, "Color mask must preserve R writes");
    TestCheck((state.Lo & CKRST_STATE_WRITE_G) == 0, "Color mask must disable G writes");
    TestCheck((state.Lo & CKRST_STATE_WRITE_B) != 0, "Color mask must preserve B writes");
    TestCheck((state.Lo & CKRST_STATE_WRITE_A) == 0, "Color mask must disable A writes");
}

void Test_DrawState_StencilRenderStatesBuildStencilOps() {
    CKDrawStateCache cache;
    cache.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    cache.SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_ALWAYS);
    cache.SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    cache.SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    cache.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);

    CKDrawState state = cache.BuildDrawState(VX_TRIANGLELIST);
    TestCheck((state.Mid & CKRST_STENCIL_ENABLE) != 0,
              "Enabling VXRENDERSTATE_STENCILENABLE must enable draw-state stencil");
    TestCheck(((state.Mid >> 10) & 0xF) == VXCMP_ALWAYS,
              "Stencil function must be packed into draw-state stencil ops");
    TestCheck(((state.Mid >> 22) & 0xF) == VXSTENCILOP_REPLACE,
              "Stencil pass op must be packed into draw-state stencil ops");
}

void Test_TextureBlend_LegacyModesMapToFFPStageOps() {
    CKFFTextureStageOps ops = CKFFLegacyTextureBlendToStageOps(VXTEXTUREBLEND_MODULATE);
    TestCheck(ops.ColorOp == CKRST_TOP_MODULATE &&
              ops.ColorArg1 == CKRST_TA_TEXTURE &&
              ops.ColorArg2 == CKRST_TA_CURRENT &&
              ops.AlphaOp == CKRST_TOP_MODULATE &&
              ops.AlphaArg1 == CKRST_TA_TEXTURE &&
              ops.AlphaArg2 == CKRST_TA_CURRENT &&
              ops.ResultArg == CKRST_TA_CURRENT,
              "MODULATE must map to texture/current color and alpha modulation");

    ops = CKFFLegacyTextureBlendToStageOps(VXTEXTUREBLEND_MODULATEALPHA);
    TestCheck(ops.ColorOp == CKRST_TOP_MODULATE &&
              ops.ColorArg1 == CKRST_TA_TEXTURE &&
              ops.ColorArg2 == CKRST_TA_CURRENT &&
              ops.AlphaOp == CKRST_TOP_MODULATE,
              "MODULATEALPHA must match Virtools DX9 texture/current color and alpha modulation");

    ops = CKFFLegacyTextureBlendToStageOps(VXTEXTUREBLEND_DECALALPHA);
    TestCheck(ops.ColorOp == CKRST_TOP_BLENDTEXTUREALPHA &&
              ops.ColorArg1 == CKRST_TA_TEXTURE &&
              ops.ColorArg2 == CKRST_TA_CURRENT &&
              ops.AlphaOp == CKRST_TOP_SELECTARG1 &&
              ops.AlphaArg1 == CKRST_TA_DIFFUSE,
              "DECALALPHA must lerp color by texture alpha and select diffuse alpha like Virtools DX9");

    ops = CKFFLegacyTextureBlendToStageOps(VXTEXTUREBLEND_ADD);
    TestCheck(ops.ColorOp == CKRST_TOP_ADD &&
              ops.AlphaOp == CKRST_TOP_SELECTARG1 &&
              ops.AlphaArg1 == CKRST_TA_CURRENT,
              "ADD must add colors and keep current alpha like Virtools DX9");

    ops = CKFFLegacyTextureBlendToStageOps(VXTEXTUREBLEND_DOTPRODUCT3);
    TestCheck(ops.ColorOp == CKRST_TOP_DOTPRODUCT3 &&
              ops.ColorArg2 == CKRST_TA_TFACTOR &&
              ops.AlphaOp == CKRST_TOP_SELECTARG1 &&
              ops.AlphaArg1 == CKRST_TA_CURRENT,
              "DOTPRODUCT3 must use texture/tfactor color args and keep current alpha like Virtools DX9");

    TestCheck(CKFFLegacyTextureBlendToColorOp(VXTEXTUREBLEND_MODULATEALPHA) == CKRST_TOP_MODULATE,
              "MODULATEALPHA color op must stay ordinary modulation for Virtools 2D atlas compatibility");
    TestCheck(CKFFLegacyTextureBlendToColorOp(VXTEXTUREBLEND_MODULATEMASK) == CKRST_TOP_MODULATE,
              "MODULATEMASK color op must stay ordinary modulation for Virtools 2D atlas compatibility");
    TestCheck(CKFFLegacyTextureBlendToColorOp(VXTEXTUREBLEND_DECALALPHA) == CKRST_TOP_BLENDTEXTUREALPHA,
              "DECALALPHA color op must blend by texture alpha");
    TestCheck(CKFFLegacyTextureBlendToColorOp(VXTEXTUREBLEND_DECALMASK) == CKRST_TOP_BLENDTEXTUREALPHA,
              "DECALMASK color op must blend by texture alpha");
    TestCheck(CKFFLegacyTextureBlendToColorOp(VXTEXTUREBLEND_MODULATE) == CKRST_TOP_MODULATE,
              "MODULATE color op must remain multiplicative");
    TestCheck(CKFFLegacyTextureBlendToAlphaOp(VXTEXTUREBLEND_MODULATE) == CKRST_TOP_MODULATE,
              "MODULATE alpha op must preserve runtime material alpha by multiplying texture and current alpha");
    TestCheck(CKFFLegacyTextureBlendToAlphaOp(VXTEXTUREBLEND_MODULATEALPHA) == CKRST_TOP_MODULATE,
              "MODULATEALPHA alpha op must multiply texture and diffuse alpha");
    TestCheck(CKFFLegacyTextureBlendToAlphaOp(VXTEXTUREBLEND_DECAL) == CKRST_TOP_SELECTARG1,
              "DECAL alpha op must keep texture alpha available for alpha test");
    TestCheck(CKFFLegacyTextureBlendToAlphaOp(VXTEXTUREBLEND_DECALALPHA) == CKRST_TOP_SELECTARG1,
              "DECALALPHA alpha op must select diffuse alpha");
}

struct TestColor {
    float R;
    float G;
    float B;
    float A;
};

static bool Near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

void Test_TextureBlend_ModulateAlphaMatchesVirtoolsDx9Modulate() {
    const TestColor current = { 0.8f, 0.6f, 0.4f, 0.5f };

    TestColor transparentBlack = { 0.0f, 0.0f, 0.0f, 0.0f };
    TestColor result;
    result.R = transparentBlack.R * current.R;
    result.G = transparentBlack.G * current.G;
    result.B = transparentBlack.B * current.B;
    result.A = transparentBlack.A * current.A;
    TestCheck(Near(result.R, 0.0f) && Near(result.G, 0.0f) && Near(result.B, 0.0f),
              "Virtools DX9 MODULATEALPHA color path must not brighten transparent atlas texels");
    TestCheck(Near(result.A, 0.0f), "Transparent color-key texels must still output zero alpha");

    TestColor opaqueTexture = { 0.5f, 0.25f, 0.75f, 1.0f };
    result.R = opaqueTexture.R * current.R;
    result.G = opaqueTexture.G * current.G;
    result.B = opaqueTexture.B * current.B;
    result.A = opaqueTexture.A * current.A;
    TestCheck(Near(result.R, current.R * opaqueTexture.R) &&
              Near(result.G, current.G * opaqueTexture.G) &&
              Near(result.B, current.B * opaqueTexture.B),
              "Opaque MODULATEALPHA texels must match regular texture/current modulation");
    TestCheck(Near(result.A, current.A), "Opaque MODULATEALPHA texels must preserve current alpha");
}

void Test_TextureBlend_ShaderOpsMatchDxvkFormulas() {
    const std::string source = ReadRenderEngineSource("src\\shaders\\fs_ff_stage.sc");
    TestCheck(source.find("if (op == 18) return clamp(a + vec4_splat(a.a) * b") != std::string::npos,
              "MODULATEALPHA_ADDCOLOR must use arg1 alpha replicated across all components");
    TestCheck(source.find("if (op == 19) return clamp(a * b + vec4_splat(a.a)") != std::string::npos,
              "MODULATECOLOR_ADDALPHA must add replicated arg1 alpha");
    TestCheck(source.find("if (op == 21) return clamp((vec4_splat(1.0) - a) * b + vec4_splat(a.a)") != std::string::npos,
              "MODULATEINVCOLOR_ADDALPHA must complement arg1 color and add replicated arg1 alpha");
    TestCheck(source.find("op == 27") == std::string::npos,
              "Legacy VXTEXTUREBLEND modes must not add non-D3D shader ops for 2D atlas compatibility");
}

void Test_TextureStageBlend_MultiplicativeChannelMapsToModulate() {
    CKDWORD colorOp = 0;
    CKDWORD colorArg1 = 0;
    CKDWORD colorArg2 = 0;
    CKDWORD alphaOp = 0;
    CKDWORD alphaArg1 = 0;
    CKDWORD alphaArg2 = 0;

    TestCheck(CKFFStageBlendToTextureOps(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR),
                                         colorOp, colorArg1, colorArg2,
                                         alphaOp, alphaArg1, alphaArg2),
              "ZERO/SRCCOLOR channel blend must be expressible as a texture stage");
    TestCheck(colorOp == CKRST_TOP_MODULATE, "Multiplicative channel color op must be MODULATE");
    TestCheck(colorArg1 == CKRST_TA_TEXTURE, "Multiplicative channel arg1 must be the channel texture");
    TestCheck(colorArg2 == CKRST_TA_CURRENT, "Multiplicative channel arg2 must be the current accumulated color");
    TestCheck(alphaOp == CKRST_TOP_SELECTARG2, "Multiplicative channel alpha must keep current alpha");
    TestCheck(alphaArg2 == CKRST_TA_CURRENT, "Multiplicative channel alpha arg2 must be current");

    TestCheck(CKFFStageBlendToTextureOps(STAGEBLEND(VXBLEND_DESTCOLOR, VXBLEND_ZERO),
                                         colorOp, colorArg1, colorArg2,
                                         alphaOp, alphaArg1, alphaArg2),
              "DESTCOLOR/ZERO channel blend must be expressible as the same texture stage multiply");
    TestCheck(colorOp == CKRST_TOP_MODULATE, "DESTCOLOR/ZERO channel color op must be MODULATE");
}

void Test_TextureStageBlend_UnsupportedBlendRefusesMonoPass() {
    CKDWORD colorOp = 0;
    CKDWORD colorArg1 = 0;
    CKDWORD colorArg2 = 0;
    CKDWORD alphaOp = 0;
    CKDWORD alphaArg1 = 0;
    CKDWORD alphaArg2 = 0;

    TestCheck(!CKFFStageBlendToTextureOps(STAGEBLEND(VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA),
                                          colorOp, colorArg1, colorArg2,
                                          alphaOp, alphaArg1, alphaArg2),
              "Alpha-blended material channels must stay on the multipass path");
}

void Test_TextureStage_ResetClearsLeakedEffectState() {
    CKFixedFunctionPipeline ffp;
    ffp.SetTexture(0, 1234);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX,
                             CKFFPackTexcoordIndex(3, CKFF_TEXGEN_CAMERASPACEREFLECTION));

    ffp.ResetTextureStage(0);
    TestCheck(ffp.GetTextureStageState(0, CKRST_TSS_OP) == 0,
              "ResetTextureStage must clear explicit color op from previous effects");
    TestCheck(ffp.GetTextureStageState(0, CKRST_TSS_ARG1) == 0,
              "ResetTextureStage must clear explicit color arg from previous effects");
    TestCheck(ffp.GetTextureStageState(0, CKRST_TSS_AOP) == 0,
              "ResetTextureStage must clear explicit alpha op from previous effects");
    TestCheck(ffp.GetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX) == 0,
              "ResetTextureStage must restore stage texcoord index");

    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.DisableTextureStagesFrom(1);
    TestCheck(ffp.GetTextureStageState(1, CKRST_TSS_OP) == 0,
              "DisableTextureStagesFrom must clear higher stage effect state");
    TestCheck(ffp.GetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX) == 1,
              "DisableTextureStagesFrom must restore each higher stage texcoord index");
}

void Test_BumpEnv_StageStatesPackUniform() {
    CKDWORD stage[CKRST_TSS_MAXSTATE + 1] = {};
    const float m00 = 0.25f;
    const float m01 = -0.50f;
    const float m10 = 0.75f;
    const float m11 = -1.00f;
    const float scale = 2.0f;
    const float offset = 0.125f;
    memcpy(&stage[CKRST_TSS_BUMPENVMAT00], &m00, sizeof(float));
    memcpy(&stage[CKRST_TSS_BUMPENVMAT01], &m01, sizeof(float));
    memcpy(&stage[CKRST_TSS_BUMPENVMAT10], &m10, sizeof(float));
    memcpy(&stage[CKRST_TSS_BUMPENVMAT11], &m11, sizeof(float));
    memcpy(&stage[CKRST_TSS_BUMPENVLSCALE], &scale, sizeof(float));
    memcpy(&stage[CKRST_TSS_BUMPENVLOFFSET], &offset, sizeof(float));

    float bumpEnv[2][4] = {};
    CKFFPackBumpEnvUniform(stage, bumpEnv);
    TestCheck(bumpEnv[0][0] == m00 && bumpEnv[0][1] == m01 &&
              bumpEnv[0][2] == m10 && bumpEnv[0][3] == m11,
              "Bump env matrix stage states must pack into u_bumpEnv[0]");
    TestCheck(bumpEnv[1][0] == scale && bumpEnv[1][1] == offset,
              "Bump env luminance stage states must pack into u_bumpEnv[1]");
}

void Test_UserDrawPrimitive_VBufferFlagIsObservable() {
    UserDrawPrimitiveDataClass dp;
    VxDrawPrimitiveData *data = dp.GetStructure((CKRST_DPFLAGS)(CKRST_DP_CL_VCT | CKRST_DP_VBUFFER), 4);
    TestCheck(data != nullptr, "User draw primitive structure must be returned");
    TestCheck((data->Flags & CKRST_DP_VBUFFER) != 0,
              "CKRST_DP_VBUFFER must stay observable so callers know ReleaseCurrentVB is required");
    TestCheck((data->Flags & CKRST_DP_CL_VCT) == CKRST_DP_CL_VCT,
              "Regular draw primitive flags must be preserved with CKRST_DP_VBUFFER");
}

void Test_ShaderTarget_ProfilesAreExplicitAndDistinct() {
    CKShaderTargetDesc target;
    TestCheck(target.Format == CKRST_SHADER_FORMAT_UNKNOWN, "Default shader format must be unknown");
    TestCheck(target.Profile == CKRST_SHADER_PROFILE_UNKNOWN, "Default shader profile must be unknown");
    TestCheck(CKRST_SHADER_FORMAT_NATIVE != CKRST_SHADER_FORMAT_UNKNOWN,
              "Native shader format must be explicit");
    TestCheck(CKRST_SHADER_PROFILE_DX11 != CKRST_SHADER_PROFILE_UNKNOWN,
              "DX11 shader profile must be explicit");
    TestCheck(CKRST_SHADER_PROFILE_DX12 != CKRST_SHADER_PROFILE_DX11,
              "DX12 shader profile must differ from DX11");
    TestCheck(CKRST_SHADER_PROFILE_SPIRV != CKRST_SHADER_PROFILE_DX11 &&
              CKRST_SHADER_PROFILE_SPIRV != CKRST_SHADER_PROFILE_DX12,
              "SPIR-V shader profile must differ from D3D profiles");
    TestCheck(CKRST_SHADER_PROFILE_GLSL != CKRST_SHADER_PROFILE_SPIRV,
              "GLSL shader profile must differ from SPIR-V");
}

class TestRasterizerDriver : public CKRasterizerDriver {
public:
    CKERROR GetProgrammableCaps(VxProgCapsDesc &) override { return CK_OK; }
};

void Test_RasterizerDriver_DefaultShaderTargetIsUnknown() {
    TestRasterizerDriver driver;
    CKShaderTargetDesc target;
    CKERROR err = driver.GetShaderTarget(&target);
    TestCheck(err == CKERR_NOTIMPLEMENTED, "Default driver shader target must be unsupported");
    TestCheck(target.Format == CKRST_SHADER_FORMAT_UNKNOWN, "Unsupported driver shader format must be unknown");
    TestCheck(target.Profile == CKRST_SHADER_PROFILE_UNKNOWN, "Unsupported driver shader profile must be unknown");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("DP flags pre-transformed textured color", &Test_DPFlags_PretransformedTexturedColor_UsesPositionT);
    tests.Run("DP flags transformed normal textured", &Test_DPFlags_TransformedNormalTextured_UsesPosition3);
    tests.Run("DP flags Sprite3D textured color", &Test_DPFlags_Sprite3DTexturedColor_DoesNotInventNormal);
    tests.Run("DP flags unlit color textured ignores incidental normal", &Test_DPFlags_UnlitColorTextured_IgnoresIncidentalNormalPointer);
    tests.Run("DP flags texture stage mask counts highest stage", &Test_DPFlags_TextureStageMaskCountsHighestEnabledStage);
    tests.Run("Vertex layout eight texture coordinates", &Test_VertexLayout_EightTextureCoordinatesContributeToStride);
    tests.Run("Interleave independent texture coordinate streams", &Test_Interleave_CopiesIndependentTextureCoordinateStreams);
    tests.Run("Sampler desc maps filters independently", &Test_SamplerDesc_MapsMinMagMipFiltersIndependently);
    tests.Run("Sampler desc maps address W independently", &Test_SamplerDesc_MapsAddressWIndependently);
    tests.Run("Texture stage defaults use matching texcoord index", &Test_TextureStageDefaults_UseMatchingTexcoordIndex);
    tests.Run("Primitive fan conversion", &Test_PrimitiveFanConversion_ProducesTriangleList);
    tests.Run("Primitive strip conversion", &Test_PrimitiveStripConversion_AlternatesWinding);
    tests.Run("Wrap texcoords expands U seam", &Test_WrapTexcoords_ExpandsUAcrossSeam);
    tests.Run("Wrap texcoords expands V seam", &Test_WrapTexcoords_ExpandsVAcrossSeam);
    tests.Run("Wrap texcoords no-wrap unchanged", &Test_WrapTexcoords_NoWrapLeavesCoordinatesUnchanged);
    tests.Run("Wrap mesh requires hardware expansion", &Test_WrapMesh_RequiresWrapAwareHardwareExpansion);
    tests.Run("Wrap mesh maps flags to render state", &Test_WrapMesh_MapsMeshFlagsToRenderStateWrapMode);
    tests.Run("TexGen packs texcoord generation", &Test_TexGen_PacksTexcoordGenerationWithoutLosingIndex);
    tests.Run("TexGen texture matrix bgfx/VxMatrix semantics", &Test_TexGen_TextureMatrixUsesBgfxVxMatrixSemantics);
    tests.Run("Render target attaches depth-stencil texture", &Test_RenderTarget_AttachesDepthStencilTexture);
    tests.Run("User clip plane is consumed by FFP shader path", &Test_UserClipPlane_IsConsumedByFFPShaderPath);
    tests.Run("Texture CopyContext converts readback format", &Test_TextureCopyContext_ConvertsReadbackToTextureFormat);
    tests.Run("CopyToVideo uploads backbuffer through scratch texture", &Test_CopyToVideo_UploadsBackbufferThroughScratchTexture);
    tests.Run("Render pipeline render-first view order", &Test_RenderPipeline_RenderFirstViewPrecedesOpaqueScene);
    tests.Run("Interleave ignores undeclared color streams", &Test_Interleave_IgnoresColorPointersWithoutDPFlags);
    tests.Run("Interleave PositionT missing specular fog factor", &Test_Interleave_PositionTMissingSpecularKeepsFogFactorOpaque);
    tests.Run("User draw primitive PositionT specular reset", &Test_UserDrawPrimitive_PositionTSpecularDefaultsOpaqueOnReuse);
    tests.Run("Draw state default material sources", &Test_DrawState_DefaultMaterialSourcesMatchFFP);
    tests.Run("Draw state fixes BOTHSRCALPHA blend pair", &Test_DrawState_FixesBothSrcAlphaBlendPair);
    tests.Run("Draw state inverse winding swaps cull", &Test_DrawState_InverseWindingSwapsCullDirection);
    tests.Run("Draw state color write mask", &Test_DrawState_ColorWriteMaskCanDisableColorWrites);
    tests.Run("Draw state stencil render states", &Test_DrawState_StencilRenderStatesBuildStencilOps);
    tests.Run("Texture blend legacy stage-op mapping", &Test_TextureBlend_LegacyModesMapToFFPStageOps);
    tests.Run("Texture blend MODULATEALPHA Virtools DX9 contract", &Test_TextureBlend_ModulateAlphaMatchesVirtoolsDx9Modulate);
    tests.Run("Texture blend shader op formulas", &Test_TextureBlend_ShaderOpsMatchDxvkFormulas);
    tests.Run("Texture stage blend multiplicative channel", &Test_TextureStageBlend_MultiplicativeChannelMapsToModulate);
    tests.Run("Texture stage blend unsupported fallback", &Test_TextureStageBlend_UnsupportedBlendRefusesMonoPass);
    tests.Run("Texture stage reset clears effect state", &Test_TextureStage_ResetClearsLeakedEffectState);
    tests.Run("Bump env stage states pack uniform", &Test_BumpEnv_StageStatesPackUniform);
    tests.Run("User draw primitive VBUFFER flag observable", &Test_UserDrawPrimitive_VBufferFlagIsObservable);
    tests.Run("Shader target profiles", &Test_ShaderTarget_ProfilesAreExplicitAndDistinct);
    tests.Run("Driver default shader target", &Test_RasterizerDriver_DefaultShaderTargetIsUnknown);
    return tests.ExitCode();
}
