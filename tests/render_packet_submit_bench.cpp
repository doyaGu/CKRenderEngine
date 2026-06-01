#include <stdio.h>

#include "CKFixedFunctionPipeline.h"
#include "FFPDiagnosticHarness.h"

#include <cstring>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

static double BenchNow()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart;
}

static double BenchElapsedUs(double start)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (BenchNow() - start) * 1000000.0 / (double)frequency.QuadPart;
}

struct BenchScenario {
    const char *Name;
    int DrawsPerFrame;
    int Frames;
    int MeshCount;
    int TextureCount;
    int TextureStep;
    CKBOOL StateChurn;
};

struct BenchResult {
    double UsPerDraw;
    double DrawUsPerDraw;
    double FlushUsPerDraw;
    CKDWORD SubmitCount;
    CKDWORD StateCount;
    CKDWORD TextureCount;
    CKDWORD UniformCount;
    CKDWORD VertexBufferCount;
    CKDWORD IndexBufferCount;
    CKDWORD InstanceBufferCount;
    CKDWORD InstanceCount;
    CKDWORD InstanceBytes;
    CKDWORD SubmitSavedEstimate;
    CKDWORD AdaptiveSamples;
    CKDWORD AdaptiveBypasses;
    CKDWORD AdaptiveSavedBindEstimate;
};

static void SetupBenchPipeline(CKFixedFunctionPipeline *ffp,
                               FFPDiagnosticContext *context,
                               CKBOOL packetQueue)
{
    ffp->Init(context);
    ffp->SetOpaqueSortingEnabled(packetQueue);
    ffp->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
    ffp->SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
    ffp->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, TRUE);
    ffp->SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
}

static void DrawBenchPacket(CKFixedFunctionPipeline *ffp,
                            FFPDiagnosticContext *context,
                            int drawIndex,
                            const BenchScenario *scenario)
{
    const int meshIndex = scenario->MeshCount > 0 ? drawIndex % scenario->MeshCount : 0;
    const int textureIndex = scenario->TextureCount > 0
        ? (drawIndex / scenario->TextureStep) % scenario->TextureCount
        : 0;
    const CKDWORD vb = 1000 + (CKDWORD)meshIndex;
    const CKDWORD ib = 2000 + (CKDWORD)meshIndex;
    const CKDWORD texture = 3000 + (CKDWORD)textureIndex;
    CKDWORD textureFlags = CKRST_TEXTURE_VALID;

    VxMatrix world;
    world.Identity();
    world[3][0] = (float)(drawIndex & 255);
    ffp->SetTransform(VXMATRIX_WORLD, world);
    if (scenario->StateChurn) {
        ffp->SetRenderState(VXRENDERSTATE_CULLMODE,
                            (drawIndex & 1) ? VXCULL_NONE : VXCULL_CCW);
        textureFlags = (drawIndex & 1)
            ? (CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP)
            : CKRST_TEXTURE_VALID;
    }
    ffp->SetTexture(0, texture, textureFlags);
    ffp->DrawVertexBuffer(&context->Encoder, CKRP_VIEW_OPAQUE3D, VX_TRIANGLELIST,
                          vb, ib,
                          0, 3,
                          0, 3,
                          CKRST_DP_TRANSFORM,
                          CKFF_VF_POSITION | CKFF_VF_TEXCOORD(0),
                          77);
}

static BenchResult RunBenchScenario(const BenchScenario *scenario, CKBOOL packetQueue)
{
    FFPDiagnosticDriver driver;
    FFPDiagnosticContext context(&driver);
    CKFixedFunctionPipeline ffp;
    BenchResult result;
    memset(&result, 0, sizeof(result));

    SetupBenchPipeline(&ffp, &context, packetQueue);

    double drawUs = 0.0;
    double flushUs = 0.0;
    const double start = BenchNow();
    for (int frame = 0; frame < scenario->Frames; ++frame) {
        ffp.BeginDebugFrame();
        double phaseStart = BenchNow();
        for (int draw = 0; draw < scenario->DrawsPerFrame; ++draw)
            DrawBenchPacket(&ffp, &context, draw, scenario);
        drawUs += BenchElapsedUs(phaseStart);
        phaseStart = BenchNow();
        ffp.FlushOpaqueRenderPackets(&context.Encoder);
        flushUs += BenchElapsedUs(phaseStart);
        result.AdaptiveSamples += ffp.GetOpaquePacketAdaptiveSamples();
        result.AdaptiveBypasses += ffp.GetOpaquePacketAdaptiveBypasses();
        result.AdaptiveSavedBindEstimate += ffp.GetOpaquePacketAdaptiveSavedBindEstimate();
    }
    const double elapsedUs = BenchElapsedUs(start);
    const CKDWORD totalDraws = (CKDWORD)(scenario->DrawsPerFrame * scenario->Frames);

    result.UsPerDraw = totalDraws > 0 ? elapsedUs / (double)totalDraws : 0.0;
    result.DrawUsPerDraw = totalDraws > 0 ? drawUs / (double)totalDraws : 0.0;
    result.FlushUsPerDraw = totalDraws > 0 ? flushUs / (double)totalDraws : 0.0;
    result.SubmitCount = context.Encoder.SubmitCount;
    result.StateCount = context.Encoder.StateSetCount;
    result.TextureCount = context.Encoder.TextureBindCount;
    result.UniformCount = context.Encoder.UniformSetCount;
    result.VertexBufferCount = context.Encoder.VertexBufferSetCount;
    result.IndexBufferCount = context.Encoder.IndexBufferSetCount;
    result.InstanceBufferCount = context.Encoder.TransientInstanceSetCount;
    result.InstanceCount = context.Encoder.TotalInstanceCount;
    result.InstanceBytes = context.Encoder.TotalInstanceBytes;
    result.SubmitSavedEstimate = totalDraws > result.SubmitCount
        ? totalDraws - result.SubmitCount
        : 0;

    ffp.Shutdown();
    return result;
}

static void PrintBenchResult(const BenchScenario *scenario,
                             const BenchResult *immediate,
                             const BenchResult *packet)
{
    printf("%s\n", scenario->Name);
    printf("  immediate: us/draw=%.3f draw=%.3f flush=%.3f submit=%lu state=%lu texture=%lu uniform=%lu vb=%lu ib=%lu instanceBuffers=%lu instances=%lu instanceBytes=%lu submitSaved=%lu adaptiveSamples=%lu adaptiveBypass=%lu adaptiveSaved=%lu\n",
           immediate->UsPerDraw,
           immediate->DrawUsPerDraw,
           immediate->FlushUsPerDraw,
           (unsigned long)immediate->SubmitCount,
           (unsigned long)immediate->StateCount,
           (unsigned long)immediate->TextureCount,
           (unsigned long)immediate->UniformCount,
           (unsigned long)immediate->VertexBufferCount,
           (unsigned long)immediate->IndexBufferCount,
           (unsigned long)immediate->InstanceBufferCount,
           (unsigned long)immediate->InstanceCount,
           (unsigned long)immediate->InstanceBytes,
           (unsigned long)immediate->SubmitSavedEstimate,
           (unsigned long)immediate->AdaptiveSamples,
           (unsigned long)immediate->AdaptiveBypasses,
           (unsigned long)immediate->AdaptiveSavedBindEstimate);
    printf("  packet:    us/draw=%.3f draw=%.3f flush=%.3f submit=%lu state=%lu texture=%lu uniform=%lu vb=%lu ib=%lu instanceBuffers=%lu instances=%lu instanceBytes=%lu submitSaved=%lu adaptiveSamples=%lu adaptiveBypass=%lu adaptiveSaved=%lu\n",
           packet->UsPerDraw,
           packet->DrawUsPerDraw,
           packet->FlushUsPerDraw,
           (unsigned long)packet->SubmitCount,
           (unsigned long)packet->StateCount,
           (unsigned long)packet->TextureCount,
           (unsigned long)packet->UniformCount,
           (unsigned long)packet->VertexBufferCount,
           (unsigned long)packet->IndexBufferCount,
           (unsigned long)packet->InstanceBufferCount,
           (unsigned long)packet->InstanceCount,
           (unsigned long)packet->InstanceBytes,
           (unsigned long)packet->SubmitSavedEstimate,
           (unsigned long)packet->AdaptiveSamples,
           (unsigned long)packet->AdaptiveBypasses,
           (unsigned long)packet->AdaptiveSavedBindEstimate);
}

int main()
{
    const BenchScenario scenarios[] = {
        {"low_repeat", 4096, 32, 4096, 4096, 1, FALSE},
        {"high_repeat", 4096, 32, 1, 1, 4096, FALSE},
        {"multi_material", 4096, 32, 8, 64, 16, FALSE},
        {"multi_mesh", 4096, 32, 128, 8, 512, FALSE},
        {"state_churn_no_benefit", 4096, 32, 4096, 4096, 1, TRUE},
    };

    const int scenarioCount = (int)(sizeof(scenarios) / sizeof(scenarios[0]));
    printf("render_packet_submit_bench drawsPerScenario=%lu\n",
           (unsigned long)(scenarios[0].DrawsPerFrame * scenarios[0].Frames));
    for (int i = 0; i < scenarioCount; ++i) {
        BenchResult immediate = RunBenchScenario(&scenarios[i], FALSE);
        BenchResult packet = RunBenchScenario(&scenarios[i], TRUE);
        PrintBenchResult(&scenarios[i], &immediate, &packet);
    }
    return 0;
}
