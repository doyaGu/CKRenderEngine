#include "FFPBenchmarkWorkload.h"
#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

uint64_t Mix(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t MixBytes(uint64_t hash, const void *data, size_t size)
{
    const CKBYTE *bytes = static_cast<const CKBYTE *>(data);
    for (size_t i = 0; i < size; ++i)
        hash = Mix(hash, bytes[i]);
    return hash;
}

CKDWORD TextureOrdinal(CKDWORD handle,
                       const std::array<CKDWORD, 4> &textures)
{
    for (CKDWORD i = 0; i < textures.size(); ++i) {
        if (textures[i] == handle)
            return i + 1;
    }
    return 0;
}

CKSamplerDesc ExpectedSampler(const CKFFBenchmark::MaterialProfile &profile)
{
    CKDWORD stage[CKFF_MAX_TEXTURE_STAGE_STATES]{};
    for (CKDWORD state = 0; state < CKFF_MAX_TEXTURE_STAGE_STATES; ++state) {
        stage[state] = CKRSTDefaultTextureStageStateValue(
            0, static_cast<CKRST_TEXTURESTAGESTATETYPE>(state));
    }
    stage[CKRST_TSS_ADDRESS] = profile.Address;
    stage[CKRST_TSS_ADDRESSU] = profile.Address;
    stage[CKRST_TSS_ADDRESSV] = profile.Address;
    stage[CKRST_TSS_ADDRESW] = profile.Address;
    stage[CKRST_TSS_MINFILTER] = profile.Filter;
    stage[CKRST_TSS_MAGFILTER] = profile.Filter;
    return CKFFBuildSamplerDesc(stage);
}

using DrawFingerprint =
    std::array<uint64_t, 12 + CKBACKEND_MAX_CONSTANT_SLOTS>;

DrawFingerprint NormalizedDrawFingerprint(
    const FFPRecordingBackend &backend,
    const std::array<CKDWORD, 4> &textures)
{
    const FFPBackendLog &log = backend.Log;
    DrawFingerprint fingerprint{};
    fingerprint[0] = log.LastState.Lo;
    fingerprint[1] = log.LastState.Mid;
    fingerprint[2] = log.LastState.Hi;
    fingerprint[3] = log.LastStencilRef;
    fingerprint[4] = log.LastStencilReadMask;
    fingerprint[5] = log.LastStencilWriteMask;
    fingerprint[6] = log.ScissorEnabled;
    fingerprint[7] = MixBytes(1469598103934665603ull,
                              &log.LastScissor, sizeof(log.LastScissor));
    fingerprint[8] = MixBytes(1469598103934665603ull,
                              &log.LastPointSize, sizeof(log.LastPointSize));
    fingerprint[9] = TextureOrdinal(log.LastTextureHandle, textures);
    fingerprint[10] = MixBytes(1469598103934665603ull,
                               &log.LastTextureSampler,
                               sizeof(log.LastTextureSampler));
    fingerprint[11] = log.LastProgram != 0;
    for (CKDWORD block = 0; block < CKBACKEND_MAX_CONSTANT_SLOTS; ++block) {
        const CKDWORD uniform = backend.GetBlockUniformForTests(block);
        const auto found = log.FloatUniforms.find(uniform);
        if (found == log.FloatUniforms.end()) {
            continue;
        }
        uint64_t hash = Mix(1469598103934665603ull, found->second.size());
        fingerprint[12 + block] = MixBytes(
            hash, found->second.data(),
            found->second.size() * sizeof(float));
    }
    return fingerprint;
}

struct RecordingCoreFixture {
    FFPRecordingDriver Driver;
    FFPRecordingBackend Backend;
    CKFixedFunctionPipeline Pipeline;
    std::array<CKDWORD, 4> Textures{{101, 102, 103, 104}};
    CKDWORD FormatFlags = 0;
    CKDWORD VertexLayout = 0;

    RecordingCoreFixture() : Backend(&Driver)
    {
        TestCheck(Pipeline.Init(Backend.StartedBackend(), Backend.ShaderSet()),
                  "benchmark workload FFP initialization");
        FormatFlags = CKVertexLayoutCache::DPFlagsToFormatFlags(
            CKFFBenchmark::VertexFormat, true, true);
        VertexLayout = Pipeline.ResolveVertexLayout(FormatFlags);
        TestCheck(FormatFlags != 0 && VertexLayout != 0,
                  "benchmark workload vertex layout");
    }

    ~RecordingCoreFixture()
    {
        Pipeline.Shutdown();
    }
};

struct FingerprintObserver {
    const FFPRecordingBackend &Backend;
    const std::array<CKDWORD, 4> &Textures;
    std::vector<DrawFingerprint> &Digests;

    void operator()(CKDWORD) const
    {
        Digests.push_back(NormalizedDrawFingerprint(Backend, Textures));
    }
};

void CoreVertexBufferWorkloadsSubmitExactly44Draws()
{
    const CKFFBenchmark::Workload workload;

    RecordingCoreFixture steady;
    workload.ConfigureCore(steady.Pipeline, steady.Textures);
    TestCheck(workload.RunCoreVertexBufferFrame(
                  steady.Pipeline, steady.Textures, 201, 301,
                  steady.VertexLayout, steady.FormatFlags, false),
              "steady 44-draw workload must complete");
    TestCheck(steady.Backend.Log.DrawCount == CKFFBenchmark::DrawsPerFrame &&
                  steady.Backend.Log.DiscardCount == 0 &&
                  steady.Backend.Log.VertexBufferSetCount == CKFFBenchmark::DrawsPerFrame &&
                  steady.Backend.Log.IndexBufferSetCount == CKFFBenchmark::DrawsPerFrame,
              "steady workload must submit exactly 44 VB/IB draws");

    RecordingCoreFixture cycling;
    workload.ConfigureCore(cycling.Pipeline, cycling.Textures);
    TestCheck(workload.RunCoreVertexBufferFrame(
                  cycling.Pipeline, cycling.Textures, 202, 302,
                  cycling.VertexLayout, cycling.FormatFlags, true),
              "material-cycle 44-draw workload must complete");
    TestCheck(cycling.Backend.Log.DrawCount == CKFFBenchmark::DrawsPerFrame &&
                  cycling.Backend.Log.DiscardCount == 0 &&
                  cycling.Backend.Log.TextureBindings.size() == CKFFBenchmark::DrawsPerFrame,
              "material-cycle workload must bind one texture for every draw");
    for (CKDWORD draw = 0; draw < CKFFBenchmark::DrawsPerFrame; ++draw) {
        const CKSamplerDesc expected = ExpectedSampler(
            workload.Profiles[draw & 7u]);
        TestCheck(cycling.Backend.Log.TextureBindings[draw].Texture ==
                          cycling.Textures[draw & 3u] &&
                      std::memcmp(
                          &cycling.Backend.Log.TextureBindings[draw].Sampler,
                          &expected, sizeof(expected)) == 0,
                  "material-cycle texture and sampler order must remain deterministic");
    }
}

void TransientWorkloadsKeepTheirShapeAndPacking()
{
    const CKFFBenchmark::Workload workload;

    RecordingCoreFixture common;
    workload.ConfigureCore(common.Pipeline, common.Textures);
    common.Pipeline.SetTexcoordComponentCount(0, 2);
    TestCheck(workload.RunCoreTransientFrame(common.Pipeline, false),
              "common transient workload must complete");
    TestCheck(common.Backend.Log.DrawCount == CKFFBenchmark::DrawsPerFrame &&
                  common.Backend.Log.DiscardCount == 0 &&
                  common.Backend.TransientVertexAllocations == CKFFBenchmark::DrawsPerFrame &&
                  common.Backend.TransientIndexAllocations == CKFFBenchmark::DrawsPerFrame &&
                  common.Backend.Log.LastVertexBytes.size() ==
                      CKFFBenchmark::VerticesPerDraw * CKFFBenchmark::PackedVertexStride &&
                  common.Backend.Log.LastIndexBytes.size() ==
                      CKFFBenchmark::IndicesPerDraw * sizeof(CKWORD),
              "common transient workload must retain the 45,936-byte frame shape");

    RecordingCoreFixture general;
    workload.ConfigureCore(general.Pipeline, general.Textures);
    general.Pipeline.SetTexcoordComponentCount(0, 3);
    TestCheck(workload.RunCoreTransientFrame(general.Pipeline, true),
              "general transient workload must complete");
    TestCheck(general.Backend.Log.DrawCount == CKFFBenchmark::DrawsPerFrame &&
                  general.Backend.Log.DiscardCount == 0 &&
                  general.Backend.TransientVertexAllocations == CKFFBenchmark::DrawsPerFrame &&
                  general.Backend.TransientIndexAllocations == CKFFBenchmark::DrawsPerFrame &&
                  general.Backend.Log.LastVertexBytes.size() ==
                      CKFFBenchmark::VerticesPerDraw * CKFFBenchmark::PackedVertexStride &&
                  general.Backend.Log.LastIndexBytes == common.Backend.Log.LastIndexBytes,
              "general transient workload must keep identical draw and index shape");

    float commonZ = -1.0f;
    float generalZ = -1.0f;
    std::memcpy(&commonZ, common.Backend.Log.LastVertexBytes.data() + 32,
                sizeof(commonZ));
    std::memcpy(&generalZ, general.Backend.Log.LastVertexBytes.data() + 32,
                sizeof(generalZ));
    TestCheck(commonZ == 0.0f && generalZ == workload.Texcoords3[0][2],
              "UV2 and UV3 cases must exercise common and general interleave semantics");
}

void DirectAndTranslatedMaterialFramesAreEquivalent()
{
    const CKFFBenchmark::Workload workload;

    RecordingCoreFixture direct;
    workload.ConfigureCore(direct.Pipeline, direct.Textures);
    std::vector<DrawFingerprint> directDigests;
    directDigests.reserve(CKFFBenchmark::DrawsPerFrame);
    const FingerprintObserver recordDirect{
        direct.Backend, direct.Textures, directDigests};
    TestCheck(workload.RunCoreVertexBufferFrame(
                  direct.Pipeline, direct.Textures, 211, 311,
                  direct.VertexLayout, direct.FormatFlags, true, recordDirect),
              "direct material frame must complete");

    FFPTranslatedWorld world;
    TestCheck(world.CreateContext(640, 480),
              "translated benchmark workload context");
    CKFFBenchmark::PublicResources resources;
    TestCheck(workload.CreatePublicResources(*world.Context, resources),
              "translated benchmark workload resources");
    workload.ConfigurePublic(*world.Context, resources);
    std::vector<DrawFingerprint> translatedDigests;
    translatedDigests.reserve(CKFFBenchmark::DrawsPerFrame);
    const FingerprintObserver recordTranslated{
        *world.Backend, resources.Textures, translatedDigests};
    TestCheck(workload.RunTranslatedVertexBufferFrame(
                  *world.Context, resources, recordTranslated),
              "translated material frame must complete");

    if (directDigests.size() == translatedDigests.size()) {
        for (size_t draw = 0; draw < directDigests.size(); ++draw) {
            for (size_t field = 0; field < directDigests[draw].size(); ++field) {
                if (directDigests[draw][field] !=
                    translatedDigests[draw][field]) {
                    std::cout << "draw " << draw << ", normalized field "
                              << field << ": direct="
                              << directDigests[draw][field]
                              << ", translated="
                              << translatedDigests[draw][field] << '\n';
                }
            }
        }
    }
    TestCheck(directDigests.size() == CKFFBenchmark::DrawsPerFrame &&
                  translatedDigests.size() == CKFFBenchmark::DrawsPerFrame &&
                  directDigests == translatedDigests,
              "direct and public translated paths must emit equivalent normalized draws");
    // FFPRecordingBackend has no native PresentTexture implementation, so
    // BackToFront records one shader presentation draw after the 44 scene
    // draws.  The benchmark backend implements PresentTexture as a fixed
    // no-GPU operation and therefore counts only the 44 scene draws.
    if (world.Backend->Log.DrawCount != CKFFBenchmark::DrawsPerFrame + 1 ||
        world.Backend->Log.DiscardCount != 0 ||
        world.Backend->Log.VertexBufferSetCount != CKFFBenchmark::DrawsPerFrame ||
        world.Backend->Log.IndexBufferSetCount != CKFFBenchmark::DrawsPerFrame ||
        world.Backend->GetFrameNumber() != 1) {
        std::cout << "translated counters: draws=" << world.Backend->Log.DrawCount
                  << ", discarded=" << world.Backend->Log.DiscardCount
                  << ", vb=" << world.Backend->Log.VertexBufferSetCount
                  << ", ib=" << world.Backend->Log.IndexBufferSetCount
                  << ", frame=" << world.Backend->GetFrameNumber() << '\n';
    }
    TestCheck(world.Backend->Log.DrawCount == CKFFBenchmark::DrawsPerFrame + 1 &&
                  world.Backend->Log.DiscardCount == 0 &&
                  world.Backend->Log.VertexBufferSetCount ==
                      CKFFBenchmark::DrawsPerFrame &&
                  world.Backend->Log.IndexBufferSetCount ==
                      CKFFBenchmark::DrawsPerFrame &&
                  world.Backend->GetFrameNumber() == 1,
              "translated frame must submit once without discarded draws");
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("44-draw persistent vertex-buffer workloads",
              &CoreVertexBufferWorkloadsSubmitExactly44Draws);
    tests.Run("44-draw transient workload shape",
              &TransientWorkloadsKeepTheirShapeAndPacking);
    tests.Run("direct and translated benchmark workload equivalence",
              &DirectAndTranslatedMaterialFramesAreEquivalent);
    return tests.ExitCode();
}
