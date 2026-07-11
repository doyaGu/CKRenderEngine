#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKFFShaderKey.h"
#include "CKFFConstants.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "XHashTable.h"
#include <stdint.h>

#define CKFF_MAX_PROGRAM_BINDINGS 4096

class CKRasterizerContext;

enum CKFFShaderMode {
    CKFF_SHADER_MODE_UBER_SPECIALIZED = 0,
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

typedef XHashTable<CKFFProgramBinding, CKFFShaderKey, CKFFShaderKeyXHash> CKFFProgramCacheTable;

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

    bool UsesUberShader() const { return m_UseUberShader; }
    CKFFShaderMode GetShaderMode() const {
        return m_UseUberShader ? CKFF_SHADER_MODE_UBER_SPECIALIZED : CKFF_SHADER_MODE_FULL_SPECIALIZED;
    }
    size_t CachedProgramCount() const { return (size_t)m_ModuleProgramCache.Size(); }
    size_t CachedBindingCount() const { return (size_t)m_ProgramCache.Size(); }
    size_t MaxCachedBindingCount() const { return CKFF_MAX_PROGRAM_BINDINGS; }

private:
    CKRasterizerContext *m_Context;
    CKFFUniformHandles m_Uniforms;
    CKRasterizerTargetDesc m_Target;
    const void *m_BlobSet;
    bool m_UseUberShader;
    CKFFProgramCacheTable m_ProgramCache;
    CKFFProgramModuleCacheTable m_ModuleProgramCache;

    bool CreateUniforms();
    bool ResolveShaderTarget();
    CKFFProgramBinding CreateVariantProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateUberSpecializedProgram(const CKFFShaderKey &key);
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
