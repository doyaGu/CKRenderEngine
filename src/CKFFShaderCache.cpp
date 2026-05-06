#include "CKFFShaderCache.h"
#include "CKRasterizer.h"
#include "CKDebugLogger.h"

#include <cstdio>

#include "shaders/generated/vs_ff_3d.bin.h"
#include "shaders/generated/vs_ff_positiont.bin.h"
#include "shaders/generated/fs_ff_stage.bin.h"

CKFFShaderCache::CKFFShaderCache()
    : m_Context(nullptr), m_FallbackProgram(0), m_Stage3DProgram(0), m_PositionTProgram(0),
      m_NextShaderHandle(100), m_NextProgramHandle(200), m_NextUniformHandle(300) {}

CKFFShaderCache::~CKFFShaderCache() {
    Shutdown();
}

void CKFFShaderCache::Init(CKRasterizerContext *ctx) {
    m_Context = ctx;
    CreateUniforms();
    CreatePrograms();
    m_FallbackProgram = m_Stage3DProgram;
}

void CKFFShaderCache::Shutdown() {
    if (m_Context) {
        for (auto &pair : m_ProgramCache) {
            m_Context->DeleteObject(pair.second, CKRST_OBJ_PROGRAM);
        }
        if (m_Stage3DProgram) {
            m_Context->DeleteObject(m_Stage3DProgram, CKRST_OBJ_PROGRAM);
            m_Stage3DProgram = 0;
        }
        if (m_PositionTProgram) {
            m_Context->DeleteObject(m_PositionTProgram, CKRST_OBJ_PROGRAM);
            m_PositionTProgram = 0;
        }
        m_FallbackProgram = 0;
    }
    m_ProgramCache.clear();
    m_Context = nullptr;
}

void CKFFShaderCache::CreateUniforms() {
    if (!m_Context) return;

    CKUniformDesc desc;

    desc.Type = CKRST_UNIFORM_MATRIX4;
    desc.Name = (char *)"u_ckModelViewProj";
    desc.Count = 1;
    m_Uniforms.u_ckModelViewProj = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckModelViewProj, &desc);

    desc.Name = (char *)"u_ckModelView";
    desc.Count = 1;
    m_Uniforms.u_ckModelView = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckModelView, &desc);

    desc.Name = (char *)"u_ckNormalMatrix";
    desc.Count = 1;
    m_Uniforms.u_ckNormalMatrix = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ckNormalMatrix, &desc);

    desc.Type = CKRST_UNIFORM_FLOAT4;
    desc.Name = (char *)"u_lights";
    desc.Count = CKFF_MAX_LIGHTS * 7;
    m_Uniforms.u_lights = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lights, &desc);

    desc.Name = (char *)"u_lightParams";
    desc.Count = 1;
    m_Uniforms.u_lightParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lightParams, &desc);

    desc.Name = (char *)"u_material";
    desc.Count = 5;
    m_Uniforms.u_material = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_material, &desc);

    desc.Name = (char *)"u_ffParams";
    desc.Count = 1;
    m_Uniforms.u_ffParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_ffParams, &desc);

    desc.Name = (char *)"u_lightModelParams";
    desc.Count = 1;
    m_Uniforms.u_lightModelParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_lightModelParams, &desc);

    desc.Name = (char *)"u_fogParams";
    desc.Count = 1;
    m_Uniforms.u_fogParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_fogParams, &desc);

    desc.Name = (char *)"u_fogColor";
    desc.Count = 1;
    m_Uniforms.u_fogColor = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_fogColor, &desc);

    desc.Name = (char *)"u_texFactor";
    desc.Count = 1;
    m_Uniforms.u_texFactor = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_texFactor, &desc);

    desc.Name = (char *)"u_alphaParams";
    desc.Count = 1;
    m_Uniforms.u_alphaParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_alphaParams, &desc);

    desc.Name = (char *)"u_bumpEnv";
    desc.Count = 2;
    m_Uniforms.u_bumpEnv = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_bumpEnv, &desc);

    desc.Name = (char *)"u_viewport";
    desc.Count = 1;
    m_Uniforms.u_viewport = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_viewport, &desc);

    desc.Name = (char *)"u_stageParams";
    desc.Count = CKFF_MAX_TEXTURE_STAGES * 2;
    m_Uniforms.u_stageParams = AllocUniformHandle();
    m_Context->CreateUniform(m_Uniforms.u_stageParams, &desc);

    desc.Type = CKRST_UNIFORM_SAMPLER;
    desc.Count = 1;
    for (int i = 0; i < CKFF_MAX_TEXTURE_STAGES; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "s_texture%d", i);
        desc.Name = name;
        m_Uniforms.s_texture[i] = AllocUniformHandle();
        m_Context->CreateUniform(m_Uniforms.s_texture[i], &desc);
    }
}

void CKFFShaderCache::CreatePrograms() {
    if (!m_Context) return;

    m_Stage3DProgram = CreateProgramFromBinary(
        s_vs_ff_3d, sizeof(s_vs_ff_3d),
        s_fs_ff_stage, sizeof(s_fs_ff_stage));
    CK_LOG_FMT("ShaderCache", "Stage3D program: %u (vs=%u bytes, fs=%u bytes)",
               m_Stage3DProgram, (unsigned)sizeof(s_vs_ff_3d), (unsigned)sizeof(s_fs_ff_stage));

    m_PositionTProgram = CreateProgramFromBinary(
        s_vs_ff_positiont, sizeof(s_vs_ff_positiont),
        s_fs_ff_stage, sizeof(s_fs_ff_stage));
    CK_LOG_FMT("ShaderCache", "PositionT program: %u (vs=%u bytes, fs=%u bytes)",
               m_PositionTProgram, (unsigned)sizeof(s_vs_ff_positiont), (unsigned)sizeof(s_fs_ff_stage));
}

CKDWORD CKFFShaderCache::CreateProgramFromBinary(
    const unsigned char *vsData, unsigned int vsSize,
    const unsigned char *fsData, unsigned int fsSize)
{
    if (!m_Context) return 0;

    CKDWORD hVS = AllocShaderHandle();
    CKShaderDesc vsDesc = {};
    vsDesc.Stage = CKRST_SHADER_VERTEX;
    vsDesc.Format = CKRST_SHADER_DXBC;
    vsDesc.Code = (CKBYTE *)vsData;
    vsDesc.CodeSize = vsSize;
    CKERROR err = m_Context->CreateShader(hVS, &vsDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(VS) FAILED: err=%d handle=%u size=%u", err, hVS, vsSize);
        return 0;
    }

    CKDWORD hFS = AllocShaderHandle();
    CKShaderDesc fsDesc = {};
    fsDesc.Stage = CKRST_SHADER_PIXEL;
    fsDesc.Format = CKRST_SHADER_DXBC;
    fsDesc.Code = (CKBYTE *)fsData;
    fsDesc.CodeSize = fsSize;
    err = m_Context->CreateShader(hFS, &fsDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateShader(FS) FAILED: err=%d handle=%u size=%u", err, hFS, fsSize);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        return 0;
    }

    CKDWORD hProgram = AllocProgramHandle();
    CKProgramDesc progDesc = {};
    progDesc.VertexShader = hVS;
    progDesc.PixelShader = hFS;
    progDesc.ConsumeShaders = TRUE;
    err = m_Context->CreateProgram(hProgram, &progDesc);
    if (err != CK_OK) {
        CK_LOG_FMT("ShaderCache", "CreateProgram FAILED: err=%d vs=%u fs=%u", err, hVS, hFS);
        m_Context->DeleteObject(hVS, CKRST_OBJ_SHADER);
        m_Context->DeleteObject(hFS, CKRST_OBJ_SHADER);
        return 0;
    }

    CK_LOG_FMT("ShaderCache", "CreateProgram OK: program=%u vs=%u fs=%u", hProgram, hVS, hFS);
    return hProgram;
}

CKDWORD CKFFShaderCache::GetProgram(const CKFFShaderKey &key) {
    return key.VS.GetHasPositionT() ? m_PositionTProgram : m_Stage3DProgram;
}
