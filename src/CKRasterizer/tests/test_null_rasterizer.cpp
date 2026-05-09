#include "CKRasterizer.h"

#include <cstdlib>

extern CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizer *rst);

static int Fail()
{
    return EXIT_FAILURE;
}

int main()
{
    CKRasterizer *rasterizer = CKNULLRasterizerStart(NULL);
    if (!rasterizer)
        return Fail();

    if (rasterizer->GetDriverCount() != 1)
        return Fail();

    CKRasterizerDriver *driver = rasterizer->GetDriver(0);
    if (!driver || driver->m_Owner != rasterizer || driver->m_DriverIndex != 0)
        return Fail();

    if (driver->m_Hardware || !driver->m_CapsUpToDate)
        return Fail();

    if (driver->m_DisplayModes.Size() != 1 || driver->m_TextureFormats.Size() != 1)
        return Fail();

    if ((driver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED) == 0 ||
        (driver->m_2DCaps.Caps & CKRST_2DCAPS_3D) == 0)
        return Fail();

    VxProgCapsDesc progCaps;
    if (driver->GetProgrammableCaps(progCaps) != CK_OK)
        return Fail();

    CKRasterizerContext *context = driver->CreateContext();
    if (!context || context->m_Driver != driver)
        return Fail();

    if (!context->Create(NULL, 10, 20, 800, 600, 32, FALSE, 60, 24, 8))
        return Fail();

    if (context->m_PosX != 10 || context->m_PosY != 20 ||
        context->m_Width != 800 || context->m_Height != 600 ||
        context->m_Bpp != 32 || context->m_ZBpp != 24 ||
        context->m_StencilBpp != 8 || context->m_RefreshRate != 60)
        return Fail();

    if (!context->Resize(1, 2, 320, 240, 0))
        return Fail();

    if (context->m_PosX != 1 || context->m_PosY != 2 ||
        context->m_Width != 320 || context->m_Height != 240)
        return Fail();

    CKRasterizerEncoder *encoder = context->BeginEncoder();
    if (!encoder)
        return Fail();

    encoder->SetState(CKDrawState());
    encoder->Touch(0);
    context->EndEncoder(encoder);

    if (context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) != CK_OK)
        return Fail();

    if (!driver->DestroyContext(context))
        return Fail();

    CKNULLRasterizerClose(rasterizer);
    return EXIT_SUCCESS;
}
