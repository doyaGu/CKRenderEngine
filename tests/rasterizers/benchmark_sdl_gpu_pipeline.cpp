#include "CKSdlGpuBackend.h"
#include "CKSdlGpuInternal.h"
#include "CKSdlGpuShaders.h"
#include "CKPresentStage.h"
#include "FFPBenchmarkWorkload.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

struct CKSdlGpuBenchmarkAccess {
    static CKERROR Flush(CKSdlGpuBackend &backend)
    {
        return backend.m->Flush(false);
    }

    static CKERROR AcquireSwapchain(CKSdlGpuBackend &backend)
    {
        if (!backend.m->EnsureCommands())
            return backend.m->Error;
        const CKERROR result = backend.m->AcquireSwapchain();
        if (result != CK_OK)
            return result;
        return backend.m->Swapchain ? CK_OK : CKERR_INVALIDOPERATION;
    }

    static void Collect(CKSdlGpuBackend &backend)
    {
        backend.m->Collect();
    }
};

namespace {

constexpr CKDWORD kWidth = 640;
constexpr CKDWORD kHeight = 480;
constexpr CKDWORD kClearColor = 0xff102030u;

enum Stage {
    StageBeginPass,
    StageRecordDraws,
    StageEncodeDraws,
    StageAcquire,
    StageEncodePresentCopy,
    StageSubmitPresentCollect,
    StageCollectProbe,
    StageCount
};

const char *const kStageNames[StageCount] = {
    "begin_pass",
    "record_44",
    "command_encode",
    "acquire_swapchain",
    "present_copy_encode",
    "submit_present_collect",
    "collect_probe"
};

enum class CaseKind {
    PresentOnly,
    Transient44,
    TransientMaterial8
};

struct Options {
    std::string CaseName = "all";
    std::string Format = "text";
    int Samples = 11;
    double SampleMilliseconds = 250.0;
    double WarmupMilliseconds = 500.0;
    int Cpu = -1;
    bool Visible = false;
    bool Help = false;
};

struct Sample {
    std::array<uint64_t, StageCount> Ticks{};
    uint64_t Frames = 0;
    uint64_t Checksum = 1469598103934665603ull;
};

struct StageResult {
    double MedianUs = 0.0;
    double P10Us = 0.0;
    double P90Us = 0.0;
    double MadPercent = 0.0;
};

struct CaseResult {
    std::string Name;
    std::array<StageResult, StageCount> Stages;
    StageResult ProductionTotal;
    uint64_t FramesPerSample = 0;
    uint64_t PixelChecksum = 0;
    bool Unstable = false;
};

uint64_t Counter()
{
#if defined(_WIN32)
    LARGE_INTEGER value;
    QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
#else
    return SDL_GetPerformanceCounter();
#endif
}

double CounterFrequency()
{
#if defined(_WIN32)
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    return static_cast<double>(value.QuadPart);
#else
    return static_cast<double>(SDL_GetPerformanceFrequency());
#endif
}

uint64_t Mix(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64_t HashBytes(const std::vector<CKBYTE> &bytes)
{
    uint64_t hash = 1469598103934665603ull;
    for (CKBYTE value : bytes)
        hash = Mix(hash, value);
    return hash;
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

StageResult Summarize(std::vector<double> values)
{
    StageResult result;
    std::sort(values.begin(), values.end());
    result.MedianUs = Quantile(values, 0.5);
    result.P10Us = Quantile(values, 0.1);
    result.P90Us = Quantile(values, 0.9);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values)
        deviations.push_back(std::abs(value - result.MedianUs));
    std::sort(deviations.begin(), deviations.end());
    const double mad = Quantile(deviations, 0.5);
    result.MadPercent = result.MedianUs > 0.0
        ? 100.0 * mad / result.MedianUs : 0.0;
    return result;
}

bool ParseInteger(const std::string &text, int &value)
{
    char *end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed < (std::numeric_limits<int>::min)() ||
        parsed > (std::numeric_limits<int>::max)())
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const std::string &text, double &value)
{
    char *end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value);
}

bool ParseOptions(int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--visible") {
            options.Visible = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            options.Help = true;
            continue;
        }
        const size_t equal = argument.find('=');
        const std::string name = equal == std::string::npos
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
    if ((options.CaseName != "all" && options.CaseName != "present_only" &&
         options.CaseName != "transient_44" &&
         options.CaseName != "transient_material8_44") ||
        (options.Format != "text" && options.Format != "json") ||
        options.Samples < 3 || options.Samples > 101 ||
        options.SampleMilliseconds < 1.0 ||
        options.WarmupMilliseconds < 0.0 || options.Cpu < -1) {
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
    const unsigned bitCount = sizeof(DWORD_PTR) * 8u;
    if (static_cast<unsigned>(cpu) >= bitCount) {
        error = "--cpu exceeds the current processor-group mask";
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
    error = "--cpu is supported only on Windows";
    return false;
#endif
}

void PrintHelp()
{
    std::cout
        << "Usage: sdl_gpu_pipeline_benchmark --visible [options]\n"
        << "  --case <all|present_only|transient_44|transient_material8_44>\n"
        << "  --samples <n>       sample count (default 11)\n"
        << "  --sample-ms <ms>    minimum production time per sample (default 250)\n"
        << "  --warmup-ms <ms>    warmup per case (default 500)\n"
        << "  --cpu <index>       pin the benchmark thread\n"
        << "  --format <text|json>\n";
}

class BenchmarkFixture {
public:
    explicit BenchmarkFixture(SDL_Window *window) : m_Window(window) {}

    ~BenchmarkFixture()
    {
        Shutdown();
    }

    bool Init(std::string &error)
    {
        CKBackendInitDesc init;
        init.Window = m_Window;
        init.Width = kWidth;
        init.Height = kHeight;
        init.Bpp = 32;
        init.ZBpp = 24;
        init.StencilBpp = 8;
        if (m_Backend.Init(&init) != CK_OK) {
            error = "backend initialization failed";
            return false;
        }
        const SDL_GPUShaderFormat format =
            m_Backend.GetCaps().ShaderFormat == CKRST_SHADER_FORMAT_DXIL
                ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
        if (!CKSdlGpuShaderSet(format, m_Shaders)) {
            error = "native shader set is unavailable";
            return false;
        }
        if (!m_Pipeline.Init(&m_Backend, m_Shaders)) {
            error = "fixed-function pipeline initialization failed";
            return false;
        }
        m_Present.Init(&m_Backend, m_Shaders);
        if (!m_Present.EnsureNativeTarget(kWidth, kHeight)) {
            error = "native presentation target creation failed";
            return false;
        }
        if (!CreateTextures()) {
            error = "benchmark texture creation failed";
            return false;
        }
        m_Workload.ConfigureCore(m_Pipeline, m_Textures);

        // Establish IMMEDIATE before any swapchain image is acquired. This
        // mode transition and shader/pipeline creation are outside timing.
        CKDWORD number = 0;
        if (m_Backend.Submit({CKRST_BACKEND_SYNC_IMMEDIATE, FALSE}, &number) != CK_OK) {
            error = "failed to select IMMEDIATE presentation";
            return false;
        }
        m_LastSubmission = number;
        m_Ready = true;
        return true;
    }

    bool ValidateRenderedOutput(uint64_t &checksum, std::string &error)
    {
        std::vector<CKBYTE> first;
        std::vector<CKBYTE> second;
        if (!CaptureRenderedFrame(first, error) ||
            !CaptureRenderedFrame(second, error))
            return false;
        if (first != second) {
            error = "two deterministic GPU readbacks differ";
            return false;
        }
        if (first.size() != size_t(kWidth) * kHeight * 4u) {
            error = "unexpected readback byte count";
            return false;
        }
        const CKDWORD firstPixel = ReadPixel(first, 0);
        bool changed = false;
        for (size_t offset = 4; offset < first.size(); offset += 4) {
            if (ReadPixel(first, offset) != firstPixel) {
                changed = true;
                break;
            }
        }
        if (!changed) {
            error = "rendered frame contains only one pixel value";
            return false;
        }
        checksum = HashBytes(first);
        return true;
    }

    bool RunSample(CaseKind kind, uint64_t frames, Sample &sample,
                   std::string &error)
    {
        sample = Sample();
        for (uint64_t frame = 0; frame < frames; ++frame) {
            bool ok = false;
            if (kind == CaseKind::PresentOnly)
                ok = RunPresentOnlyFrame(sample, error);
            else
                ok = RunTransientFrame(
                    sample, kind == CaseKind::TransientMaterial8, error);
            if (!ok)
                return false;
            if ((frame & 255u) == 255u && !PumpEvents(error))
                return false;
        }
        return true;
    }

private:
    static CKDWORD ReadPixel(const std::vector<CKBYTE> &bytes, size_t offset)
    {
        CKDWORD pixel = 0;
        std::memcpy(&pixel, bytes.data() + offset, sizeof(pixel));
        return pixel;
    }

    bool CreateTextures()
    {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = 4;
        desc.Format.Height = 4;
        desc.Format.BytesPerLine = 16;
        desc.MipMapCount = 1;
        desc.Depth = 1;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                     CKRST_TEXTURE_ALPHA;
        std::array<CKDWORD, 16> pixels;
        for (size_t texture = 0; texture < m_Textures.size(); ++texture) {
            for (size_t pixel = 0; pixel < pixels.size(); ++pixel) {
                const CKDWORD red = CKDWORD((pixel * 37u + texture * 53u) & 255u);
                const CKDWORD green = CKDWORD((pixel * 19u + texture * 71u) & 255u);
                const CKDWORD blue = CKDWORD((pixel * 11u + texture * 29u) & 255u);
                pixels[pixel] = 0xff000000u | (red << 16) | (green << 8) | blue;
            }
            VxImageDescEx image = desc.Format;
            image.Image = reinterpret_cast<CKBYTE *>(pixels.data());
            if (m_Backend.CreateTexture(&desc, &image, &m_Textures[texture]) != CK_OK)
                return false;
        }
        return true;
    }

    bool BeginBenchmarkPass()
    {
        CKBackendPassDesc pass;
        pass.RenderTarget = m_Present.NativeTarget().FrameBuffer;
        pass.Rect = {0, 0, int(kWidth), int(kHeight)};
        pass.ClearFlags = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH;
        pass.ClearColor = kClearColor;
        pass.ClearZ = 1.0f;
        return m_Backend.BeginPass(&pass) == CK_OK;
    }

    bool CaptureRenderedFrame(std::vector<CKBYTE> &output,
                              std::string &error)
    {
        if (!BeginBenchmarkPass() ||
            !m_Workload.RunCoreTransientFrame(m_Pipeline, false) ||
            CKSdlGpuBenchmarkAccess::Flush(m_Backend) != CK_OK) {
            error = "validation draw or encode failed";
            return false;
        }
        const CKPresentTarget &target = m_Present.NativeTarget();
        const CKDWORD readbackTexture =
            m_Present.AcquireReadbackTexture(kWidth, kHeight);
        if (!readbackTexture ||
            m_Backend.Blit(readbackTexture, 0, 0, 0, 0,
                           target.ColorTexture, 0, 0, nullptr) != CK_OK) {
            error = "validation blit failed";
            return false;
        }
        CKReadbackDesc desc;
        CKBackendReadbackTicket ticket;
        if (m_Backend.ReadTexture(readbackTexture, 0, &desc, &ticket) != CK_OK) {
            error = "validation readback encoding failed";
            return false;
        }
        CKDWORD number = 0;
        if (m_Backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, &number) != CK_OK ||
            number != m_LastSubmission + 1 ||
            m_Backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY) {
            error = "validation readback submission failed";
            return false;
        }
        m_LastSubmission = number;
        output = ticket->Data;
        return true;
    }

    bool RunPresentOnlyFrame(Sample &sample, std::string &error)
    {
        const uint64_t start = Counter();
        const uint64_t afterBegin = start;
        const uint64_t afterRecord = afterBegin;
        const uint64_t afterEncode = afterRecord;
        if (CKSdlGpuBenchmarkAccess::AcquireSwapchain(m_Backend) != CK_OK) {
            error = "present-only swapchain acquisition failed";
            return false;
        }
        const uint64_t afterAcquire = Counter();
        const uint64_t afterPresentEncode = afterAcquire;
        CKDWORD number = 0;
        if (m_Backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, TRUE}, &number) != CK_OK) {
            error = "present-only submission failed";
            return false;
        }
        const uint64_t afterSubmit = Counter();
        CKSdlGpuBenchmarkAccess::Collect(m_Backend);
        const uint64_t afterCollect = Counter();
        if (!ValidateFrame(number, 0, 0, 0, error))
            return false;
        Accumulate(sample, start, afterBegin, afterRecord, afterEncode,
                   afterAcquire, afterPresentEncode, afterSubmit, afterCollect,
                   0, 0, 0);
        return true;
    }

    bool RunTransientFrame(Sample &sample, bool cycleMaterials,
                           std::string &error)
    {
        const uint64_t start = Counter();
        if (!BeginBenchmarkPass()) {
            error = "44-draw BeginPass failed";
            return false;
        }
        const uint64_t afterBegin = Counter();
        m_Pipeline.SetFrameNumber(m_LastSubmission + 1);
        const bool recorded = cycleMaterials
            ? m_Workload.RunCoreTransientMaterialFrame(
                  m_Pipeline, false, m_Textures)
            : m_Workload.RunCoreTransientFrame(m_Pipeline, false);
        if (!recorded) {
            error = "44-draw recording failed";
            return false;
        }
        const uint64_t afterRecord = Counter();
        if (CKSdlGpuBenchmarkAccess::Flush(m_Backend) != CK_OK) {
            error = "44-draw command encoding failed";
            return false;
        }
        const uint64_t afterEncode = Counter();
        if (CKSdlGpuBenchmarkAccess::AcquireSwapchain(m_Backend) != CK_OK) {
            error = "44-draw swapchain acquisition failed";
            return false;
        }
        const uint64_t afterAcquire = Counter();
        const CKPresentTarget &target = m_Present.NativeTarget();
        if (m_Backend.PresentTexture(target.ColorTexture, kWidth, kHeight,
                                     CKRST_BACKEND_SYNC_UNCHANGED) != CK_OK) {
            error = "44-draw present-copy encoding failed";
            return false;
        }
        const uint64_t afterPresentEncode = Counter();
        CKDWORD number = 0;
        if (m_Backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, TRUE}, &number) != CK_OK) {
            error = "44-draw submission failed";
            return false;
        }
        const uint64_t afterSubmit = Counter();
        CKSdlGpuBenchmarkAccess::Collect(m_Backend);
        const uint64_t afterCollect = Counter();
        if (!ValidateFrame(number, CKFFBenchmark::DrawsPerFrame, 2, 1, error))
            return false;
        Accumulate(sample, start, afterBegin, afterRecord, afterEncode,
                   afterAcquire, afterPresentEncode, afterSubmit, afterCollect,
                   CKFFBenchmark::DrawsPerFrame, 2, 1);
        return true;
    }

    bool ValidateFrame(CKDWORD number, CKDWORD draws, CKDWORD passes,
                       CKDWORD blits, std::string &error)
    {
        const CKBackendStats &stats = m_Backend.GetStats();
        if (number != m_LastSubmission + 1 || stats.Frames != number ||
            stats.Draws != draws || stats.Passes != passes ||
            stats.Blits != blits || m_Backend.GetDeviceStatus() != CK_OK) {
            error = "per-frame backend accounting changed";
            return false;
        }
        m_LastSubmission = number;
        return true;
    }

    static void Accumulate(Sample &sample, uint64_t start,
                           uint64_t afterBegin, uint64_t afterRecord,
                           uint64_t afterEncode, uint64_t afterAcquire,
                           uint64_t afterPresentEncode, uint64_t afterSubmit,
                           uint64_t afterCollect, CKDWORD draws,
                           CKDWORD passes, CKDWORD blits)
    {
        sample.Ticks[StageBeginPass] += afterBegin - start;
        sample.Ticks[StageRecordDraws] += afterRecord - afterBegin;
        sample.Ticks[StageEncodeDraws] += afterEncode - afterRecord;
        sample.Ticks[StageAcquire] += afterAcquire - afterEncode;
        sample.Ticks[StageEncodePresentCopy] += afterPresentEncode - afterAcquire;
        sample.Ticks[StageSubmitPresentCollect] += afterSubmit - afterPresentEncode;
        sample.Ticks[StageCollectProbe] += afterCollect - afterSubmit;
        sample.Checksum = Mix(sample.Checksum, draws);
        sample.Checksum = Mix(sample.Checksum, passes);
        sample.Checksum = Mix(sample.Checksum, blits);
        ++sample.Frames;
    }

    static bool PumpEvents(std::string &error)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                error = "window was closed";
                return false;
            }
        }
        return true;
    }

    void Shutdown()
    {
        if (!m_Ready && m_Backend.GetDeviceStatus() != CK_OK)
            return;
        m_Pipeline.Shutdown();
        m_Present.Shutdown();
        for (CKDWORD texture : m_Textures) {
            if (texture)
                m_Backend.DestroyObject(texture, CKRST_OBJ_TEXTURE);
        }
        m_Textures = {};
        m_Backend.Shutdown();
        m_Ready = false;
    }

    SDL_Window *m_Window = nullptr;
    CKSdlGpuBackend m_Backend;
    CKFixedFunctionPipeline m_Pipeline;
    CKPresentStage m_Present;
    CKBackendShaderSet m_Shaders;
    CKFFBenchmark::Workload m_Workload;
    std::array<CKDWORD, 4> m_Textures{};
    CKDWORD m_LastSubmission = 0;
    bool m_Ready = false;
};

uint64_t ProductionTicks(const Sample &sample)
{
    uint64_t ticks = 0;
    for (size_t stage = 0; stage < StageCollectProbe; ++stage)
        ticks += sample.Ticks[stage];
    return ticks;
}

bool MeasureCase(SDL_Window *window, CaseKind kind, const Options &options,
                 CaseResult &result, std::string &error)
{
    BenchmarkFixture fixture(window);
    if (!fixture.Init(error))
        return false;

    uint64_t pixelChecksum = 0;
    if (kind != CaseKind::PresentOnly &&
        !fixture.ValidateRenderedOutput(pixelChecksum, error))
        return false;

    const double frequency = CounterFrequency();
    const double warmupTicks = options.WarmupMilliseconds * frequency / 1000.0;
    uint64_t warmedTicks = 0;
    while (warmedTicks < static_cast<uint64_t>(warmupTicks)) {
        Sample warmup;
        if (!fixture.RunSample(kind, 32, warmup, error))
            return false;
        warmedTicks += ProductionTicks(warmup);
    }

    Sample calibration;
    if (!fixture.RunSample(kind, 32, calibration, error))
        return false;
    const double ticksPerFrame = static_cast<double>(ProductionTicks(calibration)) /
                                 static_cast<double>(calibration.Frames);
    const double targetTicks = options.SampleMilliseconds * frequency / 1000.0;
    uint64_t framesPerSample = (std::max<uint64_t>)(
        1, static_cast<uint64_t>(std::ceil(targetTicks / ticksPerFrame * 1.03)));

    std::array<std::vector<double>, StageCount> stageValues;
    std::vector<double> totalValues;
    uint64_t expectedChecksum = 0;
    int sampleIndex = 0;
    while (sampleIndex < options.Samples) {
        Sample sample;
        if (!fixture.RunSample(kind, framesPerSample, sample, error))
            return false;
        if (sample.Frames != framesPerSample) {
            error = "sample frame count changed";
            return false;
        }
        const uint64_t productionTicks = ProductionTicks(sample);
        if (productionTicks < targetTicks) {
            const double scale = targetTicks / static_cast<double>(productionTicks);
            framesPerSample = (std::max<uint64_t>)(
                framesPerSample + 1,
                static_cast<uint64_t>(std::ceil(
                    static_cast<double>(framesPerSample) * scale * 1.03)));
            for (std::vector<double> &values : stageValues)
                values.clear();
            totalValues.clear();
            expectedChecksum = 0;
            sampleIndex = 0;
            continue;
        }
        if (sampleIndex == 0)
            expectedChecksum = sample.Checksum;
        else if (sample.Checksum != expectedChecksum) {
            error = "sample checksum changed";
            return false;
        }
        double totalUs = 0.0;
        for (size_t stage = 0; stage < StageCount; ++stage) {
            const double us = static_cast<double>(sample.Ticks[stage]) *
                              1000000.0 / frequency /
                              static_cast<double>(sample.Frames);
            stageValues[stage].push_back(us);
            if (stage < StageCollectProbe)
                totalUs += us;
        }
        totalValues.push_back(totalUs);
        ++sampleIndex;
    }

    if (kind == CaseKind::PresentOnly)
        result.Name = "present_only";
    else if (kind == CaseKind::Transient44)
        result.Name = "transient_44";
    else
        result.Name = "transient_material8_44";
    result.FramesPerSample = framesPerSample;
    result.PixelChecksum = pixelChecksum;
    for (size_t stage = 0; stage < StageCount; ++stage) {
        result.Stages[stage] = Summarize(stageValues[stage]);
        if (result.Stages[stage].MedianUs >= 0.25 &&
            result.Stages[stage].MadPercent > 3.0)
            result.Unstable = true;
    }
    result.ProductionTotal = Summarize(totalValues);
    if (result.ProductionTotal.MadPercent > 3.0)
        result.Unstable = true;
    return true;
}

void PrintText(const Options &options, const std::vector<CaseResult> &results)
{
    std::cout << "SDL_gpu production-path CPU benchmark\n"
              << "Release, diagnostics=off, profiling=off, frame-cost=off"
              << ", 640x480, IMMEDIATE, two frames in flight"
              << ", samples=" << options.Samples
              << ", sample_ms=" << options.SampleMilliseconds;
    if (options.Cpu >= 0)
        std::cout << ", cpu=" << options.Cpu;
    std::cout << "\nsubmit_present_collect includes production Collect; "
                 "collect_probe is a second isolated query and is excluded "
                 "from production_total.\n";
    for (const CaseResult &result : results) {
        const double framesPerSecond = result.ProductionTotal.MedianUs > 0.0
            ? 1000000.0 / result.ProductionTotal.MedianUs : 0.0;
        std::cout << "\n" << result.Name
                  << ": production_total=" << std::fixed << std::setprecision(2)
                  << result.ProductionTotal.MedianUs << " us/frame, "
                  << framesPerSecond << " frames/s, frames/sample="
                  << result.FramesPerSample << ", pixel_checksum=0x"
                  << std::hex << result.PixelChecksum << std::dec << ", "
                  << (result.Unstable ? "unstable" : "stable") << "\n";
        std::cout << std::left << std::setw(25) << "stage"
                  << std::right << std::setw(14) << "median us"
                  << std::setw(12) << "p10 us"
                  << std::setw(12) << "p90 us"
                  << std::setw(10) << "MAD %" << "\n";
        for (size_t stage = 0; stage < StageCount; ++stage) {
            const StageResult &value = result.Stages[stage];
            std::cout << std::left << std::setw(25) << kStageNames[stage]
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(14) << value.MedianUs
                      << std::setw(12) << value.P10Us
                      << std::setw(12) << value.P90Us
                      << std::setw(10) << value.MadPercent << "\n";
        }
    }
}

void PrintJson(const Options &options, const std::vector<CaseResult> &results)
{
    std::cout << "{\"benchmark\":\"sdl_gpu_pipeline\",\"samples\":"
              << options.Samples << ",\"sample_ms\":"
              << options.SampleMilliseconds << ",\"cases\":[";
    for (size_t index = 0; index < results.size(); ++index) {
        if (index)
            std::cout << ',';
        const CaseResult &result = results[index];
        std::cout << "{\"name\":\"" << result.Name
                  << "\",\"production_total_us\":"
                  << std::setprecision(9) << result.ProductionTotal.MedianUs
                  << ",\"frames_per_second\":"
                  << 1000000.0 / result.ProductionTotal.MedianUs
                  << ",\"frames_per_sample\":" << result.FramesPerSample
                  << ",\"pixel_checksum\":" << result.PixelChecksum
                  << ",\"unstable\":" << (result.Unstable ? "true" : "false")
                  << ",\"stages\":{";
        for (size_t stage = 0; stage < StageCount; ++stage) {
            if (stage)
                std::cout << ',';
            const StageResult &value = result.Stages[stage];
            std::cout << '\"' << kStageNames[stage] << "\":{"
                      << "\"median_us\":" << value.MedianUs
                      << ",\"p10_us\":" << value.P10Us
                      << ",\"p90_us\":" << value.P90Us
                      << ",\"mad_percent\":" << value.MadPercent << '}';
        }
        std::cout << "}}";
    }
    std::cout << "]}\n";
}

bool BenchmarkBuildIsValid(std::string &error)
{
#if !defined(CKRE_BENCHMARK_RELEASE) || !CKRE_BENCHMARK_RELEASE
    error = "formal timing requires a Release build";
    return false;
#elif (defined(CKRE_ENABLE_PROFILING) && CKRE_ENABLE_PROFILING) || \
      (defined(CKRE_ENABLE_FFP_DIAGNOSTICS) && CKRE_ENABLE_FFP_DIAGNOSTICS) || \
      (defined(CKRE_ENABLE_FRAME_COST_STATS) && CKRE_ENABLE_FRAME_COST_STATS)
    error = "formal timing requires profiling and diagnostics to be disabled";
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
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    if (options.Help) {
        PrintHelp();
        return 0;
    }
    if (!options.Visible) {
        std::fprintf(stderr, "--visible is required; offscreen runs are not performance evidence\n");
        return 2;
    }
    if (!BenchmarkBuildIsValid(error) || !PinCpu(options.Cpu, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }
    SDL_Window *window = SDL_CreateWindow(
        "SDL_gpu 44-draw CPU benchmark", kWidth, kHeight,
        SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 3;
    }
    SDL_SetWindowPosition(window, 160, 120);
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_SyncWindow(window);

    std::vector<CaseResult> results;
    if (options.CaseName == "all" || options.CaseName == "present_only") {
        CaseResult result;
        if (!MeasureCase(window, CaseKind::PresentOnly, options, result, error)) {
            std::fprintf(stderr, "present_only: %s\n", error.c_str());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 4;
        }
        results.push_back(result);
    }
    if (options.CaseName == "all" || options.CaseName == "transient_44") {
        CaseResult result;
        if (!MeasureCase(window, CaseKind::Transient44, options, result, error)) {
            std::fprintf(stderr, "transient_44: %s\n", error.c_str());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 4;
        }
        results.push_back(result);
    }
    if (options.CaseName == "all" ||
        options.CaseName == "transient_material8_44") {
        CaseResult result;
        if (!MeasureCase(window, CaseKind::TransientMaterial8,
                         options, result, error)) {
            std::fprintf(stderr, "transient_material8_44: %s\n", error.c_str());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 4;
        }
        results.push_back(result);
    }

    if (options.Format == "json")
        PrintJson(options, results);
    else
        PrintText(options, results);

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
