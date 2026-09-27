#ifndef CKFFPROGRAM_H
#define CKFFPROGRAM_H

#include "CKFFShaderKey.h"
#include "CKFFFragmentProgram.h"

enum CKFFProgramVariant {
    CKFF_PROGRAM_3D = 0,
    CKFF_PROGRAM_3D_CLIP = 1,
    CKFF_PROGRAM_POSITIONT = 2,
    CKFF_PROGRAM_POSITIONT_CLIP = 3,
    CKFF_PROGRAM_VARIANT_COUNT = 4
};

enum CKFFFragmentSamplingMode {
    CKFF_FRAGMENT_SAMPLING_FULL_EXACT = 0,
    CKFF_FRAGMENT_SAMPLING_NATIVE_EXACT = 1,
    CKFF_FRAGMENT_SAMPLING_MODE_COUNT = 2
};

// Shader selection produced by fixed-function state resolution. Native
// shader and program objects are deliberately absent.
struct CKFFProgramContext {
    CKFFShaderKey ShaderKey;
    CKFFFragmentProgram FragmentProgram;
    CKFFSamplerLayoutPlan SamplerLayoutPlan;

    CKFFProgramContext()
        : ShaderKey(), FragmentProgram(), SamplerLayoutPlan() {}
};

inline void CKFFInitProgramContext(CKFFProgramContext *context,
                                   const CKFFShaderKey &key,
                                   const CKFFFragmentProgram &fragmentProgram,
                                   const CKFFSamplerLayoutPlan &samplerLayoutPlan)
{
    if (!context)
        return;
    context->ShaderKey = key;
    context->FragmentProgram = fragmentProgram;
    context->SamplerLayoutPlan = samplerLayoutPlan;
}

#endif // CKFFPROGRAM_H
