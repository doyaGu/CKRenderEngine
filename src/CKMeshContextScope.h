#ifndef CKMESHCONTEXTSCOPE_H
#define CKMESHCONTEXTSCOPE_H

#include "RCKRenderContext.h"
#include "CKRasterizerObservation.h"

// Independent nested mesh calls restore the context state present at entry.
// Observed successful writes provide the physical callback-owned payload and
// viewport separately from the render-context cache.
class CKMeshContextScope {
public:
    explicit CKMeshContextScope(RCKRenderContext *context)
        : m_Previous(Current()), m_Context(context), m_Enclosing(nullptr),
          m_State(nullptr), m_MaterialKnown(FALSE), m_Nested(FALSE),
          m_PhysicalViewportKnown(FALSE), m_SavedMaterialKnown(FALSE), m_Observation(nullptr) {
        for (CKMeshContextScope *scope = m_Previous; scope; scope = scope->m_Previous) {
            if (scope->m_Context == context) {
                m_Enclosing = scope;
                break;
            }
        }
        m_Nested = m_Enclosing != nullptr || CKRasterizerCallbackScope::InCallback(context);
        if (m_Nested) {
            m_World = context->m_WorldMatrix;
            m_View = context->m_ViewMatrix;
            m_Projection = context->m_ProjectionMatrix;
            m_Viewport = context->m_ViewportData;
            m_ProjectionUpdated = context->m_ProjectionUpdated;
            m_CurrentEntity = context->m_Current3dEntity;
            m_MaterialKnown = CKRasterizerObservation::ReadMaterial(context->m_RasterizerContext, m_Material);
            if (!m_MaterialKnown && m_Enclosing && m_Enclosing->m_MaterialKnown) {
                m_Material = m_Enclosing->m_Material;
                m_MaterialKnown = TRUE;
            }
            if (m_MaterialKnown)
                m_SavedMaterial = m_Material;
            m_SavedMaterialKnown = m_MaterialKnown;
            m_PhysicalViewportKnown = CKRasterizerObservation::ReadViewport(
                context->m_RasterizerContext, m_PhysicalViewport);
            m_State = new CKRenderContextStateGuard(context->m_RasterizerContext);
        }
        m_Observation = new CKRasterizerObservation(context);
        Current() = this;
    }
    ~CKMeshContextScope() {
        if (m_Nested) {
            CKRasterizerContext *rst = m_Context->m_RasterizerContext;
            if (m_SavedMaterialKnown)
                rst->SetMaterial(&m_SavedMaterial);
            delete m_State;
            m_Context->m_WorldMatrix = m_World;
            m_Context->m_ViewMatrix = m_View;
            m_Context->m_ProjectionMatrix = m_Projection;
            m_Context->m_ViewportData = m_Viewport;
            m_Context->m_ProjectionUpdated = m_ProjectionUpdated;
            m_Context->m_Current3dEntity = m_CurrentEntity;
            rst->SetViewport(m_PhysicalViewportKnown ? &m_PhysicalViewport : &m_Viewport);
        }
        Current() = m_Previous;
        delete m_Observation;
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
    CKBOOL m_Nested;
    CKViewportData m_PhysicalViewport;
    CKBOOL m_PhysicalViewportKnown;
    CKMaterialData m_SavedMaterial;
    CKBOOL m_SavedMaterialKnown;
    CKRasterizerObservation *m_Observation;
};

#endif
