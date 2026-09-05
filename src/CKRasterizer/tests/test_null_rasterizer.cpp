// NULL backend (CKNullBackend.h): the engine's fallback when no rasterizer
// plugin loads and the base of the recording test harness. Checks the driver
// description (baseline caps, display modes), the frame protocol and the
// handle table.

#include "CKNullBackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int Fail(const char *what)
{
    fprintf(stderr, "test_null_rasterizer: %s\n", what);
    return EXIT_FAILURE;
}

static bool HasDisplayMode(CKRasterizerBackendDriver *driver, int width, int height, int bpp, int refreshRate)
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
    CKNullBackendLibrary library;
    if (library.GetDriverCount() != 0 || !library.Start(NULL))
        return Fail("library start");
    if (library.GetDriverCount() != 1 || !library.Start(NULL) || library.GetDriverCount() != 1)
        return Fail("one driver, idempotent start");

    CKRasterizerBackendDriver *driver = library.GetDriver(0);
    if (!driver || driver->m_DriverIndex != 0 || library.GetDriver(1) != NULL)
        return Fail("driver lookup");

    if (driver->m_Hardware || !driver->m_CapsUpToDate)
        return Fail("software driver with final caps");

    if (driver->m_DisplayModes.Size() < 4 || driver->m_TextureFormats.Size() != 1)
        return Fail("display modes / texture formats");

    if (!HasDisplayMode(driver, 640, 480, 32, 60) ||
        !HasDisplayMode(driver, 800, 600, 32, 60) ||
        !HasDisplayMode(driver, 1024, 768, 16, 60) ||
        !HasDisplayMode(driver, 1920, 1080, 32, 60))
        return Fail("legacy display modes");

    if ((driver->m_2DCaps.Caps & CKRST_2DCAPS_WINDOWED) == 0 ||
        (driver->m_2DCaps.Caps & CKRST_2DCAPS_3D) == 0)
        return Fail("2D caps");
    if ((driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_SOFTWARE) == 0 ||
        (driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_HARDWARETL) != 0)
        return Fail("software T&L bits");

    CKRasterizerBackend *backend = driver->CreateBackend();
    if (!backend)
        return Fail("CreateBackend");
    if (backend->GetDeviceStatus() == CK_OK)
        return Fail("status before Init");

    CKBackendInitDesc init;
    init.PosX = 10;
    init.PosY = 20;
    init.Width = 800;
    init.Height = 600;
    init.Bpp = 32;
    init.ZBpp = 24;
    init.StencilBpp = 8;
    if (backend->Init(&init) != CK_OK || backend->Init(&init) != CKERR_INVALIDOPERATION)
        return Fail("Init once");
    if (backend->GetDeviceStatus() != CK_OK || !backend->IsIdle())
        return Fail("status after Init");

    const CKBackendCaps &caps = backend->GetCaps();
    if ((caps.Features & (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER)) !=
            (CKRST_DEVCAPS_VERTEX_SHADER | CKRST_DEVCAPS_PIXEL_SHADER) ||
        (caps.Features & CKRST_DEVCAPS_TEXTURE_READBACK) == 0 ||
        caps.MaxPasses != CKRST_MAX_PASSES ||
        caps.MaxTextureBindings != CKRST_BACKEND_SLOT_COUNT ||
        caps.ShaderFormat != CKRST_SHADER_FORMAT_BGFX ||
        caps.ShaderProfile != CKRST_SHADER_PROFILE_DX11 ||
        caps.OriginBottomLeft || caps.HomogeneousDepth)
        return Fail("caps");

    if (backend->Resize(1, 2, 320, 240) != CK_OK || backend->Resize(0, 0, 0, 240) != CKERR_INVALIDPARAMETER)
        return Fail("Resize");

    // Handles: never zero, never reused, typed.
    CKTextureDesc texDesc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texDesc.Format);
    texDesc.Format.Width = 16;
    texDesc.Format.Height = 16;
    texDesc.MipMapCount = 1;
    texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_READBACK;
    CKDWORD texture = 0;
    if (backend->CreateTexture(&texDesc, NULL, &texture) != CK_OK || texture == 0 ||
        !backend->IsObjectAlive(texture, CKRST_OBJ_TEXTURE) || backend->IsObjectAlive(texture, CKRST_OBJ_VERTEXBUFFER))
        return Fail("CreateTexture");
    if (backend->DestroyObject(texture, CKRST_OBJ_VERTEXBUFFER) != CKERR_INVALIDPARAMETER ||
        backend->DestroyObject(texture, CKRST_OBJ_TEXTURE) != CK_OK ||
        backend->IsObjectAlive(texture, CKRST_OBJ_TEXTURE) ||
        backend->DestroyObject(texture, CKRST_OBJ_TEXTURE) != CKERR_INVALIDPARAMETER)
        return Fail("DestroyObject");
    CKDWORD replacement = 0;
    if (backend->CreateTexture(&texDesc, NULL, &replacement) != CK_OK || replacement == 0 || replacement == texture)
        return Fail("handles are not reused");

    // Frame protocol: draws need a pass, passes need Present.
    CKBackendDraw draw;
    draw.Program = 1;
    if (backend->Draw(&draw) != CKERR_INVALIDOPERATION)
        return Fail("draw outside a pass");
    CKBackendPassDesc pass;
    pass.Rect.right = 320;
    pass.Rect.bottom = 240;
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR;
    pass.Name = "scene";
    if (backend->BeginPass(&pass) != CK_OK || backend->IsIdle())
        return Fail("BeginPass");
    if (backend->Resize(0, 0, 640, 480) != CKERR_INVALIDOPERATION)
        return Fail("Resize inside a frame");
    if (backend->Draw(&draw) != CKERR_INVALIDPARAMETER)
        return Fail("draw with an unknown program");
    if (driver->DestroyBackend(backend))
        return Fail("DestroyBackend inside a frame");

    CKReadbackDesc readback;
    if (backend->ReadTexture(replacement, 0, &readback, NULL) != CK_OK || readback.Width != 16 ||
        readback.RequiredSize != 16 * 16 * 4 || readback.Format != _32_ARGB8888)
        return Fail("ReadTexture layout");
    CKBYTE pixels[16 * 16 * 4];
    memset(pixels, 0xAA, sizeof(pixels));
    readback.Data = pixels;
    readback.Capacity = sizeof(pixels);
    CKDWORD available = 0;
    if (backend->ReadTexture(replacement, 0, &readback, &available) != CK_OK || available == 0 || pixels[0] != 0)
        return Fail("ReadTexture zero image");

    CKDWORD frame = 0;
    if (backend->Present(CKRST_BACKEND_PRESENT_IMMEDIATE, &frame) != CK_OK || frame == 0 || frame != available ||
        !backend->IsIdle())
        return Fail("Present");
    if (backend->GetStats().Frames != 1 || backend->GetStats().Passes != 1 || backend->GetStats().Draws != 0)
        return Fail("stats");
    CKDWORD next = 0;
    if (backend->Present(CKRST_BACKEND_PRESENT_PRESERVE, &next) != CK_OK || next != frame + 1)
        return Fail("frame numbers increase");

    backend->Shutdown();
    if (backend->GetDeviceStatus() != CKERR_INVALIDOPERATION || backend->BeginPass(&pass) != CKERR_INVALIDOPERATION)
        return Fail("shut down backend refuses work");
    if (!driver->DestroyBackend(backend) || driver->DestroyBackend(backend))
        return Fail("DestroyBackend");

    library.Close();
    if (library.GetDriverCount() != 0)
        return Fail("Close");
    return EXIT_SUCCESS;
}
