#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaders.h"
#include "CKFFNativeFragmentJit.h"
#include "CKJitDxbc.h"
#include "CKJitSpirv.h"
#include "CKRenderProfile.h"

#include <cstring>

// CKSdlGpuRasterizerContext runtime compilation of fixed-function fragment
// programs. A precompiled artifact decodes the fragment program and the draw
// state per fragment; the compiled program has both constant and keeps the
// artifact's interface, so it replaces the precompiled program in the same
// draws and falls back to it for every pipeline the worker has not created
// yet. The manifest of an earlier run queues its programs, then their
// pipelines, at idle priority.

namespace {
// Bounds the shaders and programs one device keeps. Later fragment programs
// draw with the precompiled artifacts.
const int kFFJitProgramLimit = 256;
// A manifest loads half of them, so every run can compile its own.
const CKDWORD kFFJitLoadedProgramLimit = kFFJitProgramLimit / 2;
// Ranks the loaded programs no draw has used after the used ones.
const CKDWORD kFFJitLoadedRank = 0x80000000u;
// Bounds the draw keys remembered. Draw keys only name entries, so a full
// table starts over.
const int kFFJitDrawKeyLimit = 4 * kFFJitProgramLimit;
// SDL_gpu places fragment samplers in space (set) 2, uniform buffers in 3.
const CKJitResourceLayout kFFJitResources = {3, 0, 2};

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
}

// Compiles one fragment program and creates its shader on the worker.
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
    }
    ~FFJitJob() override { Shader->Job = nullptr; }

    void Run() override {
        CKRE_PROFILE_SCOPE("CKRE.SDL.FFJitCompile");
        if (!Described)
            return;
        CKJitFragmentShader program;
        XArray<uint32_t> code;
        if (!CKFFCompileNativeFragmentProgram(Fragment, Layout, program) ||
            !(Format == SDL_GPU_SHADERFORMAT_DXBC
                  ? CKJitEmitDxbc(program, kFFJitResources, code)
                  : CKJitEmitSpirv(program, kFFJitResources, code))) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "FF fragment program compilation failed; drawing it precompiled");
            return;
        }
        SDL_GPUShaderCreateInfo info = CKSdlGpuShaderInfo(Shader->Desc, Format);
        info.code = reinterpret_cast<const Uint8 *>(code.Begin());
        info.code_size = code.Size() * sizeof(uint32_t);
        Shader->Shader = CKSdlGpuOwn(Device, SDL_CreateGPUShader(Device, &info), SDL_ReleaseGPUShader);
        if (!Shader->Shader)
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "SDL_gpu CreateGPUShader failed: %s; drawing the FF fragment program precompiled",
                         SDL_GetError());
    }
    void Complete() override {
        Context.CompleteFFJitProgram(Key, Shader);
    }

    // Programs take the shader before it is created.
    std::shared_ptr<CKSdlGpuShader> Shader;

private:
    CKSdlGpuRasterizerContext &Context;
    FFJitKey Key;
    CKFFNativeFragmentKey Fragment;
    CKFFSamplerLayout Layout;
    SDL_GPUDevice *Device;
    SDL_GPUShaderFormat Format;
    bool Described = false;
};

void CKSdlGpuRasterizerContext::InitFFJit()
{
    m_FFJitFormat = SDL_GPU_SHADERFORMAT_INVALID;
    m_FFJitManifest = "";
    m_FFJitIdentity = 0;
    m_FFJitUses = 0;
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
        !CKSdlGpuLoadFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, manifest))
        return;
    // The entry of each program record, -1 past the limit.
    XArray<int> entries;
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
        if (!found && loaded < kFFJitLoadedProgramLimit) {
            index = AddFFJitProgram(key, kFFJitLoadedRank | loaded++);
            if (!SubmitFFJitProgram(m_FFJitPrograms[index], fragment, layout, CKSDLGPU_JOB_IDLE))
                return;
        }
        entries.PushBack(index);
    }
    for (int i = 0; i < manifest.Pipelines.Size(); ++i) {
        const CKSdlGpuFFJitPipelineRecord &record = manifest.Pipelines[i];
        if (entries[record.Program] >= 0)
            m_FFJitPrograms[entries[record.Program]].Prewarm.PushBack(record);
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
    m_FFJitKeys.Insert(Key, index, FALSE);
    return index;
}

bool CKSdlGpuRasterizerContext::SubmitFFJitProgram(
    FFJitProgram &Entry,
    const CKFFNativeFragmentKey &Fragment,
    CKFFSamplerLayout Layout,
    CKSdlGpuJobPriority Priority)
{
    FFJitJob *job = new FFJitJob(*this, Entry.Key, Fragment, Layout);
    const std::shared_ptr<CKSdlGpuShader> shader = job->Shader;
    if (SubmitJob(job, Priority))
        Entry.PixelShader = ShaderObjects.Add(shader);
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
    if (!found && m_FFJitPrograms.Size() < kFFJitProgramLimit) {
        // Never compile while drawing. A program the worker cannot take
        // keeps its precompiled shader.
        index = AddFFJitProgram(key, ++m_FFJitUses);
        SubmitFFJitProgram(m_FFJitPrograms[index], Fragment, Layout, CKSDLGPU_JOB_NORMAL);
        CKRE_PROFILE_VALUE("CKRE.SDL.FFJitCompiles", 1);
    }
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
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_INVALID || !Constants)
        return Precompiled;
    // A draw computes its key, canonicalizing it only when it is new.
    const CKFFNativeFragmentKey fragment = CKFFNativeFragmentDrawKey(
        FragmentProgram, *Constants, Artifact.UsesShaderSampling != FALSE,
        Artifact.ComparisonResourceCount);
    const FFJitKey drawKey = MakeFFJitKey(fragment, Artifact.SamplerLayout);
    const int *found = m_FFJitDrawKeys.FindPtr(drawKey);
    const int index = found ? *found : AddFFJitDrawKey(drawKey, fragment, Artifact.SamplerLayout);
    if (index < 0)
        return Precompiled;
    FFJitProgram &entry = m_FFJitPrograms[index];
    if (entry.Rank & kFFJitLoadedRank)
        entry.Rank = ++m_FFJitUses;
    if (entry.State == FFJitProgram::REJECTED)
        return Precompiled;
    // A draw now waits for a program loaded at idle priority.
    if (entry.IdleJob) {
        Worker.Promote(entry.IdleJob);
        entry.IdleJob = nullptr;
    }
    const CKDWORD program = BindFFJitProgram(entry, Variant, Precompiled);
    return program ? program : Precompiled;
}

CKDWORD CKSdlGpuRasterizerContext::BindFFJitProgram(
    FFJitProgram &Entry,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled)
{
    for (int i = 0; i < Entry.Programs.Size(); ++i) {
        if (Entry.Programs[i].Precompiled == Precompiled)
            return Entry.Programs[i].Program;
    }
    const FFJitProgram::Binding binding = {
        Precompiled, Variant, CreateFFJitProgram(Entry.PixelShader, Variant, Precompiled)};
    // The entry's draws are then drawn precompiled.
    if (!binding.Program) {
        Entry.State = FFJitProgram::REJECTED;
        return 0;
    }
    Entry.Programs.PushBack(binding);
    return binding.Program;
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
        // The DXBC shader of the variant, padding depth if the fallback's does.
        const CKDWORD clip = Variant == CKFF_PROGRAM_POSITIONT_CLIP ? 1u : 0u;
        const bool pad = desc.VertexShader != 0 &&
            desc.VertexShader == m_NativeFFDepthPadVertexShaders[clip];
        CKDWORD &vertexShader =
            pad ? m_FFJitDepthPadVertexShaders[clip] : m_FFJitVertexShaders[Variant];
        CKShaderDesc vertexDesc;
        if (!vertexShader &&
            (!(pad ? CKSdlGpuFFDepthPadVertexShader(m_FFJitFormat, clip != 0, vertexDesc)
                   : CKSdlGpuFFDxbcVertexShader(Variant, vertexDesc)) ||
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
        // As when a draw creates it, the program is then drawn precompiled.
        const CKDWORD handle = precompiled ? BindFFJitProgram(Entry, variant, precompiled) : 0;
        if (!handle) {
            Entry.State = FFJitProgram::REJECTED;
            break;
        }
        const CKDWORD layout = GetNativeVertexLayout(record.VertexFormat);
        const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(handle);
        const std::shared_ptr<CKSdlGpuLayout> &vertexLayout = Layouts.Borrow(layout);
        if (!program || !vertexLayout)
            continue;
        // The fields a pipeline depends on, as the record's draw had them.
        CKSdlGpuDraw draw;
        draw.State.State.Lo = record.StateLo;
        draw.State.State.Mid = record.StateMid;
        draw.State.State.Hi = record.StateHi;
        draw.State.StencilReadMask = record.StencilReadMask;
        draw.State.StencilWriteMask = record.StencilWriteMask;
        draw.State.DepthClipEnabled =
            (record.Flags & CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP) ? TRUE : FALSE;
        draw.Program = program.get();
        draw.Layout = vertexLayout.get();
        draw.LayoutHandle = layout;
        QueuePipeline(draw, (SDL_GPUTextureFormat)record.ColorFormat,
                      (SDL_GPUTextureFormat)record.DepthFormat,
                      (SDL_GPUSampleCount)record.SampleCount, CKSDLGPU_JOB_IDLE);
    }
    Entry.Prewarm.Clear();
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
              [](const FFJitProgram *a, const FFJitProgram *b) { return a->Rank < b->Rank; });
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
            // Pending and failed pipelines are recorded too.
            for (auto it = program->Pipelines.Begin(); it != program->Pipelines.End(); ++it) {
                const CKDWORD *key = it.GetKey().Values;
                const CKDWORD *vertexFormat = vertexFormats.FindPtr(key[0]);
                if (!vertexFormat || key[1] != 0)
                    continue;
                record.Flags = (CKBYTE)((pad ? CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_PAD : 0) |
                                        (key[10] ? CKSDL_GPU_FF_JIT_PIPELINE_DEPTH_CLIP : 0));
                record.ColorFormat = (CKBYTE)key[2];
                record.DepthFormat = (CKBYTE)key[3];
                record.SampleCount = (CKBYTE)key[4];
                record.StencilReadMask = (CKBYTE)key[8];
                record.StencilWriteMask = (CKBYTE)key[9];
                record.VertexFormat = *vertexFormat;
                record.StateLo = key[5];
                record.StateMid = key[6];
                record.StateHi = key[7];
                manifest.Pipelines.PushBack(record);
            }
        }
    }
    CKSdlGpuSaveFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, manifest);
}

void CKSdlGpuRasterizerContext::ClearFFJitPrograms()
{
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitProgram &entry = m_FFJitPrograms[i];
        for (int b = 0; b < entry.Programs.Size(); ++b)
            DestroyObject(entry.Programs[b].Program, CKRST_OBJ_PROGRAM);
        if (entry.PixelShader)
            DestroyObject(entry.PixelShader, CKRST_OBJ_SHADER);
    }
    m_FFJitPrograms.Clear();
    m_FFJitKeys.Clear();
    m_FFJitDrawKeys.Clear();
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
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitProgram &entry = m_FFJitPrograms[i];
        if (entry.State == FFJitProgram::QUEUED)
            ++counts.Queued;
        else if (entry.State == FFJitProgram::READY)
            ++counts.Ready;
        else
            ++counts.Rejected;
        for (int b = 0; b < entry.Programs.Size(); ++b) {
            const std::shared_ptr<CKSdlGpuProgram> &program =
                Programs.Borrow(entry.Programs[b].Program);
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
