#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKFFShaderKey.h"
#include "CKFFConstants.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "XArray.h"
#include "XHashTable.h"
#include <stdint.h>

#define CKFF_MAX_PROGRAM_BINDINGS 4096
#define CKFF_MAX_PROGRAM_SAMPLER_BINDINGS (CKFF_MAX_TEXTURE_STAGES * 2)

class CKRasterizerContext;

enum CKFFShaderMode {
    CKFF_SHADER_MODE_RUNTIME_SPECIALIZED = 0,
    CKFF_SHADER_MODE_FULL_SPECIALIZED = 1
};

struct CKFFProgramBinding {
    CKDWORD Program;
    bool FullSpecialized;
    CKFFSpecializationInfo Specialization;

    CKFFProgramBinding() : Program(0), FullSpecialized(false), Specialization() {}
    CKFFProgramBinding(CKDWORD program, bool fullSpecialized, const CKFFSpecializationInfo &specialization)
        : Program(program), FullSpecialized(fullSpecialized), Specialization(specialization) {}

    operator CKDWORD() const { return Program; }
};

struct CKFFProgramContext {
    CKFFShaderKey ShaderKey;
    CKFFProgramBinding Binding;
    CKDWORD Program;
    CKBOOL FullSpecialized;
    CKFFSpecializationInfo Specialization;

    CKFFProgramContext()
        : ShaderKey(), Binding(), Program(0), FullSpecialized(FALSE), Specialization() {}
};

struct CKFFProgramSamplerBinding {
    CKDWORD Stage;
    CKDWORD Uniform;

    CKFFProgramSamplerBinding() : Stage(0), Uniform(0) {}
};

struct CKFFProgramSamplerLayout {
    CKDWORD BindingCount;
    CKFFProgramSamplerBinding Bindings[CKFF_MAX_PROGRAM_SAMPLER_BINDINGS];

    CKFFProgramSamplerLayout() : BindingCount(0), Bindings() {}
};

struct CKFFShaderCacheStats {
    uint64_t BindingHits;
    uint64_t BindingMisses;
    uint64_t BindingEvictions;

    CKFFShaderCacheStats()
        : BindingHits(0), BindingMisses(0), BindingEvictions(0) {}
};

void CKFFInitProgramContext(CKFFProgramContext *context,
                            const CKFFShaderKey &key,
                            const CKFFProgramBinding &binding);
CKBOOL CKFFCanUseInstancedProgramForPacket(const CKFFProgramContext &normalContext,
                                           const CKFFProgramContext &instancedContext);

struct CKFFShaderKeyXHash {
    int operator()(const CKFFShaderKey &key) const {
        CKFFShaderKeyHash hash;
        return (int)hash(key);
    }
};

struct CKFFProgramModuleKey {
    CK_SHADER_PROFILE Profile;
    const unsigned char *VSData;
    unsigned int VSSize;
    const unsigned char *FSData;
    unsigned int FSSize;

    bool operator==(const CKFFProgramModuleKey &other) const {
        return Profile == other.Profile &&
               VSData == other.VSData && VSSize == other.VSSize &&
               FSData == other.FSData && FSSize == other.FSSize;
    }
};

struct CKFFProgramModuleKeyXHash {
    int operator()(const CKFFProgramModuleKey &key) const {
        uint64_t vs = (uint64_t)reinterpret_cast<uintptr_t>(key.VSData);
        uint64_t fs = (uint64_t)reinterpret_cast<uintptr_t>(key.FSData);
        uint32_t hash = 2166136261u;
        hash = (hash ^ key.Profile) * 16777619u;
        hash = (hash ^ (uint32_t)vs) * 16777619u;
        hash = (hash ^ (uint32_t)(vs >> 32)) * 16777619u;
        hash = (hash ^ key.VSSize) * 16777619u;
        hash = (hash ^ (uint32_t)fs) * 16777619u;
        hash = (hash ^ (uint32_t)(fs >> 32)) * 16777619u;
        hash = (hash ^ key.FSSize) * 16777619u;
        return (int)hash;
    }
};

typedef XHashTable<CKDWORD, CKFFProgramModuleKey, CKFFProgramModuleKeyXHash>
    CKFFProgramModuleCacheTable;

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    bool Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Select the fixed-function program for the given FFP shader key.
    // Returns the program handle (0 if unavailable).
    CKFFProgramBinding GetProgram(const CKFFShaderKey &key);
    CKBOOL SupportsSamplerLayout(const CKFFShaderKey &key) const;
    CKBOOL RequiresExplicitSamplerInitialization() const {
        return m_Target.ShaderProfile == CKRST_SHADER_PROFILE_GLSL ||
               m_Target.ShaderProfile == CKRST_SHADER_PROFILE_ESSL;
    }
    CKBOOL GetProgramSamplerLayout(
        CKDWORD program, CKFFProgramSamplerLayout *layout) const;

    // Get uniform handles (created once at Init)
    const CKFFUniformHandles &GetUniforms() const { return m_Uniforms; }
    CKDWORD GetTargetFlags() const {
        CKDWORD flags = 0;
        if (m_Target.HomogeneousDepth)
            flags |= CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE;
        if (m_Target.OriginBottomLeft)
            flags |= CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT;
        return flags;
    }

    bool UsesRuntimeSpecializedShader() const {
        return m_ShaderMode == CKFF_SHADER_MODE_RUNTIME_SPECIALIZED;
    }
    CKFFShaderMode GetShaderMode() const { return m_ShaderMode; }
    size_t CachedProgramCount() const { return (size_t)m_ModuleProgramCache.Size(); }
    size_t CachedBindingCount() const { return (size_t)m_ProgramCache.Size(); }
    size_t MaxCachedBindingCount() const { return CKFF_MAX_PROGRAM_BINDINGS; }
    const CKFFShaderCacheStats &GetCacheStats() const { return m_CacheStats; }

private:
    struct CKFFProgramBindingCacheEntry {
        CKFFProgramBinding Binding;
        bool RecentlyUsed;

        CKFFProgramBindingCacheEntry()
            : Binding(), RecentlyUsed(false) {}
        explicit CKFFProgramBindingCacheEntry(const CKFFProgramBinding &binding)
            : Binding(binding), RecentlyUsed(false) {}
    };

    typedef XHashTable<CKFFProgramBindingCacheEntry, CKFFShaderKey, CKFFShaderKeyXHash>
        CKFFProgramCacheTable;

    CKRasterizerContext *m_Context;
    CKFFUniformHandles m_Uniforms;
    CKRasterizerTargetDesc m_Target;
    const void *m_BlobSet;
    CKFFShaderMode m_ShaderMode;
    bool m_PrewarmPrograms;
    CKFFProgramCacheTable m_ProgramCache;
    XArray<CKFFShaderKey> m_ProgramBindingClock;
    int m_ProgramBindingClockHand;
    CKFFProgramModuleCacheTable m_ModuleProgramCache;
    XHashTable<CKFFProgramSamplerLayout, CKDWORD> m_ProgramSamplerLayouts;
    CKFFShaderCacheStats m_CacheStats;

    bool CreateUniforms();
    bool ResolveShaderTarget();
    void PrewarmPrograms();
    void CacheProgramBinding(const CKFFShaderKey &key,
                             const CKFFProgramBinding &binding);
    void CacheProgramSamplerLayout(const CKFFShaderKey &key,
                                   const CKFFProgramBinding &binding);
    CKFFProgramBinding CreateVariantProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateRuntimeSpecializedProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateFullSpecializedProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateVolumeSamplerLayoutProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateStaticSamplerLayoutProgram(const CKFFShaderKey &key);
    CKDWORD CreateProgramFromBinary(
        const CKRasterizerTargetDesc &target,
        const unsigned char *vsData, unsigned int vsSize,
        const unsigned char *fsData, unsigned int fsSize,
        const CKFFSpecializationInfo &specInfo);

};

#endif // CKFFSHADERCACHE_H
