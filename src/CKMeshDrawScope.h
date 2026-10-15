#ifndef CKMESHDRAWSCOPE_H
#define CKMESHDRAWSCOPE_H

#include "CKTypes.h"
class CKRenderContext;
struct VxDrawPrimitiveData;
class CKMeshDrawCall;

// Mesh has already selected lighting when it submits a channel. Keep that
// state across the SDK virtual call, independently of the normal declaration.
class CKMeshDrawScope {
public:
    CKMeshDrawScope(CKRenderContext *context, VxDrawPrimitiveData *data)
        : m_Previous(Current()), m_Context(context), m_Data(data), m_InBaseCall(FALSE) {
        Current() = this;
    }
    ~CKMeshDrawScope() { Current() = m_Previous; }
private:
    friend class CKMeshDrawCall;
    static CKMeshDrawScope *&Current() {
        static thread_local CKMeshDrawScope *current = nullptr;
        return current;
    }
    CKMeshDrawScope(const CKMeshDrawScope &) = delete;
    CKMeshDrawScope &operator=(const CKMeshDrawScope &) = delete;
    CKMeshDrawScope *m_Previous;
    CKRenderContext *m_Context;
    VxDrawPrimitiveData *m_Data;
    CKBOOL m_InBaseCall;
};

// A derived context may delegate the original packet to the base. Unrelated
// packets/contexts and recursive base calls retain the public SDK contract.
class CKMeshDrawCall {
public:
    CKMeshDrawCall(CKRenderContext *context, VxDrawPrimitiveData *data)
        : m_Scope(CKMeshDrawScope::Current()) {
        if (!m_Scope || m_Scope->m_InBaseCall || m_Scope->m_Context != context ||
            m_Scope->m_Data != data) {
            m_Scope = nullptr;
        } else {
            m_Scope->m_InBaseCall = TRUE;
        }
    }
    ~CKMeshDrawCall() { if (m_Scope) m_Scope->m_InBaseCall = FALSE; }
    CKBOOL PreservesLighting() const { return m_Scope != nullptr; }
private:
    CKMeshDrawCall(const CKMeshDrawCall &) = delete;
    CKMeshDrawCall &operator=(const CKMeshDrawCall &) = delete;
    CKMeshDrawScope *m_Scope;
};

#endif
