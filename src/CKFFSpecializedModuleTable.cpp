#include "CKFFSpecializedModuleTable.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

bool CKFFFindSpecializedModule(const CKFFShaderKey &key,
                               CK_SHADER_PROFILE profile,
                               CKFFSpecializedModule &module) {
    for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        const CKFFSpecializedModuleEntry &entry = g_CKFFSpecializedModules[i];
        if (entry.Profile == profile && entry.Key == key) {
            module = entry.Module;
            return true;
        }
    }

    module = CKFFSpecializedModule{};
    return false;
}

size_t CKFFSpecializedModuleCount() {
    return g_CKFFSpecializedModuleCount;
}

CKDWORD CKFFGeneratedShaderABIVersion() {
    return g_CKFFGeneratedShaderABIVersion;
}

CKDWORD CKFFGeneratedShaderInterfaceHash() {
    return g_CKFFGeneratedShaderInterfaceHash;
}

bool CKFFFindSamplerLayoutModule(const CKFFSamplerLayoutKey &key,
                                 CK_SHADER_PROFILE profile,
                                 CKFFSamplerLayoutModule &module) {
    for (size_t i = 0; i < g_CKFFSamplerLayoutModuleCount; ++i) {
        const CKFFSamplerLayoutModuleEntry &entry = g_CKFFSamplerLayoutModules[i];
        if (entry.Profile == profile && entry.Key == key) {
            module = entry.Module;
            return true;
        }
    }

    module = CKFFSamplerLayoutModule{};
    return false;
}

size_t CKFFSamplerLayoutModuleCount() {
    return g_CKFFSamplerLayoutModuleCount;
}
