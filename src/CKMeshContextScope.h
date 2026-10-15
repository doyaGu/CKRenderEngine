#ifndef CKMESHCONTEXTSCOPE_H
#define CKMESHCONTEXTSCOPE_H

#include "RCKRenderContext.h"

// Independent nested mesh calls restore the context state present at entry.
// The public rasterizer has no material getter: only engine uploads can be
// remembered here; arbitrary callback-owned material payloads are not queried.
class CKMeshContextScope {
public:
    explicit CKMeshContextScope(RCKRenderContext *context)
        : m_Previous(Current()), m_Context(context), m_Enclosing(nullptr),
          m_State(nullptr), m_MaterialKnown(FALSE) {
        for (CKMeshContextScope *scope = m_Previous; scope; scope = scope->m_Previous) {
            if (scope->m_Context == context) {
                m_Enclosing = scope;
                break;
            }
        }
        if (m_Enclosing) {
            m_World = context->m_WorldMatrix;
            m_View = context->m_ViewMatrix;
            m_Projection = context->m_ProjectionMatrix;
            m_Viewport = context->m_ViewportData;
            m_ProjectionUpdated = context->m_ProjectionUpdated;
            m_CurrentEntity = context->m_Current3dEntity;
            m_MaterialKnown = m_Enclosing->m_MaterialKnown;
            if (m_MaterialKnown)
                m_Material = m_Enclosing->m_Material;
            m_State = new CKRenderContextStateGuard(context->m_RasterizerContext);
        }
        Current() = this;
    }
    ~CKMeshContextScope() {
        if (m_Enclosing) {
            CKRasterizerContext *rst = m_Context->m_RasterizerContext;
            if (m_Enclosing->m_MaterialKnown)
                rst->SetMaterial(&m_Enclosing->m_Material);
            delete m_State;
            m_Context->m_WorldMatrix = m_World;
            m_Context->m_ViewMatrix = m_View;
            m_Context->m_ProjectionMatrix = m_Projection;
            m_Context->m_ViewportData = m_Viewport;
            m_Context->m_ProjectionUpdated = m_ProjectionUpdated;
            m_Context->m_Current3dEntity = m_CurrentEntity;
            rst->SetViewport(&m_Viewport);
        }
        Current() = m_Previous;
    }
    static void RecordMaterial(RCKRenderContext *context, const CKMaterialData &material) {
        for (CKMeshContextScope *scope = Current(); scope; scope = scope->m_Previous) {
            if (scope->m_Context == context) {
                scope->m_Material = material;
                scope->m_MaterialKnown = TRUE;
                break;
            }
        }
    }
private:
    CKMeshContextScope(const CKMeshContextScope &) = delete;
    CKMeshContextScope &operator=(const CKMeshContextScope &) = delete;
    static CKMeshContextScope *&Current() {
        static thread_local CKMeshContextScope *current = nullptr;
        return current;
    }
    CKMeshContextScope *m_Previous;
    RCKRenderContext *m_Context;
    CKMeshContextScope *m_Enclosing;
    CKRenderContextStateGuard *m_State;
    VxMatrix m_World;
    VxMatrix m_View;
    VxMatrix m_Projection;
    CKViewportData m_Viewport;
    CKBOOL m_ProjectionUpdated;
    RCK3dEntity *m_CurrentEntity;
    CKMaterialData m_Material;
    CKBOOL m_MaterialKnown;
};

#endif
