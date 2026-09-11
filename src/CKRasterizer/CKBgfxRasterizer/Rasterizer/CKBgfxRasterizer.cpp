#include "CKBgfxRasterizer.h"

#include <new>

#if defined(_WIN32)
#define CK_BGFX_RASTERIZER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CK_BGFX_RASTERIZER_EXPORT __attribute__((visibility("default")))
#else
#define CK_BGFX_RASTERIZER_EXPORT
#endif

namespace {

CKRasterizer *StartRasterizer(WIN_HANDLE appWindow)
{
    CKBgfxRasterizer *rasterizer = new (std::nothrow) CKBgfxRasterizer();
    if (!rasterizer)
        return NULL;
    if (!rasterizer->Start(appWindow)) {
        delete rasterizer;
        return NULL;
    }
    return rasterizer;
}
void CloseRasterizer(CKRasterizer *rasterizer)
{
    delete rasterizer;
}

} // namespace

#ifdef CK_LIB
void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info)
#else
extern "C" CK_BGFX_RASTERIZER_EXPORT void CKRasterizerGetInfo(CKRasterizerInfo *info)
#endif
{
    if (!info)
        return;

    info->Desc = "bgfx Rasterizer";
    info->StartFct = StartRasterizer;
    info->CloseFct = CloseRasterizer;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
