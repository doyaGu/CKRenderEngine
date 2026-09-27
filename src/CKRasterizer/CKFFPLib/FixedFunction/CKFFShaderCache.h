#ifndef CKFFSHADERCACHE_H
#define CKFFSHADERCACHE_H

#include "CKBuiltinShaders.h"
#include "CKFFShaderKey.h"
#include "CKFFProgram.h"
#include "CKFFShaderABI.h"
#include "CKFFShaderInterface.h"
#include "CKFFConstants.h"
#include "CKRasterizerContextEnums.h"
#include "CKRasterizerContextTypes.h"

#include <stdint.h>

struct CKFFProgramBinding {
    CKDWORD Program;
    CKFFFragmentProgram FragmentProgram;

    CKFFProgramBinding() : Program(0), FragmentProgram() {}
    CKFFProgramBinding(CKDWORD program, const CKFFFragmentProgram &fragmentProgram)
        : Program(program), FragmentProgram(fragmentProgram) {}

    operator CKDWORD() const { return Program; }
};

struct CKFFProgramSelection {
    CKFFProgramVariant Variant;
    CKFFSamplerLayoutPlan SamplerLayoutPlan;
    CKFFFragmentProgram FragmentProgram;

    CKFFProgramSelection()
        : Variant(CKFF_PROGRAM_3D),
          SamplerLayoutPlan(), FragmentProgram() {}
};

struct CKFFProgramSamplerBinding {
    CKDWORD Stage;          // backend sampler slot

    CKFFProgramSamplerBinding() : Stage(0) {}
};

// The default wide-2D sampler layout. Native contexts select one of the three
// sixteen-slot layouts for each fixed-function program.
struct CKFFProgramSamplerLayout {
    CKDWORD BindingCount;
    CKFFProgramSamplerBinding Bindings[CKFF_SAMPLER_SLOT_COUNT];

    CKFFProgramSamplerLayout() : BindingCount(0), Bindings() {}
};

class CKFFShaderCache {
public:
    CKFFShaderCache();
    ~CKFFShaderCache();

    bool Init(const CKRasterizerTargetDesc &target, const CKFFShaderSet &shaders);
    void Shutdown();

    // Resolve CPU-only shader choice and fragment program. Concrete contexts
    // own and cache the native shader/program objects for the chosen variant.
    CKFFProgramSelection ResolveProgram(
        const CKFFShaderKey &key,
        const CKFFSamplerLayoutPlan &samplerLayoutPlan);
    CKFFProgramSelection ResolveProgram(const CKFFShaderKey &key) {
        return ResolveProgram(key, CKFFBuildSamplerLayoutPlan(key.FS));
    }
    static CKFFProgramVariant ProgramVariantForKey(const CKFFShaderKey &key);
    const CKShaderDesc &GetVertexShader(CKFFProgramVariant variant) const;
    const CKShaderDesc &GetPixelShader() const;
    CK_SHADER_FORMAT GetShaderFormat() const { return m_Target.ShaderFormat; }

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

    CKDWORD GetCachedFragmentProgramCount(CKFFProgramVariant variant) const;
    static CKDWORD GetFragmentProgramCapacity();
    CKBOOL HasCachedFragmentProgram(CKFFProgramVariant variant, const CKFFShaderKeyFS &key) const;

private:
    CKRasterizerTargetDesc m_Target;
    CKFFShaderSet m_Shaders;
    CKFFProgramSamplerLayout m_SamplerLayout;

    // Repeated materials change matrices much more often than fragment state.
    // Retain a bounded working set per vertex variant; draws keep their own
    // fragment-program copy when an older entry is evicted.
    static constexpr CKDWORD FRAGMENT_PROGRAM_CACHE_CAPACITY = 16;
    struct FragmentProgramEntry {
        CKFFShaderKeyFS Key;
        CKFFFragmentProgram Value;
        uint64_t LastUse = 0;
    };
    struct FragmentProgramCache {
        FragmentProgramEntry Entries[FRAGMENT_PROGRAM_CACHE_CAPACITY];
        CKDWORD Count = 0;
        uint64_t Clock = 0;
    };
    FragmentProgramCache m_FragmentPrograms[CKFF_PROGRAM_VARIANT_COUNT];

    bool ResolveShaderTarget(const CKRasterizerTargetDesc &target);
    void BuildSamplerLayout();
    void Reset();
};

#endif // CKFFSHADERCACHE_H
