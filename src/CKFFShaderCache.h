#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKFFShaderKey.h"
#include "CKFFConstants.h"
#include "CKRasterizerEnums.h"

#include <unordered_map>

class CKRasterizerContext;

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    void Init(CKRasterizerContext *ctx);
    void Shutdown();

    // Look up or create a program for the given shader key.
    // Returns the program handle (0 if unavailable).
    CKDWORD GetProgram(const CKFFShaderKey &key);

    // Get uniform handles (created once at Init)
    const CKFFUniformHandles &GetUniforms() const { return m_Uniforms; }

    // Get the fallback program (3D fixed-function stage shader)
    CKDWORD GetFallbackProgram() const { return m_FallbackProgram; }

    // Get specific programs
    CKDWORD GetLitTexturedProgram() const { return m_Stage3DProgram; }
    CKDWORD GetUnlitColorProgram() const { return m_PositionTProgram; }

private:
    CKRasterizerContext *m_Context;
    CKFFUniformHandles m_Uniforms;
    CKDWORD m_FallbackProgram;
    CKDWORD m_Stage3DProgram;
    CKDWORD m_PositionTProgram;
    CKDWORD m_NextShaderHandle;
    CKDWORD m_NextProgramHandle;
    CKDWORD m_NextUniformHandle;

    std::unordered_map<CKFFShaderKey, CKDWORD> m_ProgramCache;

    void CreateUniforms();
    void CreatePrograms();
    CKDWORD CreateProgramFromBinary(
        const unsigned char *vsData, unsigned int vsSize,
        const unsigned char *fsData, unsigned int fsSize);

    CKDWORD AllocShaderHandle() { return m_NextShaderHandle++; }
    CKDWORD AllocProgramHandle() { return m_NextProgramHandle++; }
    CKDWORD AllocUniformHandle() { return m_NextUniformHandle++; }
};

#endif // CKFFSHADERCACHE_H
