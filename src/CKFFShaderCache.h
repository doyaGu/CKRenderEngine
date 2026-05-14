#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKFFShaderKey.h"
#include "CKFFConstants.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"
#include "XHashTable.h"

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

struct CKFFShaderKeyXHash {
    int operator()(const CKFFShaderKey &key) const {
        CKFFShaderKeyHash hash;
        return (int)hash(key);
    }
};

typedef XHashTable<CKFFProgramBinding, CKFFShaderKey, CKFFShaderKeyXHash> CKFFProgramCacheTable;

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Select the fixed-function program for the given FFP shader key.
    // Returns the program handle (0 if unavailable).
    CKFFProgramBinding GetProgram(const CKFFShaderKey &key);

    // Get uniform handles (created once at Init)
    const CKFFUniformHandles &GetUniforms() const { return m_Uniforms; }

    bool UsesUberShader() const { return m_UseUberShader; }
    CKFFShaderMode GetShaderMode() const {
        return m_UseUberShader ? CKFF_SHADER_MODE_UBER_SPECIALIZED : CKFF_SHADER_MODE_FULL_SPECIALIZED;
    }
    size_t CachedProgramCount() const { return (size_t)m_ProgramCache.Size(); }

private:
    CKRasterizerContext *m_Context;
    CKFFUniformHandles m_Uniforms;
    CKShaderTargetDesc m_Target;
    const void *m_BlobSet;
    bool m_UseUberShader;
    CKFFProgramCacheTable m_ProgramCache;
    CKDWORD m_NextShaderHandle;
    CKDWORD m_NextProgramHandle;
    CKDWORD m_NextUniformHandle;

    void CreateUniforms();
    void ResolveShaderTarget();
    CKFFProgramBinding CreateVariantProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateUberSpecializedProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateFullSpecializedProgram(const CKFFShaderKey &key);
    CKFFProgramBinding CreateVolumeSamplerLayoutProgram(const CKFFShaderKey &key);
    CKDWORD CreateProgramFromBinary(
        const CKShaderTargetDesc &target,
        const unsigned char *vsData, unsigned int vsSize,
        const unsigned char *fsData, unsigned int fsSize,
        const CKFFSpecializationInfo &specInfo);

    CKDWORD AllocShaderHandle() { return m_NextShaderHandle++; }
    CKDWORD AllocProgramHandle() { return m_NextProgramHandle++; }
    CKDWORD AllocUniformHandle() { return m_NextUniformHandle++; }
};

#endif // CKFFSHADERCACHE_H
