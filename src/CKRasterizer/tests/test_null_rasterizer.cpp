#include "CKRasterizer.h"

#include <stdlib.h>

extern CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
extern void CKNULLRasterizerClose(CKRasterizer *rst);

static int Fail()
{
    return EXIT_FAILURE;
}

static void ScreenShotCallback(void *, CKDWORD, CKDWORD, CKDWORD,
                               CKDWORD, VX_PIXELFORMAT, const void *,
                               CKDWORD, CKBOOL)
{
}

static bool HasDisplayMode(CKRasterizerDriver *driver, int width, int height, int bpp, int refreshRate)
{
    for (int i = 0; i < driver->m_DisplayModes.Size(); ++i) {
        const VxDisplayMode &mode = driver->m_DisplayModes[i];
        if (mode.Width == width &&
            mode.Height == height &&
            mode.Bpp == bpp &&
            mode.RefreshRate == refreshRate) {
            return true;
        }
    }

    return false;
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

    if (driver->m_DisplayModes.Size() < 4 || driver->m_TextureFormats.Size() != 1)
        return Fail();

    if (!HasDisplayMode(driver, 640, 480, 32, 60) ||
        !HasDisplayMode(driver, 800, 600, 32, 60) ||
        !HasDisplayMode(driver, 1024, 768, 16, 60) ||
        !HasDisplayMode(driver, 1920, 1080, 32, 60))
        return Fail();

    if ((driver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED) == 0 ||
        (driver->m_2DCaps.Caps & CKRST_2DCAPS_3D) == 0)
        return Fail();

    CKRasterizerContext *context = driver->CreateContext();
    if (!context || context->m_Driver != driver)
        return Fail();

    CKRasterizerTargetDesc target;
    CKRasterizerCapsDesc caps;
    if (context->GetTargetDesc(&target) != CKERR_INVALIDOPERATION ||
        context->GetCaps(&caps) != CKERR_INVALIDOPERATION)
        return Fail();

    if (context->Create(NULL, 10, 20, 800, 600, 32, FALSE, 60, 24, 8) != CK_OK)
        return Fail();

    if (context->GetTargetDesc(&target) != CK_OK ||
        target.ShaderProfile != CKRST_SHADER_PROFILE_UNKNOWN ||
        context->GetCaps(&caps) != CK_OK ||
        (caps.Features & (CKRST_CAPS_VERTEX_SHADER | CKRST_CAPS_PIXEL_SHADER)) != 0 ||
        (caps.Features & (CKRST_CAPS_RENDER_VIEWS | CKRST_CAPS_FRAMEBUFFER |
                          CKRST_CAPS_BUFFER_UPDATE | CKRST_CAPS_TEXTURE_UPDATE)) !=
            (CKRST_CAPS_RENDER_VIEWS | CKRST_CAPS_FRAMEBUFFER |
             CKRST_CAPS_BUFFER_UPDATE | CKRST_CAPS_TEXTURE_UPDATE) ||
        caps.MaxRenderViews != CKRST_MAX_RENDER_VIEWS ||
        caps.MaxShaders != 0 || caps.MaxPrograms != 0 ||
        caps.MaxTransforms != CKRST_MAX_TRANSFORMS)
        return Fail();

    CKTextureFormatCaps textureCaps;
    if (context->GetTextureFormatCaps(_32_ARGB8888, &textureCaps) != CK_OK ||
        textureCaps.Format != _32_ARGB8888 ||
        (textureCaps.Caps & (CKRST_FORMAT_CAPS_TEXTURE_2D |
                             CKRST_FORMAT_CAPS_FRAMEBUFFER)) !=
            (CKRST_FORMAT_CAPS_TEXTURE_2D | CKRST_FORMAT_CAPS_FRAMEBUFFER))
        return Fail();

    if (context->m_PosX != 10 || context->m_PosY != 20 ||
        context->m_Width != 800 || context->m_Height != 600 ||
        context->m_Bpp != 32 || context->m_ZBpp != 24 ||
        context->m_StencilBpp != 8 || context->m_RefreshRate != 60)
        return Fail();

    if (context->RequestScreenShot(0, ScreenShotCallback) !=
            CKERR_NOTIMPLEMENTED ||
        context->RequestScreenShot(0x7fffffffu, ScreenShotCallback) !=
            CKERR_INVALIDPARAMETER ||
        context->RequestScreenShot(0, NULL) != CKERR_INVALIDPARAMETER)
        return Fail();

    if (context->Resize(1, 2, 320, 240, 0) != CK_OK)
        return Fail();

    if (context->m_PosX != 1 || context->m_PosY != 2 ||
        context->m_Width != 320 || context->m_Height != 240)
        return Fail();

    CKRasterizerContext *secondContext = driver->CreateContext();
    if (!secondContext ||
        secondContext->Create(NULL, 0, 0, 320, 240, 32, FALSE, 60, 24, 8) !=
            CK_OK)
        return Fail();

    CKUniformDesc uniformDesc;
    uniformDesc.Name = (CKSTRING)"u_nullEncoderBoundary";
    uniformDesc.Type = CKRST_UNIFORM_VEC4;
    uniformDesc.Count = 1;
    CKDWORD uniform = 0;
    if (context->CreateUniform(&uniformDesc, &uniform) != CK_OK || uniform == 0)
        return Fail();

    CKRasterizerEncoder *encoder = context->BeginEncoder();
    if (!encoder)
        return Fail();
    if (context->IsIdle() ||
        context->BeginShutdown() != CKERR_INVALIDOPERATION ||
        driver->DestroyContext(context))
        return Fail();

    CKDWORD activeFrameNumber = 0;
    if (context->Frame(CKRST_FRAME_SYNC_IMMEDIATE,
                       CKRST_FRAME_NONE, &activeFrameNumber) !=
            CKERR_INVALIDOPERATION ||
        activeFrameNumber != 0)
        return Fail();
    if (context->DeleteObject(uniform, CKRST_OBJ_UNIFORM) !=
            CKERR_INVALIDOPERATION ||
        context->FlushObjects(CKRST_OBJ_UNIFORM) != CKERR_INVALIDOPERATION ||
        context->Resize(0, 0, 640, 480, 0) != CKERR_INVALIDOPERATION ||
        context->SetAntialias(4) != CKERR_INVALIDOPERATION ||
        !context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM))
        return Fail();

    encoder->SetState(CKDrawStateBuilder().Build());
    encoder->Touch(0);
    if (context->EndEncoder(encoder) != CK_OK)
        return Fail();
    if (!context->IsIdle())
        return Fail();
    if (context->DeleteObject(uniform, CKRST_OBJ_UNIFORM) != CK_OK ||
        context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM))
        return Fail();
    CKDWORD replacementUniform = 0;
    if (context->CreateUniform(&uniformDesc, &replacementUniform) != CK_OK ||
        replacementUniform == 0 || replacementUniform == uniform ||
        context->IsObjectAlive(uniform, CKRST_OBJ_UNIFORM) ||
        !context->IsObjectAlive(replacementUniform, CKRST_OBJ_UNIFORM) ||
        context->DeleteObject(replacementUniform, CKRST_OBJ_UNIFORM) != CK_OK)
        return Fail();

    CKDWORD frameNumber = 0;
    if (context->Frame(CKRST_FRAME_SYNC_IMMEDIATE,
                       CKRST_FRAME_NONE, &frameNumber) != CK_OK ||
        frameNumber == 0)
        return Fail();

    if (context->BeginShutdown() != CK_OK ||
        context->GetDeviceStatus() != CKERR_INVALIDOPERATION ||
        context->BeginEncoder() != NULL ||
        context->Frame(CKRST_FRAME_SYNC_IMMEDIATE,
                       CKRST_FRAME_NONE, &frameNumber) !=
            CKERR_INVALIDOPERATION)
        return Fail();

    if (!driver->DestroyContext(context))
        return Fail();

    if (!driver->DestroyContext(secondContext))
        return Fail();

    CKNULLRasterizerClose(rasterizer);
    return EXIT_SUCCESS;
}
