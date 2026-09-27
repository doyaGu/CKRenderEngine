#ifndef CKFFPROGRAM_H
#define CKFFPROGRAM_H

#include "CKFFShaderKey.h"
#include "CKFFSpecializationInfo.h"

enum CKFFProgramVariant {
    CKFF_PROGRAM_3D = 0,
    CKFF_PROGRAM_3D_CLIP = 1,
    CKFF_PROGRAM_POSITIONT = 2,
    CKFF_PROGRAM_POSITIONT_CLIP = 3,
    CKFF_PROGRAM_VARIANT_COUNT = 4
};

// Shader selection produced by fixed-function state resolution. Native
// shader and program objects are deliberately absent.
struct CKFFProgramContext {
    CKFFShaderKey ShaderKey;
    CKFFSpecializationInfo Specialization;
    CKFFSamplerLayoutPlan SamplerLayoutPlan;

    CKFFProgramContext()
        : ShaderKey(), Specialization(), SamplerLayoutPlan() {}
};

inline void CKFFInitProgramContext(CKFFProgramContext *context,
                                   const CKFFShaderKey &key,
                                   const CKFFSpecializationInfo &specialization)
{
    if (!context)
        return;
    context->ShaderKey = key;
    context->Specialization = specialization;
    context->SamplerLayoutPlan = CKFFBuildSamplerLayoutPlan(key.FS);
}

#endif // CKFFPROGRAM_H
