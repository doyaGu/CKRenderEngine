#include "FFPBenchmarkWorkload.h"
#include "CKBenchmarkBackend.h"
#include "CKFFDrawTypes.h"
#include "CKFFStateResolver.h"
#include "CKFFTextureBinder.h"
#include "CKFFUniformEmitter.h"
#include "CKRecordingRasterizer.h"
#include "FFPRecordingContext.h"
#include "CKFFTestPipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

uint64_t Mix(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t MixBytes(uint64_t hash, const void *data, size_t size)
{
    if (!data || size == 0)
        return Mix(hash, 0);
    const CKBYTE *bytes = static_cast<const CKBYTE *>(data);
    hash = Mix(hash, size);
    hash = Mix(hash, bytes[0]);
    hash = Mix(hash, bytes[size / 2]);
    hash = Mix(hash, bytes[size - 1]);
    return hash;
}

struct Observation {
    bool Ok = true;
    uint64_t Draws = 0;
    uint64_t Bytes = 0;
    uint64_t Checksum = 1469598103934665603ull;
};

class BenchmarkTask {
public:
    virtual ~BenchmarkTask() = default;
    virtual Observation Run(uint64_t iterations) = 0;
};

struct BenchmarkCase {
    std::string Name;
    std::shared_ptr<BenchmarkTask> Task;
    uint64_t DrawsPerIteration = 1;
    uint64_t BytesPerIteration = 0;
    uint64_t IterationQuantum = 1;

    Observation Run(uint64_t iterations) const
    {
        return Task->Run(iterations);
    }
};

struct CaseResult {
    std::string Name;
    uint64_t Iterations = 0;
    uint64_t DrawsPerSample = 0;
    uint64_t BytesPerSample = 0;
    uint64_t DrawsPerIteration = 0;
    uint64_t BytesPerIteration = 0;
    uint64_t Checksum = 0;
    double MedianNsPerDraw = 0.0;
    double MedianUsPerFrame = 0.0;
    double P10UsPerFrame = 0.0;
    double P90UsPerFrame = 0.0;
    double MadPercent = 0.0;
    bool Unstable = false;
};

struct TimedObservation {
    Observation Value;
    double ElapsedNs = 0.0;
};

TimedObservation TimeCase(const BenchmarkCase &test, uint64_t iterations)
{
    const auto begin = Clock::now();
    Observation observation = test.Run(iterations);
    const auto end = Clock::now();
    return {observation,
            std::chrono::duration<double, std::nano>(end - begin).count()};
}

double Quantile(const std::vector<double> &sorted, double quantile)
{
    if (sorted.empty())
        return 0.0;
    const double position = quantile * static_cast<double>(sorted.size() - 1);
    const size_t lower = static_cast<size_t>(position);
    const size_t upper = (std::min)(lower + 1, sorted.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

uint64_t RoundUp(uint64_t value, uint64_t quantum)
{
    if (quantum <= 1)
        return value;
    return ((value + quantum - 1) / quantum) * quantum;
}

bool ValidateObservation(const BenchmarkCase &test, uint64_t iterations,
                         const Observation &observation,
                         std::string &error)
{
    if (!observation.Ok) {
        error = "workload returned failure";
        return false;
    }
    const uint64_t expectedDraws = iterations * test.DrawsPerIteration;
    const uint64_t expectedBytes = iterations * test.BytesPerIteration;
    if (observation.Draws != expectedDraws) {
        error = "draw count mismatch: expected " +
                std::to_string(expectedDraws) + ", got " +
                std::to_string(observation.Draws);
        return false;
    }
    if (observation.Bytes != expectedBytes) {
        error = "byte count mismatch: expected " +
                std::to_string(expectedBytes) + ", got " +
                std::to_string(observation.Bytes);
        return false;
    }
    if (observation.Checksum == 0) {
        error = "zero checksum";
        return false;
    }
    return true;
}

bool MeasureCase(const BenchmarkCase &test, int sampleCount,
                 double sampleMilliseconds, double warmupMilliseconds,
                 CaseResult &result, std::string &error)
{
    const auto warmupStart = Clock::now();
    uint64_t warmupIterations = test.IterationQuantum;
    do {
        const Observation observation = test.Run(warmupIterations);
        if (!observation.Ok) {
            error = "warmup failed";
            return false;
        }
        warmupIterations = (std::min<uint64_t>)(warmupIterations * 2,
                                                1u << 20);
    } while (std::chrono::duration<double, std::milli>(
                 Clock::now() - warmupStart).count() < warmupMilliseconds);

    uint64_t iterations = test.IterationQuantum;
    TimedObservation calibration = TimeCase(test, iterations);
    while (calibration.ElapsedNs < 1000000.0) {
        iterations = RoundUp(iterations * 10, test.IterationQuantum);
        calibration = TimeCase(test, iterations);
    }
    const double targetNs = sampleMilliseconds * 1000000.0;
    const double scale = targetNs / calibration.ElapsedNs;
    iterations = RoundUp((std::max<uint64_t>)(
        test.IterationQuantum,
        static_cast<uint64_t>(std::ceil(static_cast<double>(iterations) *
                                        scale * 1.05))),
        test.IterationQuantum);

    std::vector<double> nsPerDraw;
    nsPerDraw.reserve(static_cast<size_t>(sampleCount));
    uint64_t checksum = 0;
    while (nsPerDraw.size() < static_cast<size_t>(sampleCount)) {
        const TimedObservation timed = TimeCase(test, iterations);
        if (!ValidateObservation(test, iterations, timed.Value, error))
            return false;
        if (timed.ElapsedNs < targetNs) {
            const double retryScale = targetNs / timed.ElapsedNs;
            iterations = RoundUp(static_cast<uint64_t>(std::ceil(
                static_cast<double>(iterations) * retryScale * 1.02)),
                test.IterationQuantum);
            nsPerDraw.clear();
            checksum = 0;
            continue;
        }
        if (nsPerDraw.empty())
            checksum = timed.Value.Checksum;
        else if (checksum != timed.Value.Checksum) {
            error = "checksum changed between samples";
            return false;
        }
        nsPerDraw.push_back(timed.ElapsedNs /
                            static_cast<double>(timed.Value.Draws));
    }

    std::sort(nsPerDraw.begin(), nsPerDraw.end());
    const double median = Quantile(nsPerDraw, 0.5);
    std::vector<double> deviations;
    deviations.reserve(nsPerDraw.size());
    for (double sample : nsPerDraw)
        deviations.push_back(std::abs(sample - median));
    std::sort(deviations.begin(), deviations.end());
    const double mad = Quantile(deviations, 0.5);

    result.Name = test.Name;
    result.Iterations = iterations;
    result.DrawsPerSample = iterations * test.DrawsPerIteration;
    result.BytesPerSample = iterations * test.BytesPerIteration;
    result.DrawsPerIteration = test.DrawsPerIteration;
    result.BytesPerIteration = test.BytesPerIteration;
    result.Checksum = checksum;
    result.MedianNsPerDraw = median;
    result.MedianUsPerFrame = median * CKFFBenchmark::DrawsPerFrame / 1000.0;
    result.P10UsPerFrame = Quantile(nsPerDraw, 0.1) *
                           CKFFBenchmark::DrawsPerFrame / 1000.0;
    result.P90UsPerFrame = Quantile(nsPerDraw, 0.9) *
                           CKFFBenchmark::DrawsPerFrame / 1000.0;
    result.MadPercent = median > 0.0 ? mad * 100.0 / median : 0.0;
    result.Unstable = result.MadPercent > 3.0;
    return true;
}

bool InitBackend(CKBenchmarkBackend &backend)
{
    CKRasterizerInitParameters desc;
    desc.Width = 640;
    desc.Height = 480;
    desc.Bpp = 32;
    desc.ZBpp = 24;
    desc.StencilBpp = 8;
    if (backend.Init(&desc) != CK_OK)
        return false;
    CKRenderPassDesc pass;
    pass.Rect.right = 640;
    pass.Rect.bottom = 480;
    return backend.BeginPass(&pass) == CK_OK;
}

struct CoreFixture {
    const CKFFBenchmark::Workload &Workload;
    CKBenchmarkBackend Backend;
    CKFFTestPipeline Pipeline;
    std::array<CKDWORD, 4> Textures{{101, 102, 103, 104}};
    CKDWORD FormatFlags = 0;
    CKDWORD VertexLayout = 0;
    bool Valid = false;

    explicit CoreFixture(const CKFFBenchmark::Workload &workload)
        : Workload(workload)
    {
        CKFFShaderSet shaders;
        Valid = InitBackend(Backend) &&
                CKRecordingShaderSet(Backend.GetCaps(), shaders) &&
                Pipeline.Init(&Backend, shaders);
        if (!Valid)
            return;
        FormatFlags = CKFFVertexLayout::DPFlagsToFormatFlags(
            CKFFBenchmark::VertexFormat, true, true);
        VertexLayout = Pipeline.ResolveVertexLayout(FormatFlags);
        Valid = FormatFlags != 0 && VertexLayout != 0;
        if (Valid)
            Workload.ConfigureCore(Pipeline, Textures);
    }

    ~CoreFixture()
    {
        Pipeline.Shutdown();
        Backend.Shutdown();
    }
};

class BenchmarkRecordingWorld {
public:
    explicit BenchmarkRecordingWorld(const CKFFBenchmark::Workload &workload)
    {
        CKBenchmarkRasterizer *rasterizer = new CKBenchmarkRasterizer();
        Rasterizer = rasterizer;
        if (!Rasterizer->Start(nullptr)) {
            delete Rasterizer;
            Rasterizer = nullptr;
            return;
        }
        if (!Rasterizer || Rasterizer->GetDriverCount() != 1)
            return;
        Driver = static_cast<CKBenchmarkRasterizerDriver *>(Rasterizer->GetDriver(0));
        Context = Driver
            ? static_cast<FFPRecordingContext *>(Driver->CreateContext())
            : nullptr;
        if (!Context || !Context->Create(nullptr, 0, 0, 640, 480, 32,
                                         FALSE, 60, 24, 8))
            return;
        Backend = static_cast<CKBenchmarkBackend *>(Context->GetBackend());
        Valid = Backend && workload.CreatePublicResources(*Context, Resources);
        if (Valid)
            workload.ConfigurePublic(*Context, Resources);
    }

    ~BenchmarkRecordingWorld()
    {
        if (Rasterizer)
            delete Rasterizer;
    }

    CKRasterizer *Rasterizer = nullptr;
    CKBenchmarkRasterizerDriver *Driver = nullptr;
    FFPRecordingContext *Context = nullptr;
    CKBenchmarkBackend *Backend = nullptr;
    CKFFBenchmark::PublicResources Resources;
    bool Valid = false;
};

void SetMaterialConstants(CKFFMaterialData &out,
                          const CKMaterialData &material)
{
    const VxColor colors[] = {material.Diffuse, material.Ambient,
                              material.Specular, material.Emissive};
    float *destinations[] = {out.Diffuse, out.Ambient,
                             out.Specular, out.Emissive};
    for (int i = 0; i < 4; ++i) {
        destinations[i][0] = colors[i].r;
        destinations[i][1] = colors[i].g;
        destinations[i][2] = colors[i].b;
        destinations[i][3] = colors[i].a;
    }
    out.Power = material.SpecularPower;
}

void ConfigureState(CKFFStateStore &state,
                    const CKFFBenchmark::Workload &workload,
                    CKDWORD profileIndex, CKDWORD textureCount)
{
    state.Reset();
    const CKFFBenchmark::MaterialProfile &profile =
        workload.Profiles[profileIndex & 7u];
    state.Material = profile.RenderState.Material;
    SetMaterialConstants(state.MaterialConstants, state.Material);
    state.DrawState.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    state.DrawState.SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE,
                                   profile.RenderState.AlphaBlend);
    state.DrawState.SetRenderState(VXRENDERSTATE_SRCBLEND,
                                   profile.RenderState.SourceBlend);
    state.DrawState.SetRenderState(VXRENDERSTATE_DESTBLEND,
                                   profile.RenderState.DestBlend);
    state.DrawState.SetRenderState(VXRENDERSTATE_ZWRITEENABLE,
                                   profile.RenderState.ZWrite);
    state.DrawState.SetRenderState(VXRENDERSTATE_ZFUNC,
                                   profile.RenderState.ZFunc);
    state.DrawState.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE,
                                   profile.AlphaTest);
    state.DrawState.SetRenderState(VXRENDERSTATE_ALPHAFUNC,
                                   profile.AlphaFunc);
    state.DrawState.SetRenderState(VXRENDERSTATE_ALPHAREF,
                                   profile.AlphaRef);
    state.ActiveLightCount = 1;
    state.LightEnabled[0] = TRUE;
    state.LightConstants[0].Direction[2] = -1.0f;
    state.LightConstants[0].Direction[3] = 1000.0f;
    state.LightConstants[0].Diffuse[0] = 0.9f;
    state.LightConstants[0].Diffuse[1] = 0.85f;
    state.LightConstants[0].Diffuse[2] = 0.8f;
    state.LightConstants[0].Diffuse[3] = 1.0f;

    const CKDWORD explicitStates[] = {
        CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2,
        CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
        CKRST_TSS_ADDRESS, CKRST_TSS_MINFILTER, CKRST_TSS_MAGFILTER};
    for (CKDWORD stage = 0; stage < textureCount; ++stage) {
        state.TextureHandles[stage] = 101 + stage;
        state.TextureFlags[stage] = CKFFBenchmark::TextureFlags;
        state.StageStates[stage][CKRST_TSS_OP] = profile.TextureOp;
        state.StageStates[stage][CKRST_TSS_ARG1] = CKRST_TA_TEXTURE;
        state.StageStates[stage][CKRST_TSS_ARG2] = CKRST_TA_DIFFUSE;
        state.StageStates[stage][CKRST_TSS_AOP] = CKRST_TOP_MODULATE;
        state.StageStates[stage][CKRST_TSS_AARG1] = CKRST_TA_TEXTURE;
        state.StageStates[stage][CKRST_TSS_AARG2] = CKRST_TA_DIFFUSE;
        state.StageStates[stage][CKRST_TSS_ADDRESS] = profile.Address;
        state.StageStates[stage][CKRST_TSS_MINFILTER] = profile.Filter;
        state.StageStates[stage][CKRST_TSS_MAGFILTER] = profile.Filter;
        for (CKDWORD value : explicitStates)
            state.StageStateSetMasks[stage] |= 1ull << value;
    }
}

struct ComponentFixture {
    const CKFFBenchmark::Workload &Workload;
    CKBenchmarkBackend Backend;
    CKFFTestShaderCache ShaderCache;
    CKFFDrawProbes Probes;
    std::array<CKFFStateStore, 8> States;
    std::array<CKFFShaderKey, 8> Keys;
    std::array<CKFFProgramContext, 8> Contexts;
    CKFFStateStore UniformState;
    CKFFConstantSet ObjectConstants;
    CKFFConstantSet StaticConstants;
    CKFFConstantSet FullConstants;
    CKDWORD ShaderTargetFlags = 0;
    std::unique_ptr<CKFFTextureBinder> UniformTextureBinder;
    CKFFTextureBindingSet UniformTextureBindings;
    std::unique_ptr<CKFFUniformEmitter> UniformEmitter;
    CKFFStateStore OneTextureState;
    CKFFStateStore FourTextureState;
    CKFFSamplerLayoutPlan OneTextureLayoutPlan;
    CKFFSamplerLayoutPlan FourTextureLayoutPlan;
    std::unique_ptr<CKFFTextureBinder> OneTextureBinder;
    std::unique_ptr<CKFFTextureBinder> FourTextureBinder;
    bool Valid = false;

    explicit ComponentFixture(const CKFFBenchmark::Workload &workload)
        : Workload(workload)
    {
        CKFFShaderSet shaders;
        Valid = InitBackend(Backend) &&
                CKRecordingShaderSet(Backend.GetCaps(), shaders) &&
                ShaderCache.Init(Backend.GetCaps(), shaders);
        if (!Valid)
            return;
        ShaderTargetFlags = ShaderCache.GetTargetFlags();
        for (CKDWORD i = 0; i < States.size(); ++i) {
            ConfigureState(States[i], Workload, i, 1);
            CKFFPreparedState prepared;
            CKFFStateResolver::BuildPreparedState(
                States[i], States[i].DrawState, &prepared,
                CKFFBenchmark::VertexFormat, 1,
                CKFFVertexLayout::DPFlagsToFormatFlags(
                    CKFFBenchmark::VertexFormat, true, true),
                States[i].TexcoordComponentCounts, FALSE);
            Keys[i] = CKFFBuildShaderKeyFromPreparedState(&prepared);
            const CKFFProgramBinding binding = ShaderCache.GetProgram(&Backend, Keys[i]);
            if (!binding.Program) {
                Valid = false;
                return;
            }
            const CKFFSamplerLayoutPlan samplerLayoutPlan =
                CKFFBuildSamplerLayoutPlan(Keys[i].FS);
            CKFFInitProgramContext(&Contexts[i], Keys[i], binding.FragmentProgram,
                                   samplerLayoutPlan);
        }
        ConfigureState(UniformState, Workload, 0, 1);
        UniformTextureBinder = std::make_unique<CKFFTextureBinder>(
            UniformState, ShaderTargetFlags, Probes);
        UniformTextureBinder->BuildBindingSet(
            &UniformTextureBindings, 1,
            Contexts[0].SamplerLayoutPlan);
        UniformEmitter = std::make_unique<CKFFUniformEmitter>(
            UniformState, UniformState.DrawState, ShaderTargetFlags, Probes);
        ConfigureState(OneTextureState, Workload, 0, 1);
        ConfigureState(FourTextureState, Workload, 0, 4);
        CKFFShaderKeyFS textureKeys[2];
        for (CKDWORD stage = 0; stage < 4; ++stage) {
            textureKeys[1].Stages[stage].HasTexture = true;
            textureKeys[1].Stages[stage].SamplerType = CKFF_SAMPLER_2D;
            if (stage == 0)
                textureKeys[0].Stages[stage] = textureKeys[1].Stages[stage];
        }
        OneTextureLayoutPlan = CKFFBuildSamplerLayoutPlan(textureKeys[0]);
        FourTextureLayoutPlan = CKFFBuildSamplerLayoutPlan(textureKeys[1]);
        OneTextureBinder = std::make_unique<CKFFTextureBinder>(
            OneTextureState, ShaderTargetFlags, Probes);
        FourTextureBinder = std::make_unique<CKFFTextureBinder>(
            FourTextureState, ShaderTargetFlags, Probes);
    }

    ~ComponentFixture()
    {
        ShaderCache.Shutdown(&Backend);
        Backend.Shutdown();
    }

    void SetUniformProfile(CKDWORD profileIndex)
    {
        const CKFFBenchmark::MaterialProfile &profile =
            Workload.Profiles[profileIndex & 7u];
        UniformState.Material = profile.RenderState.Material;
        SetMaterialConstants(UniformState.MaterialConstants,
                             UniformState.Material);
        UniformState.DrawState.SetRenderState(
            VXRENDERSTATE_ALPHABLENDENABLE,
            profile.RenderState.AlphaBlend);
        UniformState.DrawState.SetRenderState(
            VXRENDERSTATE_ALPHATESTENABLE, profile.AlphaTest);
        UniformState.DrawState.SetRenderState(VXRENDERSTATE_ALPHAFUNC,
                                               profile.AlphaFunc);
        UniformState.DrawState.SetRenderState(VXRENDERSTATE_ALPHAREF,
                                               profile.AlphaRef);
        UniformState.StageStates[0][CKRST_TSS_OP] = profile.TextureOp;
    }
};

uint64_t ConstantsChecksum(const CKFFConstantSet &constants)
{
    uint64_t hash = 1469598103934665603ull;
    for (CKDWORD slot = 0; slot < CKFF_CONSTANT_SLOT_COUNT; ++slot) {
        const auto &bytes = constants[slot].Bytes;
        hash = MixBytes(hash, bytes.Begin(), bytes.Size());
    }
    return hash;
}

enum class CoreCaseKind {
    VertexBufferSteady,
    VertexBufferMaterial8,
    VertexBufferCpuIndices,
    TransientCommon,
    TransientGeneral
};

class CoreBenchmarkTask final : public BenchmarkTask {
public:
    CoreBenchmarkTask(const CKFFBenchmark::Workload &workload, CoreCaseKind kind)
        : m_Fixture(workload), m_Kind(kind)
    {
        if (kind == CoreCaseKind::TransientCommon)
            m_Fixture.Pipeline.SetTexcoordComponentCount(0, 2);
        else if (kind == CoreCaseKind::TransientGeneral)
            m_Fixture.Pipeline.SetTexcoordComponentCount(0, 3);
    }

    Observation Run(uint64_t frames) override
    {
        m_Fixture.Backend.ResetMeasurements();
        bool ok = m_Fixture.Valid;
        for (uint64_t frame = 0; ok && frame < frames; ++frame) {
            switch (m_Kind) {
            case CoreCaseKind::VertexBufferSteady:
                ok = m_Fixture.Workload.RunCoreVertexBufferFrame(
                    m_Fixture.Pipeline, m_Fixture.Textures, 201, 301,
                    m_Fixture.VertexLayout, m_Fixture.FormatFlags, false);
                break;
            case CoreCaseKind::VertexBufferMaterial8:
                ok = m_Fixture.Workload.RunCoreVertexBufferFrame(
                    m_Fixture.Pipeline, m_Fixture.Textures, 202, 302,
                    m_Fixture.VertexLayout, m_Fixture.FormatFlags, true);
                break;
            case CoreCaseKind::VertexBufferCpuIndices:
                ok = m_Fixture.Workload.RunCoreCpuIndexFrame(
                    m_Fixture.Pipeline, m_Fixture.Textures, 203,
                    m_Fixture.VertexLayout, m_Fixture.FormatFlags);
                break;
            case CoreCaseKind::TransientCommon:
                ok = m_Fixture.Workload.RunCoreTransientFrame(
                    m_Fixture.Pipeline, false);
                break;
            case CoreCaseKind::TransientGeneral:
                ok = m_Fixture.Workload.RunCoreTransientFrame(
                    m_Fixture.Pipeline, true);
                break;
            }
        }
        return {ok, m_Fixture.Backend.DrawCount(),
                m_Fixture.Backend.TransientBytes(),
                m_Fixture.Backend.Checksum()};
    }

private:
    CoreFixture m_Fixture;
    CoreCaseKind m_Kind;
};

class TranslatedBenchmarkTask final : public BenchmarkTask {
public:
    explicit TranslatedBenchmarkTask(const CKFFBenchmark::Workload &workload)
        : m_Workload(workload), m_World(workload) {}

    Observation Run(uint64_t frames) override
    {
        if (!m_World.Valid)
            return {false, 0, 0, 0};
        m_World.Backend->ResetMeasurements();
        bool ok = true;
        for (uint64_t frame = 0; ok && frame < frames; ++frame) {
            ok = m_Workload.RunTranslatedVertexBufferFrame(
                *m_World.Context, m_World.Resources);
        }
        ok = ok && m_World.Backend->PassCount() == frames &&
             m_World.Backend->PresentCount() == frames &&
             m_World.Backend->SubmitCount() == frames;
        return {ok, m_World.Backend->DrawCount(),
                m_World.Backend->TransientBytes(),
                m_World.Backend->Checksum()};
    }

private:
    const CKFFBenchmark::Workload &m_Workload;
    BenchmarkRecordingWorld m_World;
};

enum class ComponentCaseKind {
    PreparedSteady,
    PreparedMaterial8,
    ShaderHit,
    ShaderMaterial8,
    UniformObject,
    UniformStatic,
    UniformFull,
    TextureBinding1,
    TextureBinding4
};

class ComponentBenchmarkTask final : public BenchmarkTask {
public:
    ComponentBenchmarkTask(std::shared_ptr<ComponentFixture> fixture,
                           ComponentCaseKind kind, CKDWORD formatFlags)
        : m_Fixture(std::move(fixture)), m_Kind(kind),
          m_FormatFlags(formatFlags) {}

    Observation Run(uint64_t count) override
    {
        if (!m_Fixture->Valid)
            return {false, 0, 0, 0};
        switch (m_Kind) {
        case ComponentCaseKind::PreparedSteady:
            return RunPrepared(count, false);
        case ComponentCaseKind::PreparedMaterial8:
            return RunPrepared(count, true);
        case ComponentCaseKind::ShaderHit:
            return RunShaderLookup(count, false);
        case ComponentCaseKind::ShaderMaterial8:
            return RunShaderLookup(count, true);
        case ComponentCaseKind::UniformObject:
            return RunObjectUniform(count);
        case ComponentCaseKind::UniformStatic:
            return RunStaticUniform(count);
        case ComponentCaseKind::UniformFull:
            return RunFullUniform(count);
        case ComponentCaseKind::TextureBinding1:
            return RunTextureBinding(count, 1);
        case ComponentCaseKind::TextureBinding4:
            return RunTextureBinding(count, 4);
        }
        return {false, 0, 0, 0};
    }

private:
    Observation RunPrepared(uint64_t count, bool rotateMaterials)
    {
        uint64_t hash = 1469598103934665603ull;
        CKFFPreparedState prepared;
        for (uint64_t i = 0; i < count; ++i) {
            const CKDWORD profile = rotateMaterials
                ? static_cast<CKDWORD>(i & 7u) : 0u;
            CKFFStateResolver::BuildPreparedState(
                m_Fixture->States[profile], m_Fixture->States[profile].DrawState,
                &prepared, CKFFBenchmark::VertexFormat, 1, m_FormatFlags,
                m_Fixture->States[profile].TexcoordComponentCounts, FALSE);
            hash = Mix(hash, prepared.ActiveTextureCount);
            hash = Mix(hash, prepared.TextureBoundMask);
            hash = Mix(hash, prepared.StateDesc.FS.GetStageColorOp(0));
        }
        return {true, count, 0, hash};
    }

    Observation RunShaderLookup(uint64_t count, bool rotateMaterials)
    {
        uint64_t hash = 1469598103934665603ull;
        for (uint64_t i = 0; i < count; ++i) {
            const size_t profile = rotateMaterials ? size_t(i & 7u) : 0u;
            const CKFFProgramBinding binding =
                m_Fixture->ShaderCache.GetProgram(
                    &m_Fixture->Backend, m_Fixture->Keys[profile]);
            hash = Mix(hash, binding.Program);
            hash = Mix(hash, binding.FragmentProgram.Lanes()[0]);
        }
        return {true, count, 0, hash};
    }

    Observation RunObjectUniform(uint64_t count)
    {
        uint64_t hash = 1469598103934665603ull;
        bool ok = true;
        m_Fixture->SetUniformProfile(0);
        for (uint64_t i = 0; ok && i < count; ++i) {
            m_Fixture->UniformState.World = m_Fixture->Workload.Worlds[
                static_cast<size_t>(i % CKFFBenchmark::DrawsPerFrame)];
            ok = m_Fixture->UniformEmitter->UploadObjectUniforms(
                     &m_Fixture->ObjectConstants, &m_Fixture->Contexts[0], 1) != FALSE;
            hash = Mix(hash, ConstantsChecksum(m_Fixture->ObjectConstants));
        }
        return {ok, count, 0, hash};
    }

    Observation RunStaticUniform(uint64_t count)
    {
        uint64_t hash = 1469598103934665603ull;
        bool ok = true;
        m_Fixture->UniformState.World = m_Fixture->Workload.Worlds[0];
        for (uint64_t i = 0; ok && i < count; ++i) {
            const CKDWORD profile = static_cast<CKDWORD>(i & 7u);
            m_Fixture->SetUniformProfile(profile);
            ok = m_Fixture->UniformEmitter->UploadStaticUniforms(
                     &m_Fixture->StaticConstants,
                     &m_Fixture->Contexts[profile],
                     m_Fixture->UniformTextureBindings) != FALSE;
            hash = Mix(hash, ConstantsChecksum(m_Fixture->StaticConstants));
        }
        return {ok, count, 0, hash};
    }

    Observation RunFullUniform(uint64_t count)
    {
        uint64_t hash = 1469598103934665603ull;
        bool ok = true;
        for (uint64_t i = 0; ok && i < count; ++i) {
            const CKDWORD profile = static_cast<CKDWORD>(i & 7u);
            m_Fixture->SetUniformProfile(profile);
            m_Fixture->UniformState.World = m_Fixture->Workload.Worlds[
                static_cast<size_t>(i % CKFFBenchmark::DrawsPerFrame)];
            ok = m_Fixture->UniformEmitter->UploadUniforms(
                     &m_Fixture->FullConstants, &m_Fixture->Contexts[profile],
                     m_Fixture->UniformTextureBindings, i + 1, i + 1) != FALSE;
            hash = Mix(hash, ConstantsChecksum(m_Fixture->FullConstants));
        }
        return {ok, count, 0, hash};
    }

    Observation RunTextureBinding(uint64_t count, CKDWORD textureCount)
    {
        uint64_t hash = 1469598103934665603ull;
        CKFFTextureBinder &binder = textureCount == 1
            ? *m_Fixture->OneTextureBinder : *m_Fixture->FourTextureBinder;
        const CKFFSamplerLayoutPlan &layoutPlan = textureCount == 1
            ? m_Fixture->OneTextureLayoutPlan
            : m_Fixture->FourTextureLayoutPlan;
        for (uint64_t i = 0; i < count; ++i) {
            const CKFFTextureBindingSet &set =
                binder.ResolveBindingSet(textureCount, layoutPlan);
            hash = Mix(hash, set.Hash);
            hash = Mix(hash, set.Bindings[textureCount - 1].Texture);
        }
        return {true, count, 0, hash};
    }

    std::shared_ptr<ComponentFixture> m_Fixture;
    ComponentCaseKind m_Kind;
    CKDWORD m_FormatFlags;
};

class DrawStateBenchmarkTask final : public BenchmarkTask {
public:
    explicit DrawStateBenchmarkTask(bool rebuild) : m_Rebuild(rebuild)
    {
        if (!m_Rebuild)
            m_Cache.BuildDrawState(VX_TRIANGLELIST);
    }

    Observation Run(uint64_t count) override
    {
        uint64_t hash = 1469598103934665603ull;
        for (uint64_t i = 0; i < count; ++i) {
            if (m_Rebuild) {
                m_Cache.SetRenderState(
                    VXRENDERSTATE_ALPHABLENDENABLE, (i & 1u) ? TRUE : FALSE);
            }
            const CKDrawState state = m_Cache.BuildDrawState(VX_TRIANGLELIST);
            hash = Mix(hash, state.Lo);
            hash = Mix(hash, state.Mid);
        }
        return {true, count, 0, hash};
    }

private:
    CKDrawStateCache m_Cache;
    bool m_Rebuild;
};

class InterleaveBenchmarkTask final : public BenchmarkTask {
public:
    InterleaveBenchmarkTask(const CKFFBenchmark::Workload &workload,
                            CKDWORD formatFlags, bool general)
        : m_Workload(workload), m_FormatFlags(formatFlags), m_General(general) {}

    Observation Run(uint64_t count) override
    {
        VxDrawPrimitiveData data = m_Workload.PrimitiveData(m_General);
        CKBYTE dimensions[CKRST_MAX_STAGES] = {};
        dimensions[0] = m_General ? 3 : 2;
        for (uint64_t i = 0; i < count; ++i) {
            CKTransientGeometry::InterleaveVertices(
                m_Output.data(), CKFFBenchmark::PackedVertexStride,
                CKFFBenchmark::VerticesPerDraw, m_FormatFlags,
                &data, dimensions);
        }
        const uint64_t hash = MixBytes(1469598103934665603ull,
                                       m_Output.data(), m_Output.size());
        return {true, count, count * m_Output.size(), hash};
    }

private:
    const CKFFBenchmark::Workload &m_Workload;
    CKDWORD m_FormatFlags;
    bool m_General;
    std::array<CKBYTE, CKFFBenchmark::VerticesPerDraw *
                       CKFFBenchmark::PackedVertexStride> m_Output = {};
};

class SinkBenchmarkTask final : public BenchmarkTask {
public:
    SinkBenchmarkTask()
    {
        m_Valid = InitBackend(m_Backend);
        const float value[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        m_Constants.Set(0, value, sizeof(value));
        m_Textures[0].Texture = 101;
    }

    Observation Run(uint64_t frames) override
    {
        m_Backend.ResetMeasurements();
        CKDrawCommand draw;
        draw.Program = 1;
        draw.Layout = 1;
        draw.VertexBuffer = 201;
        draw.IndexBuffer = 301;
        draw.VertexCount = CKFFBenchmark::VerticesPerDraw;
        draw.IndexCount = CKFFBenchmark::IndicesPerDraw;
        draw.Constants = &m_Constants;
        draw.Textures = &m_Textures;
        bool ok = m_Valid;
        const uint64_t count = frames * CKFFBenchmark::DrawsPerFrame;
        for (uint64_t i = 0; ok && i < count; ++i)
            ok = m_Backend.Draw(&draw) == CK_OK;
        return {ok, m_Backend.DrawCount(), m_Backend.TransientBytes(),
                m_Backend.Checksum()};
    }

private:
    CKBenchmarkBackend m_Backend;
    CKFFConstantSet m_Constants;
    CKFFTextureBindings m_Textures;
    bool m_Valid = false;
};

std::vector<BenchmarkCase> BuildCases(const CKFFBenchmark::Workload &workload)
{
    std::vector<BenchmarkCase> cases;
    cases.push_back({"core_vb_steady_44",
        std::make_shared<CoreBenchmarkTask>(
            workload, CoreCaseKind::VertexBufferSteady),
        CKFFBenchmark::DrawsPerFrame, 0, 1});
    cases.push_back({"core_vb_material8_44",
        std::make_shared<CoreBenchmarkTask>(
            workload, CoreCaseKind::VertexBufferMaterial8),
        CKFFBenchmark::DrawsPerFrame, 0, 1});
    cases.push_back({"core_vb_cpu_indices_44",
        std::make_shared<CoreBenchmarkTask>(
            workload, CoreCaseKind::VertexBufferCpuIndices),
        CKFFBenchmark::DrawsPerFrame,
        CKFFBenchmark::CpuIndexBytesPerFrame, 1});
    cases.push_back({"core_transient_common_44",
        std::make_shared<CoreBenchmarkTask>(
            workload, CoreCaseKind::TransientCommon),
        CKFFBenchmark::DrawsPerFrame,
        CKFFBenchmark::TransientBytesPerFrame, 1});
    cases.push_back({"core_transient_general_44",
        std::make_shared<CoreBenchmarkTask>(
            workload, CoreCaseKind::TransientGeneral),
        CKFFBenchmark::DrawsPerFrame,
        CKFFBenchmark::TransientBytesPerFrame, 1});
    cases.push_back({"translated_vbib_material8_44",
        std::make_shared<TranslatedBenchmarkTask>(workload),
        CKFFBenchmark::DrawsPerFrame, 0, 1});

    const CKDWORD formatFlags = CKFFVertexLayout::DPFlagsToFormatFlags(
        CKFFBenchmark::VertexFormat, true, true);
    auto components = std::make_shared<ComponentFixture>(workload);
    cases.push_back({"prepared_state_steady",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::PreparedSteady, formatFlags), 1, 0, 8});
    cases.push_back({"prepared_state_material8",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::PreparedMaterial8, formatFlags), 1, 0, 8});
    cases.push_back({"shader_lookup_hit",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::ShaderHit, formatFlags), 1, 0, 8});
    cases.push_back({"shader_lookup_material8",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::ShaderMaterial8, formatFlags), 1, 0, 8});
    cases.push_back({"uniform_object_change",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::UniformObject, formatFlags), 1, 0, 8});
    cases.push_back({"uniform_static_change",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::UniformStatic, formatFlags), 1, 0, 8});
    cases.push_back({"uniform_full_change",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::UniformFull, formatFlags), 1, 0, 8});
    cases.push_back({"texture_binding_1",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::TextureBinding1, formatFlags), 1, 0, 8});
    cases.push_back({"texture_binding_4",
        std::make_shared<ComponentBenchmarkTask>(components,
            ComponentCaseKind::TextureBinding4, formatFlags), 1, 0, 8});

    cases.push_back({"draw_state_cache_hit",
        std::make_shared<DrawStateBenchmarkTask>(false), 1, 0, 8});
    cases.push_back({"draw_state_rebuild",
        std::make_shared<DrawStateBenchmarkTask>(true), 1, 0, 8});
    cases.push_back({"interleave_common",
        std::make_shared<InterleaveBenchmarkTask>(workload, formatFlags, false),
        1, CKFFBenchmark::VerticesPerDraw * CKFFBenchmark::PackedVertexStride, 8});
    cases.push_back({"interleave_general",
        std::make_shared<InterleaveBenchmarkTask>(workload, formatFlags, true),
        1, CKFFBenchmark::VerticesPerDraw * CKFFBenchmark::PackedVertexStride, 8});
    cases.push_back({"sink_draw_44", std::make_shared<SinkBenchmarkTask>(),
                     CKFFBenchmark::DrawsPerFrame, 0, 1});
    return cases;
}

struct Options {
    std::string CaseName = "all";
    std::string Format = "text";
    int Samples = 11;
    double SampleMilliseconds = 250.0;
    double WarmupMilliseconds = 500.0;
    int Cpu = -1;
    bool Help = false;
    bool List = false;
};

bool ParseInteger(const std::string &text, int &value)
{
    char *end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0')
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const std::string &text, double &value)
{
    char *end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end && *end == '\0';
}

bool ParseOptions(int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            options.Help = true;
            continue;
        }
        if (argument == "--list") {
            options.List = true;
            continue;
        }
        const size_t equal = argument.find('=');
        std::string name = equal == std::string::npos
            ? argument : argument.substr(0, equal);
        std::string value;
        if (equal != std::string::npos) {
            value = argument.substr(equal + 1);
        } else {
            if (i + 1 >= argc) {
                error = "missing value for " + name;
                return false;
            }
            value = argv[++i];
        }
        if (name == "--case")
            options.CaseName = value;
        else if (name == "--format")
            options.Format = value;
        else if (name == "--samples") {
            if (!ParseInteger(value, options.Samples)) {
                error = "invalid --samples";
                return false;
            }
        } else if (name == "--sample-ms") {
            if (!ParseDouble(value, options.SampleMilliseconds)) {
                error = "invalid --sample-ms";
                return false;
            }
        } else if (name == "--warmup-ms") {
            if (!ParseDouble(value, options.WarmupMilliseconds)) {
                error = "invalid --warmup-ms";
                return false;
            }
        } else if (name == "--cpu") {
            if (!ParseInteger(value, options.Cpu)) {
                error = "invalid --cpu";
                return false;
            }
        } else {
            error = "unknown option: " + name;
            return false;
        }
    }
    if (options.Samples < 3 || options.Samples > 101 ||
        options.SampleMilliseconds < 1.0 ||
        options.WarmupMilliseconds < 0.0 ||
        !std::isfinite(options.SampleMilliseconds) ||
        !std::isfinite(options.WarmupMilliseconds) ||
        options.Cpu < -1 ||
        (options.Format != "text" && options.Format != "json")) {
        error = "options outside supported range";
        return false;
    }
    return true;
}

bool PinCpu(int cpu, std::string &error)
{
    if (cpu < 0)
        return true;
#if defined(_WIN32)
    const unsigned bits = sizeof(DWORD_PTR) * 8u;
    if (static_cast<unsigned>(cpu) >= bits) {
        error = "--cpu exceeds the current processor group mask";
        return false;
    }
    const DWORD_PTR mask = static_cast<DWORD_PTR>(1) << cpu;
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        error = "SetThreadAffinityMask failed";
        return false;
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    return true;
#else
    error = "--cpu is only supported on Windows";
    return false;
#endif
}

void PrintHelp()
{
    std::cout
        << "Usage: ffp_pipeline_benchmark [options]\n"
        << "  --case <name|all>   select one case\n"
        << "  --samples <n>       sample count (default 11)\n"
        << "  --sample-ms <ms>    minimum time per sample (default 250)\n"
        << "  --warmup-ms <ms>    warmup per case (default 500)\n"
        << "  --cpu <index>       pin benchmark thread to one logical CPU\n"
        << "  --format <text|json>\n"
        << "  --list              list case names\n";
}

void PrintText(const Options &options, const std::vector<CaseResult> &results)
{
    std::cout << "CKFFPLib 44-draw CPU benchmark\n"
              << "Release, diagnostics=off, profiling=off, frame-cost=off"
              << ", samples=" << options.Samples
              << ", sample_ms=" << options.SampleMilliseconds;
    if (options.Cpu >= 0)
        std::cout << ", cpu=" << options.Cpu;
    std::cout << "\n\n"
              << std::left << std::setw(31) << "case"
              << std::right << std::setw(12) << "ns/draw"
              << std::setw(14) << "us/44"
              << std::setw(14) << "p10 us"
              << std::setw(14) << "p90 us"
              << std::setw(10) << "MAD %"
              << std::setw(12) << "frames/s"
              << std::setw(14) << "draws/sample"
              << std::setw(16) << "bytes/sample"
              << std::setw(12) << "bytes/44" << "  status\n";
    for (const CaseResult &result : results) {
        const double framesPerSecond = result.MedianUsPerFrame > 0.0
            ? 1000000.0 / result.MedianUsPerFrame : 0.0;
        const uint64_t bytesPerFrame = result.DrawsPerIteration > 0
            ? result.BytesPerIteration * CKFFBenchmark::DrawsPerFrame /
                  result.DrawsPerIteration
            : 0;
        std::cout << std::left << std::setw(31) << result.Name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << result.MedianNsPerDraw
                  << std::setw(14) << result.MedianUsPerFrame
                  << std::setw(14) << result.P10UsPerFrame
                  << std::setw(14) << result.P90UsPerFrame
                  << std::setw(10) << result.MadPercent
                  << std::setw(12) << framesPerSecond
                  << std::setw(14) << result.DrawsPerSample
                  << std::setw(16) << result.BytesPerSample
                  << std::setw(12) << bytesPerFrame << "  "
                  << (result.Unstable ? "unstable" : "stable") << '\n';
    }
    std::cout << "\nSynthetic CPU cost only; not Level 1 or displayed FPS.\n";
}

void PrintJson(const Options &options, const std::vector<CaseResult> &results)
{
    std::cout << "{\n  \"schema\": 1,\n"
              << "  \"drawsPerFrame\": " << CKFFBenchmark::DrawsPerFrame << ",\n"
              << "  \"samples\": " << options.Samples << ",\n"
              << "  \"sampleMilliseconds\": " << options.SampleMilliseconds << ",\n"
              << "  \"cpu\": " << options.Cpu << ",\n"
              << "  \"syntheticOnly\": true,\n"
              << "  \"cases\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const CaseResult &result = results[i];
        const uint64_t bytesPerFrame = result.DrawsPerIteration > 0
            ? result.BytesPerIteration * CKFFBenchmark::DrawsPerFrame /
                  result.DrawsPerIteration
            : 0;
        std::cout << "    {\"name\": \"" << result.Name
                  << "\", \"iterations\": " << result.Iterations
                  << ", \"drawsPerSample\": " << result.DrawsPerSample
                  << ", \"bytesPerSample\": " << result.BytesPerSample
                  << ", \"medianNsPerDraw\": " << std::fixed << std::setprecision(4)
                  << result.MedianNsPerDraw
                  << ", \"medianUsPer44DrawFrame\": " << result.MedianUsPerFrame
                  << ", \"p10UsPer44DrawFrame\": " << result.P10UsPerFrame
                  << ", \"p90UsPer44DrawFrame\": " << result.P90UsPerFrame
                  << ", \"madPercent\": " << result.MadPercent
                  << ", \"bytesPer44DrawFrame\": " << bytesPerFrame
                  << ", \"checksum\": \"0x" << std::hex << result.Checksum
                  << std::dec << "\", \"unstable\": "
                  << (result.Unstable ? "true" : "false") << "}"
                  << (i + 1 == results.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n}\n";
}

bool IsReleaseUninstrumented()
{
#if !defined(CKRE_BENCHMARK_RELEASE) || !CKRE_BENCHMARK_RELEASE
    return false;
#elif (defined(CKRE_ENABLE_FFP_DIAGNOSTICS) && CKRE_ENABLE_FFP_DIAGNOSTICS) || \
      (defined(CKRE_ENABLE_PROFILING) && CKRE_ENABLE_PROFILING) || \
      (defined(CKRE_ENABLE_FRAME_COST_STATS) && CKRE_ENABLE_FRAME_COST_STATS)
    return false;
#else
    return true;
#endif
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "ffp_pipeline_benchmark: " << error << '\n';
        PrintHelp();
        return 2;
    }
    if (options.Help) {
        PrintHelp();
        return 0;
    }

    const CKFFBenchmark::Workload workload;
    std::vector<BenchmarkCase> cases = BuildCases(workload);
    if (options.List) {
        for (const BenchmarkCase &test : cases)
            std::cout << test.Name << '\n';
        return 0;
    }
    if (!IsReleaseUninstrumented()) {
        std::cerr << "ffp_pipeline_benchmark: performance results require "
                     "Release with FFP diagnostics, profiling, and frame-cost "
                     "instrumentation disabled\n";
        return 3;
    }
    if (!PinCpu(options.Cpu, error)) {
        std::cerr << "ffp_pipeline_benchmark: " << error << '\n';
        return 2;
    }

    std::vector<CaseResult> results;
    for (const BenchmarkCase &test : cases) {
        if (options.CaseName != "all" && options.CaseName != test.Name)
            continue;
        CaseResult result;
        if (!MeasureCase(test, options.Samples, options.SampleMilliseconds,
                         options.WarmupMilliseconds, result, error)) {
            std::cerr << "ffp_pipeline_benchmark: " << test.Name
                      << ": " << error << '\n';
            return 4;
        }
        results.push_back(result);
    }
    if (results.empty()) {
        std::cerr << "ffp_pipeline_benchmark: unknown case: "
                  << options.CaseName << '\n';
        return 2;
    }

    if (options.Format == "json")
        PrintJson(options, results);
    else
        PrintText(options, results);
    return 0;
}
