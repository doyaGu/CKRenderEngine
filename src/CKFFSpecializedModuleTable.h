#ifndef CKFFSPECIALIZEDMODULETABLE_H
#define CKFFSPECIALIZEDMODULETABLE_H

#include "CKFFShaderKey.h"
#include "CKFFSamplerLayout.h"
#include "CKRasterizerTypes.h"

#include <stddef.h>

struct CKFFSpecializedModule {
    const unsigned char *VSData;
    unsigned int VSSize;
    const unsigned char *FSData;
    unsigned int FSSize;
    CKFFSpecializationInfo Specialization;
};

struct CKFFSpecializedModuleEntry {
    CK_SHADER_PROFILE Profile;
    CKFFShaderKey Key;
    CKFFSpecializedModule Module;
};

bool CKFFFindSpecializedModule(const CKFFShaderKey &key,
                               CK_SHADER_PROFILE profile,
                               CKFFSpecializedModule &module);
size_t CKFFSpecializedModuleCount();
CKDWORD CKFFGeneratedShaderABIVersion();
CKDWORD CKFFGeneratedShaderInterfaceHash();

struct CKFFSamplerLayoutModule {
    const unsigned char *FSData;
    unsigned int FSSize;
};

struct CKFFSamplerLayoutModuleEntry {
    CK_SHADER_PROFILE Profile;
    CKFFSamplerLayoutKey Key;
    CKFFSamplerLayoutModule Module;
};

bool CKFFFindSamplerLayoutModule(const CKFFSamplerLayoutKey &key,
                                 CK_SHADER_PROFILE profile,
                                 CKFFSamplerLayoutModule &module);
size_t CKFFSamplerLayoutModuleCount();

#endif // CKFFSPECIALIZEDMODULETABLE_H
