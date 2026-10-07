#ifndef CKBUILTINSHADERS_H
#define CKBUILTINSHADERS_H

#include "CKBuiltinShaderIdentity.h"
#include "CKRasterizerContextTypes.h"
#include "XClassArray.h"

struct CKFFShaderTarget {
    CK_SHADER_FORMAT Format = CKRST_SHADER_FORMAT_UNKNOWN;
    CK_SHADER_PROFILE Profile = CKRST_SHADER_PROFILE_UNKNOWN;
};

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

struct CKFFShaderSet {
    CKDWORD ABIVersion = 0;
    CKDWORD InterfaceHash = 0;
    CKShaderDesc Shaders[CKRST_BUILTIN_SHADER_COUNT];

    bool Matches(CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile) const {
        if (format == CKRST_SHADER_FORMAT_UNKNOWN || profile == CKRST_SHADER_PROFILE_UNKNOWN ||
            ABIVersion != CKFF_SHADER_ABI_VERSION || InterfaceHash != CKFF_SHADER_NATIVE_INTERFACE_HASH)
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

// Immutable shader data consumed by one fixed-function context. Concrete
// rasterizers build this value from their checked-in shader artifacts before
// creating a context; CKFFPLib does not call back into rasterizer lifecycle
// objects to discover shaders.
class CKFFShaderLibrary {
public:
    CKBOOL Add(const CKFFShaderSet &shaderSet)
    {
        const CKShaderDesc &identity = shaderSet.Shaders[0];
        if (!shaderSet.Matches(identity.Format, identity.Profile))
            return FALSE;
        for (int i = 0; i < m_ShaderSets.Size(); ++i) {
            const CKShaderDesc &existing = m_ShaderSets[i].Shaders[0];
            if (existing.Format == identity.Format && existing.Profile == identity.Profile) {
                m_ShaderSets[i] = shaderSet;
                return TRUE;
            }
        }
        m_ShaderSets.PushBack(shaderSet);
        return TRUE;
    }

    void Clear() { m_ShaderSets.Clear(); }
    CKBOOL Empty() const { return m_ShaderSets.Size() == 0 ? TRUE : FALSE; }

    void GetTargets(XClassArray<CKFFShaderTarget> &targets) const
    {
        targets.Clear();
        targets.Reserve(m_ShaderSets.Size());
        for (int i = 0; i < m_ShaderSets.Size(); ++i) {
            const CKShaderDesc &identity = m_ShaderSets[i].Shaders[0];
            CKFFShaderTarget target;
            target.Format = identity.Format;
            target.Profile = identity.Profile;
            targets.PushBack(target);
        }
    }

    CKBOOL Find(CK_SHADER_FORMAT format, CK_SHADER_PROFILE profile,
                CKFFShaderSet &shaderSet) const
    {
        for (int i = 0; i < m_ShaderSets.Size(); ++i) {
            if (m_ShaderSets[i].Matches(format, profile)) {
                shaderSet = m_ShaderSets[i];
                return TRUE;
            }
        }
        shaderSet = CKFFShaderSet();
        return FALSE;
    }

private:
    XClassArray<CKFFShaderSet> m_ShaderSets;
};

#endif
