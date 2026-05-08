#ifndef CKFFSPECIALIZEDMODULETABLE_H
#define CKFFSPECIALIZEDMODULETABLE_H

#include "CKFFShaderKey.h"
#include "CKRasterizerTypes.h"

struct CKFFSpecializedModule {
    const unsigned char *VSData;
    unsigned int VSSize;
    const unsigned char *FSData;
    unsigned int FSSize;
    CKFFSpecializationInfo Specialization;
};

bool CKFFFindSpecializedModule(const CKFFShaderKey &key,
                               CK_SHADER_PROFILE profile,
                               CKFFSpecializedModule &module);

#endif // CKFFSPECIALIZEDMODULETABLE_H
