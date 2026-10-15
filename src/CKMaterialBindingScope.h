#ifndef CKMATERIALBINDINGSCOPE_H
#define CKMATERIALBINDINGSCOPE_H

#include "CKTypes.h"
class CKMaterial;
class CKRenderContext;
class CKMaterialBindingCall;

// Private observation of one SDK virtual SetAsCurrent invocation.
// A material override that does not call the base keeps ownership unknown.
class CKMaterialBindingScope {
public:
    CKMaterialBindingScope(CKMaterial *material, CKRenderContext *context, int stage)
        : m_Previous(Current()), m_Material(material), m_Context(context), m_Stage(stage),
          m_Outcome(Unknown), m_InBaseCall(FALSE) { Current() = this; }
    ~CKMaterialBindingScope() { Current() = m_Previous; }
    CKBOOL TextureStagesWereReset() const { return m_Outcome == TextureReset; }
    CKBOOL UsesDefaultTextures() const {
        return m_Outcome == TextureReset || m_Outcome == DefaultTextures;
    }
    CKBOOL AllowsRenderStateOverrides() const {
        return UsesDefaultTextures() || m_Outcome == TextureOwned;
    }
private:
    friend class CKMaterialBindingCall;
    enum Outcome { Unknown, AllOwned, TextureOwned, TextureReset, DefaultTextures };
    static CKMaterialBindingScope *&Current() {
        static thread_local CKMaterialBindingScope *current = nullptr;
        return current;
    }
    CKMaterialBindingScope(const CKMaterialBindingScope &) = delete;
    CKMaterialBindingScope &operator=(const CKMaterialBindingScope &) = delete;
    CKMaterialBindingScope *m_Previous;
    CKMaterial *m_Material;
    CKRenderContext *m_Context;
    int m_Stage;
    Outcome m_Outcome;
    CKBOOL m_InBaseCall;
};

// Nested direct base calls must not report into the enclosing virtual call.
// Nested RenderGroup calls create their own scope and receive their own report.
class CKMaterialBindingCall {
public:
    CKMaterialBindingCall(CKMaterial *material, CKRenderContext *context, int stage)
        : m_Scope(CKMaterialBindingScope::Current()) {
        if (!m_Scope || m_Scope->m_InBaseCall || m_Scope->m_Material != material ||
            m_Scope->m_Context != context || m_Scope->m_Stage != stage) {
            m_Scope = nullptr;
        } else {
            m_Scope->m_InBaseCall = TRUE;
            m_Scope->m_Outcome = CKMaterialBindingScope::Unknown;
        }
    }
    ~CKMaterialBindingCall() { if (m_Scope) m_Scope->m_InBaseCall = FALSE; }
    void OwnAllState() { if (m_Scope) m_Scope->m_Outcome = CKMaterialBindingScope::AllOwned; }
    void OwnTextureStages() { if (m_Scope) m_Scope->m_Outcome = CKMaterialBindingScope::TextureOwned; }
    void ResetTextureStages() { if (m_Scope) m_Scope->m_Outcome = CKMaterialBindingScope::TextureReset; }
    void UseDefaultTextures() { if (m_Scope) m_Scope->m_Outcome = CKMaterialBindingScope::DefaultTextures; }
private:
    CKMaterialBindingCall(const CKMaterialBindingCall &) = delete;
    CKMaterialBindingCall &operator=(const CKMaterialBindingCall &) = delete;
    CKMaterialBindingScope *m_Scope;
};

#endif
