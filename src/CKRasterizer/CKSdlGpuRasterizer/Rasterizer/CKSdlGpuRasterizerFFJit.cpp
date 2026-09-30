#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaders.h"
#include "CKFFNativeFragmentJit.h"
#include "CKJitDxbc.h"
#include "CKJitSpirv.h"
#include "CKRenderProfile.h"

#include <cstring>

// CKSdlGpuRasterizerContext runtime compilation of fixed-function fragment
// programs. A native artifact decodes the fragment program and the draw state
// per fragment; the compiled program has both constant and keeps the
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
bool PrewarmablePipeline(SDL_GPUDevice *device, const CKSdlGpuFFJitRecord &record)
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

// Compiles one fragment program on the worker.
class CKSdlGpuRasterizerContext::FFJitJob : public CKSdlGpuJob {
public:
    FFJitJob(CKSdlGpuRasterizerContext &context, const FFJitKey &key,
             const CKFFNativeFragmentKey &fragment, CKFFSamplerLayout layout,
             SDL_GPUShaderFormat format)
        : Context(context), Key(key), Fragment(fragment), Layout(layout), Format(format) {}

    void Run() override {
        CKRE_PROFILE_SCOPE("CKRE.SDL.FFJitCompile");
        CKJitFragmentShader shader;
        Compiled = CKFFCompileNativeFragmentProgram(Fragment, Layout, shader) &&
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
    CKFFNativeFragmentKey Fragment;
    CKFFSamplerLayout Layout;
    SDL_GPUShaderFormat Format;
    XArray<uint32_t> Code;
    bool Compiled = false;
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
    XArray<CKSdlGpuFFJitRecord> records;
    if (m_FFJitManifest.Length() == 0 ||
        !CKSdlGpuLoadFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, records))
        return;
    CKDWORD loaded = 0;
    for (int i = 0; i < records.Size(); ++i) {
        const CKSdlGpuFFJitRecord &record = records[i];
        const CKFFSamplerLayout layout = (CKFFSamplerLayout)record.SamplerLayout;
        CKFFNativeFragmentKey fragment;
        fragment.Program.SetLanes(record.Lanes, CKFF_FRAGMENT_PROGRAM_LANE_COUNT);
        std::memcpy(fragment.Switches, record.Switches, sizeof(fragment.Switches));
        // Records hold canonical keys; any other key would stay unused.
        CKFFCanonicalizeNativeFragmentKey(fragment, layout);
        const FFJitKey key = MakeFFJitKey(fragment, layout);
        const int *found = m_FFJitKeys.FindPtr(key);
        int index = found ? *found : -1;
        if (!found) {
            if (loaded == kFFJitLoadedProgramLimit)
                continue;
            FFJitJob *job = new FFJitJob(*this, key, fragment, layout, m_FFJitFormat);
            if (!SubmitJob(job, CKSDLGPU_JOB_IDLE))
                return;
            index = AddFFJitProgram(key, kFFJitLoadedRank | loaded++);
            m_FFJitPrograms[index].IdleJob = job;
        }
        m_FFJitPrograms[index].Prewarm.PushBack(record);
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
        if (!SubmitJob(new FFJitJob(*this, key, Fragment, Layout, m_FFJitFormat)))
            m_FFJitPrograms[index].State = FFJitProgram::REJECTED;
        CKRE_PROFILE_VALUE("CKRE.SDL.FFJitCompiles", 1);
    }
    m_FFJitDrawKeys.Insert(DrawKey, index, FALSE);
    return index;
}

CKDWORD CKSdlGpuRasterizerContext::ResolveFFJitProgram(
    const CKFFFragmentProgram &FragmentProgram,
    const CKFFConstantSet *Constants,
    CKFFSamplerLayout Layout,
    CKFFProgramVariant Variant,
    CKDWORD Precompiled)
{
    if (m_FFJitFormat == SDL_GPU_SHADERFORMAT_INVALID || !Constants)
        return Precompiled;
    // A draw computes its key, canonicalizing it only when it is new.
    const CKFFNativeFragmentKey fragment =
        CKFFNativeFragmentDrawKey(FragmentProgram, *Constants, false);
    const FFJitKey drawKey = MakeFFJitKey(fragment, Layout);
    const int *found = m_FFJitDrawKeys.FindPtr(drawKey);
    const int index = found ? *found : AddFFJitDrawKey(drawKey, fragment, Layout);
    if (index < 0)
        return Precompiled;
    FFJitProgram &entry = m_FFJitPrograms[index];
    if (entry.Rank & kFFJitLoadedRank)
        entry.Rank = ++m_FFJitUses;
    if (entry.State != FFJitProgram::READY) {
        // A draw now waits for a program loaded at idle priority.
        if (entry.IdleJob) {
            Worker.Promote(entry.IdleJob);
            entry.IdleJob = nullptr;
        }
        return Precompiled;
    }
    if (!entry.Programs[Variant]) {
        entry.Programs[Variant] =
            CreateFFJitProgram(entry.PixelShader, Variant, Precompiled);
        if (!entry.Programs[Variant]) {
            entry.State = FFJitProgram::REJECTED;
            return Precompiled;
        }
    }
    return entry.Programs[Variant];
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
    const int *index = m_FFJitKeys.FindPtr(Key);
    if (!index || m_FFJitPrograms[*index].State != FFJitProgram::QUEUED)
        return;
    FFJitProgram &entry = m_FFJitPrograms[*index];
    entry.IdleJob = nullptr;
    entry.State = FFJitProgram::REJECTED;
    if (!Code) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                     "FF fragment program compilation failed; drawing it precompiled");
        return;
    }
    // The compiled shader replaces the artifact of its layout.
    CKSdlGpuFFFragmentArtifactKey artifact;
    artifact.SamplerLayout = (CKFFSamplerLayout)Key.Values[FF_JIT_KEY_LAYOUT];
    CKShaderDesc desc;
    if (!CKSdlGpuFFFragmentShader(ShaderFormat, artifact, desc))
        return;
    const bool dxbc = m_FFJitFormat == SDL_GPU_SHADERFORMAT_DXBC;
    desc.Format = dxbc ? CKRST_SHADER_FORMAT_DXBC : CKRST_SHADER_FORMAT_SPIRV;
    desc.Profile = dxbc ? CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
    desc.Code = reinterpret_cast<const CKBYTE *>(Code->Begin());
    desc.CodeSize = (CKDWORD)(Code->Size() * sizeof(uint32_t));
    if (CreateShader(&desc, &entry.PixelShader) != CK_OK)
        return;
    entry.State = FFJitProgram::READY;
    PrewarmFFJitProgram(entry);
}

void CKSdlGpuRasterizerContext::PrewarmFFJitProgram(FFJitProgram &Entry)
{
    for (int i = 0; i < Entry.Prewarm.Size(); ++i) {
        const CKSdlGpuFFJitRecord &record = Entry.Prewarm[i];
        if (!PrewarmablePipeline(Device, record))
            continue;
        const CKFFProgramVariant variant = (CKFFProgramVariant)record.Variant;
        if (!Entry.Programs[variant]) {
            CKSdlGpuFFFragmentArtifactKey artifact;
            artifact.SamplerLayout = (CKFFSamplerLayout)record.SamplerLayout;
            const CKDWORD precompiled = NativeFFProgram(variant, artifact, FALSE);
            Entry.Programs[variant] = precompiled
                ? CreateFFJitProgram(Entry.PixelShader, variant, precompiled) : 0;
            // As when a draw creates it, the program is then drawn precompiled.
            if (!Entry.Programs[variant]) {
                Entry.State = FFJitProgram::REJECTED;
                break;
            }
        }
        const CKDWORD layout = GetNativeVertexLayout(record.VertexFormat);
        const std::shared_ptr<CKSdlGpuProgram> &program = Programs.Borrow(Entry.Programs[variant]);
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
        draw.State.DepthClipEnabled = (CKBOOL)record.DepthClipEnabled;
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
    XArray<CKSdlGpuFFJitRecord> records;
    for (int i = 0; i < ranked.Size() &&
                    records.Size() < CKSDL_GPU_FF_JIT_MANIFEST_MAX_RECORDS; ++i) {
        const FFJitProgram &entry = *ranked[i];
        // A loaded program not compiled yet keeps the records it came with.
        for (int r = 0; r < entry.Prewarm.Size(); ++r)
            records.PushBack(entry.Prewarm[r]);
        CKSdlGpuFFJitRecord record = {};
        std::memcpy(record.Lanes, entry.Key.Values, sizeof(record.Lanes));
        std::memcpy(record.Switches, entry.Key.Values + CKFF_FRAGMENT_PROGRAM_LANE_COUNT,
                    sizeof(record.Switches));
        record.SamplerLayout = entry.Key.Values[FF_JIT_KEY_LAYOUT];
        for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            const std::shared_ptr<CKSdlGpuProgram> &program =
                Programs.Borrow(entry.Programs[variant]);
            if (!program)
                continue;
            record.Variant = variant;
            // Pending and failed pipelines are recorded too.
            for (auto it = program->Pipelines.Begin(); it != program->Pipelines.End(); ++it) {
                const CKDWORD *key = it.GetKey().Values;
                const CKDWORD *vertexFormat = vertexFormats.FindPtr(key[0]);
                if (!vertexFormat || key[1] != 0)
                    continue;
                record.VertexFormat = *vertexFormat;
                record.ColorFormat = key[2];
                record.DepthFormat = key[3];
                record.SampleCount = key[4];
                record.StateLo = key[5];
                record.StateMid = key[6];
                record.StateHi = key[7];
                record.StencilReadMask = key[8];
                record.StencilWriteMask = key[9];
                record.DepthClipEnabled = key[10];
                records.PushBack(record);
            }
        }
    }
    CKSdlGpuSaveFFJitManifest(m_FFJitManifest.CStr(), m_FFJitIdentity, records);
}

void CKSdlGpuRasterizerContext::ClearFFJitPrograms()
{
    for (int i = 0; i < m_FFJitPrograms.Size(); ++i) {
        const FFJitProgram &entry = m_FFJitPrograms[i];
        for (CKDWORD variant = 0; variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            if (entry.Programs[variant])
                DestroyObject(entry.Programs[variant], CKRST_OBJ_PROGRAM);
        }
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
