// Plugin entry points: the engine sees the v3 contract, implemented by the
// translation core over the bgfx backend library.

#include "CKBgfxBackend.h"
#include "CKTranslatedRasterizer.h"

#include <new>

#if defined(_WIN32)
#define CK_BGFX_RASTERIZER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CK_BGFX_RASTERIZER_EXPORT __attribute__((visibility("default")))
#else
#define CK_BGFX_RASTERIZER_EXPORT
#endif

static void CKBgfxLibraryClose(CKRasterizerBackendLibrary *library)
{
    delete static_cast<CKBgfxBackendLibrary *>(library);
}

static CKRasterizer *CKBgfxRasterizerStart(WIN_HANDLE AppWnd)
{
    auto *library = new (std::nothrow) CKBgfxBackendLibrary();
    if (!library)
        return NULL;
    if (!library->Start(AppWnd)) {
        delete library;
        return NULL;
    }
    return CKTranslatedRasterizerStart(library, CKBgfxLibraryClose);
}

static void CKBgfxRasterizerClose(CKRasterizer *rst)
{
    CKTranslatedRasterizerClose(rst);
}

#ifdef CK_LIB
void CKBgfxRasterizerGetInfo(CKRasterizerInfo *info)
#else
extern "C" CK_BGFX_RASTERIZER_EXPORT void CKRasterizerGetInfo(CKRasterizerInfo *info)
#endif
{
    if (!info)
        return;

    info->Desc = "bgfx Rasterizer";
    info->StartFct = CKBgfxRasterizerStart;
    info->CloseFct = CKBgfxRasterizerClose;
    info->InterfaceRevision = CKRST_INTERFACE_REVISION;
}
