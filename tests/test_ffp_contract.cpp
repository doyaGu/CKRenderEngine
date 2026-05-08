#include "CKTransientGeometry.h"
#include "CKVertexLayoutCache.h"
#include "CKDrawStateCache.h"
#include "CKFixedFunctionPipeline.h"
#include "CKFFShaderKey.h"
#include "CKFFSpecializationInfo.h"
#include "CKFFShaderCache.h"
#include "CKDebugLogger.h"
#include "CKRasterizer.h"
#include "CKRenderPipeline.h"
#include "RCKRenderContext.h"
#include "RCKMesh.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include <fstream>
#include <iterator>
#include <cmath>
#include <string>
#include <cstdio>
#include <cstdlib>

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

void TestColorClose(const VxColor &actual, const VxColor &expected, const char *message) {
    TestCheck(fabs(actual.r - expected.r) < 0.0001f &&
              fabs(actual.g - expected.g) < 0.0001f &&
              fabs(actual.b - expected.b) < 0.0001f &&
              fabs(actual.a - expected.a) < 0.0001f,
              message);
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
              "Camera-space reflection vector must follow D3D FFP reflect(viewPosition, normal) semantics");
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
    TestCheck(ffp.find("clipCount = 0") != std::string::npos &&
              ffp.find("clipPlanes[clipCount][0]") != std::string::npos &&
              ffp.find("float clipParams[4] = {(float)clipCount") != std::string::npos,
              "FFP clip plane upload must compact the enabled render-state mask into packed uniforms");
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
    TestCheck(copyBody.find("CubeMapFace < CKRST_CUBEFACE_XPOS") != std::string::npos &&
              copyBody.find("CubeMapFace > CKRST_CUBEFACE_ZNEG") != std::string::npos,
              "Texture CopyContext must reject invalid cubemap face indices before upload");
    TestCheck(copyBody.find("!IsCubeMap() && CubeMapFace != CKRST_CUBEFACE_XPOS") != std::string::npos,
              "Texture CopyContext must reject nonzero cubemap faces for 2D texture targets");
    TestCheck(source.find("BuildCopyUploadRegion") != std::string::npos &&
              copyBody.find("BuildCopyUploadRegion(Dest, GetWidth(), GetHeight(), uploadDesc") != std::string::npos,
              "Texture CopyContext must clamp destination upload regions before UpdateTexture");
    TestCheck(source.find("skipX = -left") != std::string::npos &&
              source.find("uploadDesc.Image = image") != std::string::npos &&
              source.find("region.right = left + width") != std::string::npos,
              "Texture CopyContext region clamping must crop source pixels when the destination rect is negative or oversized");
}

void Test_SpriteCopyContext_ClampsDestinationRegion() {
    std::string source = ReadRenderEngineSource("src/CKSprite.cpp");
    const size_t copyPos = source.find("CKBOOL RCKSprite::CopyContext");
    TestCheck(copyPos != std::string::npos, "RCKSprite::CopyContext implementation must be present");
    const size_t nextPos = source.find("CKBOOL RCKSprite::GetVideoTextureDesc", copyPos);
    TestCheck(nextPos != std::string::npos, "RCKSprite::CopyContext source range must be bounded");
    const std::string copyBody = source.substr(copyPos, nextPos - copyPos);
    TestCheck(source.find("BuildSpriteCopyUploadRegion") != std::string::npos &&
              copyBody.find("BuildSpriteCopyUploadRegion(Dest, m_VideoFormatDesc.Width, m_VideoFormatDesc.Height, uploadDesc") != std::string::npos,
              "Sprite CopyContext must clamp destination upload regions before UpdateTexture");
    TestCheck(source.find("skipX = -left") != std::string::npos &&
              source.find("uploadDesc.Image = image") != std::string::npos &&
              source.find("region.right = left + width") != std::string::npos,
              "Sprite CopyContext region clamping must crop source pixels when the destination rect is negative or oversized");
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
    TestCheck(body.find("CKFFStateGuard ffpState(ffp)") != std::string::npos,
              "CopyToVideo must use the FFP state guard around its screen-space blit");
    TestCheck(body.find("VXRENDERSTATE_CLIPPLANEENABLE, 0") != std::string::npos,
              "CopyToVideo must disable clip planes so user clipping cannot affect the blit");
    TestCheck(body.find("const CKViewportData oldViewport = m_ViewportData") != std::string::npos &&
              body.find("m_ViewportData = oldViewport") != std::string::npos,
              "CopyToVideo must restore the viewport used by normal rendering");
}

void Test_2dEntity_DrawUsesFFPStateGuard() {
    std::string source = ReadRenderEngineSource("src/CK2dEntity.cpp");
    const size_t drawPos = source.find("CKERROR RCK2dEntity::Draw");
    TestCheck(drawPos != std::string::npos, "2D entity Draw implementation must be present");
    const size_t nextPos = source.find("void RCK2dEntity::GetExtents", drawPos);
    TestCheck(nextPos != std::string::npos, "2D entity Draw source range must be bounded");
    const std::string body = source.substr(drawPos, nextPos - drawPos);
    TestCheck(body.find("CKFFStateGuard ffpState(dev->m_FFPipeline)") != std::string::npos,
              "2D entity Draw must guard its temporary FFP render and texture state");
    TestCheck(body.find("SkipFullscreenBlack2DEnabled()") != std::string::npos &&
              body.find("dev->SetViewRect(savedViewRect)") != std::string::npos,
              "2D entity debug skip must restore the viewport before returning");
}

void Test_RenderTarget_ValidatesCubeFacesAndPreservesDepth() {
    std::string source = ReadRenderEngineSource("src/CKRenderContext.cpp");
    const size_t setPos = source.find("CKBOOL RCKRenderContext::SetRenderTarget");
    TestCheck(setPos != std::string::npos, "SetRenderTarget implementation must be present");
    const size_t nextPos = source.find("CKERROR RCKRenderContext::Create", setPos);
    TestCheck(nextPos != std::string::npos, "SetRenderTarget source range must be bounded");
    const std::string body = source.substr(setPos, nextPos - setPos);
    TestCheck(body.find("CubeMapFace < CKRST_CUBEFACE_XPOS") != std::string::npos &&
              body.find("CubeMapFace > CKRST_CUBEFACE_ZNEG") != std::string::npos,
              "SetRenderTarget must reject invalid cubemap face indices");
    TestCheck(body.find("!texture->IsCubeMap() && CubeMapFace != CKRST_CUBEFACE_XPOS") != std::string::npos,
              "SetRenderTarget must reject nonzero cubemap faces for 2D targets");
    TestCheck(body.find("CreateDepthTexture(m_TargetDepthTexture") != std::string::npos &&
              body.find("desc.DepthStencil.Texture = m_TargetDepthTexture") != std::string::npos,
              "Render-to-texture framebuffers must bind a depth-stencil attachment");

    std::string texture = ReadRenderEngineSource("src/CKTexture.cpp");
    const size_t ensurePos = texture.find("CKBOOL RCKTexture::EnsureRenderTarget");
    TestCheck(ensurePos != std::string::npos, "EnsureRenderTarget implementation must be present");
    const size_t ensureEnd = texture.find("RCKTexture::RCKTexture", ensurePos);
    TestCheck(ensureEnd != std::string::npos, "EnsureRenderTarget source range must be bounded");
    const std::string ensureBody = texture.substr(ensurePos, ensureEnd - ensurePos);
    TestCheck(ensureBody.find("isCubeTarget ? CKRST_TEXTURE_CUBEMAP : 0") != std::string::npos,
              "EnsureRenderTarget must create cubemap render targets for cubemap textures");
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

void Test_DrawState_FixesLegacyScreenBlendPair() {
    CKDrawStateCache cache;
    cache.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    cache.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ONE);
    cache.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_SRCCOLOR);

    const CKDrawState state = cache.BuildDrawState(VX_POINTLIST);
    const CKDWORD src = (state.Lo >> 16) & 0xF;
    const CKDWORD dst = (state.Lo >> 20) & 0xF;

    TestCheck(src == VXBLEND_ONE, "ONE/SRCCOLOR source must remain ONE");
    TestCheck(dst == VXBLEND_INVSRCCOLOR,
              "Legacy particle screen blend must use inverse source color so black transparent texels preserve the framebuffer");
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

void Test_TextureBlend_ShaderOpsMatchFFPFormulas() {
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

void Test_MaterialChannels_ClampToFFPStageCapacityAndClearAfterDraw() {
    std::string source = ReadRenderEngineSource("src/CKMesh.cpp");
    TestCheck(source.find("maxAdditionalStages > CKFF_MAX_TEXTURE_STAGES - 1") != std::string::npos,
              "Material channel mono-pass selection must clamp to the eight-stage FFP capacity");
    TestCheck(source.find("m_ActiveTextureChannels.Size() && i < maxAdditionalStages") != std::string::npos,
              "Material channel binding must not address stages beyond the FFP stage array");
    TestCheck(source.find("DisableTextureStagesFrom(1)") != std::string::npos,
              "Mesh rendering must clear additional mono-pass texture stages after draw");
}

void Test_Mesh_DepthStencilOnlyDrawsUseFFPStateGuard() {
    std::string source = ReadRenderEngineSource("src/CKMesh.cpp");
    TestCheck(source.find("CKFFStateGuard ffpState(ffp)") != std::string::npos,
              "Depth/stencil-only mesh paths must guard temporary FFP state");
    TestCheck(source.find("savedLighting") == std::string::npos &&
              source.find("savedAlphaBlend") == std::string::npos &&
              source.find("savedStencil") == std::string::npos,
              "Depth/stencil-only mesh paths must not keep duplicate manual FFP restore variables");
    TestCheck(source.find("SetColorWriteMask(TRUE, TRUE, TRUE, TRUE)") == std::string::npos,
              "Depth/stencil-only mesh paths must restore color writes through the FFP state guard");
}

void Test_Mesh_DefaultRenderRestoresSceneSpecularState() {
    std::string source = ReadRenderEngineSource("src/CKMesh.cpp");
    TestCheck(source.find("RestoreSceneSpecularState(rc)") != std::string::npos,
              "Mesh default rendering must restore scene specular state before materials so callback state leaks cannot pop specular highlights");
}

void Test_MaterialChannels_NoBaseTextureKeepsStageChainAlive() {
    std::string source = ReadRenderEngineSource("src/CKMesh.cpp");
    TestCheck(source.find("!mat->GetTexture(0) && m_ActiveTextureChannels.Size() > 0") != std::string::npos,
              "Meshes with channel textures but no base texture must explicitly configure stage 0");
    TestCheck(source.find("SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1)") != std::string::npos &&
              source.find("SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CURRENT)") != std::string::npos,
              "No-base-texture channel draws must keep the FFP evaluator running by selecting CURRENT at stage 0");
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

void DrawDiagnosticPositionTTriangle(CKFixedFunctionPipeline &ffp, FFPDiagnosticContext &ctx) {
    float positions[3][4] = {
        {0.0f, 0.0f, 0.5f, 1.0f},
        {1.0f, 0.0f, 0.5f, 1.0f},
        {0.0f, 1.0f, 0.5f, 1.0f},
    };
    float uv[3][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.0f, 1.0f},
    };
    CKDWORD diffuse[3] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

    VxDrawPrimitiveData data;
    memset(&data, 0, sizeof(data));
    data.Flags = CKRST_DP_CL_VCT;
    data.VertexCount = 3;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(positions[0]);
    data.TexCoordPtr = uv;
    data.TexCoordStride = sizeof(uv[0]);
    data.ColorPtr = diffuse;
    data.ColorStride = sizeof(diffuse[0]);

    ffp.DrawPrimitive(&ctx.Encoder, CKRP_VIEW_OPAQUE3D, VX_TRIANGLELIST, nullptr, 0, &data);
}

void Test_FFPDiagnosticHarness_SpecularStateFollowsDrawOrder() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&ctx);
    const CKDWORD alphaUniform = ffp.GetShaderCache().GetUniforms().u_alphaParams;

    ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, FALSE);
    DrawDiagnosticPositionTTriangle(ffp, ctx);
    TestCheck(ctx.Encoder.SubmitCount == 1 && ctx.Encoder.LastProgram != 0,
              "FFP diagnostic draw must submit through the runtime pipeline");
    TestCheck(ctx.Encoder.FloatUniforms[alphaUniform].size() >= 4 &&
              ctx.Encoder.FloatUniforms[alphaUniform][2] == 0.0f,
              "A draw with specular disabled must upload a disabled specular flag");

    ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, TRUE);
    DrawDiagnosticPositionTTriangle(ffp, ctx);
    TestCheck(ctx.Encoder.SubmitCount == 2,
              "FFP diagnostic harness must record consecutive runtime draws");
    TestCheck(ctx.Encoder.FloatUniforms[alphaUniform].size() >= 4 &&
              ctx.Encoder.FloatUniforms[alphaUniform][2] == 1.0f,
              "A later draw must observe restored specular state instead of leaking the previous draw");
}

void Test_FFPDiagnosticHarness_PositionTDefaultColor1KeepsRGBBlack() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&ctx);

    DrawDiagnosticPositionTTriangle(ffp, ctx);
    TestCheck(ctx.Encoder.LastVertexBytes.size() >= 32,
              "FFP diagnostic harness must capture the submitted transient vertices");

    CKDWORD color1 = 0;
    memcpy(&color1, &ctx.Encoder.LastVertexBytes[28], sizeof(color1));
    TestCheck((color1 & 0x00FFFFFF) == 0,
              "PositionT vertices without a specular stream must default COLOR1 RGB to black");
    TestCheck((color1 & 0xFF000000) == 0xFF000000,
              "PositionT vertices without a specular stream must keep COLOR1 alpha opaque");
}

void Test_FFPDiagnosticHarness_BlendFixupReachesSubmittedDrawState() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&ctx);

    ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_SRCBLEND, VXBLEND_ONE);
    ffp.SetRenderState(VXRENDERSTATE_DESTBLEND, VXBLEND_SRCCOLOR);
    DrawDiagnosticPositionTTriangle(ffp, ctx);

    const CKDWORD src = (ctx.Encoder.LastState.Lo >> 16) & 0xF;
    const CKDWORD dst = (ctx.Encoder.LastState.Lo >> 20) & 0xF;
    TestCheck(src == VXBLEND_ONE,
              "Runtime FFP draw state must preserve ONE as the fixed-up source blend");
    TestCheck(dst == VXBLEND_INVSRCCOLOR,
              "Runtime FFP draw state must submit the ONE/SRCCOLOR compatibility fixup");
}

void Test_FFPDiagnosticHarness_TextureStageResetReachesUniforms() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&ctx);
    const CKDWORD stageUniform = ffp.GetShaderCache().GetUniforms().u_stageParams;

    ffp.SetTexture(0, 0);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TFACTOR);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX,
                             CKFFPackTexcoordIndex(3, CKFF_TEXGEN_CAMERASPACEREFLECTION));
    DrawDiagnosticPositionTTriangle(ffp, ctx);

    TestCheck(ctx.Encoder.FloatUniforms[stageUniform].size() >= CKFF_MAX_TEXTURE_STAGES * 16,
              "FFP diagnostic harness must capture uploaded texture-stage uniforms");
    TestCheck(ctx.Encoder.FloatUniforms[stageUniform][0] == (float)CKRST_TOP_ADD,
              "Runtime stage uniform must expose the temporary color op before reset");
    TestCheck(ctx.Encoder.FloatUniforms[stageUniform][9] ==
                  (float)CKFFPackTexcoordIndex(3, CKFF_TEXGEN_CAMERASPACEREFLECTION),
              "Runtime stage uniform must expose the temporary texcoord index before reset");

    ffp.ResetTextureStage(0);
    DrawDiagnosticPositionTTriangle(ffp, ctx);
    TestCheck(ctx.Encoder.FloatUniforms[stageUniform][0] == (float)CKRST_TOP_DISABLE,
              "Runtime stage uniform must disable a reset stage with no bound texture");
    TestCheck(ctx.Encoder.FloatUniforms[stageUniform][9] == 0.0f,
              "Runtime stage uniform must restore the reset stage texcoord index");
}

void Test_FFPDiagnosticHarness_TextureMatrixResetReachesUniforms() {
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(&ctx);
    const CKDWORD texMatrixUniform = ffp.GetShaderCache().GetUniforms().u_texMatrix;

    VxMatrix textureMatrix = VxMatrix::Identity();
    textureMatrix[0][0] = 2.0f;
    textureMatrix[3][0] = 0.125f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, textureMatrix);
    DrawDiagnosticPositionTTriangle(ffp, ctx);

    TestCheck(ctx.Encoder.FloatUniforms[texMatrixUniform].size() >= 16,
              "FFP diagnostic harness must capture uploaded texture matrix uniforms");
    TestCheck(ctx.Encoder.FloatUniforms[texMatrixUniform][0] == 2.0f &&
              ctx.Encoder.FloatUniforms[texMatrixUniform][12] == 0.125f,
              "Runtime texture matrix uniform must expose the temporary texture transform");

    ffp.ResetTextureStage(0);
    DrawDiagnosticPositionTTriangle(ffp, ctx);
    TestCheck(ctx.Encoder.FloatUniforms[texMatrixUniform][0] == 1.0f &&
              ctx.Encoder.FloatUniforms[texMatrixUniform][12] == 0.0f,
              "Runtime texture matrix uniform must restore identity after stage reset");
}

void Test_FFPStateGuard_RestoresRenderAndTextureState() {
    CKFixedFunctionPipeline ffp;
    ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    ffp.SetColorWriteMask(TRUE, FALSE, TRUE, FALSE);
    ffp.SetTexture(0, 1234);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTexture(2, 5678);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
    VxMatrix world = VxMatrix::Identity();
    world[3][0] = 3.0f;
    ffp.SetTransform(VXMATRIX_WORLD, world);
    VxMatrix textureMatrix = VxMatrix::Identity();
    textureMatrix[0][0] = 2.0f;
    textureMatrix[3][0] = 0.25f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, textureMatrix);
    VxMatrix textureMatrix2 = VxMatrix::Identity();
    textureMatrix2[1][1] = 3.0f;
    textureMatrix2[3][1] = 0.5f;
    ffp.SetTransform(VXMATRIX_TEXTURE2, textureMatrix2);

    {
        CKFFStateGuard guard(ffp);
        ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, FALSE);
        ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
        ffp.SetColorWriteMask(FALSE, FALSE, FALSE, FALSE);
        ffp.SetTexture(0, 0);
        ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
        ffp.SetTexture(2, 0);
        ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_DISABLE);
        VxMatrix changedTexture2 = VxMatrix::Identity();
        changedTexture2[1][1] = 5.0f;
        ffp.SetTransform(VXMATRIX_TEXTURE2, changedTexture2);
        VxMatrix changedWorld = VxMatrix::Identity();
        changedWorld[3][0] = 9.0f;
        ffp.SetTransform(VXMATRIX_WORLD, changedWorld);
        VxMatrix changed = VxMatrix::Identity();
        changed[0][0] = 4.0f;
        ffp.SetTransform(VXMATRIX_TEXTURE0, changed);
    }

    CKFFTextureStageSnapshot restored;
    ffp.SaveTextureStage(0, restored);
    CKFFTextureStageSnapshot restoredStage2;
    ffp.SaveTextureStage(2, restoredStage2);
    TestCheck(ffp.GetRenderState(VXRENDERSTATE_SPECULARENABLE) == TRUE,
              "FFP state guard must restore specular render state");
    TestCheck(ffp.GetRenderState(VXRENDERSTATE_LIGHTING) == TRUE,
              "FFP state guard must restore lighting render state");
    TestCheck(ffp.GetColorWriteMask() == (CKRST_STATE_WRITE_R | CKRST_STATE_WRITE_B),
              "FFP state guard must restore color write mask");
    TestCheck(restored.Texture == 1234,
              "FFP state guard must restore texture binding");
    TestCheck(restored.States[CKRST_TSS_OP] == CKRST_TOP_MODULATE &&
              restored.States[CKRST_TSS_ARG1] == CKRST_TA_TEXTURE,
              "FFP state guard must restore texture stage state");
    TestCheck(restored.TextureMatrix[0][0] == 2.0f &&
              restored.TextureMatrix[3][0] == 0.25f,
              "FFP state guard must restore texture matrix");
    TestCheck(restoredStage2.Texture == 5678 &&
              restoredStage2.States[CKRST_TSS_OP] == CKRST_TOP_ADD &&
              restoredStage2.States[CKRST_TSS_ARG2] == CKRST_TA_CURRENT,
              "FFP state guard must restore nonzero texture stages");
    TestCheck(restoredStage2.TextureMatrix[1][1] == 3.0f &&
              restoredStage2.TextureMatrix[3][1] == 0.5f,
              "FFP state guard must restore nonzero texture matrices");
    TestCheck(ffp.GetWorldMatrix()[3][0] == 3.0f,
              "FFP state guard must restore world matrix");
}

void Test_FFPRenderStateGuard_RestoresOnlyScopedState() {
    CKFixedFunctionPipeline ffp;
    ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);

    {
        CKFFRenderStateGuard guard(ffp, VXRENDERSTATE_SPECULARENABLE);
        ffp.SetRenderState(VXRENDERSTATE_SPECULARENABLE, FALSE);
        ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    }

    TestCheck(ffp.GetRenderState(VXRENDERSTATE_SPECULARENABLE) == TRUE,
              "Single render-state guard must restore the scoped state");
    TestCheck(ffp.GetRenderState(VXRENDERSTATE_LIGHTING) == FALSE,
              "Single render-state guard must not restore unrelated render states");
}

void Test_3dEntity_IndirectMatrixUsesRenderStateGuard() {
    std::string source = ReadRenderEngineSource("src/CK3dEntity.cpp");
    TestCheck(source.find("CKFFRenderStateGuard inverseWindingGuard") != std::string::npos,
              "Indirect-matrix entity rendering must guard temporary inverse winding");
    TestCheck(source.find("savedInverseWinding") == std::string::npos,
              "Indirect-matrix entity rendering must not use hand-written inverse winding restore state");
}

void Test_Sprite3DBatches_UseFFPStateGuard() {
    std::string source = ReadRenderEngineSource("src/CKRenderContext.cpp");
    const size_t batchPos = source.find("void RCKRenderContext::CallSprite3DBatches");
    TestCheck(batchPos != std::string::npos, "Sprite3D batch implementation must be present");
    const size_t nextPos = source.find("void RCKRenderContext::CheckObjectExtents", batchPos);
    TestCheck(nextPos != std::string::npos, "Sprite3D batch source range must be bounded");
    const std::string body = source.substr(batchPos, nextPos - batchPos);
    TestCheck(body.find("CKFFStateGuard ffpState(m_FFPipeline)") != std::string::npos,
              "Sprite3D batch flush must guard temporary FFP state and transforms");
    TestCheck(body.find("SetTransform(VXMATRIX_WORLD, identity)") != std::string::npos,
              "Sprite3D batch flush must still render batches in identity world space");
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

void Test_MaterialEffects_UseEightStageFFPContract() {
    std::string material = ReadRenderEngineSource("src/CKMaterial.cpp");
    TestCheck(material.find("stage >= 4") == std::string::npos &&
              material.find("currentStage >= 4") == std::string::npos,
              "Material effect helpers must not keep the old four-stage limit");
    TestCheck(material.find("DisableTextureStagesFrom(TextureStage + 1)") != std::string::npos,
              "SetAsCurrent must clear stale higher effect stages before applying the current material");
    TestCheck(material.find("CKRST_TOP_BUMPENVMAPLUMINANCE") != std::string::npos,
              "BumpEnv effect mapping must expose the luminance bump op when requested by parameters");
}

void Test_ShaderEvaluator_Dot3MatchesD3DFFP() {
    std::string shader = ReadRenderEngineSource("src/shaders/fs_ff_stage.sc");
    TestCheck(shader.find("clamp(dot(a.rgb - 0.5, b.rgb - 0.5) * 4.0, 0.0, 1.0)") != std::string::npos,
              "DOTPRODUCT3 must use the D3D fixed-function dot formula and saturate the result");
    TestCheck(shader.find("return vec4_splat(v);") != std::string::npos,
              "DOTPRODUCT3 must replicate the dot result into all channels");
    TestCheck(shader.find("if (colorOp == 24)") != std::string::npos &&
              shader.find("stageResult = colorResult;") != std::string::npos,
              "DOTPRODUCT3 must write the full result vector instead of preserving a separate alpha path");
}

void Test_ShaderEvaluator_CoversFFPStageOps() {
    std::string shader = ReadRenderEngineSource("src/shaders/fs_ff_stage.sc");
    TestCheck(shader.find("else if (baseArg == 5) value = temp") != std::string::npos &&
              shader.find("if (resultArg == 5)") != std::string::npos,
              "Shader evaluator must support TEMP/RESULTARG stage routing");
    TestCheck(shader.find("value.rgb = value.aaa") != std::string::npos &&
              shader.find("value.rgb = 1.0 - value.rgb") != std::string::npos,
              "Shader evaluator must support ALPHAREPLICATE and COMPLEMENT argument modifiers");
    TestCheck(shader.find("previousColorOp == 22 || previousColorOp == 23") != std::string::npos &&
              shader.find("stageUv.x += dot(u_bumpEnv[0].xy, bump)") != std::string::npos &&
              shader.find("previousColorOp == 23") != std::string::npos,
              "Shader evaluator must perturb the next stage for BUMPENVMAP and apply luminance for BUMPENVMAPLUMINANCE");
    TestCheck(shader.find("if (op == 25) return clamp(a * b + c") != std::string::npos &&
              shader.find("if (op == 26) return clamp(c * a + (vec4_splat(1.0) - c) * b") != std::string::npos,
              "Shader evaluator must implement MULTIPLYADD and LERP with ARG0 input");
}

void Test_TextureOpEvaluator_MatchesFFPFormulas() {
    const VxColor arg0(0.25f, 0.50f, 0.75f, 0.25f);
    const VxColor arg1(0.20f, 0.40f, 0.60f, 0.25f);
    const VxColor arg2(0.80f, 0.50f, 0.25f, 0.50f);
    const VxColor current(0.10f, 0.10f, 0.10f, 0.10f);
    const VxColor diffuse(0.60f, 0.60f, 0.60f, 0.50f);
    const VxColor texture(0.70f, 0.70f, 0.70f, 0.25f);

    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_MULTIPLYADD, arg0, arg1, arg2, current, diffuse, texture),
                   VxColor(0.41f, 0.70f, 0.90f, 0.375f),
                   "MULTIPLYADD must evaluate arg1 * arg2 + arg0");
    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_LERP, arg0, arg1, arg2, current, diffuse, texture),
                   VxColor(0.65f, 0.45f, 0.5125f, 0.4375f),
                   "LERP must evaluate arg0 * arg1 + (1 - arg0) * arg2");
    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_MODULATEALPHA_ADDCOLOR, arg0, arg1, arg2, current, diffuse, texture),
                   VxColor(0.40f, 0.525f, 0.6625f, 0.375f),
                   "MODULATEALPHA_ADDCOLOR must use arg1 alpha as the multiplier");
    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_MODULATEINVCOLOR_ADDALPHA, arg0, arg1, arg2, current, diffuse, texture),
                   VxColor(0.89f, 0.55f, 0.35f, 0.625f),
                   "MODULATEINVCOLOR_ADDALPHA must complement arg1 color and add arg1 alpha");
    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_DOTPRODUCT3, arg0, arg1, arg2, current, diffuse, texture),
                   VxColor(0.0f, 0.0f, 0.0f, 0.0f),
                   "DOTPRODUCT3 must replicate the saturated signed dot product");
}

void Test_TextureOpCoverage_ClassifiesEveryKnownFFPOp() {
    for (CKDWORD op = CKRST_TOP_DISABLE; op <= CKRST_TOP_LERP; ++op) {
        TestCheck(CKFFClassifyTextureOpCoverage(op) != CKFF_COVERAGE_UNTESTED,
                  "Every known CKRST_TOP_* op must have an explicit coverage classification");
    }
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_BUMPENVMAP) == CKFF_COVERAGE_APPROXIMATE &&
              CKFFClassifyTextureOpCoverage(CKRST_TOP_BUMPENVMAPLUMINANCE) == CKFF_COVERAGE_APPROXIMATE,
              "Bump env ops must be documented as approximate shader coverage");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_PREMODULATE) == CKFF_COVERAGE_FALLBACK,
              "PREMODULATE must be an explicit fallback instead of an accidental default path");
    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_LERP + 1) == CKFF_COVERAGE_UNTESTED,
              "Unknown texture ops must stay visibly untested");
}

void Test_ShaderSemanticCoverage_ClassifiesKnownFFPSemantics() {
    for (int i = 0; i < CKFF_SHADER_SEMANTIC_COUNT; ++i) {
        TestCheck(CKFFClassifyShaderSemanticCoverage((CKFFShaderSemantic)i) != CKFF_COVERAGE_UNTESTED,
                  "Every tracked FFP shader semantic must have an explicit coverage classification");
    }

    TestCheck(CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_ARG_CURRENT) == CKFF_COVERAGE_EXACT &&
              CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_ARG_TEMP) == CKFF_COVERAGE_EXACT,
              "CURRENT and TEMP argument routing must be documented as exact shader coverage");
    TestCheck(CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_ARG_COMPLEMENT) == CKFF_COVERAGE_EXACT &&
              CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_ARG_ALPHAREPLICATE) == CKFF_COVERAGE_EXACT,
              "Argument modifiers must be documented as exact shader coverage");
    TestCheck(CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_PROJECTED_SAMPLING) == CKFF_COVERAGE_EXACT,
              "Projected sampling must be documented in the shader coverage table");
    TestCheck(CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_BUMPENVMAP) == CKFF_COVERAGE_APPROXIMATE &&
              CKFFClassifyShaderSemanticCoverage(CKFF_SHADER_SEMANTIC_BUMPENVMAPLUMINANCE) == CKFF_COVERAGE_APPROXIMATE,
              "Bump env shader semantics must remain visibly approximate");
    TestCheck(CKFFClassifyShaderSemanticCoverage((CKFFShaderSemantic)CKFF_SHADER_SEMANTIC_COUNT) == CKFF_COVERAGE_UNTESTED,
              "Unknown shader semantics must stay visibly untested");
}

void Test_FFPShaderKey_UsesFixedFunctionStageRules() {
    TestCheck(CKFFShaderKeyArgsMask(CKRST_TOP_DISABLE) == 0b000u,
              "FFP key ArgsMask must disable all args for DISABLE");
    TestCheck(CKFFShaderKeyArgsMask(CKRST_TOP_SELECTARG1) == 0b010u &&
              CKFFShaderKeyArgsMask(CKRST_TOP_PREMODULATE) == 0b010u,
              "FFP key ArgsMask must read only arg1 for SELECTARG1/PREMODULATE");
    TestCheck(CKFFShaderKeyArgsMask(CKRST_TOP_SELECTARG2) == 0b100u,
              "FFP key ArgsMask must read only arg2 for SELECTARG2");
    TestCheck(CKFFShaderKeyArgsMask(CKRST_TOP_MULTIPLYADD) == 0b111u &&
              CKFFShaderKeyArgsMask(CKRST_TOP_LERP) == 0b111u,
              "FFP key ArgsMask must read arg0/arg1/arg2 for MULTIPLYADD/LERP");
    TestCheck(CKFFShaderKeyArgsMask(CKRST_TOP_MODULATE) == 0b110u,
              "FFP key ArgsMask default must read arg1/arg2");

    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_MODULATE);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageColorArg2(0, CKRST_TA_CURRENT);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG2);
    desc.SetStageAlphaArg2(0, CKRST_TA_CURRENT);
    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 0);
    TestCheck(key.Stages[0].ColorOp == CKRST_TOP_DISABLE &&
              key.LastActiveTextureStage == 0,
              "FFP key must truncate the stage chain when a used TEXTURE arg has no bound texture");

    desc = CKFFFSStateDesc();
    desc.SetStageColorOp(0, CKRST_TOP_MODULATE);
    desc.SetStageColorArg1(0, CKRST_TA_CURRENT);
    desc.SetStageColorArg2(0, CKRST_TA_DIFFUSE);
    desc.SetStageAlphaOp(0, CKRST_TOP_DISABLE);
    desc.SetStageResultIsTemp(0, true);
    key = CKFFBuildShaderKeyFS(desc, 0);
    TestCheck(key.Stages[0].AlphaOp == CKRST_TOP_SELECTARG1 &&
              key.Stages[0].AlphaArg1 == CKRST_TA_DIFFUSE,
              "FFP key must apply the stage0 TEMP color / disabled alpha fixup");
    TestCheck(!key.Stages[0].ResultIsTemp,
              "FFP key must force the final active stage to write CURRENT");

    desc.SetSpecularAdd(true);
    key = CKFFBuildShaderKeyFS(desc, 0);
    TestCheck(key.GlobalSpecularEnable,
              "FFP key must preserve global specular add state");
}

void Test_FFPSpecializationInfo_MatchesFFPVariantLayout() {
    CKFFSpecializationInfo info;
    info.SetOptimized(true);
    info.Set(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE, 3);
    info.Set(CKFF_SPEC_GLOBAL_SPECULAR_ENABLED, 1);
    info.Set(CKFF_SPEC_STAGE0_COLOR_OP, CKRST_TOP_LERP);
    info.Set(CKFF_SPEC_STAGE0_COLOR_ARG1, CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE);
    info.Set(CKFF_SPEC_STAGE0_RESULT_IS_TEMP, 1);

    TestCheck(info.IsOptimized() &&
              info.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 3 &&
              info.Data()[4] == (3u << 16),
              "FFP specialization layout must mark optimized mode and pack LastActiveTextureStage into specialization dword 4 bits 16..18");
    TestCheck(info.Get(CKFF_SPEC_GLOBAL_SPECULAR_ENABLED) == 1 &&
              (info.Data()[6] & (1u << 31)) != 0,
              "FFP specialization layout must pack GlobalSpecularEnabled into specialization dword 6 bit 31");
    TestCheck(info.Get(CKFF_SPEC_STAGE0_COLOR_OP) == CKRST_TOP_LERP &&
              info.Get(CKFF_SPEC_STAGE0_RESULT_IS_TEMP) == 1,
              "FFP specialization layout must round-trip stage 0 op/result fields");
    TestCheck(CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE) ==
              ((CKRST_TA_TEXTURE & 0b111u) | ((CKRST_TA_ALPHAREPLICATE & 0b110000u) >> 1u)),
              "FFP specialization arg packing must match FFP specialization repackArg");

    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_MODULATE);
    desc.SetStageColorArg1(0, CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE);
    desc.SetStageColorArg2(0, CKRST_TA_CURRENT | CKRST_TA_COMPLEMENT);
    desc.SetStageAlphaOp(0, CKRST_TOP_SELECTARG1);
    desc.SetStageAlphaArg1(0, CKRST_TA_DIFFUSE);
    CKFFShaderKeyFS key = CKFFBuildShaderKeyFS(desc, 1);
    CKFFSpecializationInfo built = CKFFBuildSpecializationInfo(key);
    TestCheck(built.IsOptimized() &&
              built.Get(CKFF_SPEC_STAGE0_COLOR_ARG1) == CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE) &&
              built.Get(CKFF_SPEC_STAGE0_COLOR_ARG2) == CKFFSpecializationInfo::RepackArg(CKRST_TA_CURRENT | CKRST_TA_COMPLEMENT),
              "Built FFP specialization info must enable optimized mode and pack modifier args for shader-side unpacking");
}

void Test_FFPShaderCache_UsesKeyedFFPVariantContract() {
    std::string cacheHeader = ReadRenderEngineSource("src/CKFFShaderCache.h");
    std::string cacheSource = ReadRenderEngineSource("src/CKFFShaderCache.cpp");
    std::string moduleHeader = ReadRenderEngineSource("src/CKFFSpecializedModuleTable.h");
    std::string moduleSource = ReadRenderEngineSource("src/CKFFSpecializedModuleTable.cpp");
    std::string generatedTable = ReadRenderEngineSource("src/shaders/generated/CKFFSpecializedModuleTable.generated.h");
    std::string shaderCompiler = ReadRenderEngineSource("src/shaders/compile_shaders.py");
    std::string variantManifest = ReadRenderEngineSource("src/shaders/ffp_specialized_variants.json");
    std::string rasterTypes = ReadRenderEngineSource("include/CKRasterizerTypes.h");
    std::string bgfxContext = ReadRenderEngineSource("src/CKRasterizer/CKBgfxRasterizerContext.cpp");
    std::string rootCmake = ReadRenderEngineSource("CMakeLists.txt");
    std::string srcCmake = ReadRenderEngineSource("src/CMakeLists.txt");

    TestCheck(cacheHeader.find("CKDWORD GetProgram(const CKFFShaderKey &key)") != std::string::npos &&
              cacheHeader.find("unordered_map<CKFFShaderKey, CKDWORD, CKFFShaderKeyHash>") != std::string::npos,
              "FFP shader cache must be keyed by explicit specialized FFP shader keys");
    TestCheck(cacheHeader.find("CKFF_SHADER_MODE_UBER_SPECIALIZED") != std::string::npos &&
              cacheHeader.find("CKFF_SHADER_MODE_FULL_SPECIALIZED") != std::string::npos,
              "FFP shader cache must expose distinct specialized uber and full specialized modes");
    TestCheck(cacheHeader.find("m_Stage3DProgram") == std::string::npos &&
              cacheHeader.find("m_PositionTProgram") == std::string::npos &&
              cacheSource.find("GetProgram(const CKFFStateDesc") == std::string::npos,
              "Clean-break FFP shader cache must not keep the old fixed-program selection API");
    TestCheck(cacheSource.find("CK2_FFP_UBERSHADER") != std::string::npos &&
              cacheSource.find("CKFFBuildSpecializationInfo(key.FS)") != std::string::npos,
              "FFP shader cache must expose the specialized ubershader/specialization mode switch");
    TestCheck(cacheHeader.find("m_VariantStats") != std::string::npos &&
              cacheHeader.find("RecordVariantKey") != std::string::npos &&
              cacheSource.find("CK2_FFP_VARIANT_LOG_LIMIT") != std::string::npos &&
              cacheSource.find("FFP variant specDwords") != std::string::npos &&
              cacheSource.find("FFP_VARIANT_MANIFEST_CANDIDATE_BEGIN") != std::string::npos,
              "FFP shader cache must provide an internal opt-in runtime key dump and manifest candidate for variant growth");
    TestCheck(rootCmake.find("option(CKRE_FFP_VARIANT_CAPTURE") != std::string::npos &&
              rootCmake.find("variant key capture logging") != std::string::npos &&
              srcCmake.find("CKRE_FFP_VARIANT_CAPTURE=$<IF:$<BOOL:${CKRE_FFP_VARIANT_CAPTURE}>,1,0>") != std::string::npos &&
              cacheHeader.find("#if CKRE_FFP_VARIANT_CAPTURE") != std::string::npos &&
              cacheSource.find("#if CKRE_FFP_VARIANT_CAPTURE") != std::string::npos,
              "FFP variant key dump must remain a temporary build-gated capture facility, not always-on runtime infrastructure");
    TestCheck(cacheSource.find("CreateFullSpecializedProgram") != std::string::npos &&
              cacheSource.find("CKFFFindSpecializedModule") != std::string::npos &&
              cacheSource.find("Full FFP specialized module cache miss") != std::string::npos,
              "CK2_FFP_UBERSHADER=0 must use an explicit full-specialized module lookup path, not silently reuse the uber blob");
    TestCheck(moduleHeader.find("struct CKFFSpecializedModule") != std::string::npos &&
              moduleHeader.find("struct CKFFSpecializedModuleEntry") != std::string::npos &&
              moduleHeader.find("CKFFFindSpecializedModule") != std::string::npos &&
              moduleSource.find("CKFFSpecializedModuleTable.generated.h") != std::string::npos &&
              moduleSource.find("g_CKFFSpecializedModules") != std::string::npos &&
              generatedTable.find("CKFFSpecializedKey_variant_3d_stage0_modulate") != std::string::npos &&
              generatedTable.find("CKFFSpecializedKey_positiont_stage0_modulate") != std::string::npos &&
              generatedTable.find("CKFFSpecializedKey_variant_3d_stage1_add_specular") != std::string::npos &&
              generatedTable.find("s_dx11_ffp_spec_") != std::string::npos &&
              generatedTable.find("positiont_stage0_modulate_fs_ff_stage") == std::string::npos &&
              shaderCompiler.find("load_specialized_variant_manifest") != std::string::npos &&
              shaderCompiler.find("ffp_specialized_shader_defines") != std::string::npos &&
              shaderCompiler.find("ffp_specialization_dwords_from_key") != std::string::npos &&
              shaderCompiler.find("specDwords does not match key-derived FFP specialization payload") != std::string::npos &&
              shaderCompiler.find("normalize_specialized_variant") != std::string::npos &&
              shaderCompiler.find("default_specialized_stage") != std::string::npos &&
              shaderCompiler.find("stages must contain up to 8 stage objects") != std::string::npos &&
              shaderCompiler.find("specialization_identifier") != std::string::npos &&
              shaderCompiler.find("unique_specialized_fs_variants") != std::string::npos &&
              shaderCompiler.find("validate_specialized_variants") != std::string::npos &&
              shaderCompiler.find("duplicate FFP shader key") != std::string::npos &&
              shaderCompiler.find("duplicate identifier") != std::string::npos &&
              shaderCompiler.find("compile_specialized_variants") != std::string::npos &&
              shaderCompiler.find("write_specialized_module_table(generated_dir, selected, specialized_variants)") != std::string::npos &&
              shaderCompiler.find("--define") != std::string::npos &&
              variantManifest.find("\"name\": \"3d_stage0_modulate\"") != std::string::npos &&
              variantManifest.find("\"name\": \"positiont_stage0_modulate\"") != std::string::npos &&
              variantManifest.find("\"name\": \"3d_stage1_add_specular\"") != std::string::npos &&
              variantManifest.find("\"lastActiveTextureStage\": 1") != std::string::npos &&
              variantManifest.find("\"globalSpecularEnable\": true") != std::string::npos &&
              srcCmake.find("ffp_specialized_variants.json") != std::string::npos,
              "Full specialized FFP modules must have an explicit manifest-generated table and shader define boundary");
    TestCheck(rasterTypes.find("SpecializationDwords") != std::string::npos &&
              rasterTypes.find("SpecializationDwordCount") != std::string::npos &&
              bgfxContext.find("rec->SpecializationDwords") != std::string::npos,
              "Internal rasterizer program descriptors must carry FFP specialization payloads");
    TestCheck(ReadRenderEngineSource("src/CKFFSpecializationInfo.h").find("SetDwords") != std::string::npos &&
              ReadRenderEngineSource("src/CKFFSpecializationInfo.cpp").find("memcpy(m_Data") != std::string::npos,
              "Generated full-specialized module entries must be able to preserve their exact 10-dword FFP specialization payload");
    TestCheck(rootCmake.find("option(CKRE_FFP_VARIANTS") != std::string::npos &&
              srcCmake.find("CKRE_FFP_VARIANTS=$<IF:$<BOOL:${CKRE_FFP_VARIANTS}>,1,0>") != std::string::npos,
              "RenderEngine CMake must expose and propagate the FFP variant clean-break option");
    TestCheck(cacheSource.find("#ifndef CKRE_FFP_VARIANTS") != std::string::npos &&
              cacheSource.find("#define CKRE_FFP_VARIANTS 1") != std::string::npos &&
              cacheSource.find("#if !CKRE_FFP_VARIANTS") != std::string::npos &&
              cacheSource.find("clean-break pure FFP variant pipeline") != std::string::npos,
              "CKRE_FFP_VARIANTS must default on and OFF must fail explicitly instead of implying an old fixed-program fallback");
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char *name, const char *value) : m_Name(name) {
        const char *existing = std::getenv(name);
        if (existing) {
            m_HadValue = true;
            m_Previous = existing;
        }
        Set(value);
    }

    ~ScopedEnvVar() {
        Set(m_HadValue ? m_Previous.c_str() : nullptr);
    }

private:
    static void Set(const char *name, const char *value) {
#if defined(_WIN32)
        _putenv_s(name, value ? value : "");
#else
        if (value)
            setenv(name, value, 1);
        else
            unsetenv(name);
#endif
    }

    void Set(const char *value) {
        Set(m_Name.c_str(), value);
    }

    std::string m_Name;
    std::string m_Previous;
    bool m_HadValue = false;
};

void Test_FFPShaderCache_FullSpecializedVariantHit(bool positionT) {
    ScopedEnvVar forceFullSpecialized("CK2_FFP_UBERSHADER", "0");
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFFShaderCache cache;
    cache.Init(&ctx);

    CKFFFSStateDesc fs;
    fs.SetStageColorOp(0, CKRST_TOP_MODULATE);
    fs.SetStageColorArg0(0, CKRST_TA_CURRENT);
    fs.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageColorArg2(0, CKRST_TA_CURRENT);
    fs.SetStageAlphaOp(0, CKRST_TOP_MODULATE);
    fs.SetStageAlphaArg0(0, CKRST_TA_CURRENT);
    fs.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageAlphaArg2(0, CKRST_TA_CURRENT);
    fs.SetStageResultIsTemp(0, false);

    CKFFShaderKey key;
    CKFFVSStateDesc vs;
    vs.SetHasPosition(!positionT);
    vs.SetHasPositionT(positionT);
    key.VS = CKFFShaderKeyVS(vs);
    key.FS = CKFFBuildShaderKeyFS(fs, 1u);
    CKFFSpecializationInfo expectedSpec = CKFFBuildSpecializationInfo(key.FS);

    CKDWORD program = cache.GetProgram(key);
    TestCheck(!cache.UsesUberShader() &&
              cache.GetShaderMode() == CKFF_SHADER_MODE_FULL_SPECIALIZED,
              "CK2_FFP_UBERSHADER=0 must select full-specialized FFP shader mode");
    TestCheck(program != 0 &&
              cache.CachedProgramCount() == 1 &&
              ctx.CreatedShaderCount == 2 &&
              ctx.CreatedProgramCount == 1,
              "Full-specialized cache must create a program when the manifest table contains the exact FFP key");
    TestCheck(ctx.LastProgramSpecializationDwords.size() == CKFFSpecializationInfo::MaxSpecDwords,
              "Full-specialized program creation must preserve the manifest specialization payload");
    for (CKDWORD i = 0; i < CKFFSpecializationInfo::MaxSpecDwords; ++i) {
        TestCheck(ctx.LastProgramSpecializationDwords[i] == expectedSpec.Data()[i],
                  "Full-specialized program payload must match the key-derived FFP specialization dwords");
    }

    cache.Shutdown();
}

void Test_FFPShaderCache_FullSpecialized3DVariantHit() {
    Test_FFPShaderCache_FullSpecializedVariantHit(false);
}

void Test_FFPShaderCache_FullSpecializedPositionTVariantHit() {
    Test_FFPShaderCache_FullSpecializedVariantHit(true);
}

void Test_FFPShaderCache_FullSpecializedTwoStageSpecularVariantHit() {
    ScopedEnvVar forceFullSpecialized("CK2_FFP_UBERSHADER", "0");
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFFShaderCache cache;
    cache.Init(&ctx);

    CKFFFSStateDesc fs;
    fs.SetSpecularAdd(true);
    fs.SetStageColorOp(0, CKRST_TOP_MODULATE);
    fs.SetStageColorArg0(0, CKRST_TA_CURRENT);
    fs.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageColorArg2(0, CKRST_TA_CURRENT);
    fs.SetStageAlphaOp(0, CKRST_TOP_MODULATE);
    fs.SetStageAlphaArg0(0, CKRST_TA_CURRENT);
    fs.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageAlphaArg2(0, CKRST_TA_CURRENT);

    fs.SetStageColorOp(1, CKRST_TOP_ADD);
    fs.SetStageColorArg0(1, CKRST_TA_CURRENT);
    fs.SetStageColorArg1(1, CKRST_TA_TEXTURE);
    fs.SetStageColorArg2(1, CKRST_TA_CURRENT);
    fs.SetStageAlphaOp(1, CKRST_TOP_SELECTARG2);
    fs.SetStageAlphaArg0(1, CKRST_TA_CURRENT);
    fs.SetStageAlphaArg1(1, CKRST_TA_TEXTURE);
    fs.SetStageAlphaArg2(1, CKRST_TA_CURRENT);

    CKFFShaderKey key;
    CKFFVSStateDesc vs;
    vs.SetHasPosition(true);
    key.VS = CKFFShaderKeyVS(vs);
    key.FS = CKFFBuildShaderKeyFS(fs, 0b11u);
    CKFFSpecializationInfo expectedSpec = CKFFBuildSpecializationInfo(key.FS);

    CKDWORD program = cache.GetProgram(key);
    TestCheck(program != 0 &&
              cache.CachedProgramCount() == 1 &&
              ctx.CreatedShaderCount == 2 &&
              ctx.CreatedProgramCount == 1,
              "Full-specialized cache must include a two-stage 3D variant with global specular enabled");
    TestCheck(expectedSpec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 1 &&
              expectedSpec.Get(CKFF_SPEC_GLOBAL_SPECULAR_ENABLED) == 1 &&
              ctx.LastProgramSpecializationDwords[4] == expectedSpec.Data()[4] &&
              ctx.LastProgramSpecializationDwords[6] == expectedSpec.Data()[6] &&
              ctx.LastProgramSpecializationDwords[7] == expectedSpec.Data()[7],
              "Two-stage full-specialized variant must preserve last-stage, specular, and stage1 specialization dwords");

    cache.Shutdown();
}

void Test_FFPShaderCache_FullSpecializedVariantMissDoesNotFallback() {
    ScopedEnvVar forceFullSpecialized("CK2_FFP_UBERSHADER", "0");
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext ctx(&driver);
    CKFFShaderCache cache;
    cache.Init(&ctx);

    CKFFFSStateDesc fs;
    fs.SetSpecularAdd(true);
    fs.SetStageColorOp(0, CKRST_TOP_MODULATE);
    fs.SetStageColorArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageColorArg2(0, CKRST_TA_CURRENT);
    fs.SetStageAlphaOp(0, CKRST_TOP_MODULATE);
    fs.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    fs.SetStageAlphaArg2(0, CKRST_TA_CURRENT);

    CKFFShaderKey key;
    key.VS = CKFFShaderKeyVS();
    key.FS = CKFFBuildShaderKeyFS(fs, 1u);

    CKDWORD program = cache.GetProgram(key);
    TestCheck(program == 0 &&
              cache.CachedProgramCount() == 0 &&
              ctx.CreatedShaderCount == 0 &&
              ctx.CreatedProgramCount == 0,
              "Full-specialized cache miss must fail explicitly without falling back to the uber shader or caching a null variant");

    cache.Shutdown();
}

void Test_FFPShaderCache_VariantKeyDumpIsOptIn() {
    std::string cacheHeader = ReadRenderEngineSource("src/CKFFShaderCache.h");
    std::string cacheSource = ReadRenderEngineSource("src/CKFFShaderCache.cpp");
    TestCheck(cacheSource.find("ReadEnvDword(\"CK2_FFP_VARIANT_LOG_LIMIT\", 0)") != std::string::npos &&
              cacheSource.find("m_VariantLogLimit == 0") != std::string::npos,
              "FFP variant key logging must default to disabled unless CK2_FFP_VARIANT_LOG_LIMIT is set");
    TestCheck(cacheSource.find("#else\r\n    (void)key;\r\n#endif") != std::string::npos ||
              cacheSource.find("#else\n    (void)key;\n#endif") != std::string::npos,
              "FFP variant key logging must compile to a no-op when CKRE_FFP_VARIANT_CAPTURE is disabled");
    TestCheck(cacheSource.find("\\\"vsTexGen\\\"") != std::string::npos &&
              cacheSource.find("\\\"lastActiveTextureStage\\\"") != std::string::npos &&
              cacheSource.find("\\\"globalSpecularEnable\\\"") != std::string::npos &&
              cacheSource.find("\\\"resultIsTemp\\\"") != std::string::npos,
              "FFP variant key logging must emit manifest-ready JSON fields matching ffp_specialized_variants.json");
    TestCheck(cacheHeader.find("size_t CachedProgramCount()") != std::string::npos &&
              cacheHeader.find("VariantStats") != std::string::npos &&
              cacheHeader.find("GetVariant") == std::string::npos,
              "FFP variant key statistics must remain internal rather than becoming public RenderEngine API");
}

void Test_FFPFragmentShader_UsesFFPVariantCommonStageReader() {
    std::string shader = ReadRenderEngineSource("src/shaders/fs_ff_stage.sc");
    std::string common = ReadRenderEngineSource("src/shaders/fs_ff_common.sc");
    std::string cmake = ReadRenderEngineSource("src/CMakeLists.txt");
    std::string constants = ReadRenderEngineSource("src/CKFFConstants.h");
    std::string pipeline = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");

    TestCheck(shader.find("#include \"fs_ff_common.sc\"") != std::string::npos &&
              common.find("CKFFStageParams") != std::string::npos &&
              common.find("ckffReadStageParams") != std::string::npos,
              "Fragment shader must split FFP stage decoding into a common variant helper");
    TestCheck(shader.find("uniform vec4 u_ffSpec[10]") != std::string::npos &&
              common.find("stage < 4 && ckffSpecIsOptimized()") != std::string::npos &&
              common.find("ckffSpecDword(6 + stage)") != std::string::npos &&
              common.find("ckffUnpackSpecArg") != std::string::npos &&
              common.find("ckffSpecLastActiveTextureStage") != std::string::npos &&
              common.find("ckffSpecGlobalSpecularEnabled") != std::string::npos &&
              common.find("CKFF_FULL_SPECIALIZED") != std::string::npos &&
              common.find("CKFF_SPEC_DWORD0") != std::string::npos,
              "Fragment shader must expose the variant specialization reader, global specular bit, and last active stage");
    TestCheck(shader.find("ckffReadStageParams(stage") != std::string::npos &&
              shader.find("stageParams.ResultArg") != std::string::npos &&
              shader.find("stage > ckffSpecLastActiveTextureStage()") != std::string::npos &&
              shader.find("ckffSpecGlobalSpecularEnabled()") != std::string::npos,
              "Fragment shader main loop must consume decoded FFP stage params and variant spec global controls");
    TestCheck(cmake.find("fs_ff_common.sc") != std::string::npos,
              "Shader common helper must participate in the shader generation target dependencies");
    TestCheck(constants.find("u_ffSpec") != std::string::npos &&
              pipeline.find("m_CurrentSpecializationInfo = CKFFBuildSpecializationInfo(shaderKey.FS)") != std::string::npos &&
              pipeline.find("specDwords[i] >> 24") != std::string::npos &&
              pipeline.find("encoder->SetUniform(u.u_ffSpec") != std::string::npos,
              "FFP uniform table must bind the current draw's variant specialization mirror as byte-exact float lanes");
}

void Test_FFPStateDesc_FeedsExplicitVariantKey() {
    std::string descHeader = ReadRenderEngineSource("src/CKFFStateDesc.h");
    std::string keyHeader = ReadRenderEngineSource("src/CKFFShaderKey.h");
    std::string cacheHeader = ReadRenderEngineSource("src/CKFFShaderCache.h");
    std::string cacheSource = ReadRenderEngineSource("src/CKFFShaderCache.cpp");

    TestCheck(descHeader.find("Shader variant lookup uses") != std::string::npos &&
              keyHeader.find("struct CKFFShaderKeyFS") != std::string::npos,
              "State description must document that explicit FFP shader keys drive variant lookup");
    TestCheck(descHeader.find("hash<CKFFStateDesc>") == std::string::npos,
              "State descriptions must not carry unordered_map hash support; shader keys own cache hashing");
    TestCheck(cacheHeader.find("CKFFShaderKey") != std::string::npos ||
              cacheSource.find("CKFFShaderKey") != std::string::npos,
              "FFP shader cache must be wired toward explicit FFP shader keys, not state-desc keyed variants");
}

void Test_FFPStateDesc_StoresFullTextureOpRange() {
    CKFFFSStateDesc desc;
    desc.SetStageColorOp(0, CKRST_TOP_LERP);
    desc.SetStageColorArg0(0, CKRST_TA_TFACTOR);
    desc.SetStageAlphaOp(0, CKRST_TOP_LERP);
    desc.SetStageAlphaArg1(0, CKRST_TA_TEXTURE);
    desc.SetStageAlphaArg2(0, CKRST_TA_CURRENT);
    desc.SetStageResultIsTemp(0, true);
    desc.SetStageColorArg1(7, CKFF_TA_TEXTURE);
    desc.SetStageColorArg2(7, CKFF_TA_CURRENT);
    desc.SetStageAlphaOp(7, CKRST_TOP_MULTIPLYADD);
    desc.SetStageResultIsTemp(7, true);
    desc.SetSpecularAdd(true);
    desc.SetAlphaTestEnabled(true);
    desc.SetFogEnabled(true);
    desc.SetAlphaFunc(VXCMP_GREATEREQUAL);

    TestCheck(desc.GetStageColorOp(0) == CKRST_TOP_LERP,
              "Fragment state desc must preserve 5-bit color texture ops");
    TestCheck(desc.GetStageColorArg0(0) == CKRST_TA_TFACTOR,
              "Fragment state desc must preserve color arg0 for MULTIPLYADD/LERP keying");
    TestCheck(desc.GetStageAlphaOp(0) == CKRST_TOP_LERP,
              "Fragment state desc must preserve 5-bit alpha texture ops");
    TestCheck(desc.GetStageAlphaArg1(0) == CKRST_TA_TEXTURE &&
              desc.GetStageAlphaArg2(0) == CKRST_TA_CURRENT,
              "Fragment state desc must preserve alpha args for specialized FFP keying");
    TestCheck(desc.GetStageResultIsTemp(0),
              "Fragment state desc must preserve per-stage TEMP routing");
    TestCheck(desc.GetStageColorArg1(7) == CKFF_TA_TEXTURE &&
              desc.GetStageColorArg2(7) == CKFF_TA_CURRENT &&
              desc.GetStageAlphaOp(7) == CKRST_TOP_MULTIPLYADD &&
              desc.GetStageResultIsTemp(7),
              "Fragment state desc must preserve stage 7 texture op and routing state");
    TestCheck(desc.GetStageAlphaOp(6) == 0 && !desc.GetStageResultIsTemp(6),
              "Fragment state desc stage storage must not bleed into inactive later stages");
    TestCheck(desc.GetSpecularAdd() && desc.GetAlphaTestEnabled() && desc.GetFogEnabled(),
              "Fragment state desc must preserve global FFP flags alongside stage storage");
    TestCheck(desc.GetAlphaFunc() == VXCMP_GREATEREQUAL,
              "Fragment state desc must preserve alpha func alongside stage storage");
}

void Test_FFPStateDesc_CoversEightTextureCoordinates() {
    CKFFVSStateDesc desc;
    desc.SetHasPositionT(true);
    desc.SetHasTexCoord(7, true);
    desc.SetTexGen(7, CKFF_TG_REFLECTION, true);

    TestCheck(desc.GetHasPositionT(),
              "Vertex state desc PositionT flag must not alias high texture coordinate bits");
    TestCheck(desc.GetHasTexCoord7(),
              "Vertex state desc must expose stage 7 texture coordinates through compatibility accessors");
    TestCheck(desc.GetHasTexCoord(7),
              "Vertex state desc must preserve generic stage 7 texture coordinate state");
    TestCheck(desc.GetTexGenMode(7) == CKFF_TG_REFLECTION && desc.GetTexGenHasTransform(7),
              "Vertex state desc must preserve stage 7 texgen and texture transform state");
    TestCheck(!desc.GetHasTexCoord(8) && desc.GetTexGenMode(8) == 0 && !desc.GetTexGenHasTransform(8),
              "Vertex state desc must reject out-of-range texture coordinate stages");
}

void Test_FFPStateDesc_UsesD3DTextureArgBaseValues() {
    TestCheck(CKFF_TA_DIFFUSE == CKRST_TA_DIFFUSE &&
              CKFF_TA_CURRENT == CKRST_TA_CURRENT &&
              CKFF_TA_TEXTURE == CKRST_TA_TEXTURE &&
              CKFF_TA_TFACTOR == CKRST_TA_TFACTOR &&
              CKFF_TA_SPECULAR == CKRST_TA_SPECULAR &&
              CKFF_TA_TEMP == CKRST_TA_TEMP,
              "Fragment state desc texture args must preserve D3D/Virtools base argument values");
}

void Test_FFPStateDesc_BuildCapturesTempResultRouting() {
    std::string ffp = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    TestCheck(ffp.find("SetStageResultIsTemp(stage") != std::string::npos &&
              ffp.find("GetStageResultArg(m_StageStates[stage])") != std::string::npos &&
              ffp.find("CKRST_TA_TEMP") != std::string::npos,
              "BuildCurrentStateDesc must capture RESULTARG0 TEMP routing in the fragment state description");
}

void Test_TextureOpFallback_EvaluatesAsExplicitModulate() {
    VxColor arg0(0.9f, 0.8f, 0.7f, 0.6f);
    VxColor arg1(0.2f, 0.4f, 0.6f, 0.8f);
    VxColor arg2(0.5f, 0.25f, 0.75f, 0.5f);
    VxColor current(0.1f, 0.2f, 0.3f, 0.4f);
    VxColor diffuse(0.3f, 0.3f, 0.3f, 0.3f);
    VxColor texture(0.4f, 0.4f, 0.4f, 0.4f);

    TestCheck(CKFFClassifyTextureOpCoverage(CKRST_TOP_PREMODULATE) == CKFF_COVERAGE_FALLBACK,
              "PREMODULATE must be deliberately classified before using fallback evaluation");
    TestColorClose(CKFFEvaluateTextureOp(CKRST_TOP_PREMODULATE, arg0, arg1, arg2, current, diffuse, texture),
                   arg1 * arg2,
                   "PREMODULATE fallback must be the explicit arg1 * arg2 compatibility path");

    std::string shader = ReadRenderEngineSource("src/shaders/fs_ff_stage.sc");
    TestCheck(shader.find("return a * b;") != std::string::npos,
              "Fragment shader fallback must remain an explicit arg1 * arg2 compatibility path");
}

void Test_PatchMesh_BasicMeshConversionIsImplemented() {
    std::string source = ReadRenderEngineSource("src/CKPatchMesh.cpp");
    const size_t fromPos = source.find("CKERROR RCKPatchMesh::FromMesh");
    const size_t toPos = source.find("CKERROR RCKPatchMesh::ToMesh");
    TestCheck(fromPos != std::string::npos && toPos != std::string::npos,
              "PatchMesh conversion functions must be present");
    const size_t nextPos = source.find("void RCKPatchMesh::SetIterationCount", toPos);
    TestCheck(nextPos != std::string::npos, "PatchMesh conversion source range must be bounded");
    std::string body = source.substr(fromPos, nextPos - fromPos);
    TestCheck(body.find("CKERR_NOTIMPLEMENTED") == std::string::npos,
              "PatchMesh FromMesh/ToMesh must no longer report NOTIMPLEMENTED for all calls");
    TestCheck(body.find("SetVertVecCount(vertexCount, faceCount * 9)") != std::string::npos &&
              body.find("CK_PATCH_TRI") != std::string::npos,
              "FromMesh must build planar triangle patch control data from ordinary mesh faces");
    TestCheck(body.find("BuildRenderMesh()") != std::string::npos &&
              body.find("CopyMeshGeometry(m, this)") != std::string::npos,
              "ToMesh must tessellate patch data and copy the generated mesh to the target");
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

void Test_CKVertexBuffer_TracksDirtyRangeAndUsesHardwareWhenSafe() {
    std::string source = ReadRenderEngineSource("src/CKVertexBuffer.cpp");
    TestCheck(source.find("m_LockedStart = StartVertex") != std::string::npos &&
              source.find("m_LockedCount = VertexCount") != std::string::npos,
              "CKVertexBuffer Lock must record the locked vertex range");
    TestCheck(source.find("UpdateVertexBuffer(m_ObjectIndex, updateStart * stride") != std::string::npos,
              "CKVertexBuffer Unlock must update dirty hardware VB byte ranges");
    TestCheck(source.find("CK_LOCK_DISCARD") != std::string::npos &&
              source.find("formatFlags != m_FormatFlags") != std::string::npos &&
              source.find("layout != m_VertexLayout") != std::string::npos,
              "CKVertexBuffer Unlock must rebuild only on discard or layout changes");
    TestCheck(source.find("DrawVertexBuffer(") != std::string::npos &&
              source.find("!Indices") != std::string::npos,
              "CKVertexBuffer Draw must use the hardware VB path for safe non-indexed draws");
    TestCheck(source.find("VXRENDERSTATE_WRAP0") != std::string::npos &&
              source.find("VXRENDERSTATE_POINTSPRITEENABLE") != std::string::npos,
              "CKVertexBuffer Draw must fall back when per-triangle wrap or point-sprite expansion is required");
}

void Test_MeshIndexBuffer_ReusesKnownCapacity() {
    std::string header = ReadRenderEngineSource("include/RCKMesh.h");
    TestCheck(header.find("m_IndexBufferIndexCount") != std::string::npos,
              "Mesh hardware index buffers must track capacity without relying on removed v1 readback APIs");

    std::string source = ReadRenderEngineSource("src/CKMesh.cpp");
    TestCheck(source.find("Phase 1 stub: GetIndexBufferData removed") == std::string::npos,
              "Mesh index buffer setup must not keep the v2 migration stub");
    TestCheck(source.find("CKBOOL needResize = (m_IndexBufferIndexCount < totalIndexCount)") != std::string::npos,
              "Mesh index buffer setup must resize only when the cached capacity is insufficient");
    TestCheck(source.find("m_IndexBufferIndexCount = totalIndexCount") != std::string::npos &&
              source.find("m_IndexBufferIndexCount = 0") != std::string::npos,
              "Mesh index buffer setup must maintain capacity on create and clear it on failures");
}

void Test_PointSprite_PointListExpandsToTexturedQuads() {
    std::string ffp = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    TestCheck(ffp.find("VXRENDERSTATE_POINTSPRITEENABLE") != std::string::npos &&
              ffp.find("VX_TRIANGLELIST") != std::string::npos,
              "Point sprite draws must submit expanded quads as triangle lists");

    std::string transient = ReadRenderEngineSource("src/CKTransientGeometry.cpp");
    TestCheck(transient.find("pointSprites && primType == VX_POINTLIST") != std::string::npos,
              "Transient geometry must detect point-list point sprites");
    TestCheck(transient.find("spriteVertexCount = vertexCount * 4") != std::string::npos &&
              transient.find("spriteIndexCount = vertexCount * 6") != std::string::npos,
              "Each point sprite must expand to four vertices and six indices");
}

void Test_PointSprite_UsesD3DPointScaleAndCameraFacingAxes() {
    float scaled = CKTransientGeometry::ComputePointSpriteSizeForDistance(
        8.0f, 1.0f, 64.0f, TRUE, 0.0f, 1.0f, 0.0f, 4.0f);
    TestCheck(fabs(scaled - 4.0f) < 0.0001f,
              "Point scale helper must use size / sqrt(A + B*d + C*d*d)");
    scaled = CKTransientGeometry::ComputePointSpriteSizeForDistance(
        8.0f, 1.0f, 64.0f, TRUE, -1.0f, 0.0f, 0.0f, 4.0f);
    TestCheck(fabs(scaled - 8.0f) < 0.0001f,
              "Point scale helper must ignore invalid denominators instead of producing NaN");
    scaled = CKTransientGeometry::ComputePointSpriteSizeForDistance(
        100.0f, 2.0f, 16.0f, FALSE, 1.0f, 0.0f, 0.0f, 0.0f);
    TestCheck(fabs(scaled - 16.0f) < 0.0001f,
              "Point scale helper must clamp to POINTSIZE_MAX");
    scaled = CKTransientGeometry::ComputePointSpriteSizeForDistance(
        0.0f, 2.0f, 16.0f, FALSE, 1.0f, 0.0f, 0.0f, 0.0f);
    TestCheck(fabs(scaled - 2.0f) < 0.0001f,
              "Point scale helper must apply D3D-style default size before POINTSIZE_MIN clamp");

    std::string drawState = ReadRenderEngineSource("src/CKDrawStateCache.cpp");
    TestCheck(drawState.find("VXRENDERSTATE_POINTSCALE_A") != std::string::npos &&
              drawState.find("VXRENDERSTATE_POINTSCALE_B") != std::string::npos &&
              drawState.find("VXRENDERSTATE_POINTSCALE_C") != std::string::npos,
              "Draw-state defaults must initialize D3D point-scale coefficients");

    std::string ffp = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    TestCheck(ffp.find("VXRENDERSTATE_POINTSIZE_MIN") != std::string::npos &&
              ffp.find("VXRENDERSTATE_POINTSIZE_MAX") != std::string::npos &&
              ffp.find("VXRENDERSTATE_POINTSCALEENABLE") != std::string::npos,
              "FFP draw must pass point size clamp and attenuation state to transient geometry");

    std::string transient = ReadRenderEngineSource("src/CKTransientGeometry.cpp");
    TestCheck(transient.find("denomSq = scaleA + scaleB * distance + scaleC * distance * distance") != std::string::npos &&
              transient.find("if (denomSq > 0.000001f)") != std::string::npos &&
              transient.find("sqrtf(denomSq)") != std::string::npos,
              "Point sprite size must use the D3D distance attenuation formula without producing NaN for invalid denominators");
    TestCheck(transient.find("Vx3DInverseMatrix(invWorld") != std::string::npos &&
              transient.find("cameraRightLocal") != std::string::npos &&
              transient.find("cameraUpLocal") != std::string::npos,
              "3D point sprites must expand using camera-facing axes transformed into local space");
}

void Test_CopyToVideo_TextureStageSnapshotsPreserveMatrices() {
    std::string header = ReadRenderEngineSource("src/CKFixedFunctionPipeline.h");
    TestCheck(header.find("struct CKFFTextureStageSnapshot") != std::string::npos &&
              header.find("VxMatrix TextureMatrix") != std::string::npos,
              "Texture stage snapshots must preserve texture matrices as well as stage states");

    std::string ffp = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    TestCheck(ffp.find("void CKFixedFunctionPipeline::SaveTextureStage") != std::string::npos &&
              ffp.find("snapshot.TextureMatrix = m_TexMatrix[stage]") != std::string::npos,
              "SaveTextureStage must capture the complete stage contract");
    TestCheck(ffp.find("void CKFixedFunctionPipeline::RestoreTextureStage") != std::string::npos &&
              ffp.find("m_TexMatrix[stage] = snapshot.TextureMatrix") != std::string::npos,
              "RestoreTextureStage must restore texture matrix state after temporary blits");
}

void Test_ViewportClip_RestoresFFPProjectionAndViewport() {
    std::string context = ReadRenderEngineSource("src/CKRenderContext.cpp");
    TestCheck(context.find("m_FFPipeline.SetViewport(m_ViewportData);") != std::string::npos,
              "RenderContext viewport changes must update the FFP viewport uniform");

    std::string sceneGraph = ReadRenderEngineSource("src/CKSceneGraph.cpp");
    TestCheck(sceneGraph.find("SetProjectionTransformationMatrix(rc->m_ProjectionMatrix)") != std::string::npos &&
              sceneGraph.find("SetProjectionTransformationMatrix(dev->m_ProjectionMatrix)") != std::string::npos,
              "Scene graph viewport clipping must restore the render-context projection after clipped traversal");
}

void Test_FrustumVisibility_UsesProjectedBoundingBoxes() {
    std::string header = ReadRenderEngineSource("include/RCKRenderContext.h");
    TestCheck(header.find("ComputeBoxVisibility") != std::string::npos,
              "RenderContext must expose a CPU box projection helper for frustum tests");

    std::string context = ReadRenderEngineSource("src/CKRenderContext.cpp");
    TestCheck(context.find("VxTransformBox2D") != std::string::npos &&
              context.find("allOr") != std::string::npos &&
              context.find("allAnd") != std::string::npos,
              "ComputeBoxVisibility must project bbox corners and classify inside/intersect/outside");
    TestCheck(context.find("m_Frustum.Classify(box, world)") != std::string::npos &&
              context.find("frustumClass > 0.0f") != std::string::npos,
              "ComputeBoxVisibility must use 3D frustum plane classification for reject decisions");
    TestCheck(context.find("if (!box.IsValid())\n        return 1;") != std::string::npos ||
              context.find("if (!box.IsValid())\r\n        return 1;") != std::string::npos,
              "Invalid or unknown bounds must remain potentially visible instead of culling objects");
    TestCheck(context.find("if (!visible)") != std::string::npos &&
              context.find("*extents = screen;") != std::string::npos,
              "2D box projection failure must not override a positive 3D frustum classification");
    std::string entity = ReadRenderEngineSource("src/CK3dEntity.cpp");
    TestCheck(entity.find("Phase 1 stub: always visible") == std::string::npos,
              "3D entity frustum tests must not keep the always-visible stub");
    TestCheck(entity.find("ComputeBoxVisibility(m_LocalBoundingBox") != std::string::npos &&
              entity.find("m_SceneGraphNode->m_Bbox") != std::string::npos,
              "3D entity and hierarchical visibility must use projected bbox visibility");
    TestCheck(entity.find("!m_SceneGraphNode->ComputeHierarchicalBox() || !m_SceneGraphNode->IsHierarchyBoxValid()") != std::string::npos,
              "Hierarchy frustum tests must not reject subtrees when their bbox is unknown");
    std::string sprite = ReadRenderEngineSource("src/CKSprite3D.cpp");
    TestCheck(sprite.find("ComputeBoxVisibility(m_LocalBoundingBox") != std::string::npos,
              "Sprite3D frustum tests must use the sprite local bbox projection");
}

void Test_LegacySetVertexShaderResetCommentsAreGone() {
    const char *files[] = {
        "src/CK2dEntity.cpp",
        "src/CK3dEntity.cpp",
        "src/CKMesh.cpp",
        "src/CKSprite3D.cpp"
    };
    for (const char *file : files) {
        std::string source = ReadRenderEngineSource(file);
        TestCheck(source.find("SetVertexShader(0)") == std::string::npos,
                  "Legacy SetVertexShader(0) migration comments must not remain as fake TODOs");
    }
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

    std::string driverSource = ReadRenderEngineSource("src/CKRasterizer/CKBgfxRasterizerDriver.cpp");
    TestCheck(driverSource.find("Public legacy shader compilation is intentionally unsupported") != std::string::npos,
              "The bgfx driver must explicitly document public legacy shader target support as unsupported");

    std::string cacheSource = ReadRenderEngineSource("src/CKFFShaderCache.cpp");
    TestCheck(cacheSource.find("not the public Virtools legacy shader-target API") != std::string::npos,
              "The FFP shader cache must document that internal shader blobs do not enable the public legacy shader API");
}

void Test_FFPDebugLogging_IsCentralized() {
    std::string pipeline = ReadRenderEngineSource("src/CKFixedFunctionPipeline.cpp");
    std::string header = ReadRenderEngineSource("src/CKFFDebug.h");
    std::string source = ReadRenderEngineSource("src/CKFFDebug.cpp");
    std::string cmake = ReadRenderEngineSource("src/CMakeLists.txt");

    TestCheck(pipeline.find("DebugDrawLogLimit") == std::string::npos &&
              pipeline.find("DebugReal3DLogLimit") == std::string::npos &&
              pipeline.find("Debug3DContractLogLimit") == std::string::npos &&
              pipeline.find("DebugPositionTLogLimit") == std::string::npos &&
              pipeline.find("ShouldSkipDebug") == std::string::npos,
              "FFP pipeline must not keep debug env limit or skip helpers inline");
    TestCheck(pipeline.find("LogVertexClipSamples") == std::string::npos &&
              pipeline.find("LogPositionTSamples") == std::string::npos &&
              pipeline.find("LogPrimitiveIndexContract") == std::string::npos &&
              pipeline.find("LogMatrixRows") == std::string::npos,
              "FFP pipeline must delegate verbose debug sample logging to CKFFDebug");
    TestCheck(pipeline.find("void CKFixedFunctionPipeline::BeginDebugFrame()") != std::string::npos &&
              pipeline.find("m_DebugState.BeginFrame()") != std::string::npos,
              "BeginDebugFrame must remain as the public render-frame hook and delegate to debug state");
    TestCheck(header.find("struct CKFFDebugConfig") != std::string::npos &&
              header.find("class CKFFDebugState") != std::string::npos &&
              header.find("struct CKFFDrawDebugInfo") != std::string::npos,
              "CKFFDebug must expose the internal config/state/draw-info types for FFP logging");

    const char *envNames[] = {
        "CK2_3D_DEBUG_DRAW_LOG_LIMIT",
        "CK2_3D_DEBUG_REAL3D_LOG_LIMIT",
        "CK2_3D_DEBUG_3D_CONTRACT_LOG_LIMIT",
        "CK2_3D_DEBUG_POSITIONT_LOG_LIMIT",
        "CK2_3D_DEBUG_DRAW_SERIAL_PER_FRAME",
        "CK2_3D_DEBUG_SKIP_POSITIONT_DRAWS",
        "CK2_3D_DEBUG_SKIP_3D_DRAWS",
        "CK2_3D_DEBUG_FORCE_UNLIT",
        "CK2_3D_DEBUG_DISABLE_FOG",
        "CK2_3D_DEBUG_SKIP_OPAQUE3D_DRAWS",
        "CK2_3D_DEBUG_SKIP_TRANSPARENT3D_DRAWS",
        "CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_EXACT",
        "CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_START",
        "CK2_3D_DEBUG_ONLY_OPAQUE3D_DRAW_END",
        "CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_EXACT",
        "CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_START",
        "CK2_3D_DEBUG_ONLY_TRANSPARENT3D_DRAW_END"
    };
    for (const char *envName : envNames) {
        TestCheck(source.find(envName) != std::string::npos,
                  "CKFFDebug.cpp must keep the existing FFP debug env interface names");
    }

    TestCheck(cmake.find("CKFFDebug.h") != std::string::npos &&
              cmake.find("CKFFDebug.cpp") != std::string::npos,
              "CKFFDebug must be part of the RenderEngine internal build inputs");
}

void Test_DebugLogger_HasGlobalOutputDisableOption() {
    std::string header = ReadRenderEngineSource("src/CKDebugLogger.h");
    std::string source = ReadRenderEngineSource("src/CKDebugLogger.cpp");
    std::string rootCmake = ReadRenderEngineSource("CMakeLists.txt");
    std::string srcCmake = ReadRenderEngineSource("src/CMakeLists.txt");

    TestCheck(header.find("void EnableOutput(bool enable)") != std::string::npos &&
              header.find("bool m_OutputEnabled") != std::string::npos,
              "Debug logger must expose an internal master output switch");
    TestCheck(rootCmake.find("option(CKRE_DEBUG_OUTPUT") != std::string::npos &&
              srcCmake.find("CKRE_DEBUG_OUTPUT_DEFAULT=$<IF:$<BOOL:${CKRE_DEBUG_OUTPUT}>,1,0>") != std::string::npos,
              "RenderEngine CMake must expose CKRE_DEBUG_OUTPUT and pass it as the debug logger default");
    TestCheck(source.find("CK2_3D_DEBUG_OUTPUT") != std::string::npos &&
              source.find("CKRE_DEBUG_OUTPUT_DEFAULT") != std::string::npos &&
              source.find("_stricmp(value, \"false\")") != std::string::npos &&
              source.find("_stricmp(value, \"off\")") != std::string::npos &&
              source.find("_stricmp(value, \"no\")") != std::string::npos,
              "Debug logger must use the CMake default and support CK2_3D_DEBUG_OUTPUT=0/false/off/no as a runtime override");
    TestCheck(source.find("if (!m_OutputEnabled)") != std::string::npos &&
              source.find("fclose(m_File)") != std::string::npos,
              "Debug logger must drop output and close the log file when globally disabled");

    const char *logPath = "ffp_contract_debug_logger_silenced.log";
    std::remove(logPath);
    CKDebugLogger::Instance().SetLogFilePath(logPath);
    CKDebugLogger::Instance().EnableDebuggerOutput(false);
    CKDebugLogger::Instance().EnableFileOutput(true);
    CKDebugLogger::Instance().EnableOutput(false);
    CKDebugLogger::Instance().Log("this message must not create a file");
    CKDebugLogger::Instance().Flush();
    FILE *file = nullptr;
    fopen_s(&file, logPath, "r");
    TestCheck(file == nullptr,
              "Disabled debug logger output must not create or append the log file");
    if (file)
        fclose(file);

    CKDebugLogger::Instance().EnableOutput(true);
    CKDebugLogger::Instance().Log("this message should create a file");
    CKDebugLogger::Instance().Flush();
    CKDebugLogger::Instance().EnableFileOutput(false);
    fopen_s(&file, logPath, "r");
    TestCheck(file != nullptr,
              "Re-enabled debug logger output must resume file logging");
    if (file)
        fclose(file);
    std::remove(logPath);
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
    tests.Run("Sprite CopyContext clamps destination region", &Test_SpriteCopyContext_ClampsDestinationRegion);
    tests.Run("CopyToVideo uploads backbuffer through scratch texture", &Test_CopyToVideo_UploadsBackbufferThroughScratchTexture);
    tests.Run("2D entity Draw uses FFP state guard", &Test_2dEntity_DrawUsesFFPStateGuard);
    tests.Run("CopyToVideo texture stage snapshots", &Test_CopyToVideo_TextureStageSnapshotsPreserveMatrices);
    tests.Run("Render target validates cube faces and preserves depth", &Test_RenderTarget_ValidatesCubeFacesAndPreservesDepth);
    tests.Run("Render pipeline render-first view order", &Test_RenderPipeline_RenderFirstViewPrecedesOpaqueScene);
    tests.Run("Interleave ignores undeclared color streams", &Test_Interleave_IgnoresColorPointersWithoutDPFlags);
    tests.Run("Interleave PositionT missing specular fog factor", &Test_Interleave_PositionTMissingSpecularKeepsFogFactorOpaque);
    tests.Run("User draw primitive PositionT specular reset", &Test_UserDrawPrimitive_PositionTSpecularDefaultsOpaqueOnReuse);
    tests.Run("Draw state default material sources", &Test_DrawState_DefaultMaterialSourcesMatchFFP);
    tests.Run("Draw state fixes BOTHSRCALPHA blend pair", &Test_DrawState_FixesBothSrcAlphaBlendPair);
    tests.Run("Draw state fixes legacy screen blend pair", &Test_DrawState_FixesLegacyScreenBlendPair);
    tests.Run("Draw state inverse winding swaps cull", &Test_DrawState_InverseWindingSwapsCullDirection);
    tests.Run("Draw state color write mask", &Test_DrawState_ColorWriteMaskCanDisableColorWrites);
    tests.Run("Draw state stencil render states", &Test_DrawState_StencilRenderStatesBuildStencilOps);
    tests.Run("Texture blend legacy stage-op mapping", &Test_TextureBlend_LegacyModesMapToFFPStageOps);
    tests.Run("Texture blend MODULATEALPHA Virtools DX9 contract", &Test_TextureBlend_ModulateAlphaMatchesVirtoolsDx9Modulate);
    tests.Run("Texture blend shader op formulas", &Test_TextureBlend_ShaderOpsMatchFFPFormulas);
    tests.Run("Texture stage blend multiplicative channel", &Test_TextureStageBlend_MultiplicativeChannelMapsToModulate);
    tests.Run("Texture stage blend unsupported fallback", &Test_TextureStageBlend_UnsupportedBlendRefusesMonoPass);
    tests.Run("Material channels clamp and clear FFP stages", &Test_MaterialChannels_ClampToFFPStageCapacityAndClearAfterDraw);
    tests.Run("Mesh depth/stencil-only draws use FFP state guard", &Test_Mesh_DepthStencilOnlyDrawsUseFFPStateGuard);
    tests.Run("Mesh default render restores scene specular", &Test_Mesh_DefaultRenderRestoresSceneSpecularState);
    tests.Run("Material channels no base texture keeps stage chain", &Test_MaterialChannels_NoBaseTextureKeepsStageChainAlive);
    tests.Run("Texture stage reset clears effect state", &Test_TextureStage_ResetClearsLeakedEffectState);
    tests.Run("FFP diagnostic specular state follows draw order", &Test_FFPDiagnosticHarness_SpecularStateFollowsDrawOrder);
    tests.Run("FFP diagnostic PositionT default COLOR1", &Test_FFPDiagnosticHarness_PositionTDefaultColor1KeepsRGBBlack);
    tests.Run("FFP diagnostic blend fixup reaches draw state", &Test_FFPDiagnosticHarness_BlendFixupReachesSubmittedDrawState);
    tests.Run("FFP diagnostic texture stage reset reaches uniforms", &Test_FFPDiagnosticHarness_TextureStageResetReachesUniforms);
    tests.Run("FFP diagnostic texture matrix reset reaches uniforms", &Test_FFPDiagnosticHarness_TextureMatrixResetReachesUniforms);
    tests.Run("FFP state guard restores render and texture state", &Test_FFPStateGuard_RestoresRenderAndTextureState);
    tests.Run("FFP render state guard restores scoped state", &Test_FFPRenderStateGuard_RestoresOnlyScopedState);
    tests.Run("3D entity indirect matrix uses render state guard", &Test_3dEntity_IndirectMatrixUsesRenderStateGuard);
    tests.Run("Sprite3D batches use FFP state guard", &Test_Sprite3DBatches_UseFFPStateGuard);
    tests.Run("Bump env stage states pack uniform", &Test_BumpEnv_StageStatesPackUniform);
    tests.Run("Material effects use eight-stage FFP contract", &Test_MaterialEffects_UseEightStageFFPContract);
    tests.Run("Shader evaluator DOT3 matches D3D FFP", &Test_ShaderEvaluator_Dot3MatchesD3DFFP);
    tests.Run("Shader evaluator FFP stage ops", &Test_ShaderEvaluator_CoversFFPStageOps);
    tests.Run("Texture op evaluator FFP formulas", &Test_TextureOpEvaluator_MatchesFFPFormulas);
    tests.Run("Texture op coverage classifies known FFP ops", &Test_TextureOpCoverage_ClassifiesEveryKnownFFPOp);
    tests.Run("Shader semantic coverage classifies known FFP semantics", &Test_ShaderSemanticCoverage_ClassifiesKnownFFPSemantics);
    tests.Run("FFP shader key fixed-function stage rules", &Test_FFPShaderKey_UsesFixedFunctionStageRules);
    tests.Run("FFP specialization info variant layout", &Test_FFPSpecializationInfo_MatchesFFPVariantLayout);
    tests.Run("FFP shader cache variant contract", &Test_FFPShaderCache_UsesKeyedFFPVariantContract);
    tests.Run("FFP shader cache full specialized 3D variant hit", &Test_FFPShaderCache_FullSpecialized3DVariantHit);
    tests.Run("FFP shader cache full specialized PositionT variant hit", &Test_FFPShaderCache_FullSpecializedPositionTVariantHit);
    tests.Run("FFP shader cache full specialized two-stage specular variant hit", &Test_FFPShaderCache_FullSpecializedTwoStageSpecularVariantHit);
    tests.Run("FFP shader cache full specialized variant miss", &Test_FFPShaderCache_FullSpecializedVariantMissDoesNotFallback);
    tests.Run("FFP shader cache variant key dump opt-in", &Test_FFPShaderCache_VariantKeyDumpIsOptIn);
    tests.Run("FFP fragment shader variant common reader", &Test_FFPFragmentShader_UsesFFPVariantCommonStageReader);
    tests.Run("FFP state desc feeds explicit variant key", &Test_FFPStateDesc_FeedsExplicitVariantKey);
    tests.Run("FFP state desc stores full texture op range", &Test_FFPStateDesc_StoresFullTextureOpRange);
    tests.Run("FFP state desc covers eight texture coordinates", &Test_FFPStateDesc_CoversEightTextureCoordinates);
    tests.Run("FFP state desc uses D3D texture arg values", &Test_FFPStateDesc_UsesD3DTextureArgBaseValues);
    tests.Run("FFP state desc captures TEMP result routing", &Test_FFPStateDesc_BuildCapturesTempResultRouting);
    tests.Run("Texture op fallback evaluates as explicit modulate", &Test_TextureOpFallback_EvaluatesAsExplicitModulate);
    tests.Run("PatchMesh basic mesh conversion implemented", &Test_PatchMesh_BasicMeshConversionIsImplemented);
    tests.Run("User draw primitive VBUFFER flag observable", &Test_UserDrawPrimitive_VBufferFlagIsObservable);
    tests.Run("CKVertexBuffer dirty range and hardware path", &Test_CKVertexBuffer_TracksDirtyRangeAndUsesHardwareWhenSafe);
    tests.Run("Mesh index buffer reuses capacity", &Test_MeshIndexBuffer_ReusesKnownCapacity);
    tests.Run("Point sprite point-list expansion", &Test_PointSprite_PointListExpandsToTexturedQuads);
    tests.Run("Point sprite D3D point scale and camera-facing axes", &Test_PointSprite_UsesD3DPointScaleAndCameraFacingAxes);
    tests.Run("Viewport clip restores FFP projection and viewport", &Test_ViewportClip_RestoresFFPProjectionAndViewport);
    tests.Run("Frustum visibility uses projected bounding boxes", &Test_FrustumVisibility_UsesProjectedBoundingBoxes);
    tests.Run("Legacy SetVertexShader reset comments removed", &Test_LegacySetVertexShaderResetCommentsAreGone);
    tests.Run("Shader target profiles", &Test_ShaderTarget_ProfilesAreExplicitAndDistinct);
    tests.Run("Driver default shader target", &Test_RasterizerDriver_DefaultShaderTargetIsUnknown);
    tests.Run("FFP debug logging is centralized", &Test_FFPDebugLogging_IsCentralized);
    tests.Run("Debug logger global output disable option", &Test_DebugLogger_HasGlobalOutputDisableOption);
    return tests.ExitCode();
}
