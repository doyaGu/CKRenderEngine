#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaderJob.h"
#include "CKSdlGpuShaders.h"
#include "CKSdlGpuShaderPack.h"
#include "CKFFNativeFragmentJit.h"
#include "CKFFNativePositionTJit.h"
#include "CKFFNative3dJit.h"
#include "shaders/generated/abi.h"
#if CKRE_ENABLE_DIRECTX
#include "CKJitDxbc.h"
#endif
#include "CKJitSpirv.h"
#include "CKRenderProfile.h"

#include <cstring>
#include <array>

// CKSdlGpuRasterizerContext runtime compilation of fixed-function fragment
// programs and eligible vertex companions. A precompiled artifact decodes the
// fragment program and draw state; generated shaders keep the artifact's
// interface, so they replace the precompiled program in the same draws and fall
// back to it for every pipeline the worker has not created yet. The manifest of
// an earlier run queues its programs, then their pipelines, at idle priority.

namespace {
// Bounds resident keys and outstanding compilations (including finished jobs
// not yet collected). Draws deferred by either bound keep their fallback.
const int kFFJitProgramLimit = 256;
const uint64_t kFFJitCompileLimit = 64;
// Leave half the compilation budget available for this run's working set.
const CKDWORD kFFJitLoadedProgramLimit = kFFJitCompileLimit / 2;
// Ranks the loaded programs no draw has used after the used ones.
const CKDWORD kFFJitLoadedRank = 0x80000000u;
// Bounds the draw keys remembered. Draw keys only name entries, so a full
// table starts over.
const int kFFJitDrawKeyLimit = 4 * kFFJitProgramLimit;
// SDL_gpu places fragment samplers in space (set) 2, uniform buffers in 3.
const CKJitResourceLayout kFFJitResources = {3, 0, 2};

// The program in the device's shader format.
bool FFJitEmit(const CKJitFragmentShader &program, SDL_GPUShaderFormat format, XArray<uint32_t> &code)
{
#if CKRE_ENABLE_DIRECTX
    if (format == SDL_GPU_SHADERFORMAT_DXBC)
        return CKJitEmitDxbc(program, kFFJitResources, code);
#endif
    return format == SDL_GPU_SHADERFORMAT_SPIRV && CKJitEmitSpirv(program, kFFJitResources, code);
}

bool FFJitEmit(const CKJitVertexShader &program, SDL_GPUShaderFormat format, XArray<uint32_t> &code)
{
    const CKJitResourceLayout resources = {1, 0, 0};
#if CKRE_ENABLE_DIRECTX
    if (format == SDL_GPU_SHADERFORMAT_DXBC)
        return CKJitEmitDxbc(program, resources, code);
#endif
    return format == SDL_GPU_SHADERFORMAT_SPIRV && CKJitEmitSpirv(program, resources, code);
}

bool ValidStencilOps(CKDWORD ops)
{
    return (ops & 15) <= SDL_GPU_COMPAREOP_ALWAYS &&
        ((ops >> 4) & 15) <= SDL_GPU_STENCILOP_DECREMENT_AND_WRAP &&
        ((ops >> 8) & 15) <= SDL_GPU_STENCILOP_DECREMENT_AND_WRAP &&
        ((ops >> 12) & 15) <= SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
}

// Whether the device can create the pipeline of a manifest record. SDL
// indexes tables with these enums, so records are checked as input.
bool PrewarmablePipeline(SDL_GPUDevice *device, const CKSdlGpuFFJitPipelineRecord &record)
{
    const CKDWORD lo = record.StateLo, mid = record.StateMid;
    if (((lo >> 6) & 15) > SDL_GPU_COMPAREOP_ALWAYS ||
        (mid & 7) > SDL_GPU_BLENDOP_MAX || ((mid >> 3) & 7) > SDL_GPU_BLENDOP_MAX ||
        !ValidStencilOps((mid >> 10) & 0xffff) || !ValidStencilOps(record.StateHi & 0xffff))
        return false;
    if (record.ColorFormat == SDL_GPU_TEXTUREFORMAT_INVALID ||
        record.ColorFormat > SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT ||
        record.DepthFormat > SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT ||
        record.SampleCount > SDL_GPU_SAMPLECOUNT_8)
        return false;
    const SDL_GPUTextureFormat color = (SDL_GPUTextureFormat)record.ColorFormat;
    const SDL_GPUTextureFormat depth = (SDL_GPUTextureFormat)record.DepthFormat;
    const SDL_GPUSampleCount samples = (SDL_GPUSampleCount)record.SampleCount;
    const bool hasDepth = depth != SDL_GPU_TEXTUREFORMAT_INVALID;
    if (!SDL_GPUTextureSupportsFormat(device, color, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) ||
        (hasDepth && !SDL_GPUTextureSupportsFormat(device, depth, SDL_GPU_TEXTURETYPE_2D,
                                                   SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)))
        return false;
    return samples == SDL_GPU_SAMPLECOUNT_1 ||
        (SDL_GPUTextureSupportsSampleCount(device, color, samples) &&
         (!hasDepth || SDL_GPUTextureSupportsSampleCount(device, depth, samples)));
}

// Describes a program's pipeline in a record, false for a key that records
// cannot describe. Records name vertex layouts by format.
bool RecordPipeline(const CKSdlGpuPipelineKey &pipeline,
                    const XSHashTable<CKDWORD, CKDWORD> &vertexFormats, CKBYTE flags,
                    CKSdlGpuFFJitPipelineRecord &record)
{
    const CKDWORD *key = pipeline.Values;
    const CKDWORD *vertexFormat = vertexFormats.FindPtr(key[0]);
    if (!vertexFormat || key[1] != 0)
        return false;
    record.Flags = (CKBYTE)(flags | (key[10] ? CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP : 0));
    record.ColorFormat = (CKBYTE)key[2];
    record.DepthFormat = (CKBYTE)key[3];
    record.SampleCount = (CKBYTE)key[4];
    record.StencilReadMask = (CKBYTE)key[8];
    record.StencilWriteMask = (CKBYTE)key[9];
    record.VertexFormat = *vertexFormat;
    record.StateLo = key[5];
    record.StateMid = key[6];
    record.StateHi = key[7];
    return true;
}
}

// Compiles a fragment program and its optional vertex companions on the worker.
class CKSdlGpuRasterizerContext::FFJitJob : public CKSdlGpuJob {
public:
    FFJitJob(CKSdlGpuRasterizerContext &context, const FFJitKey &key,
             const CKFFNativeFragmentKey &fragment, CKFFSamplerLayout layout)
        : Shader(std::make_shared<CKSdlGpuShader>()), Context(context), Key(key),
          Fragment(fragment), Layout(layout), Device(context.Device),
          Format(context.m_FFJitFormat) {
        Shader->Job = this;
        // The compiled shader declares the resources of its artifact.
        CKShaderDesc &desc = Shader->Desc;
        Described = CKSdlGpuFFFragmentShader(context.ShaderFormat, FFJitArtifact(key), desc) != FALSE;
        const bool dxbc = Format == SDL_GPU_SHADERFORMAT_DXBC;
        desc.Format = dxbc ? CKRST_SHADER_FORMAT_DXBC : CKRST_SHADER_FORMAT_SPIRV;
        desc.Profile = dxbc ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
        desc.Code = nullptr;
        desc.CodeSize = 0;
        if (context.m_FFJitVertexEnabled) {
            for (unsigned clip = 0; clip < 2; ++clip) {
                Vertex[clip] = std::make_shared<CKSdlGpuShader>();
                Vertex[clip]->Job = this;
                Vertex[clip]->Desc = desc;
                Vertex[clip]->Desc.Stage = CKRST_SHADER_VERTEX;
                Vertex[clip]->Desc.SamplerCount = 0;
                Vertex[clip]->Desc.UniformBufferCount = CKSDL_SHADER_FF_POSITIONT_UNIFORM_BUFFERS;
                if (Fragment.Switches[0] & CKFF_NATIVE_FRAGMENT_COMPARISONS) {
                    DepthPad[clip] = std::make_shared<CKSdlGpuShader>();
                    DepthPad[clip]->Job = this;
                    DepthPad[clip]->Desc = Vertex[clip]->Desc;
                }
                Unlit[clip] = std::make_shared<CKSdlGpuShader>();
                Unlit[clip]->Job = this;
                Unlit[clip]->Desc = Vertex[clip]->Desc;
                Unlit[clip]->Desc.UniformBufferCount = CKSDL_SHADER_FF_3D_UNIFORM_BUFFERS;
                Lit[clip] = std::make_shared<CKSdlGpuShader>();
                Lit[clip]->Job = this;
                Lit[clip]->Desc = Unlit[clip]->Desc;
            }
        }
    }
    ~FFJitJob() override {
        Shader->Job = nullptr;
        for (unsigned clip = 0; clip < 2; ++clip) {
            if (Vertex[clip]) Vertex[clip]->Job = nullptr;
            if (DepthPad[clip]) DepthPad[clip]->Job = nullptr;
            if (Unlit[clip]) Unlit[clip]->Job = nullptr;
            if (Lit[clip]) Lit[clip]->Job = nullptr;
        }
    }

    void Run() override {
        CKRE_PROFILE_SCOPE("CKRE.SDL.FFJitCompile");
        const Uint64 start = SDL_GetTicksNS();
        Compile();
        for (unsigned clip = 0; clip < 2; ++clip) {
            if (Vertex[clip]) CompileVertex(Vertex[clip], CKSdlGpuProgram::POSITIONT_VERTEX, clip != 0);
            if (DepthPad[clip]) CompileVertex(DepthPad[clip], CKSdlGpuProgram::POSITIONT_VERTEX, clip != 0, true);
            if (Unlit[clip]) CompileVertex(Unlit[clip], CKSdlGpuProgram::UNLIT_VERTEX, clip != 0);
            if (Lit[clip]) CompileVertex(Lit[clip], CKSdlGpuProgram::LIT_VERTEX, clip != 0);
        }
        ElapsedNs = SDL_GetTicksNS() - start;
    }
    void CompileVertex(const std::shared_ptr<CKSdlGpuShader> &vertex, CKSdlGpuProgram::VertexJitKind kind, bool clipping, bool depthPad = false) {
        CKJitVertexShader program;
        XArray<uint32_t> code;
        const CK_SHADER_FORMAT reference = Format == SDL_GPU_SHADERFORMAT_DXBC
            ? CKRST_SHADER_FORMAT_DXIL : CKRST_SHADER_FORMAT_SPIRV;
        const char *name = kind == CKSdlGpuProgram::POSITIONT_VERTEX ? "POSITIONT" :
            kind == CKSdlGpuProgram::LIT_VERTEX ? "lit 3D" : "unlit 3D";
        const bool compiled = kind == CKSdlGpuProgram::POSITIONT_VERTEX
            ? CKFFCompileNativePositionTProgram(Fragment, Layout, reference, program, clipping, depthPad)
            : kind == CKSdlGpuProgram::LIT_VERTEX
                ? CKFFCompileNativeLitProgram(Fragment, Layout, reference, program, clipping)
                : CKFFCompileNativeUnlitProgram(Fragment, Layout, reference, program, clipping);
        if (!compiled || program.UniformBufferCount != vertex->Desc.UniformBufferCount || !FFJitEmit(program, Format, code)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "FF %s compilation failed (clip=%d); drawing it precompiled", name, clipping);
            return;
        }
        SDL_GPUShaderCreateInfo info = CKSdlGpuShaderInfo(*vertex, Format);
        info.code = reinterpret_cast<const Uint8 *>(code.Begin());
        info.code_size = code.Size() * sizeof(uint32_t);
        vertex->Shader = CKSdlGpuOwn(Device, SDL_CreateGPUShader(Device, &info), SDL_ReleaseGPUShader);
        if (!vertex->Shader)
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_gpu CreateGPUShader %s failed (clip=%d): %s", name, clipping, SDL_GetError());
    }
    void Compile() {
        if (!Described)
            return;
        CKJitFragmentShader program;
        XArray<uint32_t> code;
        if (!CKFFCompileNativeFragmentProgram(Fragment, Layout, program) || !FFJitEmit(program, Format, code)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "FF fragment program compilation failed; drawing it precompiled");
            return;
        }
        Shader->SamplerCount = program.SamplerCount;
        SDL_GPUShaderCreateInfo info = CKSdlGpuShaderInfo(*Shader, Format);
        info.code = reinterpret_cast<const Uint8 *>(code.Begin());
        info.code_size = code.Size() * sizeof(uint32_t);
        Shader->Shader = CKSdlGpuOwn(Device, SDL_CreateGPUShader(Device, &info), SDL_ReleaseGPUShader);
        if (!Shader->Shader)
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "SDL_gpu CreateGPUShader failed: %s; drawing the FF fragment program precompiled",
                         SDL_GetError());
    }
    void Complete() override {
        CKSdlGpuFFJitStats &stats = Context.m_FFJitStats;
        ++stats.CompileCompleted;
        if (!Shader->Shader) ++stats.CompileFailed;
        for (unsigned clip = 0; clip < 2; ++clip) {
            for (const auto &vertex : {Vertex[clip], DepthPad[clip], Unlit[clip], Lit[clip]}) {
                if (!vertex) continue;
                ++stats.VertexCompileCompleted;
                if (!vertex->Shader) ++stats.VertexCompileFailed;
            }
        }
        stats.CompileNs += ElapsedNs;
        stats.CompileMaxNs = std::max(stats.CompileMaxNs, ElapsedNs);
        Context.CompleteFFJitProgram(Key, Shader);
    }

    // Programs take the shader before it is created.
    std::shared_ptr<CKSdlGpuShader> Shader;
    std::shared_ptr<CKSdlGpuShader> Vertex[2], DepthPad[2], Unlit[2], Lit[2];

private:
    CKSdlGpuRasterizerContext &Context;
    FFJitKey Key;
    CKFFNativeFragmentKey Fragment;
    CKFFSamplerLayout Layout;
    SDL_GPUDevice *Device;
    SDL_GPUShaderFormat Format;
    bool Described = false;
    uint64_t ElapsedNs = 0;
};

void CKSdlGpuRasterizerContext::InitFFJit()
{
    m_FFJitFormat = SDL_GPU_SHADERFORMAT_INVALID;
    m_FFJitStats = {};
    const char *vertex = SDL_getenv("CKRE_SDL_GPU_FF_VERTEX_JIT");
    m_FFJitVertexEnabled = !vertex || SDL_strcmp(vertex, "0") != 0;
    m_FFJitManifest = "";
    m_FFJitIdentity = 0;
    m_FFJitUses = 0;
    m_FFJitClock = 0;
    m_FFWorkerShaders = 0;
    // CKRE_SDL_GPU_FF_JIT=0 draws every program with the precompiled shaders.
    const char *setting = SDL_getenv("CKRE_SDL_GPU_FF_JIT");
    if (setting && SDL_strcmp(setting, "0") == 0)
        return;
    // D3D12 runs the DXBC the compiler emits, as DXIL would need DXC.
    if (ShaderFormat == SDL_GPU_SHADERFORMAT_SPIRV)
        m_FFJitFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#if CKRE_ENABLE_DIRECTX
    else if (NativeShaderFormat(CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_PROFILE_DX12) ==
             SDL_GPU_SHADERFORMAT_DXBC)
        m_FFJitFormat = SDL_GPU_SHADERFORMAT_DXBC;
    // Compiled DXBC programs pair with the DXBC vertex shaders; decode them
    // now rather than when the first program is compiled.
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC && !CKSdlGpuLoadShaders(SDL_GPU_SHADERFORMAT_DXBC))
        m_FFJitFormat = SDL_GPU_SHADERFORMAT_INVALID;
#endif
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_INVALID)
        return;
    const char *device = SDL_GetStringProperty(
        SDL_GetGPUDeviceProperties(Device), SDL_PROP_GPU_DEVICE_NAME_STRING, "");
    m_FFJitIdentity = CKSdlGpuFFJitManifestIdentity(
        SDL_GetGPUDeviceDriver(Device), device, m_FFJitFormat);
    m_FFJitManifest = CKSdlGpuFFJitManifestPath(m_FFJitIdentity);
    LoadFFJitManifest();
}

void CKSdlGpuRasterizerContext::LoadFFJitManifest()
{
    CKSdlGpuFFJitManifest manifest;
    if (m_FFJitManifest.Length() == 0 ||
        !CKSdlGpuLoadFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, manifest) ||
        !StartWorker())
        return;
    // The entry of each program record, -1 past the limit.
    XArray<int> entries;
    const int first = m_FFJitPrograms.Size();
    CKDWORD loaded = 0;
    for (int i = 0; i < manifest.Programs.Size(); ++i) {
        const CKSdlGpuFFJitProgramRecord &record = manifest.Programs[i];
        const CKFFSamplerLayout layout = (CKFFSamplerLayout)record.SamplerLayout;
        CKFFNativeFragmentKey fragment;
        fragment.Program.SetLanes(record.Lanes, CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
        std::memcpy(fragment.Switches, record.Switches, sizeof(fragment.Switches));
        // Records hold canonical keys; any other key would stay unused.
        CKFFCanonicalizeNativeFragmentKey(fragment, layout);
        const FFJitKey key = MakeFFJitKey(fragment, layout);
        const int *found = m_FFJitKeys.FindPtr(key);
        int index = found ? *found : -1;
        if (!found && loaded < kFFJitLoadedProgramLimit)
            index = AddFFJitProgram(key, kFFJitLoadedRank | loaded++);
        entries.PushBack(index);
    }
    // The worker first creates the precompiled shaders of the pipelines, so
    // that neither the draws of their programs nor the compilations create
    // them on this thread.
    ShaderJob *shaders = new ShaderJob(*this);
    // The precompiled program of each pipeline record, or 0.
    XArray<CKDWORD> precompiled;
    precompiled.Resize(manifest.Pipelines.Size());
    for (int i = 0; i < manifest.Pipelines.Size(); ++i) {
        const CKSdlGpuFFJitPipelineRecord &record = manifest.Pipelines[i];
        const bool precompiledPipeline =
            (record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED) != 0;
        precompiled[i] = 0;
        CKSdlGpuFFFragmentArtifactKey artifact;
        if (precompiledPipeline) {
            // The manifest has checked the index.
            CKSdlGpuFFFragmentArtifactKeyAt(record.Program, artifact);
        } else if (entries[record.Program] >= 0) {
            FFJitProgram &entry = m_FFJitPrograms[entries[record.Program]];
            entry.Prewarm.PushBack(record);
            artifact = FFJitArtifact(entry.Key);
        } else {
            continue;
        }
        if (!PrewarmablePipeline(Device, record))
            continue;
        const CKFFProgramVariant variant = (CKFFProgramVariant)record.Variant;
        precompiled[i] = NativeFFProgram(
            variant, artifact, (record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD) != 0, shaders);
#if CKRE_ENABLE_DIRECTX
        const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(precompiled[i]);
        if (program && !precompiledPipeline && m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC)
            FFJitVertexShader(variant, program->Interface.VertexShader, shaders);
#endif
    }
    if (shaders->Empty()) {
        delete shaders;
    } else {
        Worker.Submit(shaders, CKSDLGPU_JOB_IDLE);
        m_FFShaderJob = shaders;
    }
    // The pipelines draws used precompiled while their programs compiled
    // come next, so that the same draws find them before the compilations.
    for (int i = 0; i < manifest.Pipelines.Size(); ++i) {
        if ((manifest.Pipelines[i].Flags & CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED) && precompiled[i])
            PrewarmFFJitPipeline(precompiled[i], manifest.Pipelines[i]);
    }
    for (int i = first; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitKey &key = m_FFJitPrograms[i].Key;
        CKFFNativeFragmentKey fragment;
        fragment.Program.SetLanes(key.Values, CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
        std::memcpy(fragment.Switches, key.Values + CKFF_FRAGMENT_PROGRAM_LANE_COUNT,
                    sizeof(fragment.Switches));
        SubmitFFJitProgram(m_FFJitPrograms[i], fragment,
                           (CKFFSamplerLayout)key.Values[FF_JIT_KEY_LAYOUT], CKSDLGPU_JOB_IDLE);
    }
}

CKSdlGpuRasterizerContext::FFJitKey CKSdlGpuRasterizerContext::MakeFFJitKey(
    const CKFFNativeFragmentKey &Fragment,
    CKFFSamplerLayout Layout)
{
    FFJitKey key;
    std::memcpy(key.Values, Fragment.Program.Lanes(),
                sizeof(CKDWORD) * CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
    std::memcpy(key.Values + CKFF_FRAGMENT_PROGRAM_LANE_COUNT, Fragment.Switches,
                sizeof(Fragment.Switches));
    key.Values[FF_JIT_KEY_LAYOUT] = (CKDWORD)Layout;
    return key;
}

CKSdlGpuFFFragmentArtifactKey CKSdlGpuRasterizerContext::FFJitArtifact(const FFJitKey &Key)
{
    const CKDWORD switches = Key.Values[CKFF_FRAGMENT_PROGRAM_LANE_COUNT];
    CKSdlGpuFFFragmentArtifactKey artifact;
    artifact.SamplerLayout = (CKFFSamplerLayout)Key.Values[FF_JIT_KEY_LAYOUT];
    artifact.UsesShaderSampling = (switches & CKFF_NATIVE_FRAGMENT_SHADER_SAMPLING) != 0;
    artifact.ComparisonResourceCount =
        (CKBYTE)((switches & CKFF_NATIVE_FRAGMENT_COMPARISONS) >> CKFF_NATIVE_FRAGMENT_COMPARISON_SHIFT);
    return artifact;
}

int CKSdlGpuRasterizerContext::AddFFJitProgram(const FFJitKey &Key, CKDWORD Rank)
{
    FFJitProgram entry;
    entry.Key = Key;
    entry.Rank = Rank;
    m_FFJitPrograms.PushBack(entry);
    const int index = m_FFJitPrograms.Size() - 1;
    m_FFJitKeys.InsertUnique(Key, index);
    return index;
}

// XSHashTable::Remove leaves a tombstone that Insert counts towards doubling
// the table. The bounded tables insert with InsertUnique, which grows by the
// live count, and are rebuilt in place once per cycle of replacements so that
// tombstones do not lengthen probes; Clear keeps the buckets.
void CKSdlGpuRasterizerContext::RebuildFFJitCandidates()
{
    XClassArray<CKSdlGpuFFJitUsage> usage;
    usage.Resize(m_FFJitCandidateOrder.Size());
    for (int i = 0; i < m_FFJitCandidateOrder.Size(); ++i)
        usage[i] = *m_FFJitCandidates.FindPtr(m_FFJitCandidateOrder[i]);
    m_FFJitCandidates.Clear();
    for (int i = 0; i < m_FFJitCandidateOrder.Size(); ++i)
        m_FFJitCandidates.InsertUnique(m_FFJitCandidateOrder[i], usage[i]);
}

void CKSdlGpuRasterizerContext::RebuildFFJitKeys()
{
    m_FFJitKeys.Clear();
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i)
        m_FFJitKeys.InsertUnique(m_FFJitPrograms[i].Key, i);
}

void CKSdlGpuRasterizerContext::ReleaseFFJitProgram(FFJitProgram &entry)
{
    // Queued draw packets retain their program. Removing public handles only
    // drops cache ownership; pipeline jobs also retain their shader inputs.
    for (int b = 0; b < entry.Programs.Size(); ++b) {
        if (entry.Programs[b].Program)
            DestroyObject(entry.Programs[b].Program, CKRST_OBJ_PROGRAM);
    }
    if (entry.PixelShader)
        DestroyObject(entry.PixelShader, CKRST_OBJ_SHADER);
    for (unsigned clip = 0; clip < 2; ++clip) {
        if (entry.PositionTShader[clip]) DestroyObject(entry.PositionTShader[clip], CKRST_OBJ_SHADER);
        if (entry.DepthPadShader[clip]) DestroyObject(entry.DepthPadShader[clip], CKRST_OBJ_SHADER);
        if (entry.UnlitShader[clip]) DestroyObject(entry.UnlitShader[clip], CKRST_OBJ_SHADER);
        if (entry.LitShader[clip]) DestroyObject(entry.LitShader[clip], CKRST_OBJ_SHADER);
    }
}

int CKSdlGpuRasterizerContext::AdmitFFJitProgram(const FFJitKey &key)
{
    CKSdlGpuFFJitUsage *candidate = m_FFJitCandidates.FindPtr(key);
    if (!candidate) {
        if (m_FFJitCandidateOrder.Size() == kFFJitDrawKeyLimit) {
            m_FFJitCandidates.Remove(m_FFJitCandidateOrder[m_FFJitCandidateCursor]);
            m_FFJitCandidateOrder[m_FFJitCandidateCursor] = key;
            m_FFJitCandidateCursor = (m_FFJitCandidateCursor + 1) % kFFJitDrawKeyLimit;
        } else {
            m_FFJitCandidateOrder.PushBack(key);
        }
        m_FFJitCandidates.InsertUnique(key, CKSdlGpuFFJitUsage());
        // Each full cycle of the FIFO has left as many tombstones as keys.
        if (m_FFJitCandidateCursor == 0 && m_FFJitCandidateOrder.Size() == kFFJitDrawKeyLimit) {
            RebuildFFJitCandidates();
            ++m_FFJitStats.TableRebuilds;
        }
        candidate = m_FFJitCandidates.FindPtr(key);
    }
    candidate->Touch(m_FFJitClock);
    const uint64_t pending = m_FFJitStats.CompileQueued - m_FFJitStats.CompileCompleted;
    if (pending >= kFFJitCompileLimit) {
        ++m_FFJitStats.QueueDeferred;
        return -1;
    }
    if (m_FFJitPrograms.Size() < kFFJitProgramLimit)
        return AddFFJitProgram(key, ++m_FFJitUses);
    // Most pressure misses are one-offs; avoid scanning the resident cache.
    if (candidate->Hits < 2) {
        ++m_FFJitStats.Capacity;
        return -1;
    }

    int victim = -1;
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitProgram &entry = m_FFJitPrograms[i];
        // Never churn pending work or let a one-off evict a resident program.
        if (entry.State == FFJitProgram::QUEUED ||
            entry.Usage.Score(m_FFJitClock) > candidate->Hits)
            continue;
        if (victim < 0 || entry.Usage.Score(m_FFJitClock) < m_FFJitPrograms[victim].Usage.Score(m_FFJitClock) ||
            (entry.Usage.Score(m_FFJitClock) == m_FFJitPrograms[victim].Usage.Score(m_FFJitClock) &&
             entry.Usage.Last < m_FFJitPrograms[victim].Usage.Last))
            victim = i;
    }
    if (victim < 0) {
        ++m_FFJitStats.Capacity;
        return -1;
    }
    FFJitProgram &entry = m_FFJitPrograms[victim];
    m_FFJitKeys.Remove(entry.Key);
    ReleaseFFJitProgram(entry);
    entry = FFJitProgram();
    entry.Key = key;
    entry.Rank = ++m_FFJitUses;
    // Draw-key entries contain slot indices. No alias may keep the victim's
    // index after this slot is reused for another canonical key.
    m_FFJitDrawKeys.Clear();
    m_FFJitKeys.InsertUnique(key, victim);
    ++m_FFJitStats.Evictions;
    if (m_FFJitStats.Evictions % kFFJitProgramLimit == 0) {
        RebuildFFJitKeys();
        ++m_FFJitStats.TableRebuilds;
    }
    return victim;
}

bool CKSdlGpuRasterizerContext::SubmitFFJitProgram(
    FFJitProgram &Entry,
    const CKFFNativeFragmentKey &Fragment,
    CKFFSamplerLayout Layout,
    CKSdlGpuJobPriority Priority)
{
    FFJitJob *job = new FFJitJob(*this, Entry.Key, Fragment, Layout);
    const std::shared_ptr<CKSdlGpuShader> shader = job->Shader;
    const auto vertex = std::array<std::shared_ptr<CKSdlGpuShader>, 2>{job->Vertex[0], job->Vertex[1]};
    const auto depthPad = std::array<std::shared_ptr<CKSdlGpuShader>, 2>{job->DepthPad[0], job->DepthPad[1]};
    const auto unlit = std::array<std::shared_ptr<CKSdlGpuShader>, 2>{job->Unlit[0], job->Unlit[1]};
    const auto lit = std::array<std::shared_ptr<CKSdlGpuShader>, 2>{job->Lit[0], job->Lit[1]};
    // Compilations run after the job creating the precompiled shaders of the
    // manifest, so that the pipelines waiting for them find those too. A
    // draw waiting for one promotes that job as well.
    if (SubmitJob(job, Priority, m_FFShaderJob)) {
        ++m_FFJitStats.CompileQueued;
        m_FFJitStats.CompilePendingPeak = std::max(m_FFJitStats.CompilePendingPeak,
            m_FFJitStats.CompileQueued - m_FFJitStats.CompileCompleted);
        Entry.PixelShader = ShaderObjects.Add(shader);
        for (unsigned clip = 0; clip < 2; ++clip) {
            if (vertex[clip]) Entry.PositionTShader[clip] = ShaderObjects.Add(vertex[clip]);
            if (depthPad[clip]) Entry.DepthPadShader[clip] = ShaderObjects.Add(depthPad[clip]);
            if (unlit[clip]) Entry.UnlitShader[clip] = ShaderObjects.Add(unlit[clip]);
            if (lit[clip]) Entry.LitShader[clip] = ShaderObjects.Add(lit[clip]);
        }
        if (m_FFShaderJob && Priority == CKSDLGPU_JOB_NORMAL)
            Worker.Promote(job);
    }
    if (!Entry.PixelShader) {
        Entry.State = FFJitProgram::REJECTED;
        return false;
    }
    // The worker owns the job; an idle one is kept only to promote it.
    if (Priority == CKSDLGPU_JOB_IDLE)
        Entry.IdleJob = job;
    return true;
}

int CKSdlGpuRasterizerContext::AddFFJitDrawKey(
    const FFJitKey &DrawKey,
    CKFFNativeFragmentKey Fragment,
    CKFFSamplerLayout Layout)
{
    if (m_FFJitDrawKeys.Size() >= kFFJitDrawKeyLimit)
        m_FFJitDrawKeys.Clear();
    CKFFCanonicalizeNativeFragmentKey(Fragment, Layout);
    const FFJitKey key = MakeFFJitKey(Fragment, Layout);
    const int *found = m_FFJitKeys.FindPtr(key);
    int index = found ? *found : -1;
    if (!found) {
        // Never compile while drawing. A program the worker cannot take
        // keeps its precompiled shader.
        index = AdmitFFJitProgram(key);
        if (index >= 0) {
            SubmitFFJitProgram(m_FFJitPrograms[index], Fragment, Layout, CKSDLGPU_JOB_NORMAL);
            CKRE_PROFILE_VALUE("CKRE.SDL.FFJitCompiles", 1);
        }
    }
    // Do not memoize a deferred key as permanently precompiled.
    if (index >= 0)
        m_FFJitDrawKeys.Insert(DrawKey, index, FALSE);
    return index;
}

CKDWORD CKSdlGpuRasterizerContext::ResolveFFJitProgram(
    const CKFFFragmentProgram &FragmentProgram,
    const CKFFConstantSet *Constants,
    const CKSdlGpuFFFragmentArtifactKey &Artifact,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled)
{
    ++m_FFJitStats.Requests;
    ++m_FFJitClock;
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_INVALID || !Constants) {
        ++m_FFJitStats.Unavailable;
        return Precompiled;
    }
    // A draw computes its key, canonicalizing it only when it is new.
    const CKFFNativeFragmentKey fragment = CKFFNativeFragmentDrawKey(
        FragmentProgram, *Constants, Artifact.UsesShaderSampling != FALSE,
        Artifact.ComparisonResourceCount);
    const FFJitKey drawKey = MakeFFJitKey(fragment, Artifact.SamplerLayout);
    const int *found = m_FFJitDrawKeys.FindPtr(drawKey);
    const int index = found ? *found : AddFFJitDrawKey(drawKey, fragment, Artifact.SamplerLayout);
    if (index < 0) {
        return Precompiled;
    }
    FFJitProgram &entry = m_FFJitPrograms[index];
    entry.Usage.Touch(m_FFJitClock);
    if (entry.Rank & kFFJitLoadedRank)
        entry.Rank = ++m_FFJitUses;
    if (entry.State == FFJitProgram::REJECTED) {
        ++m_FFJitStats.Rejected;
        return Precompiled;
    }
    // A draw now waits for a program loaded at idle priority.
    if (entry.IdleJob) {
        Worker.Promote(entry.IdleJob);
        entry.IdleJob = nullptr;
    }
    CKSdlGpuProgram::VertexJitKind kind = CKSdlGpuProgram::PRECOMPILED_VERTEX;
    if (Variant == CKFF_PROGRAM_3D || Variant == CKFF_PROGRAM_3D_CLIP) {
        const unsigned clip = Variant == CKFF_PROGRAM_3D_CLIP;
        if (entry.UnlitShader[clip] && CKFFNativeUnlitDraw(*Constants)) kind = CKSdlGpuProgram::UNLIT_VERTEX;
        else if (entry.LitShader[clip] && CKFFNativeLitDraw(*Constants)) kind = CKSdlGpuProgram::LIT_VERTEX;
    }
    const CKDWORD program = BindFFJitProgram(entry, Variant, Precompiled, kind);
    if (program) ++m_FFJitStats.Specialized;
    else ++m_FFJitStats.Rejected;
    return program ? program : Precompiled;
}

CKDWORD CKSdlGpuRasterizerContext::BindFFJitProgram(
    FFJitProgram &Entry,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled, CKSdlGpuProgram::VertexJitKind VertexKind)
{
    const unsigned clip = Variant == CKFF_PROGRAM_3D_CLIP || Variant == CKFF_PROGRAM_POSITIONT_CLIP;
    const CKDWORD shader3d = (Variant != CKFF_PROGRAM_3D && Variant != CKFF_PROGRAM_3D_CLIP) ? 0 :
        VertexKind == CKSdlGpuProgram::UNLIT_VERTEX ? Entry.UnlitShader[clip] :
        VertexKind == CKSdlGpuProgram::LIT_VERTEX ? Entry.LitShader[clip] : 0;
    if (!shader3d) VertexKind = CKSdlGpuProgram::PRECOMPILED_VERTEX;
    for (int i = 0; i < Entry.Programs.Size(); ++i) {
        if (Entry.Programs[i].Precompiled == Precompiled && Entry.Programs[i].VertexKind == VertexKind)
            return Entry.Programs[i].Program;
    }
    const FFJitProgram::Binding binding = {
        Precompiled, Variant, CreateFFJitProgram(Entry.PixelShader, Variant, Precompiled, Entry.PositionTShader[clip],
                                                Entry.DepthPadShader[clip], shader3d, VertexKind), VertexKind};
    // A failed binding is remembered, so its draws stay precompiled without
    // retrying; the entry's other bindings keep their programs.
    Entry.Programs.PushBack(binding);
    return binding.Program;
}

CKDWORD CKSdlGpuRasterizerContext::CreateFFJitProgram(
    CKDWORD PixelShader,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled, CKDWORD PositionTShader, CKDWORD DepthPadShader, CKDWORD Shader3d, CKSdlGpuProgram::VertexJitKind VertexKind)
{
    std::shared_ptr<CKSdlGpuProgram> fallback = Programs.Get(Precompiled);
    if (!fallback)
        return 0;
    // Draws bind the fallback's pipelines until the worker has created the
    // program's own, so it takes the fallback's interface as it is.
    CKFFProgramDesc desc = fallback->Interface;
    desc.PixelShader = PixelShader;
    const unsigned clip = Variant == CKFF_PROGRAM_3D_CLIP || Variant == CKFF_PROGRAM_POSITIONT_CLIP;
    if (desc.VertexShader && desc.VertexShader == m_NativeFFDepthPadVertexShaders[clip])
        PositionTShader = DepthPadShader;
    const bool positionT = PositionTShader && (Variant == CKFF_PROGRAM_POSITIONT || Variant == CKFF_PROGRAM_POSITIONT_CLIP);
    if (positionT)
        desc.VertexShader = PositionTShader;
    if (Shader3d) desc.VertexShader = Shader3d;
#if CKRE_ENABLE_DIRECTX
    if (!positionT && !Shader3d && m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC) {
        desc.VertexShader = FFJitVertexShader(Variant, desc.VertexShader, nullptr);
        if (!desc.VertexShader)
            return 0;
    }
#endif
    CKDWORD handle = 0;
    if (CreateProgram(&desc, &handle) != CK_OK)
        return 0;
    const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(handle);
    program->VertexJit = Shader3d ? VertexKind :
        positionT ? CKSdlGpuProgram::POSITIONT_VERTEX : CKSdlGpuProgram::PRECOMPILED_VERTEX;
    program->UserClip = clip && program->VertexJit != CKSdlGpuProgram::PRECOMPILED_VERTEX;
    program->CompareSamplerCount = fallback->CompareSamplerCount;
    program->Fallback = std::move(fallback);
    return handle;
}

#if CKRE_ENABLE_DIRECTX
CKDWORD CKSdlGpuRasterizerContext::FFJitVertexShader(
    CKFFProgramVariant Variant,
    CKDWORD FallbackShader,
    ShaderJob *Job)
{
    const CKDWORD clip = Variant == CKFF_PROGRAM_POSITIONT_CLIP ? 1u : 0u;
    const bool pad = FallbackShader != 0 &&
        FallbackShader == m_NativeFFDepthPadVertexShaders[clip];
    CKDWORD &vertexShader =
        pad ? m_FFJitDepthPadVertexShaders[clip] : m_FFJitVertexShaders[Variant];
    CKShaderDesc desc;
    if (!vertexShader &&
        (!(pad ? CKSdlGpuFFDepthPadVertexShader(m_FFJitFormat, clip != 0, desc)
               : CKSdlGpuFFDxbcVertexShader(Variant, desc)) ||
         CreateShader(&desc, &vertexShader, Job) != CK_OK))
        return 0;
    return vertexShader;
}
#endif

void CKSdlGpuRasterizerContext::CompleteFFJitProgram(
    const FFJitKey &Key, const std::shared_ptr<CKSdlGpuShader> &Shader)
{
    // Drop results for entries cleared or rejected meanwhile. An entry
    // queued again for the same key has a shader of its own.
    const int *index = m_FFJitKeys.FindPtr(Key);
    if (!index)
        return;
    FFJitProgram &entry = m_FFJitPrograms[*index];
    if (entry.State != FFJitProgram::QUEUED || ShaderObjects.Borrow(entry.PixelShader) != Shader)
        return;
    entry.IdleJob = nullptr;
    // The worker has logged why a shader is missing. Its programs never
    // create pipelines, and its draws are then drawn precompiled.
    if (!Shader->Shader) {
        entry.State = FFJitProgram::REJECTED;
        return;
    }
    entry.State = FFJitProgram::READY;
    PrewarmFFJitProgram(entry);
}

void CKSdlGpuRasterizerContext::PrewarmFFJitProgram(FFJitProgram &Entry)
{
    // The draws of the keys that canonicalize to the entry's may replace
    // other artifacts, and bind programs of their own.
    const CKSdlGpuFFFragmentArtifactKey artifact = FFJitArtifact(Entry.Key);
    for (int i = 0; i < Entry.Prewarm.Size(); ++i) {
        const CKSdlGpuFFJitPipelineRecord &record = Entry.Prewarm[i];
        if (!PrewarmablePipeline(Device, record))
            continue;
        const CKFFProgramVariant variant = (CKFFProgramVariant)record.Variant;
        const CKDWORD precompiled = NativeFFProgram(
            variant, artifact, (record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD) != 0);
        // As when a draw creates it, a binding that fails is drawn
        // precompiled; the record's other bindings are still prewarmed.
        const CKDWORD handle = precompiled ? BindFFJitProgram(Entry, variant, precompiled,
            record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_UNLIT ? CKSdlGpuProgram::UNLIT_VERTEX :
            record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_LIT ? CKSdlGpuProgram::LIT_VERTEX :
            CKSdlGpuProgram::PRECOMPILED_VERTEX) : 0;
        if (handle)
            PrewarmFFJitPipeline(handle, record);
    }
    Entry.Prewarm.Clear();
}

void CKSdlGpuRasterizerContext::PrewarmFFJitPipeline(CKDWORD Program,
                                                     const CKSdlGpuFFJitPipelineRecord &Record)
{
    const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(Program);
    if (!program || QueueFFJitPrewarm(program, Record))
        return;
    // Queued again once collected jobs free the idle budget. Without this a
    // precompiled pipeline past the budget is created at its first draw.
    if (m_FFJitDeferredPrewarms.Size() < CKSDL_GPU_FF_JIT_MANIFEST_MAX_PIPELINES) {
        FFJitDeferredPrewarm deferred;
        deferred.Program = program;
        deferred.Record = Record;
        m_FFJitDeferredPrewarms.PushBack(deferred);
    }
}

void CKSdlGpuRasterizerContext::RetryFFJitPrewarms()
{
    // In manifest order; a deferral leaves the rest for a later collection.
    for (; m_FFJitDeferredPrewarmNext < m_FFJitDeferredPrewarms.Size(); ++m_FFJitDeferredPrewarmNext) {
        const FFJitDeferredPrewarm &deferred = m_FFJitDeferredPrewarms[m_FFJitDeferredPrewarmNext];
        const std::shared_ptr<CKSdlGpuProgram> program = deferred.Program.lock();
        if (program && !QueueFFJitPrewarm(program, deferred.Record))
            return;
    }
    m_FFJitDeferredPrewarms.Clear();
    m_FFJitDeferredPrewarmNext = 0;
}

bool CKSdlGpuRasterizerContext::QueueFFJitPrewarm(const std::shared_ptr<CKSdlGpuProgram> &program,
                                                  const CKSdlGpuFFJitPipelineRecord &Record)
{
    const CKDWORD layout = GetNativeVertexLayout(Record.VertexFormat);
    const std::shared_ptr<CKSdlGpuLayout> &vertexLayout = Layouts.Borrow(layout);
    if (!vertexLayout)
        return true;
    // The fields a pipeline depends on, as the record's draw had them.
    CKSdlGpuDraw draw;
    draw.State.State.Lo = Record.StateLo;
    draw.State.State.Mid = Record.StateMid;
    draw.State.State.Hi = Record.StateHi;
    draw.State.StencilReadMask = Record.StencilReadMask;
    draw.State.StencilWriteMask = Record.StencilWriteMask;
    draw.State.DepthClipEnabled =
        (Record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP) ? TRUE : FALSE;
    draw.Program = program.get();
    draw.Layout = vertexLayout.get();
    draw.LayoutHandle = layout;
    return QueuePipeline(draw, (SDL_GPUTextureFormat)Record.ColorFormat,
                         (SDL_GPUTextureFormat)Record.DepthFormat,
                         (SDL_GPUSampleCount)Record.SampleCount, CKSDLGPU_JOB_IDLE);
}

void CKSdlGpuRasterizerContext::SaveFFJitManifest()
{
    if (m_FFJitManifest.Length() == 0 || m_FFJitPrograms.Size() == 0 ||
        GetDeviceStatus() != CK_OK)
        return;
    // Pipeline keys name vertex layouts by handle, records by format.
    XSHashTable<CKDWORD, CKDWORD> vertexFormats;
    for (auto it = m_NativeVertexLayouts.Begin(); it != m_NativeVertexLayouts.End(); ++it)
        vertexFormats.Insert(*it, it.GetKey(), FALSE);
    XArray<const FFJitProgram *> ranked;
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        if (m_FFJitPrograms[i].State != FFJitProgram::REJECTED)
            ranked.PushBack(&m_FFJitPrograms[i]);
    }
    std::sort(ranked.Begin(), ranked.End(),
              [this](const FFJitProgram *a, const FFJitProgram *b) {
                  const unsigned x = a->Usage.Score(m_FFJitClock), y = b->Usage.Score(m_FFJitClock);
                  if (x != y) return x > y;
                  if (a->Usage.Last != b->Usage.Last) return a->Usage.Last > b->Usage.Last;
                  return a->Rank < b->Rank;
              });
    CKSdlGpuFFJitManifest manifest;
    for (int i = 0; i < ranked.Size() &&
                    manifest.Programs.Size() < CKSDL_GPU_FF_JIT_MANIFEST_MAX_PROGRAMS; ++i) {
        const FFJitProgram &entry = *ranked[i];
        const CKBYTE index = (CKBYTE)manifest.Programs.Size();
        CKSdlGpuFFJitProgramRecord programRecord;
        std::memcpy(programRecord.Lanes, entry.Key.Values, sizeof(programRecord.Lanes));
        std::memcpy(programRecord.Switches, entry.Key.Values + CKFF_FRAGMENT_PROGRAM_LANE_COUNT,
                    sizeof(programRecord.Switches));
        programRecord.SamplerLayout = entry.Key.Values[FF_JIT_KEY_LAYOUT];
        manifest.Programs.PushBack(programRecord);
        // A loaded program not compiled yet keeps the pipelines it came with.
        for (int r = 0; r < entry.Prewarm.Size(); ++r) {
            manifest.Pipelines.PushBack(entry.Prewarm[r]);
            manifest.Pipelines.Back().Program = index;
        }
        const CKDWORD artifact = CKSdlGpuFFFragmentArtifactIndex(FFJitArtifact(entry.Key));
        for (int b = 0; b < entry.Programs.Size(); ++b) {
            const FFJitProgram::Binding &binding = entry.Programs[b];
            const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(binding.Program);
            if (!program)
                continue;
            CKSdlGpuFFJitPipelineRecord record = {};
            record.Program = index;
            record.Variant = (CKBYTE)binding.Variant;
            const bool pad =
                binding.Precompiled == m_NativeFFPrograms[binding.Variant][artifact][1];
            const CKBYTE flags = (CKBYTE)((pad ? CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD : 0) |
                (binding.VertexKind == CKSdlGpuProgram::UNLIT_VERTEX ? CKSDL_GPU_FF_JIT_PIPELINE_UNLIT : 0) |
                (binding.VertexKind == CKSdlGpuProgram::LIT_VERTEX ? CKSDL_GPU_FF_JIT_PIPELINE_LIT : 0));
            // Pending and failed pipelines are recorded too.
            for (auto it = program->Pipelines.Begin(); it != program->Pipelines.End(); ++it) {
                if (RecordPipeline(it.GetKey(), vertexFormats, flags, record))
                    manifest.Pipelines.PushBack(record);
            }
        }
    }
    // The pipelines draws used precompiled before their programs were
    // compiled. Those only prewarmed are left out, so that a run prewarms
    // only what the run before it drew.
    for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
        for (CKDWORD artifact = 0; artifact < CKSDL_GPU_FF_FRAGMENT_ARTIFACT_COUNT; ++artifact) {
            for (CKDWORD pad = 0; pad < 2; ++pad) {
                const std::shared_ptr<CKSdlGpuProgram> &program =
                    Programs.Borrow(m_NativeFFPrograms[variant][artifact][pad]);
                if (!program)
                    continue;
                CKSdlGpuFFJitPipelineRecord record = {};
                record.Program = (CKBYTE)artifact;
                record.Variant = (CKBYTE)variant;
                const CKBYTE flags = (CKBYTE)(CKSDL_GPU_FF_JIT_PIPELINE_PRECOMPILED |
                                              (pad ? CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD : 0));
                for (auto it = program->Pipelines.Begin(); it != program->Pipelines.End(); ++it) {
                    if ((*it).Drawn && RecordPipeline(it.GetKey(), vertexFormats, flags, record))
                        manifest.Pipelines.PushBack(record);
                }
            }
        }
    }
    CKSdlGpuSaveFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, manifest);
}

void CKSdlGpuRasterizerContext::ClearFFJitPrograms()
{
    const char *report = SDL_getenv("CKRE_SDL_GPU_FF_JIT_STATS");
    if (report && SDL_strcmp(report, "1") == 0 && m_FFJitStats.Requests) {
        const auto &s = m_FFJitStats;
        SDL_Log("FFJIT_STATS requests=%llu selected=%llu ready=%llu positiont=%llu unlit=%llu lit=%llu tween=%llu blend=%llu clip=%llu pad=%llu "
                "compile_queued=%llu compile_completed=%llu compile_failed=%llu vertex_failed=%llu "
                "pipeline_failed=%llu",
                (unsigned long long)s.Requests, (unsigned long long)s.PipelineSelections,
                (unsigned long long)s.PipelineReady, (unsigned long long)s.PositionTReady,
                (unsigned long long)s.UnlitReady, (unsigned long long)s.LitReady, (unsigned long long)s.TweenReady,
                (unsigned long long)s.MatrixBlendReady, (unsigned long long)s.ClipReady, (unsigned long long)s.DepthPadReady,
                (unsigned long long)s.CompileQueued,
                (unsigned long long)s.CompileCompleted, (unsigned long long)s.CompileFailed,
                (unsigned long long)s.VertexCompileFailed, (unsigned long long)s.PipelineBuildFailed);
    }
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        ReleaseFFJitProgram(m_FFJitPrograms[i]);
    }
    m_FFJitPrograms.Clear();
    m_FFJitKeys.Clear();
    m_FFJitDrawKeys.Clear();
    m_FFJitCandidates.Clear();
    m_FFJitCandidateOrder.Clear();
    m_FFJitCandidateCursor = 0;
    m_FFJitDeferredPrewarms.Clear();
    m_FFJitDeferredPrewarmNext = 0;
    // The job keeps the shaders it creates until it is deleted.
    m_FFShaderJob = nullptr;
    for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
        if (m_FFJitVertexShaders[variant])
            DestroyObject(m_FFJitVertexShaders[variant], CKRST_OBJ_SHADER);
        m_FFJitVertexShaders[variant] = 0;
    }
    for (CKDWORD clip = 0; clip < 2; ++clip) {
        if (m_FFJitDepthPadVertexShaders[clip])
            DestroyObject(m_FFJitDepthPadVertexShaders[clip], CKRST_OBJ_SHADER);
        m_FFJitDepthPadVertexShaders[clip] = 0;
    }
}

CKSdlGpuRasterizerContext::FFJitCounts
CKSdlGpuRasterizerContext::CountFFJitProgramsForTests() const
{
    FFJitCounts counts;
    counts.Candidates = m_FFJitCandidates.Size();
    counts.DrawKeys = m_FFJitDrawKeys.Size();
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitProgram &entry = m_FFJitPrograms[i];
        if (entry.State == FFJitProgram::QUEUED) {
            ++counts.Queued;
        } else if (entry.State == FFJitProgram::READY) {
            ++counts.Ready;
            const std::shared_ptr<CKSdlGpuShader> &shader = ShaderObjects.Borrow(entry.PixelShader);
            if (shader)
                counts.Samplers += shader->SamplerCount;
        } else {
            ++counts.Rejected;
        }
        for (int b = 0; b < entry.Programs.Size(); ++b) {
            const std::shared_ptr<CKSdlGpuProgram> &program =
                Programs.Borrow(entry.Programs[b].Program);
            if (!program)
                continue;
            ++counts.Programs;
            const bool positionT = program->VertexJit == CKSdlGpuProgram::POSITIONT_VERTEX;
            if (positionT) ++counts.PositionTPrograms;
            const bool unlit = program->VertexJit == CKSdlGpuProgram::UNLIT_VERTEX;
            if (unlit) ++counts.UnlitPrograms;
            const bool lit = program->VertexJit == CKSdlGpuProgram::LIT_VERTEX;
            if (lit) ++counts.LitPrograms;
            // Null entries are pipelines the worker has not created.
            for (auto pipeline = program->Pipelines.Begin();
                 pipeline != program->Pipelines.End(); ++pipeline) {
                if ((*pipeline).Pipeline) {
                    ++counts.Pipelines;
                    if (positionT) ++counts.PositionTPipelines;
                    if (unlit) ++counts.UnlitPipelines;
                    if (lit) ++counts.LitPipelines;
                }
            }
        }
    }
    counts.Shaders = m_FFWorkerShaders;
    const CKDWORD *precompiled = &m_NativeFFPrograms[0][0][0];
    for (size_t i = 0; i < sizeof(m_NativeFFPrograms) / sizeof(CKDWORD); ++i) {
        const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(precompiled[i]);
        if (!program)
            continue;
        for (auto pipeline = program->Pipelines.Begin();
             pipeline != program->Pipelines.End(); ++pipeline) {
            if ((*pipeline).Pipeline)
                ++counts.Precompiled;
        }
    }
    return counts;
}
