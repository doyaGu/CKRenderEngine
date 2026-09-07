#ifndef CKBUILTINSHADERS_H
#define CKBUILTINSHADERS_H

#include "CKRasterizerBackendTypes.h"
#include "CKFFShaderABI.h"
#include <vector>

struct CKBackendCaps;
struct CKBackendShaderTarget;

// Rasterizer shader roles. Plugins supply immutable artifacts for the device
// target; the FFP and presentation layers declare their resource interfaces.
// The generic backend never selects a role or discovers a shader catalog.
enum CKBuiltinShader {
    CKRST_SHADER_FF_3D,
    CKRST_SHADER_FF_3D_CLIP,
    CKRST_SHADER_FF_POSITIONT,
    CKRST_SHADER_FF_POSITIONT_CLIP,
    CKRST_SHADER_FF_FRAGMENT,
    CKRST_SHADER_PRESENT_VERTEX,
    CKRST_SHADER_PRESENT_FRAGMENT,
    CKRST_BUILTIN_SHADER_COUNT
};

struct CKBackendShaderSet {
    CKDWORD ABIVersion = 0;
    CKDWORD InterfaceHash = 0;
    CKShaderDesc Shaders[CKRST_BUILTIN_SHADER_COUNT];

    bool Matches(CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile) const {
        if (format == CKRST_SHADER_FORMAT_UNKNOWN || profile == CKRST_SHADER_PROFILE_UNKNOWN ||
            ABIVersion != CKFF_SHADER_ABI_VERSION || InterfaceHash != CKFFShaderInterfaceHash(format))
            return false;
        for (unsigned i = 0; i < CKRST_BUILTIN_SHADER_COUNT; ++i) {
            const CKShaderDesc &shader = Shaders[i];
            const CK_SHADER_STAGE stage = i == CKRST_SHADER_FF_FRAGMENT ||
                i == CKRST_SHADER_PRESENT_FRAGMENT ? CKRST_SHADER_PIXEL : CKRST_SHADER_VERTEX;
            if (shader.Stage != stage || shader.Format != format || shader.Profile != profile ||
                !shader.Code || !shader.CodeSize || !shader.EntryPoint || !shader.EntryPoint[0])
                return false;
        }
        return true;
    }
};

// The NULL rasterizer supplies its own descriptors for deterministic tests.
CKBOOL CKNullRasterizerShaderSet(const CKBackendCaps &caps, CKBackendShaderSet &out);
void CKNullRasterizerShaderTargets(std::vector<CKBackendShaderTarget> &out);

#endif
