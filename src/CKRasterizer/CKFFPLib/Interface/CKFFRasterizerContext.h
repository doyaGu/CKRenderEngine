#ifndef CKFFRASTERIZERCONTEXT_H
#define CKFFRASTERIZERCONTEXT_H

#include "CKBuiltinShaders.h"
#include "CKRasterizer.h"
#include "CKRasterizerBackend.h"

// Notification after the native device and fixed-function programs are ready.
// It lets a concrete driver lower its public capabilities to native limits
// without making CKFFPLib own or model the driver lifecycle.
typedef void (*CKFFBackendReadyFunction)(void *user, CKRasterizerBackend *backend);

struct CKFFRasterizerContextDesc {
    CKRasterizerDriver *Driver;
    CKRasterizerBackend *Backend;
    const CKFFShaderLibrary *Shaders;
    CKFFBackendReadyFunction BackendReady;
    void *BackendReadyUser;

    CKFFRasterizerContextDesc()
        : Driver(NULL), Backend(NULL), Shaders(NULL), BackendReady(NULL), BackendReadyUser(NULL) {}
};

// The concrete driver owns Backend. The returned context owns only its
// fixed-function state and must be deleted after BeginShutdown succeeds and
// while Backend is still alive. After successful shutdown, the concrete driver
// may delete the context and backend in either order.
CKRasterizerContext *CKFFCreateRasterizerContext(const CKFFRasterizerContextDesc &desc);
void CKFFDeleteRasterizerContext(CKRasterizerContext *context);

#endif
