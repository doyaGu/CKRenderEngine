#include "CKRasterizer.h"
#include "CKSdlGpuRasterizer.h"
#include "CKSdlGpuRasterizerContext.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kDraws = 44;

struct Options {
    int Samples = 11;
    double SampleMilliseconds = 250.0;
    double WarmupMilliseconds = 500.0;
    bool Visible = false;
    bool Json = false;
    bool Help = false;
};

struct Summary {
    double Median = 0.0;
    double P10 = 0.0;
    double P90 = 0.0;
    double MadPercent = 0.0;
};

bool ParseInteger(const char *text, int &value)
{
    char *end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed < (std::numeric_limits<int>::min)() ||
        parsed > (std::numeric_limits<int>::max)())
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const char *text, double &value)
{
    char *end = nullptr;
    value = std::strtod(text, &end);
    return end && *end == '\0' && std::isfinite(value);
}

bool ParseOptions(int argc, char **argv, Options &options, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--visible") {
            options.Visible = true;
        } else if (argument == "--json") {
            options.Json = true;
        } else if (argument == "--help" || argument == "-h") {
            options.Help = true;
        } else if (argument == "--samples" && i + 1 < argc) {
            if (!ParseInteger(argv[++i], options.Samples)) {
                error = "invalid --samples";
                return false;
            }
        } else if (argument == "--sample-ms" && i + 1 < argc) {
            if (!ParseDouble(argv[++i], options.SampleMilliseconds)) {
                error = "invalid --sample-ms";
                return false;
            }
        } else if (argument == "--warmup-ms" && i + 1 < argc) {
            if (!ParseDouble(argv[++i], options.WarmupMilliseconds)) {
                error = "invalid --warmup-ms";
                return false;
            }
        } else {
            error = "unknown or incomplete option: " + argument;
            return false;
        }
    }
    if (options.Samples < 11 || options.Samples > 101 ||
        options.SampleMilliseconds < 1.0 ||
        options.WarmupMilliseconds < 0.0) {
        error = "options outside supported range";
        return false;
    }
    return true;
}

double Quantile(const std::vector<double> &sorted, double quantile)
{
    const double position = quantile * double(sorted.size() - 1);
    const size_t lower = size_t(position);
    const size_t upper = (std::min)(lower + 1, sorted.size() - 1);
    const double fraction = position - double(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

Summary Summarize(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    Summary result;
    result.Median = Quantile(values, 0.5);
    result.P10 = Quantile(values, 0.1);
    result.P90 = Quantile(values, 0.9);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values)
        deviations.push_back(std::abs(value - result.Median));
    std::sort(deviations.begin(), deviations.end());
    const double mad = Quantile(deviations, 0.5);
    result.MadPercent = result.Median > 0.0
        ? 100.0 * mad / result.Median : 0.0;
    return result;
}

class Fixture {
public:
    ~Fixture() { Shutdown(); }

    bool Init(std::string &error)
    {
        m_Device = SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
                SDL_GPU_SHADERFORMAT_MSL,
            false, nullptr);
        if (!m_Device) {
            error = SDL_GetError();
            return false;
        }
        m_Window = SDL_CreateWindow("SDL_gpu virtual backbuffer benchmark",
                                    kWidth, kHeight, SDL_WINDOW_RESIZABLE);
        if (!m_Window ||
            !SDL_SetPointerProperty(SDL_GetWindowProperties(m_Window),
                                    CKSDL_GPU_DEVICE_PROPERTY, m_Device)) {
            error = SDL_GetError();
            return false;
        }
        SDL_SetWindowPosition(m_Window, 160, 120);
        SDL_ShowWindow(m_Window);
        SDL_RaiseWindow(m_Window);
        SDL_SyncWindow(m_Window);

        CKSdlGpuRasterizerGetInfo(&m_Info);
        m_Rasterizer = m_Info.StartFct((WIN_HANDLE)m_Window);
        m_Driver = m_Rasterizer ? m_Rasterizer->GetDriver(0) : nullptr;
        m_Context = m_Driver ? m_Driver->CreateContext() : nullptr;
        if (!m_Context ||
            !m_Context->Create((WIN_HANDLE)m_Window, 0, 0, kWidth, kHeight,
                               32, FALSE, 0, 24, 8)) {
            error = "rasterizer Context creation failed";
            return false;
        }
        ConfigureDrawState();

        SDL_GPUTextureCreateInfo native = {};
        native.type = SDL_GPU_TEXTURETYPE_2D;
        native.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        native.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                       SDL_GPU_TEXTUREUSAGE_SAMPLER;
        native.width = kWidth;
        native.height = kHeight;
        native.layer_count_or_depth = 1;
        native.num_levels = 1;
        native.sample_count = SDL_GPU_SAMPLECOUNT_1;
        m_Texture = SDL_CreateGPUTexture(m_Device, &native);
        if (!m_Texture) {
            error = SDL_GetError();
            return false;
        }
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ABGR8888, desc.Format);
        desc.Format.Width = kWidth;
        desc.Format.Height = kHeight;
        desc.Format.BytesPerLine = kWidth * 4;
        desc.Depth = 1;
        desc.MipMapCount = 1;
        desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                     CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
        if (!CKSdlGpuImportTexture(m_Context, &desc, m_Device, m_Texture,
                                   &m_TextureHandle)) {
            error = "texture import failed";
            return false;
        }
        if (!m_Context->CreateTexture(&desc, &m_OwnedTextureHandle) ||
            !m_OwnedTextureHandle) {
            error = "owned baseline texture creation failed";
            return false;
        }
        return true;
    }

    bool SetImported(bool imported, std::string &error)
    {
        const CKDWORD texture = imported ? m_TextureHandle
                                         : m_OwnedTextureHandle;
        if (!m_Context->SetBackBufferTexture(texture)) {
            error = imported ? "imported backbuffer binding failed"
                             : "owned baseline binding failed";
            return false;
        }
        return true;
    }

    bool RenderFrame(std::string &error)
    {
        if (!m_Context->Clear(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH,
                              0xFF102030u, 1.0f, 0, 0, nullptr) ||
            !m_Context->BeginScene()) {
            error = "frame begin failed";
            return false;
        }
        for (int draw = 0; draw < kDraws - 1; ++draw) {
            if (!DrawTriangle(draw)) {
                error = "scene draw failed";
                return false;
            }
        }
        if (!m_Context->EndScene() || !m_Context->BeginOverlayPhase() ||
            !DrawTriangle(kDraws - 1) || !m_Context->BackToFront(FALSE)) {
            error = "frame completion failed";
            return false;
        }
        return true;
    }

    bool ValidateStableStats(std::string &error)
    {
        if (!SetImported(false, error) || !RenderFrame(error))
            return false;
        m_BaselineStats =
            static_cast<CKSdlGpuRasterizerContext *>(m_Context)
                ->GetSubmissionStats();
        if (!SetImported(true, error) || !RenderFrame(error))
            return false;
        m_ImportedStats =
            static_cast<CKSdlGpuRasterizerContext *>(m_Context)
                ->GetSubmissionStats();
        const CKSdlGpuSubmissionStats &stats = m_ImportedStats;
        const CKSdlGpuSubmissionStats &baseline = m_BaselineStats;
        const auto stable = [](const CKSdlGpuSubmissionStats &value) {
            return value.Draws == kDraws && value.Submits == 1 &&
                   value.TextureUploads == 0 &&
                   value.TexturePreserveCopies == 0 && value.Blits == 0 &&
                   value.SwapchainAcquires == 0 && value.TextureCycles > 0;
        };
        if (!stable(baseline) || !stable(stats) ||
            stats.Passes > baseline.Passes) {
            char message[256];
            std::snprintf(message, sizeof(message),
                          "unexpected imported-frame work: draws=%u passes=%u/%u submits=%u uploads=%u preserve=%u blits=%u acquires=%u cycles=%u",
                          (unsigned)stats.Draws,
                          (unsigned)stats.Passes,
                          (unsigned)baseline.Passes,
                          (unsigned)stats.Submits,
                          (unsigned)stats.TextureUploads,
                          (unsigned)stats.TexturePreserveCopies,
                          (unsigned)stats.Blits,
                          (unsigned)stats.SwapchainAcquires,
                          (unsigned)stats.TextureCycles);
            error = message;
            return false;
        }
        return true;
    }

    const CKSdlGpuSubmissionStats &BaselineStats() const {
        return m_BaselineStats;
    }
    const CKSdlGpuSubmissionStats &ImportedStats() const {
        return m_ImportedStats;
    }

private:
    void ConfigureDrawState()
    {
        m_Context->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
        m_Context->SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
        m_Context->SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
        m_Context->SetRenderState(VXRENDERSTATE_CULLMODE, VXCULL_NONE);
        m_Context->SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, FALSE);
        m_Context->SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
        m_Context->SetRenderState(VXRENDERSTATE_FOGENABLE, FALSE);
        m_Context->SetRenderState(VXRENDERSTATE_ZENABLE, FALSE);
        m_Context->SetRenderState(VXRENDERSTATE_ZWRITEENABLE, FALSE);
        for (int stage = 0; stage < CKRST_MAX_TEXTURE_STAGES; ++stage) {
            m_Context->SetTexture(0, stage);
            m_Context->SetTextureStageState(stage, CKRST_TSS_OP,
                stage == 0 ? CKRST_TOP_SELECTARG1 : CKRST_TOP_DISABLE);
        }
        m_Context->SetTextureStageState(0, CKRST_TSS_ARG1,
                                        CKRST_TA_DIFFUSE);
        m_Context->SetTextureStageState(0, CKRST_TSS_AOP,
                                        CKRST_TOP_SELECTARG1);
        m_Context->SetTextureStageState(0, CKRST_TSS_AARG1,
                                        CKRST_TA_DIFFUSE);
        VxMatrix identity;
        Vx3DMatrixIdentity(identity);
        m_Context->SetTransformMatrix(VXMATRIX_WORLD, identity);
        m_Context->SetTransformMatrix(VXMATRIX_VIEW, identity);
        m_Context->SetTransformMatrix(VXMATRIX_PROJECTION, identity);
    }

    bool DrawTriangle(int draw)
    {
        const float offset = float(draw % 11) * 0.002f;
        VxVector positions[3] = {
            VxVector(-0.9f + offset, -0.9f, 0.5f),
            VxVector(0.9f, -0.9f + offset, 0.5f),
            VxVector(0.0f, 0.9f - offset, 0.5f)};
        CKDWORD colors[3] = {
            0xFF2040E0u + CKDWORD(draw & 15),
            0xFFE04020u + CKDWORD(draw & 15),
            0xFF40E020u + CKDWORD(draw & 15)};
        VxDrawPrimitiveData data;
        std::memset(&data, 0, sizeof(data));
        data.VertexCount = 3;
        data.Flags = CKRST_DP_TR_VC;
        data.PositionPtr = positions;
        data.PositionStride = sizeof(VxVector);
        data.ColorPtr = colors;
        data.ColorStride = sizeof(CKDWORD);
        return m_Context->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data) != FALSE;
    }

    void Shutdown()
    {
        if (m_Context) {
            m_Context->SetBackBufferTexture(0);
            if (m_TextureHandle) {
                m_Context->DeleteObject(m_TextureHandle, CKRST_OBJ_TEXTURE);
                m_TextureHandle = 0;
            }
            if (m_OwnedTextureHandle) {
                m_Context->DeleteObject(m_OwnedTextureHandle,
                                        CKRST_OBJ_TEXTURE);
                m_OwnedTextureHandle = 0;
            }
        }
        if (m_Texture && m_Device) {
            SDL_ReleaseGPUTexture(m_Device, m_Texture);
            m_Texture = nullptr;
        }
        if (m_Context) {
            m_Context->BeginShutdown();
            m_Driver->DestroyContext(m_Context);
            m_Context = nullptr;
        }
        if (m_Rasterizer) {
            m_Info.CloseFct(m_Rasterizer);
            m_Rasterizer = nullptr;
        }
        if (m_Window) {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }
        if (m_Device) {
            SDL_DestroyGPUDevice(m_Device);
            m_Device = nullptr;
        }
    }

    SDL_GPUDevice *m_Device = nullptr;
    SDL_Window *m_Window = nullptr;
    SDL_GPUTexture *m_Texture = nullptr;
    CKRasterizerInfo m_Info = {};
    CKRasterizer *m_Rasterizer = nullptr;
    CKRasterizerDriver *m_Driver = nullptr;
    CKRasterizerContext *m_Context = nullptr;
    CKDWORD m_TextureHandle = 0;
    CKDWORD m_OwnedTextureHandle = 0;
    CKSdlGpuSubmissionStats m_BaselineStats = {};
    CKSdlGpuSubmissionStats m_ImportedStats = {};
};

bool RunFor(Fixture &fixture, uint64_t frames, double &microseconds,
            std::string &error)
{
    const Uint64 start = SDL_GetPerformanceCounter();
    for (uint64_t frame = 0; frame < frames; ++frame) {
        if (!fixture.RenderFrame(error))
            return false;
        if ((frame & 255u) == 255u) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    error = "benchmark window was closed";
                    return false;
                }
            }
        }
    }
    const Uint64 elapsed = SDL_GetPerformanceCounter() - start;
    microseconds = double(elapsed) * 1000000.0 /
                   double(SDL_GetPerformanceFrequency()) / double(frames);
    return true;
}

bool Warm(Fixture &fixture, bool imported, double milliseconds,
          std::string &error)
{
    if (!fixture.SetImported(imported, error))
        return false;
    const Uint64 target = Uint64(milliseconds *
        double(SDL_GetPerformanceFrequency()) / 1000.0);
    const Uint64 start = SDL_GetPerformanceCounter();
    while (SDL_GetPerformanceCounter() - start < target) {
        double ignored = 0.0;
        if (!RunFor(fixture, 32, ignored, error))
            return false;
    }
    return true;
}

bool Calibrate(Fixture &fixture, bool imported, double sampleMilliseconds,
               uint64_t &frames, std::string &error)
{
    if (!fixture.SetImported(imported, error))
        return false;
    double us = 0.0;
    if (!RunFor(fixture, 32, us, error) || us <= 0.0)
        return false;
    frames = (std::max<uint64_t>)(1, uint64_t(std::ceil(
        sampleMilliseconds * 1000.0 / us * 1.03)));
    return true;
}

bool Measure(Fixture &fixture, const Options &options,
             Summary &baseline, Summary &custom,
             CKSdlGpuSubmissionStats &baselineStats,
             CKSdlGpuSubmissionStats &customStats,
             std::string &error)
{
    if (!Warm(fixture, false, options.WarmupMilliseconds, error) ||
        !Warm(fixture, true, options.WarmupMilliseconds, error) ||
        !fixture.ValidateStableStats(error))
        return false;
    baselineStats = fixture.BaselineStats();
    customStats = fixture.ImportedStats();

    uint64_t baselineFrames = 0, customFrames = 0;
    if (!Calibrate(fixture, false, options.SampleMilliseconds,
                   baselineFrames, error) ||
        !Calibrate(fixture, true, options.SampleMilliseconds,
                   customFrames, error))
        return false;

    std::vector<double> baselineSamples, customSamples;
    baselineSamples.reserve(options.Samples);
    customSamples.reserve(options.Samples);
    for (int sample = 0; sample < options.Samples; ++sample) {
        const bool customFirst = (sample & 1) != 0;
        for (int pass = 0; pass < 2; ++pass) {
            const bool useCustom = pass == 0 ? customFirst : !customFirst;
            if (!fixture.SetImported(useCustom, error))
                return false;
            double value = 0.0;
            if (!RunFor(fixture, useCustom ? customFrames : baselineFrames,
                        value, error))
                return false;
            (useCustom ? customSamples : baselineSamples).push_back(value);
        }
    }
    baseline = Summarize(baselineSamples);
    custom = Summarize(customSamples);
    return true;
}

bool ReleaseBuild(std::string &error)
{
#if !defined(CKRE_BENCHMARK_RELEASE) || !CKRE_BENCHMARK_RELEASE
    error = "formal timing requires a Release build";
    return false;
#else
    return true;
#endif
}

void PrintHelp()
{
    std::cout
        << "Usage: sdl_gpu_backbuffer_benchmark --visible [options]\n"
        << "  --samples <n>     sample count, at least 11 (default 11)\n"
        << "  --sample-ms <ms>  minimum duration per sample (default 250)\n"
        << "  --warmup-ms <ms>  warmup per path (default 500)\n"
        << "  --json             machine-readable output\n";
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
    if (!options.Visible || !ReleaseBuild(error)) {
        if (error.empty())
            error = "--visible is required; hidden runs are not performance evidence";
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    SDL_setenv_unsafe("CKRE_SDL_GPU_FF_JIT", "0", 1);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    Summary baseline, custom;
    CKSdlGpuSubmissionStats baselineStats = {}, customStats = {};
    {
        Fixture fixture;
        if (!fixture.Init(error) ||
            !Measure(fixture, options, baseline, custom, baselineStats,
                     customStats, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            SDL_Quit();
            return 4;
        }
    }
    SDL_Quit();

    const double ratio = baseline.Median > 0.0
        ? custom.Median / baseline.Median : 0.0;
    const bool passed = ratio <= 1.03 && custom.MadPercent <= 3.0;
    if (options.Json) {
        std::cout << std::setprecision(9)
                  << "{\"samples\":" << options.Samples
                  << ",\"baseline\":{\"median_us\":" << baseline.Median
                  << ",\"p10_us\":" << baseline.P10
                  << ",\"p90_us\":" << baseline.P90
                  << ",\"mad_percent\":" << baseline.MadPercent
                  << "},\"custom\":{\"median_us\":" << custom.Median
                  << ",\"p10_us\":" << custom.P10
                  << ",\"p90_us\":" << custom.P90
                  << ",\"mad_percent\":" << custom.MadPercent
                  << "},\"ratio\":" << ratio
                  << ",\"stable_frame\":{\"passes\":"
                  << customStats.Passes
                  << ",\"submits\":" << customStats.Submits
                  << ",\"texture_uploads\":"
                  << customStats.TextureUploads
                  << ",\"preserve_copies\":"
                  << customStats.TexturePreserveCopies
                  << ",\"blits\":" << customStats.Blits
                  << ",\"swapchain_acquires\":"
                  << customStats.SwapchainAcquires
                  << ",\"cycles\":" << customStats.TextureCycles << '}'
                  << ",\"passed\":" << (passed ? "true" : "false")
                  << "}\n";
    } else {
        std::cout << "SDL_gpu virtual-backbuffer A/B benchmark, Release, "
                  << options.Samples << " alternating samples\n"
                  << std::left << std::setw(22) << "path"
                  << std::right << std::setw(13) << "median us"
                  << std::setw(11) << "p10 us"
                  << std::setw(11) << "p90 us"
                  << std::setw(9) << "MAD %" << '\n';
        const auto row = [](const char *name, const Summary &value) {
            std::cout << std::left << std::setw(22) << name << std::right
                      << std::fixed << std::setprecision(2)
                      << std::setw(13) << value.Median
                      << std::setw(11) << value.P10
                      << std::setw(11) << value.P90
                      << std::setw(9) << value.MadPercent << '\n';
        };
        row("owned custom", baseline);
        row("imported custom", custom);
        std::cout << "stable custom frame: passes=" << customStats.Passes
                  << " (owned=" << baselineStats.Passes << ")"
                  << ", submits=" << customStats.Submits
                  << ", texture_uploads="
                  << customStats.TextureUploads
                  << ", preserve_copies="
                  << customStats.TexturePreserveCopies
                  << ", blits=" << customStats.Blits
                  << ", swapchain_acquires="
                  << customStats.SwapchainAcquires
                  << ", cycles=" << customStats.TextureCycles << '\n';
        std::cout << "imported/owned=" << std::fixed << std::setprecision(4)
                  << ratio << ", gate=" << (passed ? "passed" : "failed")
                  << " (<=1.03, custom MAD <=3%)\n";
    }
    return passed ? 0 : 5;
}
