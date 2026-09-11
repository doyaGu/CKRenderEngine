#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKBuiltinShaders.h"
#include "CKFFShaderKey.h"
#include "CKFFShaderABI.h"
#include "CKFFConstants.h"
#include "CKRasterizerBackendEnums.h"
#include "CKRasterizerBackendTypes.h"

#include <stdint.h>

class CKRasterizerBackend;
struct CKFFShaderCacheTestAccess;

// The fixed-function program family (spec 5.3): every draw runs the single
// fragment uber shader with one of four vertex shaders selected by the
// POSITIONT and user-clip bits of the shader key. Every other state travels
// in u_ffSpec / u_stageParams instead of selecting a variant.
enum CKFFProgramVariant {
    CKFF_PROGRAM_3D = 0,
    CKFF_PROGRAM_3D_CLIP = 1,
    CKFF_PROGRAM_POSITIONT = 2,
    CKFF_PROGRAM_POSITIONT_CLIP = 3,
    CKFF_PROGRAM_VARIANT_COUNT = 4
};

struct CKFFProgramBinding {
    CKDWORD Program;
    CKFFSpecializationInfo Specialization;

    CKFFProgramBinding() : Program(0), Specialization() {}
    CKFFProgramBinding(CKDWORD program, const CKFFSpecializationInfo &specialization)
        : Program(program), Specialization(specialization) {}

    operator CKDWORD() const { return Program; }
};

struct CKFFProgramContext {
    CKFFShaderKey ShaderKey;
    CKFFProgramBinding Binding;
    CKDWORD Program;
    CKFFSpecializationInfo Specialization;

    CKFFProgramContext()
        : ShaderKey(), Binding(), Program(0), Specialization() {}
};

struct CKFFProgramSamplerBinding {
    CKDWORD Stage;          // backend sampler slot

    CKFFProgramSamplerBinding() : Stage(0) {}
};

// The fixed sampler layout shared by every program: s_texture0..7 on slots
// 0..7, s_textureCube0..3 on 8..11, s_textureVolume0..3 on 12..15.
struct CKFFProgramSamplerLayout {
    CKDWORD BindingCount;
    CKFFProgramSamplerBinding Bindings[CKFF_SAMPLER_SLOT_COUNT];

    CKFFProgramSamplerLayout() : BindingCount(0), Bindings() {}
};

void CKFFInitProgramContext(CKFFProgramContext *context,
                            const CKFFShaderKey &key,
                            const CKFFProgramBinding &binding);

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    bool Init(CKRasterizerBackend *backend, const CKBackendShaderSet &shaders);
    void Shutdown();

    // Select the fixed-function program for the given FFP shader key and
    // derive its specialization data. Programs are created on first use.
    CKFFProgramBinding GetProgram(const CKFFShaderKey &key);
    static CKFFProgramVariant ProgramVariantForKey(const CKFFShaderKey &key);

    CKBOOL RequiresExplicitSamplerInitialization() const {
        return m_Target.ShaderProfile == CKRST_SHADER_PROFILE_GLSL ||
               m_Target.ShaderProfile == CKRST_SHADER_PROFILE_ESSL;
    }
    const CKFFProgramSamplerLayout &GetSamplerLayout() const { return m_SamplerLayout; }

    CKDWORD GetTargetFlags() const {
        CKDWORD flags = 0;
        if (m_Target.HomogeneousDepth)
            flags |= CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE;
        if (m_Target.OriginBottomLeft)
            flags |= CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT;
        return flags;
    }

    // Number of program variants created so far (at most CKFF_PROGRAM_VARIANT_COUNT).
    size_t CachedProgramCount() const;

private:
    friend struct CKFFShaderCacheTestAccess;

    CKRasterizerBackend *m_Backend;
    CKRasterizerTargetDesc m_Target;
    CKBackendShaderSet m_Shaders;
    CKDWORD m_Programs[CKFF_PROGRAM_VARIANT_COUNT];
    CKDWORD m_VertexShaders[CKFF_PROGRAM_VARIANT_COUNT];
    CKDWORD m_PixelShader;
    CKFFProgramSamplerLayout m_SamplerLayout;

    // Repeated materials change matrices much more often than fragment state.
    // Retain a bounded working set per vertex variant; draws keep their own
    // specialization copy when an older entry is evicted.
    static constexpr CKDWORD SPECIALIZATION_CACHE_CAPACITY = 16;
    struct SpecializationEntry {
        CKFFShaderKeyFS Key;
        CKFFSpecializationInfo Value;
        uint64_t LastUse = 0;
    };
    struct SpecializationCache {
        SpecializationEntry Entries[SPECIALIZATION_CACHE_CAPACITY];
        CKDWORD Count = 0;
        uint64_t Clock = 0;
    };
    SpecializationCache m_Specializations[CKFF_PROGRAM_VARIANT_COUNT];

    bool ResolveShaderTarget();
    void BuildSamplerLayout();
    CKDWORD CreateProgramVariant(CKFFProgramVariant variant);
};

#endif // CKFFSHADERCACHE_H
