#include "CKFixedFunctionPipeline.h"

CKFFStateGuard::CKFFStateGuard(CKFixedFunctionPipeline &pipeline)
    : m_Pipeline(&pipeline), m_Snapshot(pipeline.m_State) {
}

CKFFStateGuard::~CKFFStateGuard() {
    Restore();
}

void CKFFStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->m_State = m_Snapshot;
    m_Pipeline->OnFixedFunctionStateChanged(CKFixedFunctionPipeline::CKFF_CHANGE_PROGRAM | CKFixedFunctionPipeline::CKFF_CHANGE_STATIC_UNIFORM);
    m_Pipeline = nullptr;
}

void CKFFStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}

CKFFRenderStateGuard::CKFFRenderStateGuard(CKFixedFunctionPipeline &pipeline, VXRENDERSTATETYPE state, CKBOOL active)
    : m_Pipeline(active ? &pipeline : nullptr),
      m_State(state),
      m_Value(active ? pipeline.GetRenderState(state) : 0) {
}

CKFFRenderStateGuard::~CKFFRenderStateGuard() {
    Restore();
}

void CKFFRenderStateGuard::Restore() {
    if (!m_Pipeline)
        return;
    m_Pipeline->SetRenderState(m_State, m_Value);
    m_Pipeline = nullptr;
}

void CKFFRenderStateGuard::Dismiss() {
    m_Pipeline = nullptr;
}
