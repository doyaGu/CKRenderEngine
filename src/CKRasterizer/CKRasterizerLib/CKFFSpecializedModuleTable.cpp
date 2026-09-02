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

bool CKFFGetSpecializedModule(size_t index,
                              CKFFSpecializedModuleEntry &entry) {
    if (index >= g_CKFFSpecializedModuleCount)
        return false;
    entry = g_CKFFSpecializedModules[index];
    return true;
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

bool CKFFGetSamplerLayoutModule(size_t index,
                                CKFFSamplerLayoutModuleEntry &entry) {
    if (index >= g_CKFFSamplerLayoutModuleCount)
        return false;
    entry = g_CKFFSamplerLayoutModules[index];
    return true;
}

size_t CKFFSamplerLayoutModuleCount() {
    return g_CKFFSamplerLayoutModuleCount;
}
