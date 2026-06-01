#include <stdio.h>

#include "CKFixedFunctionPipeline.h"
#include "FFPDiagnosticHarness.h"

void SetupPacketPipeline(CKFixedFunctionPipeline *ffp,
                         FFPDiagnosticContext *context,
                         FFPDiagnosticDriver *driver)
{
    (void)driver;
    ffp->Init(context);
    ffp->SetOpaqueSortingEnabled(TRUE);
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

void SetPacketWorld(CKFixedFunctionPipeline *ffp, float x)
{
    VxMatrix world;
    world.Identity();
    world[3][0] = x;
    ffp->SetTransform(VXMATRIX_WORLD, world);
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
    tests.Run("Opaque packet vertex blend falls back immediate",
              &OpaquePacketVertexBlendFallsBackImmediate);
    tests.Run("Opaque packet texture handle change keeps static payload",
              &OpaquePacketTextureHandleChangeKeepsStaticPayload);
    tests.Run("Opaque packet texture kind change rebuilds static payload",
              &OpaquePacketTextureKindChangeRebuildsStaticPayload);
    tests.Run("Non-opaque vertex-buffer draw flushes queued opaque packets",
              &NonOpaqueVertexBufferDrawFlushesQueuedOpaquePackets);
    tests.Run("Opaque vertex-buffer draw without index buffer stays immediate",
              &OpaqueVertexBufferWithoutIndexBufferStaysImmediate);
    return tests.ExitCode();
}
