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
    if (g_RasterizersInfo.Size() != 1)
        return Fail("static render engine did not register exactly one rasterizer");

    CKRasterizerInfo &info = g_RasterizersInfo[0];
    if (!info.StartFct || !info.CloseFct || info.InterfaceRevision != CKRST_INTERFACE_REVISION)
        return Fail("registered rasterizer has an invalid contract");
    if (strcmp(info.DllName.Str(), "CKBgfxRasterizer") != 0)
        return Fail("static render engine did not register bgfx");
    if (strcmp(info.Desc.Str(), "NULL Rasterizer") == 0)
        return Fail("static render engine fell back to NULL");

    EnumerateRasterizers();
    if (g_RasterizersInfo.Size() != 1)
        return Fail("static rasterizer enumeration is not idempotent");

    return EXIT_SUCCESS;
}
