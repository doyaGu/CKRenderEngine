#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaders.h"
#include "CKFFNativeFragmentJit.h"
#include "CKJitDxbc.h"
#include "CKJitSpirv.h"
#include "CKRenderProfile.h"

#include <cstring>

// CKSdlGpuRasterizerContext runtime compilation of fixed-function fragment
// programs. A native artifact decodes the fragment program per fragment; the
// compiled program is constant and keeps the artifact's interface, so it
// replaces the precompiled program in the same draws and falls back to it
// for every pipeline the worker has not created yet.

namespace {
// Bounds the shaders and programs one device keeps. Later fragment programs
// draw with the precompiled artifacts.
const int kFFJitProgramLimit = 256;
// SDL_gpu places fragment samplers in space (set) 2, uniform buffers in 3.
const CKJitResourceLayout kFFJitResources = {3, 0, 2};
}

// Compiles one fragment program on the worker.
class CKSdlGpuRasterizerContext::FFJitJob : public CKSdlGpuJob {
public:
    FFJitJob(CKSdlGpuRasterizerContext &context, const FFJitKey &key,
             const CKFFFragmentProgram &program, CKFFSamplerLayout layout,
             SDL_GPUShaderFormat format)
        : Context(context), Key(key), Program(program), Layout(layout), Format(format) {}

    void Run() override {
        CKRE_PROFILE_SCOPE("CKRE.SDL.FFJitCompile");
        CKJitFragmentShader shader;
        Compiled = CKFFCompileNativeFragmentProgram(Program, Layout, shader) &&
            (Format == SDL_GPU_SHADERFORMAT_DXBC
                 ? CKJitEmitDxbc(shader, kFFJitResources, Code)
                 : CKJitEmitSpirv(shader, kFFJitResources, Code));
    }
    void Complete() override {
        Context.CompleteFFJitProgram(Key, Compiled ? &Code : nullptr);
    }

private:
    CKSdlGpuRasterizerContext &Context;
    FFJitKey Key;
    CKFFFragmentProgram Program;
    CKFFSamplerLayout Layout;
    SDL_GPUShaderFormat Format;
    XArray<uint32_t> Code;
    bool Compiled = false;
};

void CKSdlGpuRasterizerContext::InitFFJit()
{
    m_FFJitFormat = SDL_GPU_SHADERFORMAT_INVALID;
    // CKRE_SDL_GPU_FF_JIT=0 draws every program with the precompiled shaders.
    const char *setting = SDL_getenv("CKRE_SDL_GPU_FF_JIT");
    if (setting && SDL_strcmp(setting, "0") == 0)
        return;
    // D3D12 runs the DXBC the compiler emits, as DXIL would need DXC.
    if (ShaderFormat == SDL_GPU_SHADERFORMAT_SPIRV)
        m_FFJitFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    else if (NativeShaderFormat(CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_PROFILE_DX12) ==
             SDL_GPU_SHADERFORMAT_DXBC)
        m_FFJitFormat = SDL_GPU_SHADERFORMAT_DXBC;
}

CKDWORD CKSdlGpuRasterizerContext::ResolveFFJitProgram(
    const CKFFFragmentProgram &FragmentProgram,
    CKFFSamplerLayout Layout,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled)
{
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_INVALID)
        return Precompiled;
    FFJitKey key;
    std::memcpy(key.Values, FragmentProgram.Lanes(),
                sizeof(CKDWORD) * CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
    key.Values[CKFF_FRAGMENT_PROGRAM_LANE_COUNT] = (CKDWORD)Layout;
    FFJitProgram *entry = m_FFJitPrograms.FindPtr(key);
    if (!entry) {
        if (m_FFJitPrograms.Size() >= kFFJitProgramLimit)
            return Precompiled;
        // Never compile while drawing. A program the worker cannot take
        // keeps its precompiled shader.
        FFJitProgram queued;
        if (!SubmitJob(new FFJitJob(*this, key, FragmentProgram, Layout,
                                    m_FFJitFormat)))
            queued.State = FFJitProgram::REJECTED;
        m_FFJitPrograms.Insert(key, queued, FALSE);
        CKRE_PROFILE_VALUE("CKRE.SDL.FFJitCompiles", 1);
        return Precompiled;
    }
    if (entry->State != FFJitProgram::READY)
        return Precompiled;
    if (!entry->Programs[Variant]) {
        entry->Programs[Variant] =
            CreateFFJitProgram(entry->PixelShader, Variant, Precompiled);
        if (!entry->Programs[Variant]) {
            entry->State = FFJitProgram::REJECTED;
            return Precompiled;
        }
    }
    return entry->Programs[Variant];
}

CKDWORD CKSdlGpuRasterizerContext::CreateFFJitProgram(
    CKDWORD PixelShader,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled)
{
    std::shared_ptr<CKSdlGpuProgram> fallback = Programs.Get(Precompiled);
    if (!fallback)
        return 0;
    // Draws bind the fallback's pipelines until the worker has created the
    // program's own, so it takes the fallback's interface as it is.
    CKFFProgramDesc desc = fallback->Interface;
    desc.PixelShader = PixelShader;
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC) {
        CKDWORD &vertexShader = m_FFJitVertexShaders[Variant];
        CKShaderDesc vertexDesc;
        if (!vertexShader &&
            (!CKSdlGpuFFDxbcVertexShader(Variant, vertexDesc) ||
             CreateShader(&vertexDesc, &vertexShader) != CK_OK))
            return 0;
        desc.VertexShader = vertexShader;
    }
    CKDWORD handle = 0;
    if (CreateProgram(&desc, &handle) != CK_OK)
        return 0;
    const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(handle);
    program->CompareSamplerCount = fallback->CompareSamplerCount;
    program->Fallback = std::move(fallback);
    return handle;
}

void CKSdlGpuRasterizerContext::CompleteFFJitProgram(
    const FFJitKey &Key, const XArray<uint32_t> *Code)
{
    // Drop results for entries cleared meanwhile. An entry queued again for
    // the same key takes the first result, which is the same code.
    FFJitProgram *entry = m_FFJitPrograms.FindPtr(Key);
    if (!entry || entry->State != FFJitProgram::QUEUED)
        return;
    entry->State = FFJitProgram::REJECTED;
    if (!Code) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "FF fragment program compilation failed; drawing it precompiled");
        return;
    }
    // The compiled shader replaces the artifact of its layout.
    CKSdlGpuFFFragmentArtifactKey artifact;
    artifact.SamplerLayout =
        (CKFFSamplerLayout)Key.Values[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
    CKShaderDesc desc;
    if (!CKSdlGpuFFFragmentShader(ShaderFormat, artifact, desc))
        return;
    const bool dxbc = m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC;
    desc.Format = dxbc ? CKRST_SHADER_FORMAT_DXBC : CKRST_SHADER_FORMAT_SPIRV;
    desc.Profile = dxbc ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    desc.Code = reinterpret_cast<const CKBYTE *>(Code->Begin());
    desc.CodeSize = (CKDWORD)(Code->Size() * sizeof(uint32_t));
    if (CreateShader(&desc, &entry->PixelShader) == CK_OK)
        entry->State = FFJitProgram::READY;
}

void CKSdlGpuRasterizerContext::ClearFFJitPrograms()
{
    for (auto it = m_FFJitPrograms.Begin(); it != m_FFJitPrograms.End(); ++it) {
        FFJitProgram &entry = *it;
        for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            if (entry.Programs[variant])
                DestroyObject(entry.Programs[variant], CKRST_OBJ_PROGRAM);
        }
        if (entry.PixelShader)
            DestroyObject(entry.PixelShader, CKRST_OBJ_SHADER);
    }
    m_FFJitPrograms.Clear();
    for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
        if (m_FFJitVertexShaders[variant])
            DestroyObject(m_FFJitVertexShaders[variant], CKRST_OBJ_SHADER);
        m_FFJitVertexShaders[variant] = 0;
    }
}

CKSdlGpuRasterizerContext::FFJitCounts
CKSdlGpuRasterizerContext::CountFFJitProgramsForTests() const
{
    FFJitCounts counts;
    for (auto it = m_FFJitPrograms.Begin(); it != m_FFJitPrograms.End(); ++it) {
        const FFJitProgram &entry = *it;
        if (entry.State == FFJitProgram::QUEUED)
            ++counts.Queued;
        else if (entry.State == FFJitProgram::READY)
            ++counts.Ready;
        else
            ++counts.Rejected;
        for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            const std::shared_ptr<CKSdlGpuProgram> &program =
                Programs.Borrow(entry.Programs[variant]);
            if (!program)
                continue;
            ++counts.Programs;
            // Null entries are pipelines the worker has not created.
            for (auto pipeline = program->Pipelines.Begin();
                 pipeline != program->Pipelines.End(); ++pipeline) {
                if (*pipeline)
                    ++counts.Pipelines;
            }
        }
    }
    return counts;
}
