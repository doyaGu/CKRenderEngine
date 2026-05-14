#include "CKFFSpecializedModuleTable.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

bool CKFFFindSpecializedModule(const CKFFShaderKey &key,
                               CK_SHADER_PROFILE profile,
                               CKFFSpecializedModule &module) {
    for (std::size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        const CKFFSpecializedModuleEntry &entry = g_CKFFSpecializedModules[i];
        if (entry.Profile == profile && entry.Key == key) {
            module = entry.Module;
            return true;
        }
    }

    module = CKFFSpecializedModule{};
    return false;
}

bool CKFFFindVolumeSamplerModule(CKDWORD volumeMask,
                                 CK_SHADER_PROFILE profile,
                                 CKFFVolumeSamplerModule &module) {
    for (std::size_t i = 0; i < g_CKFFVolumeSamplerModuleCount; ++i) {
        const CKFFVolumeSamplerModuleEntry &entry = g_CKFFVolumeSamplerModules[i];
        if (entry.Profile == profile && entry.VolumeMask == volumeMask) {
            module = entry.Module;
            return true;
        }
    }

    module = CKFFVolumeSamplerModule{};
    return false;
}
