#include <stdio.h>

#include "CKFixedFunctionPipeline.h"
#include "CKRenderSettings.h"
#include "FFPDiagnosticHarness.h"

#include <chrono>
#include <cstring>

using BenchClock = std::chrono::steady_clock;

static BenchClock::time_point BenchNow()
{
    return BenchClock::now();
}

static double BenchElapsedUs(BenchClock::time_point start)
{
    return std::chrono::duration<double, std::micro>(BenchNow() - start).count();
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
    CKDWORD AdaptiveRunBypasses;
    CKDWORD AdaptiveCooldownBypasses;
    CKDWORD AdaptiveCooldownFrames;
    CKDWORD AdaptiveFrameEndEvaluations;
    CKDWORD AdaptiveFrameEndRunBypasses;
    CKDWORD AdaptiveSampleRuns;
    CKDWORD AdaptiveSampleMaxRun;
    CKDWORD AdaptiveSubmitSavedEstimate;
    CKDWORD QueuedRenderPackets;
    CKDWORD ReplayedRenderPackets;
    CKDWORD RenderPacketFallbacks;
    CKDWORD RenderPacketFlushes;
    CKDWORD RenderPacketUniformOverflows;
    CKDWORD RenderPacketRuns;
    CKDWORD RenderPacketMaxRunLength;
    CKDWORD RenderPacketStaticPayloadBuilds;
    CKDWORD RenderPacketStaticPayloadReuses;
    CKDWORD RenderPacketStaticPayloadInterns;
    CKDWORD RenderPacketViewProjectionRebuilds;
    CKDWORD RenderPacketInstancedRuns;
    CKDWORD RenderPacketInstancedPackets;
    CKDWORD RenderPacketInstancedSubmits;
    double RenderPacketBuildUs;
    double RenderPacketSortUs;
    double RenderPacketReplayUs;
};

#define BENCH_REPEAT_COUNT 5

static void AddFrameStats(BenchResult *result, const CKFFFrameStats *stats)
{
    if (!result || !stats)
        return;

    result->QueuedRenderPackets += stats->QueuedRenderPackets;
    result->ReplayedRenderPackets += stats->ReplayedRenderPackets;
    result->RenderPacketFallbacks += stats->RenderPacketFallbacks;
    result->RenderPacketFlushes += stats->RenderPacketFlushes;
    result->RenderPacketUniformOverflows += stats->RenderPacketUniformOverflows;
    result->RenderPacketRuns += stats->RenderPacketRuns;
    if (stats->RenderPacketMaxRunLength > result->RenderPacketMaxRunLength)
        result->RenderPacketMaxRunLength = stats->RenderPacketMaxRunLength;
    result->RenderPacketStaticPayloadBuilds += stats->RenderPacketStaticPayloadBuilds;
    result->RenderPacketStaticPayloadReuses += stats->RenderPacketStaticPayloadReuses;
    result->RenderPacketStaticPayloadInterns += stats->RenderPacketStaticPayloadInterns;
    result->RenderPacketViewProjectionRebuilds += stats->RenderPacketViewProjectionRebuilds;
    result->RenderPacketInstancedRuns += stats->RenderPacketInstancedRuns;
    result->RenderPacketInstancedPackets += stats->RenderPacketInstancedPackets;
    result->RenderPacketInstancedSubmits += stats->RenderPacketInstancedSubmits;
    result->RenderPacketBuildUs += stats->RenderPacketBuildUs;
    result->RenderPacketSortUs += stats->RenderPacketSortUs;
    result->RenderPacketReplayUs += stats->RenderPacketReplayUs;
}

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
    const auto start = BenchNow();
    for (int frame = 0; frame < scenario->Frames; ++frame) {
        ffp.BeginDebugFrame();
        auto phaseStart = BenchNow();
        for (int draw = 0; draw < scenario->DrawsPerFrame; ++draw)
            DrawBenchPacket(&ffp, &context, draw, scenario);
        drawUs += BenchElapsedUs(phaseStart);
        phaseStart = BenchNow();
        ffp.FlushOpaqueRenderPackets(&context.Encoder);
        flushUs += BenchElapsedUs(phaseStart);
        result.AdaptiveSamples += ffp.GetOpaquePacketAdaptiveSamples();
        result.AdaptiveBypasses += ffp.GetOpaquePacketAdaptiveBypasses();
        result.AdaptiveSavedBindEstimate += ffp.GetOpaquePacketAdaptiveSavedBindEstimate();
        result.AdaptiveRunBypasses += ffp.GetOpaquePacketAdaptiveRunBypasses();
        result.AdaptiveCooldownBypasses += ffp.GetOpaquePacketAdaptiveCooldownBypasses();
        if (ffp.GetOpaquePacketAdaptiveCooldownFrames() > result.AdaptiveCooldownFrames)
            result.AdaptiveCooldownFrames = ffp.GetOpaquePacketAdaptiveCooldownFrames();
        result.AdaptiveFrameEndEvaluations +=
            ffp.GetOpaquePacketAdaptiveFrameEndEvaluations();
        result.AdaptiveFrameEndRunBypasses +=
            ffp.GetOpaquePacketAdaptiveFrameEndRunBypasses();
        result.AdaptiveSampleRuns += ffp.GetOpaquePacketAdaptiveSampleRuns();
        if (ffp.GetOpaquePacketAdaptiveSampleMaxRun() > result.AdaptiveSampleMaxRun)
            result.AdaptiveSampleMaxRun = ffp.GetOpaquePacketAdaptiveSampleMaxRun();
        result.AdaptiveSubmitSavedEstimate += ffp.GetOpaquePacketAdaptiveSubmitSavedEstimate();
        AddFrameStats(&result, &ffp.GetFrameStats());
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

static void SortBenchResults(BenchResult *results, int count)
{
    for (int i = 1; i < count; ++i) {
        BenchResult item = results[i];
        int j = i - 1;
        while (j >= 0 && results[j].UsPerDraw > item.UsPerDraw) {
            results[j + 1] = results[j];
            --j;
        }
        results[j + 1] = item;
    }
}

static BenchResult RunBenchScenarioMedian(const BenchScenario *scenario, CKBOOL packetQueue)
{
    BenchResult results[BENCH_REPEAT_COUNT];
    for (int i = 0; i < BENCH_REPEAT_COUNT; ++i)
        results[i] = RunBenchScenario(scenario, packetQueue);
    SortBenchResults(results, BENCH_REPEAT_COUNT);
    return results[BENCH_REPEAT_COUNT / 2];
}

static void PrintBenchResult(const BenchScenario *scenario,
                             const BenchResult *immediate,
                             const BenchResult *packet)
{
    printf("%s\n", scenario->Name);
    printf("  immediate: us/draw=%.3f draw=%.3f flush=%.3f submit=%lu state=%lu texture=%lu uniform=%lu vb=%lu ib=%lu instanceBuffers=%lu instances=%lu instanceBytes=%lu submitSaved=%lu adaptiveSamples=%lu adaptiveBypass=%lu adaptiveSaved=%lu adaptiveRunBypass=%lu adaptiveCooldownBypass=%lu adaptiveCooldownFrames=%lu adaptiveFrameEndEval=%lu adaptiveFrameEndRunBypass=%lu adaptiveSampleRuns=%lu adaptiveSampleMaxRun=%lu adaptiveSubmitSaved=%lu\n",
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
           (unsigned long)immediate->AdaptiveSavedBindEstimate,
           (unsigned long)immediate->AdaptiveRunBypasses,
           (unsigned long)immediate->AdaptiveCooldownBypasses,
           (unsigned long)immediate->AdaptiveCooldownFrames,
           (unsigned long)immediate->AdaptiveFrameEndEvaluations,
           (unsigned long)immediate->AdaptiveFrameEndRunBypasses,
           (unsigned long)immediate->AdaptiveSampleRuns,
           (unsigned long)immediate->AdaptiveSampleMaxRun,
           (unsigned long)immediate->AdaptiveSubmitSavedEstimate);
    printf("  packet:    us/draw=%.3f draw=%.3f flush=%.3f submit=%lu state=%lu texture=%lu uniform=%lu vb=%lu ib=%lu instanceBuffers=%lu instances=%lu instanceBytes=%lu submitSaved=%lu adaptiveSamples=%lu adaptiveBypass=%lu adaptiveSaved=%lu adaptiveRunBypass=%lu adaptiveCooldownBypass=%lu adaptiveCooldownFrames=%lu adaptiveFrameEndEval=%lu adaptiveFrameEndRunBypass=%lu adaptiveSampleRuns=%lu adaptiveSampleMaxRun=%lu adaptiveSubmitSaved=%lu queued=%lu replayed=%lu fallbacks=%lu flushes=%lu overflows=%lu runs=%lu maxRun=%lu staticBuild=%lu staticReuse=%lu staticIntern=%lu viewProjRebuild=%lu instRuns=%lu instPackets=%lu instSubmits=%lu packetBuildUs=%.1f packetSortUs=%.1f packetReplayUs=%.1f\n",
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
           (unsigned long)packet->AdaptiveSavedBindEstimate,
           (unsigned long)packet->AdaptiveRunBypasses,
           (unsigned long)packet->AdaptiveCooldownBypasses,
           (unsigned long)packet->AdaptiveCooldownFrames,
           (unsigned long)packet->AdaptiveFrameEndEvaluations,
           (unsigned long)packet->AdaptiveFrameEndRunBypasses,
           (unsigned long)packet->AdaptiveSampleRuns,
           (unsigned long)packet->AdaptiveSampleMaxRun,
           (unsigned long)packet->AdaptiveSubmitSavedEstimate,
           (unsigned long)packet->QueuedRenderPackets,
           (unsigned long)packet->ReplayedRenderPackets,
           (unsigned long)packet->RenderPacketFallbacks,
           (unsigned long)packet->RenderPacketFlushes,
           (unsigned long)packet->RenderPacketUniformOverflows,
           (unsigned long)packet->RenderPacketRuns,
           (unsigned long)packet->RenderPacketMaxRunLength,
           (unsigned long)packet->RenderPacketStaticPayloadBuilds,
           (unsigned long)packet->RenderPacketStaticPayloadReuses,
           (unsigned long)packet->RenderPacketStaticPayloadInterns,
           (unsigned long)packet->RenderPacketViewProjectionRebuilds,
           (unsigned long)packet->RenderPacketInstancedRuns,
           (unsigned long)packet->RenderPacketInstancedPackets,
           (unsigned long)packet->RenderPacketInstancedSubmits,
           packet->RenderPacketBuildUs,
           packet->RenderPacketSortUs,
           packet->RenderPacketReplayUs);
}

int main()
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFFPStats, "Enabled", "1");
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugFFPStats, "Interval", "999999");

    const BenchScenario scenarios[] = {
        {"low_repeat", 4096, 32, 4096, 4096, 1, FALSE},
        {"high_repeat", 4096, 32, 1, 1, 4096, FALSE},
        {"multi_material", 4096, 32, 8, 64, 16, FALSE},
        {"multi_mesh", 4096, 32, 128, 8, 512, FALSE},
        {"state_churn_no_benefit", 4096, 32, 4096, 4096, 1, TRUE},
        {"player_menu_no_run", 29, 544, 29, 1, 29, FALSE},
    };

    const int scenarioCount = (int)(sizeof(scenarios) / sizeof(scenarios[0]));
    printf("render_packet_submit_bench drawsPerScenario=%lu repeats=%d median=us/draw diagnostics=%d\n",
           (unsigned long)(scenarios[0].DrawsPerFrame * scenarios[0].Frames),
           BENCH_REPEAT_COUNT,
#if CKRE_ENABLE_FFP_DIAGNOSTICS
           1
#else
           0
#endif
    );
    for (int i = 0; i < scenarioCount; ++i) {
        BenchResult immediate = RunBenchScenarioMedian(&scenarios[i], FALSE);
        BenchResult packet = RunBenchScenarioMedian(&scenarios[i], TRUE);
        PrintBenchResult(&scenarios[i], &immediate, &packet);
    }
    CKRenderSettingsClearOverridesForTests();
    return 0;
}
