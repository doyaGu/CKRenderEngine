#include "CKFFSpecializedModuleTable.h"

bool CKFFFindSpecializedModule(const CKFFShaderKey &key,
                               CK_SHADER_PROFILE profile,
                               CKFFSpecializedModule &module) {
    (void)key;
    (void)profile;
    module = {};
    return false;
}
