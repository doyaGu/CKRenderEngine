#include "RCKRenderObject.h"
#include "RCKRenderContext.h"
#include "RCK3dEntity.h"
#include "CKSceneGraph.h"
#include "CKDebugLogger.h"

#define RENDEROBJ_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("RenderObject", fmt, __VA_ARGS__)

CK_CLASSID RCKRenderObject::m_ClassID = CKCID_RENDEROBJECT;

void RCKRenderObject::AddToRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    m_InRenderContext |= dev->m_MaskFree;
    RENDEROBJ_DEBUG_LOG_FMT("AddToRenderContext: obj=%p mask=0x%X, is3D=%d", this, m_InRenderContext, CKIsChildClassOf(this, CKCID_3DENTITY));
    if (CKIsChildClassOf(this, CKCID_3DENTITY) && !m_Context->IsInClearAll()) {
        RCK3dEntity *entity = (RCK3dEntity *) this;
        if (entity->m_SceneGraphNode) {
            if (dev->m_Start) {
                entity->m_SceneGraphNode->m_RenderContextMask = m_InRenderContext;
                RENDEROBJ_DEBUG_LOG_FMT("AddToRenderContext: Set node mask direct to 0x%X", m_InRenderContext);
            } else {
                entity->m_SceneGraphNode->SetRenderContextMask(m_InRenderContext, FALSE);
                RENDEROBJ_DEBUG_LOG_FMT("AddToRenderContext: SetRenderContextMask(0x%X)", m_InRenderContext);
            }
        } else {
            RENDEROBJ_DEBUG_LOG_FMT("AddToRenderContext: WARNING - no scene graph node!");
        }
    }
}

void RCKRenderObject::RemoveFromRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    m_InRenderContext &= ~dev->m_MaskFree;
    if (CKIsChildClassOf(this, CKCID_3DENTITY) && !m_Context->IsInClearAll()) {
        RCK3dEntity *entity = (RCK3dEntity *) this;
        if (dev->m_Start)
            entity->m_SceneGraphNode->m_RenderContextMask = m_InRenderContext;
        else
            entity->m_SceneGraphNode->SetRenderContextMask(m_InRenderContext, FALSE);
    }
}

int RCKRenderObject::CanBeHide() {
    return 2;
}

CKBOOL RCKRenderObject::IsInRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    return (dev->m_MaskFree & m_InRenderContext) != 0;
}

CKBOOL RCKRenderObject::IsRootObject() {
    return TRUE;
}

CKBOOL RCKRenderObject::IsToBeRendered() {
    return TRUE;
}

void RCKRenderObject::SetZOrder(int Z) {
}

int RCKRenderObject::GetZOrder() {
    return 0;
}

CKBOOL RCKRenderObject::IsToBeRenderedLast() {
    return FALSE;
}

CKBOOL RCKRenderObject::AddPreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->AddPreCallback(Function, Argument, Temp, m_Context->GetRenderManager());
}

CKBOOL RCKRenderObject::RemovePreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemovePreCallback(Function, Argument);
}

CKBOOL RCKRenderObject::SetRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->SetCallBack(Function, Argument);
}

CKBOOL RCKRenderObject::RemoveRenderCallBack() {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemoveCallBack();
}

CKBOOL RCKRenderObject::AddPostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->AddPostCallback(Function, Argument, Temp, m_Context->GetRenderManager());
}

CKBOOL RCKRenderObject::RemovePostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemovePostCallback(Function, Argument);
}

void RCKRenderObject::RemoveAllCallbacks() {
}

RCKRenderObject::RCKRenderObject(CKContext *Context, CKSTRING name) : CKRenderObject(Context, name) {
    m_InRenderContext = 0;
    m_Callbacks = nullptr;
}

RCKRenderObject::~RCKRenderObject() {
}

CK_CLASSID RCKRenderObject::GetClassID() {
    return m_ClassID;
}

CKStateChunk *RCKRenderObject::Save(CKFile *file, CKDWORD flags) {
    return CKRenderObject::Save(file, flags);
}

CKERROR RCKRenderObject::Load(CKStateChunk *chunk, CKFile *file) {
    return CKRenderObject::Load(chunk, file);
}

int RCKRenderObject::GetMemoryOccupation() {
    return CKBeObject::GetMemoryOccupation() + sizeof(CKDWORD) * 2;
}

CKERROR RCKRenderObject::Copy(CKObject &o, CKDependenciesContext &context) {
    return CKRenderObject::Copy(o, context);
}

CKSTRING RCKRenderObject::GetClassName() {
    return (CKSTRING) "Render Object";
}

int RCKRenderObject::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCKRenderObject::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCKRenderObject::Register() {
    CKPARAMETERFROMCLASS(RCKRenderObject, CKPGUID_RENDEROBJECT);
}

CKRenderObject *RCKRenderObject::CreateInstance(CKContext *Context) {
    return new RCKRenderObject(Context);
}
