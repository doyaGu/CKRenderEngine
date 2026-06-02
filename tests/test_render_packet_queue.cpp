#include <stdio.h>

#include "CKFixedFunctionPipeline.h"
#include "FFPDiagnosticHarness.h"

#include <cstring>
#include <math.h>

void SetupPacketPipeline(CKFixedFunctionPipeline *ffp,
                         FFPDiagnosticContext *context,
                         FFPDiagnosticDriver *driver)
{
    (void)driver;
    ffp->Init(context);
    ffp->SetOpaqueSortingEnabled(TRUE);
    ffp->SetOpaqueInstancingEnabled(FALSE);
    ffp->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ffp->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ffp->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
}

void DrawPacketCandidate(CKFixedFunctionPipeline *ffp,
                         FFPDiagnosticContext *context,
                         CKRenderView view,
                         CKDWORD vb,
                         CKDWORD ib)
{
    ffp->DrawVertexBuffer(&context->Encoder, view, VX_TRIANGLELIST,
                          vb, ib,
                          0, 3,
                          0, 3,
                          CKRST_DP_TRANSFORM,
                          CKFF_VF_POSITION,
                          77);
}

void DrawPacketCandidateWithFormat(CKFixedFunctionPipeline *ffp,
                                   FFPDiagnosticContext *context,
                                   CKRenderView view,
                                   CKDWORD vb,
                                   CKDWORD ib,
                                   CKDWORD formatFlags)
{
    ffp->DrawVertexBuffer(&context->Encoder, view, VX_TRIANGLELIST,
                          vb, ib,
                          0, 3,
                          0, 3,
                          CKRST_DP_TRANSFORM,
                          formatFlags,
                          77);
}

void DrawPacketChurnCandidate(CKFixedFunctionPipeline *ffp,
                              FFPDiagnosticContext *context,
                              int index)
{
    const CKDWORD textureFlags = (index & 1)
        ? (CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP)
        : CKRST_TEXTURE_VALID;
    ffp->SetRenderState(VXRENDERSTATE_CULLMODE,
                        (index & 1) ? VXCULL_NONE : VXCULL_CCW);
    ffp->SetTexture(0, 3000 + (CKDWORD)index, textureFlags);
    DrawPacketCandidateWithFormat(ffp, context, CKRP_VIEW_OPAQUE3D,
                                  1000 + (CKDWORD)index,
                                  2000 + (CKDWORD)index,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
}

void DrawPacketUniqueBindingCandidate(CKFixedFunctionPipeline *ffp,
                                      FFPDiagnosticContext *context,
                                      int index)
{
    ffp->SetTexture(0, 3000 + (CKDWORD)index, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(ffp, context, CKRP_VIEW_OPAQUE3D,
                                  1000 + (CKDWORD)index,
                                  2000 + (CKDWORD)index,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
}

void SetPacketWorld(CKFixedFunctionPipeline *ffp, float x)
{
    VxMatrix world;
    world.Identity();
    world[3][0] = x;
    ffp->SetTransform(VXMATRIX_WORLD, world);
}

void SetPacketComplexWorld(CKFixedFunctionPipeline *ffp)
{
    VxMatrix world;
    world.Identity();
    world[0][0] = 0.0f;
    world[0][1] = 2.0f;
    world[1][0] = -3.0f;
    world[1][1] = 0.0f;
    world[2][2] = 4.0f;
    world[3][0] = 5.0f;
    world[3][1] = -7.0f;
    world[3][2] = 11.0f;
    ffp->SetTransform(VXMATRIX_WORLD, world);
}

void SetPacketProjection(CKFixedFunctionPipeline *ffp)
{
    VxMatrix projection;
    projection.Identity();
    projection[0][0] = 1.5f;
    projection[1][1] = 0.5f;
    projection[2][2] = 2.0f;
    projection[3][0] = 1.0f;
    ffp->SetTransform(VXMATRIX_PROJECTION, projection);
}

CKBOOL PacketMatrixAlmostEqual(const VxMatrix &a, const VxMatrix &b)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (fabs(a[r][c] - b[r][c]) > 0.0001f)
                return FALSE;
        }
    }
    return TRUE;
}

void PrepareTexturedPacketCandidate(CKFixedFunctionPipeline *ffp)
{
    ffp->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffp->SetTexture(0, 3000, CKRST_TEXTURE_VALID);
}

void DrawTexturedPacketCandidate(CKFixedFunctionPipeline *ffp,
                                 FFPDiagnosticContext *context)
{
    DrawPacketCandidateWithFormat(ffp, context, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  0);
}

void OpaqueVertexBufferDrawStaysImmediateByDefault()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    ffp.Init(&context);
    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Opaque sorting must be opt-in and stay immediate by default");
    TestCheck(context.Encoder.SubmitCount == 1,
              "Default opaque draw path must submit immediately");

    ffp.Shutdown();
}

void OpaqueVertexBufferDrawQueuesUntilFlush()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    TestCheck(ffp.HasOpaqueRenderPackets(),
              "Opaque HW vertex-buffer draw must be queued");
    TestCheck(context.Encoder.SubmitCount == 0,
              "Queued opaque draw must not submit before flush");

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Opaque packet queue must be empty after flush");
    TestCheck(context.Encoder.SubmitCount == 1,
              "Flushing one packet must submit one draw");

    ffp.Shutdown();
}

void OpaquePacketFlushSortsAndSkipsRepeatedBufferBinding()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 300, 400);
    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 300, 400);

    TestCheck(context.Encoder.SubmitCount == 0,
              "Opaque packets must remain deferred before flush");

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 3,
              "Flushing three packets must submit three draws");
    TestCheck(context.Encoder.VertexBufferSetCount == 3,
              "Small opaque packet queues must skip sorting and preserve original order");
    TestCheck(context.Encoder.VertexBufferOrder[0] == 300 &&
              context.Encoder.VertexBufferOrder[1] == 100 &&
              context.Encoder.VertexBufferOrder[2] == 300,
              "Small queue sort-skip path must preserve stable submission order");

    ffp.Shutdown();
}

void LargeOpaquePacketFlushSortsAndSkipsRepeatedBufferBinding()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    for (int i = 0; i < 80; ++i) {
        const CKDWORD vb = (i & 1) ? 100 : 300;
        const CKDWORD ib = (i & 1) ? 200 : 400;
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, vb, ib);
    }

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 80,
              "Flushing sorted packet queue must submit every draw");
    TestCheck(context.Encoder.VertexBufferSetCount == 2,
              "Large opaque queue must sort and skip repeated VB binding");
    TestCheck(context.Encoder.VertexBufferOrder[0] == 100 &&
              context.Encoder.VertexBufferOrder[1] == 300,
              "Large opaque packet sort must group by VB after shared state keys");

    ffp.Shutdown();
}

void OpaquePacketSortUsesStaticUniformsForRuns()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    for (int i = 0; i < 80; ++i) {
        const CKDWORD vb = (i & 1) ? 100 : 300;
        const CKDWORD ib = (i & 1) ? 200 : 400;
        SetPacketWorld(&ffp, (float)i);
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, vb, ib);
    }

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 80,
              "Flushing different-world packets must still submit each draw");
    TestCheck(context.Encoder.VertexBufferSetCount == 2,
              "Static uniform sort key must allow same VB packets with different worlds to form a run");
    TestCheck(context.Encoder.VertexBufferOrder[0] == 100 &&
              context.Encoder.VertexBufferOrder[1] == 300,
              "Static uniform sort key must group by VB after shared static state");
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &stats = ffp.GetFrameStats();
    TestCheck(stats.RenderPacketRuns == 2,
              "Flush diagnostics must report static-key packet runs");
    TestCheck(stats.RenderPacketMaxRunLength == 40,
              "Flush diagnostics must report the largest static-key run");
#endif

    ffp.Shutdown();
}

void OpaquePacketReplaySplitsStaticAndObjectUniforms()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    context.Encoder.MatrixUniforms.insert(ffp.GetShaderCache().GetUniforms().u_ffMatrices);

    SetPacketWorld(&ffp, 1.0f);
    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    SetPacketWorld(&ffp, 2.0f);
    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitFlags[0] == CKRST_DISCARD_NONE &&
              context.Encoder.SubmitFlags[1] == CKRST_DISCARD_ALL,
              "Packet replay must retain bindings until the final draw discards them");
    TestCheck(context.Encoder.MatrixUniformSetCount == 2,
              "Packet replay must upload object matrices per draw");
    TestCheck(context.Encoder.UniformSetCount > context.Encoder.MatrixUniformSetCount,
              "Packet replay must still upload shared static uniforms for the run");
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &stats = ffp.GetFrameStats();
    TestCheck(stats.RenderPacketStaticUniformUploads == 1,
              "Replay must upload shared static uniforms once for a same-key run");
    TestCheck(stats.RenderPacketStaticUniformSkips == 1,
              "Replay must skip repeated static uniforms inside a same-key run");
    TestCheck(stats.RenderPacketObjectUniformUploads == 2,
              "Replay must upload per-object uniforms for different world matrices");
    TestCheck(stats.RenderPacketObjectUniformSkips == 0,
              "Different world matrices must not skip object uniforms");
#endif

    ffp.Shutdown();
}

void OpaquePacketObjectMatricesTrackProjectionChanges()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    const CKDWORD matrixUniform = ffp.GetShaderCache().GetUniforms().u_ffMatrices;
    context.Encoder.MatrixUniforms.insert(matrixUniform);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    ffp.FlushOpaqueRenderPackets(&context.Encoder);
    std::vector<float> firstMatrix = context.Encoder.FloatUniforms[matrixUniform];

    VxMatrix projection;
    projection.Identity();
    projection[0][0] = 2.0f;
    ffp.SetTransform(VXMATRIX_PROJECTION, projection);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    ffp.FlushOpaqueRenderPackets(&context.Encoder);
    std::vector<float> secondMatrix = context.Encoder.FloatUniforms[matrixUniform];

    TestCheck(firstMatrix.size() >= 16 && secondMatrix.size() >= 16,
              "Packet object matrix test must capture matrix uniform payloads");
    TestCheck(firstMatrix[0] != secondMatrix[0],
              "Packet object matrices must reflect projection changes");
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &stats = ffp.GetFrameStats();
    TestCheck(stats.RenderPacketViewProjectionRebuilds == 2,
              "Packet viewProjection cache must rebuild once per changed view/projection state");
#endif

    ffp.Shutdown();
}

void OpaquePacketInstancedMatrixMatchesObjectUniformMVP()
{
    FFPDiagnosticDriver normalDriver;
    FFPDiagnosticContext normalContext(&normalDriver);
    CKFixedFunctionPipeline normalFFP;
    SetupPacketPipeline(&normalFFP, &normalContext, &normalDriver);
    normalFFP.SetOpaqueInstancingEnabled(FALSE);
    const CKDWORD matrixUniform = normalFFP.GetShaderCache().GetUniforms().u_ffMatrices;
    normalContext.Encoder.MatrixUniforms.insert(matrixUniform);
    SetPacketProjection(&normalFFP);
    SetPacketComplexWorld(&normalFFP);

    DrawPacketCandidate(&normalFFP, &normalContext, CKRP_VIEW_OPAQUE3D, 100, 200);
    normalFFP.FlushOpaqueRenderPackets(&normalContext.Encoder);

    std::vector<float> normalMatrices = normalContext.Encoder.FloatUniforms[matrixUniform];
    TestCheck(normalMatrices.size() >= 32,
              "Normal packet replay must upload MVP and world matrices");

    FFPDiagnosticDriver instancedDriver;
    FFPDiagnosticContext instancedContext(&instancedDriver);
    CKFixedFunctionPipeline instancedFFP;
    SetupPacketPipeline(&instancedFFP, &instancedContext, &instancedDriver);
    instancedFFP.SetOpaqueInstancingEnabled(TRUE);
    instancedContext.Encoder.MatrixUniforms.insert(matrixUniform);
    SetPacketProjection(&instancedFFP);
    SetPacketComplexWorld(&instancedFFP);

    for (int i = 0; i < 4; ++i)
        DrawPacketCandidate(&instancedFFP, &instancedContext, CKRP_VIEW_OPAQUE3D, 100, 200);
    instancedFFP.FlushOpaqueRenderPackets(&instancedContext.Encoder);

    TestCheck(instancedContext.Encoder.SubmitCount == 1,
              "Matrix parity fixture must use an instanced packet submit");
    TestCheck(instancedContext.Encoder.LastInstanceBytes.size() >= sizeof(VxMatrix),
              "Instanced packet submit must upload at least one world matrix");
    std::vector<float> instancedMatrices = instancedContext.Encoder.FloatUniforms[matrixUniform];
    TestCheck(instancedMatrices.size() >= 16,
              "Instanced packet replay must upload viewProjection matrix");

    if (normalMatrices.size() >= 32 &&
        instancedMatrices.size() >= 16 &&
        instancedContext.Encoder.LastInstanceBytes.size() >= sizeof(VxMatrix)) {
        VxMatrix normalMVP;
        VxMatrix instancedViewProjection;
        VxMatrix instancedWorld;
        VxMatrix instancedMVP;
        memcpy(&normalMVP, &normalMatrices[0], sizeof(normalMVP));
        memcpy(&instancedViewProjection, &instancedMatrices[0], sizeof(instancedViewProjection));
        memcpy(&instancedWorld, &instancedContext.Encoder.LastInstanceBytes[0], sizeof(instancedWorld));
        Vx3DMultiplyMatrix4(instancedMVP, instancedViewProjection, instancedWorld);
        TestCheck(PacketMatrixAlmostEqual(normalMVP, instancedMVP),
                  "Instanced viewProjection/world packing must match normal object uniform MVP for rotation and non-uniform scale");
    }

    normalFFP.Shutdown();
    instancedFFP.Shutdown();
}

void OpaquePacketVertexBlendFallsBackImmediate()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_0WEIGHTS);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Vertex blend opaque mesh must fallback immediate in packet v2.1");
    TestCheck(context.Encoder.SubmitCount == 1,
              "Vertex blend fallback must submit immediately");

    ffp.Shutdown();
}

void OpaquePacketTextureHandleChangeKeepsStaticPayload()
{
    FFPDiagnosticDriver driverA;
    FFPDiagnosticContext contextA(&driverA);
    CKFixedFunctionPipeline ffpA;
    SetupPacketPipeline(&ffpA, &contextA, &driverA);
    ffpA.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffpA.SetTexture(0, 3000, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpA, &contextA, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpA.SetTexture(0, 3001, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpA, &contextA, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpA.FlushOpaqueRenderPackets(&contextA.Encoder);

    FFPDiagnosticDriver driverB;
    FFPDiagnosticContext contextB(&driverB);
    CKFixedFunctionPipeline ffpB;
    SetupPacketPipeline(&ffpB, &contextB, &driverB);
    ffpB.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffpB.SetTexture(0, 3000, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpB, &contextB, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpB.SetTexture(0, 3000, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpB, &contextB, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpB.FlushOpaqueRenderPackets(&contextB.Encoder);

    TestCheck(contextA.Encoder.SubmitCount == 2 &&
              contextB.Encoder.SubmitCount == 2,
              "Texture handle comparison scenarios must submit both draws");
    TestCheck(contextA.Encoder.TextureBindCount == 2,
              "Different texture handles must still bind per packet");
    TestCheck(contextB.Encoder.TextureBindCount == 1,
              "Same texture set must skip repeated texture bind through the replay hash path");
    TestCheck(contextA.Encoder.UniformSetCount == contextB.Encoder.UniformSetCount,
              "Changing between same-kind non-zero textures must not rebuild static payload");

    ffpA.Shutdown();
    ffpB.Shutdown();
}

void OpaquePacketTextureKindChangeRebuildsStaticPayload()
{
    FFPDiagnosticDriver driverA;
    FFPDiagnosticContext contextA(&driverA);
    CKFixedFunctionPipeline ffpA;
    SetupPacketPipeline(&ffpA, &contextA, &driverA);
    ffpA.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffpA.SetTexture(0, 3000, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpA, &contextA, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpA.SetTexture(0, 3001, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    DrawPacketCandidateWithFormat(&ffpA, &contextA, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpA.FlushOpaqueRenderPackets(&contextA.Encoder);

    FFPDiagnosticDriver driverB;
    FFPDiagnosticContext contextB(&driverB);
    CKFixedFunctionPipeline ffpB;
    SetupPacketPipeline(&ffpB, &contextB, &driverB);
    ffpB.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffpB.SetTexture(0, 3000, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpB, &contextB, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpB.SetTexture(0, 3001, CKRST_TEXTURE_VALID);
    DrawPacketCandidateWithFormat(&ffpB, &contextB, CKRP_VIEW_OPAQUE3D,
                                  100, 200,
                                  CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0));
    ffpB.FlushOpaqueRenderPackets(&contextB.Encoder);

    TestCheck(contextA.Encoder.SubmitCount == 2 &&
              contextB.Encoder.SubmitCount == 2,
              "Texture kind comparison scenarios must submit both draws");
    TestCheck(contextA.Encoder.UniformSetCount > contextB.Encoder.UniformSetCount,
              "Changing sampler texture kind must rebuild and replay static payload");

    ffpA.Shutdown();
    ffpB.Shutdown();
}

void OpaquePacketAdaptiveKeepsHighRepeatQueued()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    for (int i = 0; i < 128; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    TestCheck(ffp.HasOpaqueRenderPackets(),
              "High-repeat adaptive sample must remain queued");
    TestCheck(context.Encoder.SubmitCount == 0,
              "High-repeat adaptive sample must not flush early");

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 128,
              "High-repeat adaptive queue must replay all sampled draws");
    TestCheck(context.Encoder.VertexBufferSetCount == 1 &&
              context.Encoder.IndexBufferSetCount == 1,
              "High-repeat adaptive queue must keep repeated buffer bind skips");

    ffp.Shutdown();
}

void OpaquePacketAdaptiveBypassesLowBenefitFrame()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    for (int i = 0; i < CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT; ++i)
        DrawPacketChurnCandidate(&ffp, &context, i);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Low-benefit adaptive sample must flush the sampled queue");
    TestCheck(context.Encoder.SubmitCount == CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT,
              "Low-benefit adaptive sample must submit the sampled queue once bypass triggers");

    DrawPacketChurnCandidate(&ffp, &context, CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Adaptive bypass must keep remaining same-frame opaque draws immediate");
    TestCheck(context.Encoder.SubmitCount == CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT + 1,
              "Adaptive bypass must submit later same-frame opaque draws immediately");
    TestCheck(ffp.GetOpaquePacketAdaptiveBypasses() == 1,
              "Adaptive bypass counter must report the low-benefit frame bypass");

    ffp.Shutdown();
}

void OpaquePacketAdaptiveBypassesNoRepeatBindings()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);

    for (int i = 0; i < CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT; ++i)
        DrawPacketUniqueBindingCandidate(&ffp, &context, i);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Adaptive sample without repeated texture or buffers must bypass");
    TestCheck(context.Encoder.SubmitCount == CKFF_RENDER_PACKET_ADAPTIVE_MIN_SAMPLE_COUNT,
              "No-repeat adaptive bypass must flush the sampled queue");
    TestCheck(ffp.GetOpaquePacketAdaptiveBypasses() == 1,
              "No-repeat adaptive bypass must be counted");

    ffp.Shutdown();
}

void OpaquePacketInstancingMergesHighRepeatRun()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(TRUE);
    context.Encoder.MatrixUniforms.insert(ffp.GetShaderCache().GetUniforms().u_ffMatrices);

    for (int i = 0; i < 8; ++i) {
        SetPacketWorld(&ffp, (float)i);
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    }

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 1,
              "Instanced opaque packet run must collapse repeated mesh draws to one submit");
    TestCheck(context.Encoder.TransientInstanceSetCount == 1,
              "Instanced opaque packet run must bind one transient instance buffer");
    TestCheck(context.Encoder.LastInstanceCount == 8 &&
              context.Encoder.LastInstanceStride == sizeof(VxMatrix),
              "Instanced opaque packet run must use one 64-byte world matrix per instance");
    TestCheck(context.Encoder.LastInstanceBytes.size() >= sizeof(VxMatrix) * 8,
              "Instanced opaque packet run must upload instance world matrices");
    if (context.Encoder.LastInstanceBytes.size() >= sizeof(VxMatrix) * 8) {
        VxMatrix firstMatrix;
        VxMatrix lastMatrix;
        memcpy(&firstMatrix, &context.Encoder.LastInstanceBytes[0], sizeof(firstMatrix));
        memcpy(&lastMatrix, &context.Encoder.LastInstanceBytes[sizeof(VxMatrix) * 7],
               sizeof(lastMatrix));
        TestCheck(firstMatrix[3][0] == 0.0f && lastMatrix[3][0] == 7.0f,
                  "Instanced opaque packet run must preserve per-draw world matrices");
    }
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &stats = ffp.GetFrameStats();
    TestCheck(stats.RenderPacketInstancedRuns == 1,
              "Instanced opaque packet diagnostics must report the instanced run");
    TestCheck(stats.RenderPacketInstancedPackets == 8,
              "Instanced opaque packet diagnostics must report merged packets");
    TestCheck(stats.RenderPacketSubmitSavedEstimate == 7,
              "Instanced opaque packet diagnostics must report submit savings");
#endif

    ffp.Shutdown();
}

void OpaquePacketInstancingCanBeDisabled()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(FALSE);

    for (int i = 0; i < 8; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 8,
              "Disabled opaque instancing must preserve v2.2 per-packet replay");
    TestCheck(context.Encoder.TransientInstanceSetCount == 0,
              "Disabled opaque instancing must not bind transient instance data");

    ffp.Shutdown();
}

void OpaquePacketInstancingRejectsMismatchedSpecializedABI()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    ffp.Init(&context);

    CKFFShaderKey key;
    key.VS.Bits = 1ull;
    key.FS.LastActiveTextureStage = 0;
    key.FS.AlphaFunc = VXCMP_GREATER;
    key.FS.AlphaTestEnable = true;
    key.FS.Stages[0].ColorOp = CKRST_TOP_MODULATE;
    key.FS.Stages[0].ColorArg0 = CKRST_TA_CURRENT;
    key.FS.Stages[0].ColorArg1 = CKRST_TA_TEXTURE;
    key.FS.Stages[0].ColorArg2 = CKRST_TA_CURRENT;
    key.FS.Stages[0].AlphaOp = CKRST_TOP_MODULATE;
    key.FS.Stages[0].AlphaArg0 = CKRST_TA_CURRENT;
    key.FS.Stages[0].AlphaArg1 = CKRST_TA_TEXTURE;
    key.FS.Stages[0].AlphaArg2 = CKRST_TA_CURRENT;
    key.FS.Stages[0].HasTexture = true;

    CKFFProgramBinding normalBinding = ffp.GetShaderCache().GetProgram(key);
    CKFFShaderKey instancedKey = key;
    instancedKey.VS.SetInstanced(true);
    CKFFProgramBinding instancedBinding = ffp.GetShaderCache().GetProgram(instancedKey);

    TestCheck(normalBinding.Program != 0 && normalBinding.FullSpecialized,
              "Alpha-test normal shader fixture must hit a full-specialized module");
    TestCheck(instancedBinding.Program != 0 && !instancedBinding.FullSpecialized,
              "Alpha-test instanced shader fixture must fallback to the uber-specialized ABI");

    ffp.Shutdown();
}

void OpaquePacketPixelFogDoesNotInstance()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);

    for (int i = 0; i < 8; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 8,
              "Pixel fog packet runs must fallback to per-packet replay");
    TestCheck(context.Encoder.TransientInstanceSetCount == 0,
              "Pixel fog packet runs must not bind transient instance data");

    ffp.Shutdown();
}

void OpaquePacketRangeFogDoesNotInstance()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_NONE);
    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, TRUE);

    for (int i = 0; i < 8; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 8,
              "Range fog packet runs must fallback to per-packet replay");
    TestCheck(context.Encoder.TransientInstanceSetCount == 0,
              "Range fog packet runs must not bind transient instance data");

    ffp.Shutdown();
}

void OpaquePacketInstancingAllocationFailureFallsBack()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(TRUE);
    context.FailTransientInstanceBuffer = TRUE;

    for (int i = 0; i < 8; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 8,
              "Transient instance allocation failure must fallback to per-packet replay");
    TestCheck(context.Encoder.TransientInstanceSetCount == 0,
              "Transient instance allocation failure must not bind instance data");
#if CKRE_ENABLE_FFP_DIAGNOSTICS
    const CKFFFrameStats &stats = ffp.GetFrameStats();
    TestCheck(stats.RenderPacketInstanceAllocFailures == 1,
              "Allocation failure diagnostics must record the failed instance allocation");
    TestCheck(stats.RenderPacketInstancingFallbacks == 1,
              "Allocation failure diagnostics must record the instancing fallback");
#endif

    ffp.Shutdown();
}

void OpaquePacketInstancingSplitsViewProjectionRuns()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);
    ffp.SetOpaqueInstancingEnabled(TRUE);

    for (int i = 0; i < 4; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    VxMatrix projection;
    projection.Identity();
    projection[0][0] = 2.0f;
    ffp.SetTransform(VXMATRIX_PROJECTION, projection);

    for (int i = 0; i < 4; ++i)
        DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);

    ffp.FlushOpaqueRenderPackets(&context.Encoder);

    TestCheck(context.Encoder.SubmitCount == 2,
              "Changed viewProjection must split instanced packet runs");
    TestCheck(context.Encoder.TransientInstanceSetCount == 2,
              "Changed viewProjection must bind one instance buffer per run");

    ffp.Shutdown();
}

void NonOpaqueVertexBufferDrawFlushesQueuedOpaquePackets()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 200);
    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_TRANSPARENT, 300, 400);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Non-opaque draw must flush pending opaque packets");
    TestCheck(context.Encoder.SubmitCount == 2,
              "Fallback draw must submit after flushing queued opaque draw");
    TestCheck(context.Encoder.SubmitViews[0] == CKRP_VIEW_OPAQUE3D &&
              context.Encoder.SubmitViews[1] == CKRP_VIEW_TRANSPARENT,
              "Barrier flush must preserve opaque-before-transparent order");

    ffp.Shutdown();
}

void OpaqueVertexBufferWithoutIndexBufferStaysImmediate()
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;

    SetupPacketPipeline(&ffp, &context, &driver);

    DrawPacketCandidate(&ffp, &context, CKRP_VIEW_OPAQUE3D, 100, 0);

    TestCheck(!ffp.HasOpaqueRenderPackets(),
              "Opaque packet queue v1 must require an index buffer");
    TestCheck(context.Encoder.SubmitCount == 1,
              "Opaque draw without an index buffer must submit immediately");

    ffp.Shutdown();
}

int main()
{
    TestFramework tests;
    tests.Run("Opaque vertex-buffer draw stays immediate by default",
              &OpaqueVertexBufferDrawStaysImmediateByDefault);
    tests.Run("Opaque vertex-buffer draw queues until flush",
              &OpaqueVertexBufferDrawQueuesUntilFlush);
    tests.Run("Small opaque packet flush skips sorting",
              &OpaquePacketFlushSortsAndSkipsRepeatedBufferBinding);
    tests.Run("Large opaque packet flush sorts and skips repeated buffer binding",
              &LargeOpaquePacketFlushSortsAndSkipsRepeatedBufferBinding);
    tests.Run("Opaque packet sort uses static uniforms for runs",
              &OpaquePacketSortUsesStaticUniformsForRuns);
    tests.Run("Opaque packet replay splits static and object uniforms",
              &OpaquePacketReplaySplitsStaticAndObjectUniforms);
    tests.Run("Opaque packet object matrices track projection changes",
              &OpaquePacketObjectMatricesTrackProjectionChanges);
    tests.Run("Opaque packet instanced matrix matches object uniform MVP",
              &OpaquePacketInstancedMatrixMatchesObjectUniformMVP);
    tests.Run("Opaque packet vertex blend falls back immediate",
              &OpaquePacketVertexBlendFallsBackImmediate);
    tests.Run("Opaque packet texture handle change keeps static payload",
              &OpaquePacketTextureHandleChangeKeepsStaticPayload);
    tests.Run("Opaque packet texture kind change rebuilds static payload",
              &OpaquePacketTextureKindChangeRebuildsStaticPayload);
    tests.Run("Opaque packet adaptive keeps high-repeat queue",
              &OpaquePacketAdaptiveKeepsHighRepeatQueued);
    tests.Run("Opaque packet adaptive bypasses low-benefit frame",
              &OpaquePacketAdaptiveBypassesLowBenefitFrame);
    tests.Run("Opaque packet adaptive bypasses no repeat bindings",
              &OpaquePacketAdaptiveBypassesNoRepeatBindings);
    tests.Run("Opaque packet instancing merges high-repeat run",
              &OpaquePacketInstancingMergesHighRepeatRun);
    tests.Run("Opaque packet instancing can be disabled",
              &OpaquePacketInstancingCanBeDisabled);
    tests.Run("Opaque packet instancing rejects mismatched specialized ABI",
              &OpaquePacketInstancingRejectsMismatchedSpecializedABI);
    tests.Run("Opaque packet pixel fog does not instance",
              &OpaquePacketPixelFogDoesNotInstance);
    tests.Run("Opaque packet range fog does not instance",
              &OpaquePacketRangeFogDoesNotInstance);
    tests.Run("Opaque packet instancing allocation failure falls back",
              &OpaquePacketInstancingAllocationFailureFallsBack);
    tests.Run("Opaque packet instancing splits viewProjection runs",
              &OpaquePacketInstancingSplitsViewProjectionRuns);
    tests.Run("Non-opaque vertex-buffer draw flushes queued opaque packets",
              &NonOpaqueVertexBufferDrawFlushesQueuedOpaquePackets);
    tests.Run("Opaque vertex-buffer draw without index buffer stays immediate",
              &OpaqueVertexBufferWithoutIndexBufferStaysImmediate);
    return tests.ExitCode();
}
