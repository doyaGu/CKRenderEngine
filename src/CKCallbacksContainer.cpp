#include "CKRenderEngineTypes.h"

#include "CKRenderContext.h"
#include "RCKRenderManager.h"

CKBOOL CKCallbacksContainer::AddPreCallback(void *Function, void *Argument, CKBOOL Temporary, CKRenderManager *renderManager) {
    if (!Function) {
        return FALSE;
    }

    for (XClassArray<VxCallBack>::Iterator it = m_PreCallBacks.Begin(); it != m_PreCallBacks.End(); ++it) {
        if (it->callback == Function && it->argument == Argument) {
            return FALSE; // Callback already exists
        }
    }

    VxCallBack cb = {Function, Argument, Temporary};
    m_PreCallBacks.PushBack(cb);
    if (Temporary && renderManager) {
        ((RCKRenderManager *) renderManager)->AddTemporaryCallback(this, Function, Argument, TRUE);
    }

    return TRUE;
}

CKBOOL CKCallbacksContainer::RemovePreCallback(void *Function, void *Argument) {
    if (!Function) {
        return FALSE;
    }

    for (XClassArray<VxCallBack>::Iterator it = m_PreCallBacks.Begin(); it != m_PreCallBacks.End(); ++it) {
        if (it->callback == Function && it->argument == Argument) {
            m_PreCallBacks.Remove(it);
            return TRUE; // Callback removed
        }
    }

    return FALSE;
}

CKBOOL CKCallbacksContainer::SetCallBack(void *Function, void *Argument) {
    if (!Function) {
        return FALSE;
    }

    if (m_Callback) {
        if (m_Callback->callback == Function && m_Callback->argument == Argument) {
            return FALSE; // Callback already exists
        }
        delete m_Callback; // Remove old callback
        m_Callback = nullptr;
    }

    m_Callback = new VxCallBack{Function, Argument, FALSE};
    return TRUE;
}

CKBOOL CKCallbacksContainer::RemoveCallBack() {
    if (!m_Callback) {
        return FALSE; // No callback to remove
    }

    delete m_Callback;
    m_Callback = nullptr;
    return TRUE;
}

CKBOOL CKCallbacksContainer::AddPostCallback(void *Function,
                                             void *Argument,
                                             CKBOOL Temporary,
                                             CKRenderManager *renderManager) {
    if (!Function) {
        return FALSE;
    }

    for (XClassArray<VxCallBack>::Iterator it = m_PostCallBacks.Begin(); it != m_PostCallBacks.End(); ++it) {
        if (it->callback == Function && it->argument == Argument) {
            return FALSE; // Callback already exists
        }
    }

    VxCallBack cb = {Function, Argument, Temporary};
    m_PostCallBacks.PushBack(cb);

    if (Temporary && renderManager) {
        ((RCKRenderManager *) renderManager)->AddTemporaryCallback(this, Function, Argument, FALSE);
    }

    return TRUE;
}

CKBOOL CKCallbacksContainer::RemovePostCallback(void *Function, void *Argument) {
    if (!Function) {
        return FALSE;
    }

    for (XClassArray<VxCallBack>::Iterator it = m_PostCallBacks.Begin(); it != m_PostCallBacks.End(); ++it) {
        if (it->callback == Function && it->argument == Argument) {
            m_PostCallBacks.Remove(it);
            return TRUE; // Callback removed
        }
    }

    return FALSE;
}

// Generic render callback type (not the same as CK_MESHRENDERCALLBACK which takes 4 args)
typedef void (*CK_RENDERCALLBACK_SIMPLE)(CKRenderContext *Dev, void *Argument);

void CKCallbacksContainer::ExecutePreCallbacks(CKRenderContext *dev, CKBOOL temporaryOnly) {
    for (XClassArray<VxCallBack>::Iterator it = m_PreCallBacks.Begin(); it != m_PreCallBacks.End();) {
        if (temporaryOnly && !it->temp) {
            ++it;
            continue;
        }

        CK_RENDERCALLBACK_SIMPLE func = (CK_RENDERCALLBACK_SIMPLE) it->callback;
        if (func) {
            func(dev, it->argument);
        }

        if (it->temp) {
            it = m_PreCallBacks.Remove(it);
        } else {
            ++it;
        }
    }
}

void CKCallbacksContainer::ExecutePostCallbacks(CKRenderContext *dev, CKBOOL temporaryOnly) {
    for (XClassArray<VxCallBack>::Iterator it = m_PostCallBacks.Begin(); it != m_PostCallBacks.End();) {
        if (temporaryOnly && !it->temp) {
            ++it;
            continue;
        }

        CK_RENDERCALLBACK_SIMPLE func = (CK_RENDERCALLBACK_SIMPLE) it->callback;
        if (func) {
            func(dev, it->argument);
        }

        if (it->temp) {
            it = m_PostCallBacks.Remove(it);
        } else {
            ++it;
        }
    }
}

void CKCallbacksContainer::ClearPreCallbacks() {
    m_PreCallBacks.Clear();
}

void CKCallbacksContainer::ClearPostCallbacks() {
    m_PostCallBacks.Clear();
}

void CKCallbacksContainer::Clear() {
    m_PreCallBacks.Clear();
    m_PostCallBacks.Clear();
    if (m_Callback) {
        delete m_Callback;
        m_Callback = nullptr;
    }
}
