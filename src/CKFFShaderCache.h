#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKFFShaderKey.h"
#include "CKFFConstants.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"

#include <unordered_map>

class CKRasterizerContext;

enum CKFFShaderMode {
    CKFF_SHADER_MODE_UBER_SPECIALIZED = 0,
    CKFF_SHADER_MODE_FULL_SPECIALIZED = 1
};

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Select the fixed-function program for the given FFP shader key.
    // Returns the program handle (0 if unavailable).
    CKDWORD GetProgram(const CKFFShaderKey &key);

    // Get uniform handles (created once at Init)
    const CKFFUniformHandles &GetUniforms() const { return m_Uniforms; }

    bool UsesUberShader() const { return m_UseUberShader; }
    CKFFShaderMode GetShaderMode() const {
        return m_UseUberShader ? CKFF_SHADER_MODE_UBER_SPECIALIZED : CKFF_SHADER_MODE_FULL_SPECIALIZED;
    }
    size_t CachedProgramCount() const { return m_ProgramCache.size(); }

private:
    CKRasterizerContext *m_Context;
    CKFFUniformHandles m_Uniforms;
    CKShaderTargetDesc m_Target;
    const void *m_BlobSet;
    bool m_UseUberShader;
    std::unordered_map<CKFFShaderKey, CKDWORD, CKFFShaderKeyHash> m_ProgramCache;
    CKDWORD m_NextShaderHandle;
    CKDWORD m_NextProgramHandle;
    CKDWORD m_NextUniformHandle;

    void CreateUniforms();
    void ResolveShaderTarget();
    CKDWORD CreateVariantProgram(const CKFFShaderKey &key);
    CKDWORD CreateUberSpecializedProgram(const CKFFShaderKey &key);
    CKDWORD CreateFullSpecializedProgram(const CKFFShaderKey &key);
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
