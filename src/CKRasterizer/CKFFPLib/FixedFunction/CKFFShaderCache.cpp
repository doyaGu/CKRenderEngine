#include "CKFFShaderCache.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderInterface.h"
#include "CKDebugLogger.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace {

CKFFShaderKeyFS CKFFProgramFragmentKey(const CKFFShaderKeyFS &key)
{
    CKFFShaderKeyFS result = key;
    // Output-target conversion is performed after the core fragment program.
    // Do not spend fragment-program cache or native pipeline entries on state
    // that does not change this program's code or uniforms.
    result.DitherEnable = false;
    result.ColorTargetFormat = CKFF_COLOR_TARGET_RGBA8;
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        result.Stages[stage].MirrorOnceMask = 0;
    return result;
}

} // namespace

CKFFShaderCache::CKFFShaderCache()
    : m_Target(), m_Shaders(), m_SamplerLayout() {
}

CKFFShaderCache::~CKFFShaderCache() {
}

bool CKFFShaderCache::Init(const CKRasterizerTargetDesc &target, const CKFFShaderSet &shaders) {
    Reset();
    m_Shaders = shaders;
    if (!ResolveShaderTarget(target)) {
        Reset();
        return false;
    }
    BuildSamplerLayout();
    return true;
}

void CKFFShaderCache::Reset() {
    for (auto &cache : m_FragmentPrograms)
        cache = FragmentProgramCache();
    m_SamplerLayout = CKFFProgramSamplerLayout();
    m_Shaders = CKFFShaderSet();
}

void CKFFShaderCache::Shutdown()
{
    Reset();
}


bool CKFFShaderCache::ResolveShaderTarget(const CKRasterizerTargetDesc &target) {
    m_Target = target;
    if (m_Target.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN ||
        m_Target.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN) {
        CK_LOG_FMT("ShaderCache", "backend reports no shader target");
        return false;
    }
    if (!m_Shaders.Matches(m_Target.ShaderFormat, m_Target.ShaderProfile)) {
        CK_LOG_FMT("ShaderCache", "Missing or incompatible rasterizer shader set: format=0x%08X profile=0x%08X",
                   m_Target.ShaderFormat, m_Target.ShaderProfile);
        return false;
    }

    return true;
}

void CKFFShaderCache::BuildSamplerLayout()
{
    m_SamplerLayout = CKFFProgramSamplerLayout();
    for (CKDWORD stage = 0; stage < CKFF_MAX_TEXTURE_STAGES; ++stage)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage =
            CKFFSamplerSlot(CKFF_SAMPLER_2D, stage,
                            CKFF_SAMPLER_LAYOUT_WIDE_2D);
    for (CKDWORD ordinal = 0; ordinal < CKFF_NARROW_SAMPLER_COUNT; ++ordinal)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage =
            CKFFSamplerSlot(CKFF_SAMPLER_CUBE, ordinal,
                            CKFF_SAMPLER_LAYOUT_WIDE_2D);
    for (CKDWORD ordinal = 0; ordinal < CKFF_NARROW_SAMPLER_COUNT; ++ordinal)
        m_SamplerLayout.Bindings[m_SamplerLayout.BindingCount++].Stage =
            CKFFSamplerSlot(CKFF_SAMPLER_VOLUME, ordinal,
                            CKFF_SAMPLER_LAYOUT_WIDE_2D);
}

CKFFProgramVariant CKFFShaderCache::ProgramVariantForKey(const CKFFShaderKey &key)
{
    const bool positionT = key.VS.GetHasPositionT();
    const bool clipDistance = key.VS.GetVertexClipping();
    if (positionT)
        return clipDistance ? CKFF_PROGRAM_POSITIONT_CLIP : CKFF_PROGRAM_POSITIONT;
    return clipDistance ? CKFF_PROGRAM_3D_CLIP : CKFF_PROGRAM_3D;
}

CKFFProgramSelection CKFFShaderCache::ResolveProgram(
    const CKFFShaderKey &key,
    const CKFFSamplerLayoutPlan &samplerLayoutPlan,
    CKFFFragmentSamplingMode samplingMode)
{
    CKFFProgramSelection selection;
    selection.Variant = ProgramVariantForKey(key);
    selection.SamplingMode = samplingMode < CKFF_FRAGMENT_SAMPLING_MODE_COUNT
        ? samplingMode : CKFF_FRAGMENT_SAMPLING_FULL_EXACT;
    selection.SamplerLayoutPlan = samplerLayoutPlan;
    const CKFFShaderKeyFS programKey = CKFFProgramFragmentKey(key.FS);
    FragmentProgramCache &cache = m_FragmentPrograms[selection.Variant];
    ++cache.Clock;
    if (cache.Clock == 0) {
        cache.Clock = 1;
        for (CKDWORD i = 0; i < cache.Count; ++i)
            cache.Entries[i].LastUse = 1;
    }

    for (CKDWORD i = 0; i < cache.Count; ++i) {
        FragmentProgramEntry &entry = cache.Entries[i];
        if (entry.Key == programKey) {
            entry.LastUse = cache.Clock;
            selection.FragmentProgram = entry.Value;
            return selection;
        }
    }

    CKDWORD entryIndex = cache.Count;
    if (cache.Count < FRAGMENT_PROGRAM_CACHE_CAPACITY) {
        ++cache.Count;
    } else {
        entryIndex = 0;
        for (CKDWORD i = 1; i < cache.Count; ++i) {
            if (cache.Entries[i].LastUse < cache.Entries[entryIndex].LastUse)
                entryIndex = i;
        }
    }

    FragmentProgramEntry &entry = cache.Entries[entryIndex];
    entry.Value = CKFFBuildFragmentProgram(programKey, samplerLayoutPlan);
    entry.Key = programKey;
    entry.LastUse = cache.Clock;
    selection.FragmentProgram = entry.Value;
    return selection;
}

CKDWORD CKFFShaderCache::GetCachedFragmentProgramCount(CKFFProgramVariant variant) const
{
    return variant < CKFF_PROGRAM_VARIANT_COUNT ? m_FragmentPrograms[variant].Count : 0;
}

CKDWORD CKFFShaderCache::GetFragmentProgramCapacity()
{
    return FRAGMENT_PROGRAM_CACHE_CAPACITY;
}

CKBOOL CKFFShaderCache::HasCachedFragmentProgram(CKFFProgramVariant variant, const CKFFShaderKeyFS &key) const
{
    if (variant >= CKFF_PROGRAM_VARIANT_COUNT)
        return FALSE;
    const FragmentProgramCache &programs = m_FragmentPrograms[variant];
    const CKFFShaderKeyFS programKey = CKFFProgramFragmentKey(key);
    for (CKDWORD i = 0; i < programs.Count; ++i)
        if (programs.Entries[i].Key == programKey)
            return TRUE;
    return FALSE;
}

const CKShaderDesc &CKFFShaderCache::GetVertexShader(
    CKFFProgramVariant variant) const
{
    return m_Shaders.Shaders[variant < CKFF_PROGRAM_VARIANT_COUNT
                                 ? variant : CKFF_PROGRAM_3D];
}

const CKShaderDesc &CKFFShaderCache::GetPixelShader() const
{
    return m_Shaders.Shaders[CKRST_SHADER_FF_FRAGMENT];
}
