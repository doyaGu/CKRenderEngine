#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CKRasterizer.h"

extern XClassArray<CKRasterizerInfo> g_RasterizersInfo;
extern void EnumerateRasterizers();

static int Fail(const char *message)
{
    fprintf(stderr, "static_rasterizer_registration_tests: %s\n", message);
    return EXIT_FAILURE;
}

int main()
{
    if (g_RasterizersInfo.Size() != 0)
        return Fail("rasterizer registry is not initially empty");

    EnumerateRasterizers();
    const int expected = CKRE_EXPECT_BGFX + CKRE_EXPECT_SDL_GPU;
    if (g_RasterizersInfo.Size() != (expected ? expected : 1))
        return Fail("static render engine registered an unexpected number of rasterizers");
    int bgfx = 0, sdl = 0, null = 0;
    for (int i = 0; i < g_RasterizersInfo.Size(); ++i) {
        const CKRasterizerInfo &info = g_RasterizersInfo[i];
        if (!info.StartFct || !info.CloseFct || info.InterfaceRevision != CKRST_INTERFACE_REVISION)
            return Fail("registered rasterizer has an invalid contract");
        if (strcmp(info.DllName.CStr(), "CKBgfxRasterizer") == 0) ++bgfx;
        if (strcmp(info.DllName.CStr(), "CKSdlGpuRasterizer") == 0) ++sdl;
        if (strcmp(info.Desc.CStr(), "NULL Rasterizer") == 0) ++null;
    }
    if (bgfx != CKRE_EXPECT_BGFX || sdl != CKRE_EXPECT_SDL_GPU || null != (expected ? 0 : 1))
        return Fail("static rasterizer names do not match configured backends");

    EnumerateRasterizers();
    if (g_RasterizersInfo.Size() != (expected ? expected : 1))
        return Fail("static rasterizer enumeration is not idempotent");

    return EXIT_SUCCESS;
}
